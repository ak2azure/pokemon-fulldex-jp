/* The PS5 title's links to the platform layer (Mihawk's payload SDK fork, platform/,
 * GPL-3.0-or-later; include/ps5platform/libc.h there says why each is needed): libc functions
 * a title is refused, that no module the console gives a title exports, or that answer wrongly
 * there. tools/ps5/native_link.py links the title with --wrap for each name here (the list is
 * WRAPS in tools/ps5/build.sh), so that the program's, SDL's and libc++'s calls land here; the
 * heap's (malloc and its family, in direct memory) are the layer's own src/heap_wrap.c.
 * The directory ones are PS5 RetroArch's src/platform_wraps.c (Mihawk, GPL-3.0-or-later).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

int scePthreadGetaffinity(pthread_t thread, uint64_t* mask);

#include <ps5platform/libc.h>

/* A title's opendir is refused; directories are read with getdents. */
DIR *__wrap_opendir(const char *path) { return ps5_opendir(path); }
DIR *__wrap_fdopendir(int fd) { return ps5_fdopendir(fd); }
struct dirent *__wrap_readdir(DIR *directory) { return ps5_readdir(directory); }
int __wrap_closedir(DIR *directory) { return ps5_closedir(directory); }
void __wrap_rewinddir(DIR *directory) { ps5_rewinddir(directory); }
int __wrap_dirfd(DIR *directory) { return ps5_dirfd(directory); }

/* Only libkernel_sys exports the *at functions, which titles do not import. */
int __wrap_openat(int directory, const char *name, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    return ps5_openat(directory, name, flags, mode);
}
int __wrap_unlinkat(int directory, const char *name, int flags) { return ps5_unlinkat(directory, name, flags); }
int __wrap_fchmodat(int directory, const char *name, mode_t mode, int flags)
{
    return ps5_fchmodat(directory, name, mode, flags);
}
int __wrap_fstatat(int directory, const char *name, struct stat *status, int flags)
{
    return ps5_fstatat(directory, name, status, flags);
}
int __wrap_mkdirat(int directory, const char *name, mode_t mode) { return ps5_mkdirat(directory, name, mode); }
int __wrap_renameat(int from_directory, const char *from, int to_directory, const char *to)
{
    return ps5_renameat(from_directory, from, to_directory, to);
}

/* getcwd goes through __getcwd, which only libkernel_sys has; realpath is refused. */
char *__wrap_getcwd(char *buffer, size_t size) { return ps5_getcwd(buffer, size); }
char *__wrap_realpath(const char *path, char *resolved) { return ps5_realpath(path, resolved); }

/* No module a title loads exports these. */
struct tm *__wrap_gmtime_r(const time_t *t, struct tm *result) { return ps5_gmtime_r(t, result); }
struct tm *__wrap_localtime_r(const time_t *t, struct tm *result) { return ps5_localtime_r(t, result); }
int __wrap_isatty(int fd) { return ps5_isatty(fd); }
char *__wrap_strcasestr(const char *haystack, const char *needle) { return ps5_strcasestr(haystack, needle); }
int __wrap_mkstemp(char *path_template) { return ps5_mkstemp(path_template); }
void *__wrap_memccpy(void *destination, const void *source, int c, size_t size)
{
    return ps5_memccpy(destination, source, c, size);
}
uint32_t __wrap_arc4random(void) { return ps5_arc4random(); }
void __wrap_arc4random_buf(void *buffer, size_t bytes) { ps5_arc4random_buf(buffer, bytes); }
int __wrap_clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain)
{
    return ps5_clock_nanosleep(clock, flags, request, remain);
}
int __wrap_statvfs(const char *path, struct statvfs *result) { return ps5_statvfs(path, result); }

/* The SDK sends name lookups to a module titles do not load (ENet's). */
int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                       struct addrinfo **result)
{
    return ps5_getaddrinfo(node, service, hints, result);
}
void __wrap_freeaddrinfo(struct addrinfo *info) { ps5_freeaddrinfo(info); }
int __wrap_getnameinfo(const void *address, unsigned int length, char *host, unsigned int host_size, char *service,
                       unsigned int service_size, int flags)
{
    return ps5_getnameinfo(address, length, host, host_size, service, service_size, flags);
}
struct hostent *__wrap_gethostbyname(const char *name) { return ps5_gethostbyname(name); }
struct hostent *__wrap_gethostbyaddr(const void *address, unsigned int length, int type)
{
    return ps5_gethostbyaddr(address, length, type);
}

/* libc++abi registers thread_local destructors here (melonDS's widescreen 3D has some), which no
 * module exports; the platform layer runs them as each thread ends, before its keys'. */
int __cxa_thread_atexit_impl(void (*destructor)(void *), void *object, void *dso)
{
    return ps5___cxa_thread_atexit_impl(destructor, object, dso);
}

/* SDL's PS5 video driver opens the USB keyboard and the on-screen keyboard as it starts. The
 * payload had both; a title is not known to be able to load their modules, and a call into
 * one it has not loaded jumps to nothing (the first test title stopped inside SDL_Init). The
 * program is played with the controller, so here there is no keyboard: SDL is told so and
 * goes on without one. */
int __wrap_sceKeyboardInit(void) { return (int)0x80000001; }
int __wrap_sceKeyboardOpen(int user, int type, int index, void *param)
{
    (void)user, (void)type, (void)index, (void)param;
    return (int)0x80000001;
}
int __wrap_sceKeyboardReadState(int handle, void *state)
{
    (void)handle, (void)state;
    return (int)0x80000001;
}
int __wrap_sceKeyboardClose(int handle)
{
    (void)handle;
    return 0;
}
int __wrap_sceImeDialogInit(const void *param, void *extended)
{
    (void)param, (void)extended;
    return (int)0x80000001;
}
int __wrap_sceImeDialogGetStatus(void) { return 0; /* none */ }
int __wrap_sceImeDialogGetResult(void *result)
{
    (void)result;
    return (int)0x80000001;
}
int __wrap_sceImeDialogTerm(void) { return 0; }

/* sysconf answers 16 processors where a title's threads run on 13 (the compositor and the
 * widescreen 3D size their thread pools by it). Only those two questions go to ps5_sysconf:
 * for any other it calls sysconf, which this link sends back here, and RADV's first
 * sysconf(_SC_PAGESIZE) recursed until its thread's stack ran out (the Vulkan title's crash). */
long __real_sysconf(int name);
long __wrap_sysconf(int name)
{
    if (name == _SC_NPROCESSORS_ONLN || name == _SC_NPROCESSORS_CONF)
    {
        uint64_t mask = 0;
        if (scePthreadGetaffinity(pthread_self(), &mask) == 0 && mask != 0)
            return __builtin_popcountll(mask);
    }
    return __real_sysconf(name);
}
