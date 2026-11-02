#pragma once
// The PS5 build as a title of its own (SSR_PS5_NATIVE, tools/ps5/build.sh): eboot.bin is the
// program, started from its Home screen tile like any game, rather than a payload that a tile
// hands to the ELF loader. A title runs in the console's sandbox, so it asks the jailbreak to
// free it (for /data), and its code memory comes from direct memory (the JIT). Used by the
// program (pc_launcher.cpp, main.cpp) and by the small test title (tools/ps5/native/probe.cpp).
//
// The way out of the sandbox is Porpoise's (src/porpoise_jailbreak.cpp in
// https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5, GPL-3.0-or-later, itself a
// port of PS5SX2's), and the executable memory and the directory functions a title is refused
// come from the platform layer of Mihawk's payload SDK fork (https://github.com/mihawk-99/
// PS5_PayloadSDK, platform/, GPL-3.0-or-later), linked in by tools/ps5/build.sh.
#if defined(SSR_PS5_NATIVE)

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <pthread.h>
#include <ucontext.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

extern "C" {
int sceKernelUsleep(unsigned microseconds);
int sceSystemServiceHideSplashScreen();
int sceSystemServiceLoadExec(const char*, const char**);
// (The platform layer's ps5platform/exec.h: read, write and execute memory within reach of
// the anchor's code, so that the JIT's calls into the program fit in 32 bits.)
void* ps5_exec_allocate(size_t bytes, uintptr_t anchor);
int ps5_exec_release(void* base);
}

