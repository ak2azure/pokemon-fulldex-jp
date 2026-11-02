#pragma once
// The PS5 payload's own parts (SSR_PS5): messages on the console's screen and the window's
// picture. Included after SDL; only the PS5 build uses it.
#ifdef SSR_PS5

#include <cerrno>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <arpa/inet.h>
#include <cctype>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "ps5_native.h"
#ifdef SSR_VK_RENDER
#include "vkrender.h"
#endif

extern "C" {
// (As ps5-payload-sdk's samples declare them: libkernel and libSceSystemService.)
struct Ps5NotifyRequest
{
    char unused[45];
    char message[3075];
};
int sceKernelSendNotificationRequest(int, Ps5NotifyRequest*, size_t, int);
int sceSystemServiceLoadExec(const char*, const char**);
}

namespace Ps5
{
// A notification in the corner of the PS5's screen, and the same line in game.log: a payload
// that cannot open the screen has no other way to say why.
inline void Notify(const char* fmt, ...)
{
    Ps5NotifyRequest req;
    memset(&req, 0, sizeof req);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof req.message, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[ps5] %s\n", req.message);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

// Started from websrv's homebrew list, the program runs as an application of its own, which
// stays on the screen (black) after the program ends unless it is closed, as SDL2main does
// for SDL's own PS5 programs.
inline void CloseAppAtExit()
{
    atexit([] { sceSystemServiceLoadExec("exit", nullptr); });
}

// Mesa's libraries (EGL, OpenGL ES, gallium's zink, RADV) are beside eboot.elf, where the
// SDK's loader finds them through LD_LIBRARY_PATH (it reads it at each dlopen): the program's
// own folder, from argv[0], else the app's usual one. One folder only: the loader does not split
// the list at its colons (crt/rtld.c tests the path being built for ':', not the list), so
// "a:b" was looked in as a single folder and libEGL.so.1 was not found.
inline void SetLibraryPath(const char* argv0)
{
#ifdef SSR_PS5_NATIVE
    // (A title loads no libraries of its own: everything it uses is linked in.)
    (void)argv0;
    return;
#endif
    std::string dir = "/data/homebrew/PKFD00001";
    if (argv0 && argv0[0] == '/' && strchr(argv0 + 1, '/'))
    {
        dir = argv0;
        dir.erase(dir.find_last_of('/'));
    }
    setenv("LD_LIBRARY_PATH", dir.c_str(), 1);
    fprintf(stderr, "[ps5] started as %s; libraries from %s\n", argv0 ? argv0 : "?", dir.c_str());
}

// A connection to a server on the console itself (the ELF loader, websrv), or -1.
inline int ConnectLocal(uint16_t port)
{
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (sockaddr*)&addr, sizeof addr) == 0) return s;
    close(s);
    return -1;
}

// The program started again, for the game list after QUIT. A payload cannot start itself, so
// it sends starter.elf, beside it, to the ELF loader (port 9021): the starter closes the running
// application (this one) and starts eboot.elf again, as the Home screen app has it do
// (tools/ps5/starter). Without the starter (an older folder), websrv is asked instead, which
// starts homebrew on request (GET /hbldr?path=..., as its homebrew list does) the same way.
// Either way this waits to be closed. False if neither is there to ask or the program was not
// closed within 20 seconds; the program then just ends.
inline bool Relaunch(const char* argv0)
{
#ifdef SSR_PS5_NATIVE
    // (A title asks the shell to open it again.)
    (void)argv0;
    Ps5Native::Restart();
#endif
    const std::string self = argv0 && argv0[0] == '/' ? argv0 : "/data/homebrew/PKFD00001/eboot.elf";
    const std::string starter = self.substr(0, self.find_last_of('/')) + "/starter.elf";
    if (FILE* f = fopen(starter.c_str(), "rb"))
    {
        std::vector<char> elf;
        char buf[16384];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) elf.insert(elf.end(), buf, buf + n);
        fclose(f);
        const int s = ConnectLocal(9021);
        if (s >= 0)
        {
            fprintf(stderr, "[ps5] sending %s to the ELF loader to start the program again for the game list\n",
                starter.c_str());
            fflush(stderr);
            size_t done = 0;
            while (done < elf.size())
            {
                const ssize_t n = send(s, elf.data() + done, elf.size() - done, 0);
                if (n <= 0) break;
                done += (size_t)n;
            }
            close(s);
            if (done == elf.size())
            {
                for (int i = 0; i < 20; i++) sleep(1);
                fprintf(stderr, "[ps5] the starter did not start the program again within 20 seconds\n");
                return false;
            }
            fprintf(stderr, "[ps5] the ELF loader did not take all of the starter (%s)\n", strerror(errno));
        }
        else
            fprintf(stderr, "[ps5] no ELF loader on port 9021 (%s): trying websrv\n", strerror(errno));
    }
    std::string path;
    for (unsigned char c : self)
    {
        if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.') path += (char)c;
        else
        {
            char hex[4];
            snprintf(hex, sizeof hex, "%%%02X", c);
            path += hex;
        }
    }
    const int s = ConnectLocal(8080);
    if (s < 0)
    {
        fprintf(stderr, "[ps5] websrv is not running (%s): the program ends\n", strerror(errno));
        return false;
    }
    const std::string req = "GET /hbldr?pipe=0&daemon=0&path=" + path + " HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
    fprintf(stderr, "[ps5] asking websrv to start %s again for the game list\n", self.c_str());
    fflush(stderr);
    send(s, req.data(), req.size(), 0);
    // websrv answers only after it has closed this application and started the new one, so an
    // answer means it could not.
    timeval tv = { 20, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[256];
    const ssize_t n = recv(s, buf, sizeof buf - 1, 0);
    buf[n > 0 ? n : 0] = 0;
    fprintf(stderr, "[ps5] websrv did not start the program again: %s\n", n > 0 ? buf : "no answer");
    close(s);
    return false;
}

