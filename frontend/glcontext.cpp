// See glcontext.h.
#include "glcontext.h"
#include "frontend/glad/glad.h"
#ifdef SSR_PS5
// The PS5: Mesa's EGL is not linked in (the payload's loader would look for it before the
// program could say where it is: frontend/ps5.h, SetLibraryPath) but loaded when the context is
// made, and its functions taken from it (LoadEgl below), under the names used here.
#define EGL_EGL_PROTOTYPES 0
#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <cstring>
#ifdef SSR_PS5
#include <cstdlib>
#include <dlfcn.h>
#include <string>
#define SSR_EGL_FUNCTIONS(X) \
    X(eglGetProcAddress, PFNEGLGETPROCADDRESSPROC) X(eglQueryString, PFNEGLQUERYSTRINGPROC) \
    X(eglGetDisplay, PFNEGLGETDISPLAYPROC) X(eglInitialize, PFNEGLINITIALIZEPROC) \
    X(eglBindAPI, PFNEGLBINDAPIPROC) X(eglQueryAPI, PFNEGLQUERYAPIPROC) \
    X(eglChooseConfig, PFNEGLCHOOSECONFIGPROC) X(eglGetConfigs, PFNEGLGETCONFIGSPROC) \
    X(eglCreateContext, PFNEGLCREATECONTEXTPROC) X(eglDestroyContext, PFNEGLDESTROYCONTEXTPROC) \
    X(eglMakeCurrent, PFNEGLMAKECURRENTPROC) X(eglCreatePbufferSurface, PFNEGLCREATEPBUFFERSURFACEPROC) \
    X(eglGetError, PFNEGLGETERRORPROC) X(eglGetCurrentDisplay, PFNEGLGETCURRENTDISPLAYPROC) \
    X(eglGetCurrentSurface, PFNEGLGETCURRENTSURFACEPROC) X(eglGetCurrentContext, PFNEGLGETCURRENTCONTEXTPROC)
#define SSR_EGL_DECLARE(name, type) static type name = nullptr;
SSR_EGL_FUNCTIONS(SSR_EGL_DECLARE)

// libEGL.so.1 opened RTLD_GLOBAL, as SDL's PS5 video driver opens it, so that SDL's later
// dlopen of it gets this same copy (the SDK's loader shares a library only then; two copies
// would each have their own Mesa). EGL_PLATFORM: the screen's platform, as SDL sets it.
// libGL.so.1 too, for desktop OpenGL's functions.
static bool LoadEgl(std::string& info)
{
    if (eglGetProcAddress) return true;
    setenv("EGL_PLATFORM", "videoout", 0);
    void* egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!egl)
    {
        const char* err = dlerror();
        info = std::string("libEGL.so.1 not loaded: ") + (err ? err : "?");
        return false;
    }
    dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