namespace Ps5Native
{
// The title's own folder (the read-only copy of /data/homebrew/<title>), and where the player's
// things are kept once the jailbreak has freed it.
constexpr const char* AppDir = "/app0";

// Before any thread: the process given its own credential. One jailbreak daemon (Lapy's) frees
// only a process that did this while it had one thread; for etaHEN it changes nothing.
inline void Prepare()
{
    if (seteuid(geteuid()) != 0) fprintf(stderr, "[ps5] seteuid(self): %s\n", strerror(errno));
}

// Whether the console's /data can be written: the folder made, a file written there.
inline bool DataWritable(const char* dir)
{
    struct stat st;
    if (stat("/data", &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    mkdir(dir, 0777);
    const std::string probe = std::string(dir) + "/.write-test";
    const int fd = open(probe.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;
    close(fd);
    unlink(probe.c_str());
    return true;
}

namespace Detail
{
// The names the jailbreak daemons watch for in the title's /download0: etaHEN's (OnionHEN and
// older Lapy take it too), OnionHEN's own, and Lapy's owned-root daemon's.
constexpr const char* Requests[] = {
    "/download0/etahen_jailbreak",
    "/download0/onionhen_jailbreak",
    "/download0/elevate_proc",
};

// {"PID":n} written to one request file through a .tmp, so the daemon never reads half of it.
inline bool Publish(const char* path, int pid)
{
    const std::string staged = std::string(path) + ".tmp";
    unlink(staged.c_str());
    unlink(path);
    const int fd = open(staged.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return false;
    fchmod(fd, 0666);
    char body[32];
    const int n = snprintf(body, sizeof body, "{\"PID\":%d}\n", pid);
    const bool written = write(fd, body, (size_t)n) == n && fsync(fd) == 0;
    close(fd);
    if (!written || rename(staged.c_str(), path) != 0)
    {
        unlink(staged.c_str());
        return false;
    }
    return true;
}

// One round of request files; true when a daemon took one (it deletes the name it watches)
// within about a second and a half.
inline bool RequestFile()
{
    const int pid = (int)getpid();
    int published = 0;
    for (const char* r : Requests) published += Publish(r, pid);
    if (!published)
    {
        fprintf(stderr, "[ps5] jailbreak: no request file could be made in /download0 (%s)\n", strerror(errno));
        return false;
    }
    bool taken = false;
    for (int poll = 0; poll < 90 && !taken; poll++)
    {
        sceKernelUsleep(16667);
        for (const char* r : Requests)
            if (access(r, F_OK) != 0)
            {
                fprintf(stderr, "[ps5] jailbreak: request taken after %d polls\n", poll);
                taken = true;
                break;
            }
    }
    for (const char* r : Requests) unlink(r);
    return taken;
}

// The older command servers on the console itself (etaHEN's 9028, the unjail daemon's 9069):
// command 5, jailbreak this process.
inline bool RequestPort()
{
    struct Command
    {
        int magic, cmd, pid, ret;
        char msg1[0x500], msg2[0x500];
    };
    for (const int port : { 9028, 9069 })
    {
        Command c;
        memset(&c, 0, sizeof c);
        c.magic = (int)0xDEADBEEF;
        c.cmd = 5;
        c.pid = (int)getpid();
        c.ret = -1337;
        const int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return false;
        timeval limit = { 2, 0 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof limit);
        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((uint16_t)port);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(s, (sockaddr*)&sa, sizeof sa) != 0)
        {
            close(s);
            continue;
        }
        const bool sent = send(s, &c, sizeof c, 0) == (ssize_t)sizeof c;
        size_t got = 0;
        while (sent && got < sizeof c)
        {
            const ssize_t r = recv(s, (char*)&c + got, sizeof c - got, 0);
            if (r <= 0) break;
            got += (size_t)r;
        }
        close(s);
        fprintf(stderr, "[ps5] jailbreak: port %d answered %d\n", port, c.ret);
        if (sent && got == sizeof c && c.ret == 0) return true;
    }
    return false;
}
}

// Out of the sandbox, for /data (the ROMs, saves and settings in dir) and the rest of the
// console: etaHEN frees homebrew it starts by itself on most setups, and otherwise is asked to
// (its jailbreak on demand, then its older command port). The daemon may start a moment after
// the title, and the first request can lose a race, so this keeps asking for a while: about
// three seconds a round, ten rounds. False if /data still cannot be written.
inline bool FreeFromSandbox(const char* dir)
{
    if (DataWritable(dir)) return true;
    fprintf(stderr, "[ps5] jailbreak: /data cannot be written (uid %d): asking the jailbreak to free the title\n",
        (int)geteuid());
    for (int round = 0; round < 10; round++)
    {
        if (!Detail::RequestFile()) Detail::RequestPort();
        for (int poll = 0; poll < 90; poll++)
        {
            if (DataWritable(dir))
            {
                fprintf(stderr, "[ps5] jailbreak: freed (round %d)\n", round);
                return true;
            }
            sceKernelUsleep(16667);
        }
    }
    fprintf(stderr, "[ps5] jailbreak: still in the sandbox\n");
    return false;
}

// A crash written to the log (stderr) before the title closes: the signal, the address it
// faulted on, where this function is (the program's base follows from it and the link map,
// build-ps5-native-work/*.map), and the words above the handler's frame that point into the
// program's code, which are the return addresses of the calls that led to the crash (the
// handler runs on the same stack). write() only: nothing here allocates.
namespace Detail
{
inline void Hex(char*& p, uint64_t v)
{
    *p++ = '0';
    *p++ = 'x';
    for (int s = 60; s >= 0; s -= 4) *p++ = "0123456789abcdef"[(v >> s) & 15];
}

inline void Put(const char* s)
{
    write(2, s, strlen(s));
}

inline void OnCrash(int sig, siginfo_t* info, void*);

// The words on a stack, from sp up, that point into the program's code: the return addresses
// of the calls that led there. (eboot.bin is about 32 MB with RADV in it, so the range is wide.)
inline void ShowStack(const uint64_t* sp)
{
    Put("[ps5] return addresses near the program's code, innermost first:\n");
    const uint64_t self = (uint64_t)(uintptr_t)&OnCrash;
    char line[32];
    int shown = 0;
    for (int i = 0; i < 4096 && shown < 48; i++)
    {
        const uint64_t v = sp[i];
        if (v + 0x6000000 > self && v < self + 0x6000000)
        {
            char* p = line;
            *p++ = ' ';
            Hex(p, v);
            *p++ = '\n';
            write(2, line, (size_t)(p - line));
            shown++;
        }
    }
}

inline void OnCrash(int sig, siginfo_t* info, void*)
{
    char line[160];
    char* p = line;
    const char* head = "\n[ps5] CRASH: signal ";
    memcpy(p, head, strlen(head));
    p += strlen(head);
    *p++ = (char)('0' + sig / 10);
    *p++ = (char)('0' + sig % 10);
    const char* at = " at address ";
    memcpy(p, at, strlen(at));
    p += strlen(at);
    Hex(p, (uint64_t)(uintptr_t)(info ? info->si_addr : nullptr));
    const char* here = "; OnCrash is at ";
    memcpy(p, here, strlen(here));
    p += strlen(here);
    Hex(p, (uint64_t)(uintptr_t)&OnCrash);
    *p++ = '\n';
    write(2, line, (size_t)(p - line));
    uint64_t local = 0;
    ShowStack(&local);
    Put("[ps5] end of the crash report\n");
    signal(sig, SIG_DFL);
    raise(sig);
}

// Where a thread that has stopped moving is (Watch below): the instruction it was interrupted
// at, then the return addresses on its stack, as for a crash.
inline void OnWhere(int, siginfo_t*, void* context)
{
    const mcontext_t& m = static_cast<ucontext_t*>(context)->uc_mcontext;
    char line[64];
    char* p = line;
    const char* head = "[ps5] it is at ";
    memcpy(p, head, strlen(head));
    p += strlen(head);
    Hex(p, (uint64_t)m.mc_rip);
    *p++ = '\n';
    write(2, line, (size_t)(p - line));
    ShowStack(reinterpret_cast<const uint64_t*>(m.mc_rsp));
    Put("[ps5] end of the report\n");
}
}

inline void LogCrashes()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = Detail::OnCrash;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (const int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT }) sigaction(sig, &sa, nullptr);
    fprintf(stderr, "[ps5] crashes are logged (OnCrash at %p)\n", (void*)&Detail::OnCrash);
}

// A watch over a step that might never finish (the graphics chip's start, which once stopped
// inside RADV with nothing in the log): if the thread that made it is still in the step after
// the given time, the log says so and where that thread is (Detail::OnWhere, through SIGUSR1).
class Watch
{
public:
    Watch(const char* what, int seconds) : What(what), Seconds(seconds), Target(pthread_self())
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = Detail::OnWhere;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR1, &sa, nullptr);
        Thread = std::thread([this] { Run(); });
    }
    ~Watch()
    {
        {
            std::lock_guard<std::mutex> lock(Lock);
            Done = true;
        }
        Wake.notify_all();
        Thread.join();
    }

private:
    void Run()
    {
        std::unique_lock<std::mutex> lock(Lock);
        for (int reported = 0; !Done && reported < 3; reported++)
        {
            if (Wake.wait_for(lock, std::chrono::seconds(Seconds), [this] { return Done; })) break;
            fprintf(stderr, "[ps5] %s has not finished after %d s; where it is:\n", What,
                    Seconds * (reported + 1));
            pthread_kill(Target, SIGUSR1);
        }
    }

