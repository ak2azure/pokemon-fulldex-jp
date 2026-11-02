#pragma once
// SDL's renderer done with Vulkan, for the PS5 title (SSR_PS5_NATIVE with SSR_VK_RENDER): a title
// cannot load Mesa, so SDL has no renderer that uses the graphics chip there. These are the
// renderer functions the program uses (the game list, the game's picture, the pause menu:
// textures, copies of them with flips and the colour and alpha changes, filled rectangles,
// triangles, the five blend modes, reading the picture back), with SDL's own signatures, drawn on
// the device the widescreen world uses (melonDS/src/VulkanSupport.h). The title's link sends
// SDL's names here (--wrap, tools/ps5/build_native.sh), so the code that draws stays as it is.
//
// Each frame's draws are recorded in order and carried out at Present (or when the picture is
// read back) into a picture of the screen's size, which is then copied to the screen: VK_KHR_display
// on the PS5's video output. Without a screen (a PC's test, tools/vk_render_test.cpp) only the
// picture is drawn.
#include <SDL.h>
#include <string>

namespace VkRender
{
// Vulkan started with the screen (the PS5: RADV, the display at its largest mode near 60 Hz),
// or without one (w x h, for tests). False (and why, in info) if it could not be.
bool Start(std::string& info, bool screen, int w = 0, int h = 0);
// Whether it started: SDL's renderer functions then come here (on the PS5 title; otherwise to
// SDL's own renderer, which draws with the processor).
bool Active();

SDL_Renderer* CreateRenderer(SDL_Window* window, int index, Uint32 flags);
void DestroyRenderer(SDL_Renderer* r);
int GetRendererInfo(SDL_Renderer* r, SDL_RendererInfo* info);
int GetRendererOutputSize(SDL_Renderer* r, int* w, int* h);
SDL_Texture* CreateTexture(SDL_Renderer* r, Uint32 format, int access, int w, int h);
void DestroyTexture(SDL_Texture* t);
int UpdateTexture(SDL_Texture* t, const SDL_Rect* rect, const void* pixels, int pitch);
int SetTextureBlendMode(SDL_Texture* t, SDL_BlendMode mode);
int SetTextureScaleMode(SDL_Texture* t, SDL_ScaleMode mode);
int SetTextureColorMod(SDL_Texture* t, Uint8 r, Uint8 g, Uint8 b);
int SetTextureAlphaMod(SDL_Texture* t, Uint8 a);
int SetRenderDrawColor(SDL_Renderer* r, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha);
int SetRenderDrawBlendMode(SDL_Renderer* r, SDL_BlendMode mode);
int RenderClear(SDL_Renderer* r);
int RenderFillRect(SDL_Renderer* r, const SDL_Rect* rect);
int RenderFillRects(SDL_Renderer* r, const SDL_Rect* rects, int count);
int RenderCopy(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst);
int RenderCopyF(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_FRect* dst);
int RenderCopyEx(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst, double angle,
    const SDL_Point* center, SDL_RendererFlip flip);
int RenderGeometry(SDL_Renderer* r, SDL_Texture* t, const SDL_Vertex* vertices, int count, const int* indices, int indexCount);
int RenderReadPixels(SDL_Renderer* r, const SDL_Rect* rect, Uint32 format, void* pixels, int pitch);
void RenderPresent(SDL_Renderer* r);
}
