#pragma once
// The PS5 payload's own parts (SSR_PS5): messages on the console's screen and the window's
// picture. Included after SDL; only the PS5 build uses it.
#ifdef SSR_PS5

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// Before the software renderer: the window's picture, which opens the PS5's video output.
// SDL replaces the reason it fails with "Couldn't find matching render driver", so it is
// asked for here first and the real reason shown, then let go again: SDL makes no renderer
// for a window that has a picture already ("Surface already associated with window"); the
// video output stays open. No texture framebuffer: SDL's PS5 build has only the software
// renderer, and trying for one only hides that error too.
inline bool PrepareWindow(SDL_Window* window)
{
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