#define SSR_EGL_LOAD(name, type) \
    if (!(name = reinterpret_cast<type>(dlsym(egl, #name)))) \
    { \
        info = "libEGL.so.1 has no " #name; \
        return false; \
    }
    SSR_EGL_FUNCTIONS(SSR_EGL_LOAD)
    return true;
}
#endif
#ifdef __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

// The context made by SsrCreateGLContext, and what was current before SsrEnterGLContext.
static EGLDisplay OurDisplay = EGL_NO_DISPLAY;
static EGLContext OurContext = EGL_NO_CONTEXT;
static EGLSurface OurSurface = EGL_NO_SURFACE;
static EGLDisplay PrevDisplay = EGL_NO_DISPLAY;
static EGLSurface PrevDraw = EGL_NO_SURFACE, PrevRead = EGL_NO_SURFACE;
static EGLContext PrevContext = EGL_NO_CONTEXT;
static EGLenum PrevApi = EGL_OPENGL_ES_API;
static SDL_Window* SdlWindow = nullptr;
static SDL_GLContext SdlContext = nullptr;

void* SsrGLGetProc(const char* name)
{
#ifdef SSR_PS5
    if (!eglGetProcAddress) return nullptr;
#endif
    return (void*)eglGetProcAddress(name);
}

void SsrSetSdlContext(void* window, void* context)
{
    SdlWindow = static_cast<SDL_Window*>(window);
    SdlContext = static_cast<SDL_GLContext>(context);
}

static void SavePrevious()
{
    PrevApi = eglQueryAPI();
    PrevDisplay = eglGetCurrentDisplay();
    PrevDraw = eglGetCurrentSurface(EGL_DRAW);
    PrevRead = eglGetCurrentSurface(EGL_READ);
    PrevContext = eglGetCurrentContext();
}

void SsrEnterGLContext()
{
    SavePrevious();
    eglBindAPI(EGL_OPENGL_API);
    eglMakeCurrent(OurDisplay, OurSurface, OurSurface, OurContext);
}

void SsrLeaveGLContext()
{
    glFlush();
    eglMakeCurrent(OurDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (SdlWindow && SdlContext)
    {
        // SDL thinks its context never stopped being current and would skip making it current
        // again, so it is let go of first.
        SDL_GL_MakeCurrent(SdlWindow, nullptr);
        SDL_GL_MakeCurrent(SdlWindow, SdlContext);
    }
    else
    {
        eglBindAPI(PrevApi);
        if (PrevContext != EGL_NO_CONTEXT)
            eglMakeCurrent(PrevDisplay, PrevDraw, PrevRead, PrevContext);
    }
}

void SsrReleaseGLContext()
{
    glFlush();
    eglMakeCurrent(OurDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

static std::string EglError(const char* what)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s failed (EGL error 0x%x)", what, (unsigned)eglGetError());
    return buf;
}

bool SsrCreateGLContext(std::string& info)
{
#ifdef SSR_PS5
    if (!LoadEgl(info)) return false;
#endif
    // A display without a window: Mesa's surfaceless platform where there is one (a PC
    // without a desktop, as in the headless tests), otherwise the default display.
    EGLDisplay display = EGL_NO_DISPLAY;
#ifndef __SWITCH__
    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    const char* clientExt = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (getPlatformDisplay && clientExt && strstr(clientExt, "EGL_MESA_platform_surfaceless"))
        display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
#endif
    if (display == EGL_NO_DISPLAY) display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY)
    {
        info = "no EGL display";
        return false;
    }
    // (On the Switch SDL has already initialised this display for its own drawing; doing it
    // again only counts it.)
    if (!eglInitialize(display, nullptr, nullptr))
    {
        info = EglError("eglInitialize");
        return false;
    }
    SavePrevious();
    if (!eglBindAPI(EGL_OPENGL_API))
    {
        info = EglError("eglBindAPI(OpenGL)");
        return false;
    }
    // A configuration that can draw desktop OpenGL. Its kind of surface is not asked for: the
    // Switch's driver only has ones for windows (asking for off-screen ones found none, and
    // the context then failed with EGL_BAD_CONFIG); the drawing goes into the renderer's own
    // framebuffers anyway. (devkitPro's OpenGL example asks for the same.)
    const EGLint configLists[][16] = {
        { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
          EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE },
        { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE },
    };
    EGLConfig config = nullptr;
    EGLint count = 0;
    for (const EGLint* attrs : configLists)
        if (eglChooseConfig(display, attrs, &config, 1, &count) && count > 0) break;
    if (count < 1) config = nullptr;
    // OpenGL 4.3 core (as devkitPro's example), else 3.3 core, which melonDS's renderer needs.
    const EGLint contextLists[][8] = {
        { EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
          EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE },
        { EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
          EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE },
    };
    EGLContext context = EGL_NO_CONTEXT;
    std::string tried;
    for (const EGLint* attrs : contextLists)
    {
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, attrs);
        if (context != EGL_NO_CONTEXT) break;
        tried += EglError(attrs[3] == 4 ? "eglCreateContext(4.3 core)" : "eglCreateContext(3.3 core)") + "; ";
    }
    if (context == EGL_NO_CONTEXT)
    {
        // What the driver has, for the log.
        EGLint all = 0;
        eglGetConfigs(display, nullptr, 0, &all);
        const char* ext = eglQueryString(display, EGL_EXTENSIONS);
        char buf[96];
        snprintf(buf, sizeof(buf), "%d OpenGL configs of %d; ", (int)count, (int)all);
        info = tried + buf + "EGL extensions: " + (ext ? ext : "?");
        return false;
    }
    // Drawing goes into the renderer's own framebuffers, so no surface is needed where EGL
    // allows that (EGL_KHR_surfaceless_context); otherwise a small off-screen one.
    OurDisplay = display;
    OurContext = context;
    OurSurface = EGL_NO_SURFACE;
    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
    {
        const std::string surfacelessError = EglError("eglMakeCurrent without a surface");
        EGLSurface surface = EGL_NO_SURFACE;
        const EGLint pbufferConfigAttrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
        EGLConfig pconfig = nullptr;
        EGLint pcount = 0;
        if (eglChooseConfig(display, pbufferConfigAttrs, &pconfig, 1, &pcount) && pcount > 0)
        {
            const EGLint pbufferAttrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
            surface = eglCreatePbufferSurface(display, pconfig, pbufferAttrs);
        }
        if (surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context))
        {
            const char* ext = eglQueryString(display, EGL_EXTENSIONS);
            info = surfacelessError + "; " + (surface == EGL_NO_SURFACE ? std::string("no off-screen surface") : EglError("eglMakeCurrent(off-screen)"))
                + "; EGL extensions: " + (ext ? ext : "?");
            eglDestroyContext(display, context);
            OurContext = EGL_NO_CONTEXT;
            return false;
        }
        OurSurface = surface;
    }
    if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress))
    {
        info = "the OpenGL functions could not be loaded";
        return false;
    }
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    info = std::string(renderer ? renderer : "?") + ", OpenGL " + (version ? version : "?");
    return true;
}
