// A small PS5 title (PKFD00099, "Fulldex Test") that tries, one at a time, what Pokemon
// Fulldex needs as a title of its own, before the whole program is tried: out of the sandbox
// for /data, the picture (SDL on the video output), the controller, the sound, the heap in
// direct memory and code made at run time (the JIT's). Each step goes to
// /data/pokemon-ds-wide/native-probe.log (or the title's /download0 without /data) and to the
// screen as a square: green when it worked, red when it did not. The squares' row of pad
// buttons lights up as they are pressed; Cross plays a tone; Options closes the title.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "../../../frontend/ps5_native.h"

namespace
{
struct Check
{
    const char* name;
    bool ok;
};
std::vector<Check> checks;

void Note(const char* name, bool ok)
{
    checks.push_back({ name, ok });
    fprintf(stderr, "[probe] %s: %s\n", name, ok ? "ok" : "FAILED");
    fflush(stderr);
}

float tonePhase = 0;
int toneFrames = 0;

void Audio(void*, Uint8* stream, int len)
{
    int16_t* out = (int16_t*)stream;
    const int frames = len / 4;
    for (int i = 0; i < frames; i++)
    {
        int16_t v = 0;
        if (toneFrames > 0)
        {
            v = (int16_t)(sinf(tonePhase) * 6000);
            tonePhase += 2 * 3.14159265f * 440 / 48000;
            if (tonePhase > 2 * 3.14159265f) tonePhase -= 2 * 3.14159265f;
            toneFrames--;
        }
        out[i * 2] = out[i * 2 + 1] = v;
    }
}
}

int main(int, char**)
{
    Ps5Native::Prepare();
    const char* dir = "/data/pokemon-ds-wide";
    const bool data = Ps5Native::FreeFromSandbox(dir);
    const std::string log = std::string(data ? dir : Ps5Native::FallbackDir()) + "/native-probe.log";
    if (freopen(log.c_str(), "w", stderr)) setvbuf(stderr, nullptr, _IOLBF, 0);
    fprintf(stderr, "[probe] Pokemon Fulldex test title, pid %d, uid %d\n", (int)getpid(), (int)geteuid());
    Note("out of the sandbox (/data written)", data);

    int roms = 0;
    if (DIR* d = opendir((std::string(dir) + "/roms").c_str()))
    {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.')
            {
                fprintf(stderr, "[probe] roms: %s\n", e->d_name);
                roms++;
            }
        closedir(d);
    }
    Note("ROM folder listed (at least one file)", roms > 0);

    // 1 GiB from the heap, touched: more than the console's own libc heap gives a title.
    std::vector<char*> blocks;
    bool heap = true;
    for (int i = 0; i < 16 && heap; i++)
    {
        char* b = (char*)malloc(64 << 20);
        heap = b != nullptr;
        if (b)
        {
            memset(b, i, 64 << 20);
            blocks.push_back(b);
        }
    }
    for (char* b : blocks) free(b);
    Note("1 GiB from the heap", heap);
    Note("code made and run (JIT)", Ps5Native::CanRunJit());

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    const bool sdl = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0;
    Note("SDL started", sdl);
    if (!sdl)
    {
        fprintf(stderr, "[probe] SDL_Init: %s\n", SDL_GetError());
        sceSystemServiceHideSplashScreen();
        sceSystemServiceLoadExec("exit", nullptr);
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Fulldex Test", 0, 0, 1920, 1080, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    Note("picture (window and renderer)", renderer != nullptr);
    if (!renderer) fprintf(stderr, "[probe] no picture: %s\n", SDL_GetError());

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = Audio;
    const SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    Note("sound opened", audio != 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);
    else fprintf(stderr, "[probe] no sound: %s\n", SDL_GetError());

    SDL_GameController* pad = nullptr;
    bool padSeen = false;
    sceSystemServiceHideSplashScreen();
    for (bool run = true; run;)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT) run = false;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad)
            {
                pad = SDL_GameControllerOpen(e.cdevice.which);
                if (pad && !padSeen)
                {
                    padSeen = true;
                    Note("controller", true);
                }
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN)
            {
                fprintf(stderr, "[probe] button %d\n", e.cbutton.button);
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A)
                {
                    SDL_LockAudioDevice(audio);
                    toneFrames = 48000 / 4;
                    SDL_UnlockAudioDevice(audio);
                }
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) run = false;
            }
        }
        if (!renderer)
        {
            SDL_Delay(16);
            continue;
        }
        SDL_SetRenderDrawColor(renderer, 16, 24, 48, 255);
        SDL_RenderClear(renderer);
        for (size_t i = 0; i < checks.size(); i++)
        {
            SDL_Rect r = { 120 + (int)i * 160, 200, 120, 120 };
            SDL_SetRenderDrawColor(renderer, checks[i].ok ? 40 : 220, checks[i].ok ? 200 : 40, 60, 255);
            SDL_RenderFillRect(renderer, &r);
        }
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
        {
            const bool down = pad && SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)b);
            SDL_Rect r = { 120 + b * 90, 500, 70, 70 };
            SDL_SetRenderDrawColor(renderer, down ? 255 : 70, down ? 220 : 70, down ? 60 : 90, 255);
            SDL_RenderFillRect(renderer, &r);
        }
        SDL_RenderPresent(renderer);
    }
    fprintf(stderr, "[probe] closing\n");
    fflush(stderr);
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_Quit();
    sceSystemServiceLoadExec("exit", nullptr);
    return 0;
}