    const char* What;
    int Seconds;
    pthread_t Target;
    std::mutex Lock;
    std::condition_variable Wake;
    bool Done = false;
    std::thread Thread;
};

// A notice for when there is no /data: written where a title can always write, its
// /download0, since game.log has no folder to go in.
inline const char* FallbackDir() { return "/download0"; }

// Whether melonDS's JIT can run: memory that is read, write and execute, from direct memory
// near the program (ARMJIT_Global.cpp asks for its code memory the same way), with a return
// instruction run in it.
inline bool CanRunJit()
{
    void* page = ps5_exec_allocate(0x4000, (uintptr_t)&CanRunJit);
    if (!page)
    {
        fprintf(stderr, "[ps5] JIT: no memory that can run code: the interpreter\n");
        return false;
    }
    static_cast<unsigned char*>(page)[0] = 0xC3; // ret
    reinterpret_cast<void (*)()>(page)();
    ps5_exec_release(page);
    fprintf(stderr, "[ps5] JIT: code can be made and run\n");
    return true;
}

// The title started again (QUIT, for the game list): the shell closes this process and opens
// its eboot.bin anew. If it will not, the title just closes.
[[noreturn]] inline void Restart()
{
    fflush(nullptr);
    const int r = sceSystemServiceLoadExec("/app0/eboot.bin", nullptr);
    fprintf(stderr, "[ps5] LoadExec(eboot.bin): %d\n", r);
    fflush(stderr);
    if (r < 0) sceSystemServiceLoadExec("exit", nullptr);
    for (;;) sceKernelUsleep(100000); // (the shell ends the process in its own time)
}
}

#endif