// The game drawn by the graphics chip (SDL's OpenGL ES 2 renderer on Mesa) unless
// SSR_PS5_SOFTWARE is set. Not in the title (SSR_PS5_NATIVE): a title cannot load Mesa's
// libraries (dlopen is refused there), so for now the processor draws its picture, 1920x1080.
inline bool UseGpu()
{
#ifdef SSR_PS5_NATIVE
    return false;
#else
    return !getenv("SSR_PS5_SOFTWARE");
#endif
}

// SDL's OpenGL ES 2 renderer says only "Couldn't find matching render driver" when anything
// on its way fails, so the way is walked here first, a step at a time, each saying why it
// failed: Mesa loaded (before the window, which is then made for OpenGL), then a context made
// in that window (which also shows what Mesa drives) and let go again for the renderer's own.
inline bool LoadGpu()
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    if (SDL_GL_LoadLibrary(nullptr) == 0)
    {
        fprintf(stderr, "[ps5] graphics chip: Mesa loaded\n");
        return true;
    }
    Notify("Pokemon Fulldex: graphics chip: Mesa not loaded (%s)", SDL_GetError());
    return false;
}

inline bool TestGpuContext(SDL_Window* window)
{
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx)
    {
        Notify("Pokemon Fulldex: graphics chip: no OpenGL ES context (%s)", SDL_GetError());
        return false;
    }
    using GetString = const unsigned char* (*)(unsigned);
    if (auto getString = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString")))
    {
        const unsigned char* version = getString(0x1F02); // GL_VERSION
        const unsigned char* name = getString(0x1F01);    // GL_RENDERER
        fprintf(stderr, "[ps5] graphics chip: %s, %s\n", version ? (const char*)version : "?", name ? (const char*)name : "?");
    }
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    fprintf(stderr, "[ps5] graphics chip: the picture is %dx%d\n", w, h);
    SDL_GL_DeleteContext(ctx);
    return true;
}

// Whether melonDS's JIT can run here: code made at run time needs memory that can be written
// and then run, which ps5-payload-sdk's libc asks the kernel for (mmap and mprotect with
// PROT_EXEC: kernel_mprotect). A page is asked for and a return instruction run in it; if
// that fails the games keep to the interpreter rather than crash.
inline bool CanRunJit()
{
#ifdef SSR_PS5_NATIVE
    return Ps5Native::CanRunJit();
#endif
    void* page = mmap(nullptr, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED)
    {
        fprintf(stderr, "[ps5] JIT: no memory that can run code (%s): the interpreter\n", strerror(errno));
        return false;
    }
    static_cast<unsigned char*>(page)[0] = 0xC3; // ret
    reinterpret_cast<void (*)()>(page)();
    munmap(page, 0x4000);
    fprintf(stderr, "[ps5] JIT: code can be made and run\n");
    return true;
}

// The title's picture by the graphics chip (SSR_VK_RENDER): Vulkan on RADV, at the screen's own
// size (frontend/vkrender.cpp), started before SDL so that SDL's video is then its "dummy" one,
// which leaves the console's video output to Vulkan. If Vulkan cannot start, SDL's PS5 video
// driver and its software renderer draw the picture with the processor, as before. Once.
inline void StartPicture()
{
#ifdef SSR_VK_RENDER
    static bool tried = false;
    if (tried) return;
    tried = true;
    // (RADV keeps the shaders it has compiled on disk, for a quicker start the next time: in the
    // player's folder, which a title can write once it is out of the sandbox.)
    mkdir("/data/pokemon-ds-wide/cache", 0777);
    setenv("MESA_SHADER_CACHE_DIR", "/data/pokemon-ds-wide/cache", 0);
    std::string info;
    if (VkRender::Start(info, true))
    {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        int w = 0, h = 0;
        VkRender::GetRendererOutputSize(nullptr, &w, &h);
        fprintf(stderr, "[ps5] the picture: Vulkan on %s, %dx%d\n", info.c_str(), w, h);
    }
    else
        Notify("Pokemon Fulldex: graphics chip not available (%s): the processor draws", info.c_str());
#endif
}

// Before the software renderer: the window's picture, which opens the PS5's video output.
// SDL replaces the reason it fails with "Couldn't find matching render driver", so it is
// asked for here first and the real reason shown, then let go again: SDL makes no renderer
// for a window that has a picture already ("Surface already associated with window"); the
// video output stays open. No texture framebuffer: SDL's PS5 build has only the software
// renderer, and trying for one only hides that error too.
inline bool PrepareWindow(SDL_Window* window)
{
#ifdef SSR_VK_RENDER
    if (VkRender::Active()) return true; // (Vulkan shows the picture; SDL's window has none)
#endif
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    if (SDL_GetWindowSurface(window))
    {
        SDL_DestroyWindowSurface(window);
        return true;
    }
    Notify("Pokemon Fulldex: no picture (%s). Start it from websrv's homebrew list.", SDL_GetError());
    return false;
}
}

#endif
