// Checks frontend/vkrender.cpp (SDL's renderer done with Vulkan, for the PS5 title) against SDL's
// own software renderer: the same draws, the kinds the program makes (textures copied at a scale
// with and without smoothing, flipped, coloured, in the five blend modes; filled rectangles; a
// texture changed between draws; triangles), on a PC with any Vulkan (Mesa's lavapipe will do).
// Prints how many pixels differ and by how much, and writes both pictures (PPM) and a map of
// where they differ.
//
//   c++ -std=c++17 -O2 tools/vk_render_test.cpp frontend/vkrender.cpp melonDS/src/VulkanSupport.cpp \
//       -ImelonDS/src -Ifrontend -I<folder with render_spv.h> $(sdl2-config --cflags --libs) -ldl -o vk_render_test
//   SDL_VIDEODRIVER=dummy ./vk_render_test <out folder>
#include "vkrender.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr int W = 1280, H = 720;

struct Pair
{
    SDL_Renderer* sw;
    SDL_Texture* swt[6];
    SDL_Texture* vkt[6];
};

// One draw on both renderers. (The software renderer is SDL's; the other is the stand-in.)
#define BOTH(fn, ...) do { SDL_##fn(p.sw, __VA_ARGS__); VkRender::fn(nullptr, __VA_ARGS__); } while (0)
#define BOTH_T(fn, i, ...) do { SDL_##fn(p.swt[i], __VA_ARGS__); VkRender::fn(p.vkt[i], __VA_ARGS__); } while (0)
#define COPY(fn, i, ...) do { SDL_##fn(p.sw, p.swt[i], __VA_ARGS__); VkRender::fn(nullptr, p.vkt[i], __VA_ARGS__); } while (0)

std::vector<uint32_t> Pattern(int w, int h, int kind)
{
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            uint32_t r = (x * 255 / std::max(1, w - 1)), g = (y * 255 / std::max(1, h - 1)), b = ((x / 8 + y / 8) & 1) ? 220 : 40;
            uint32_t a = 255;
            if (kind == 1) a = (x * 255 / std::max(1, w - 1));          // alpha across
            if (kind == 2) a = ((x / 4 + y / 4) % 3 == 0) ? 0 : 255;    // holes
            if (kind == 3) { r = g = b = 255; a = (y < h / 2) ? 160 : 0; } // an overlay's box
            if (kind == 2 && a) { r = r * a / 255; g = g * a / 255; b = b * a / 255; } // premultiplied
            px[(size_t)y * w + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    return px;
}

void WritePpm(const std::string& path, const std::vector<uint32_t>& px)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (uint32_t c : px)
    {
        const unsigned char rgb[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
}

int main(int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    std::string info;
    if (!VkRender::Start(info, false, W, H)) { fprintf(stderr, "Vulkan: %s\n", info.c_str()); return 1; }
    printf("Vulkan: %s\n", info.c_str());
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    Pair p;
    // REF=gles: against SDL's OpenGL ES 2 renderer (what the PS5 payload draws with, on Mesa) in a
    // window off the screen (SDL_VIDEODRIVER=offscreen), which also does the smoothing and the
    // custom blend modes the software renderer does not.
    const char* ref = getenv("REF");
    if (ref && !strcmp(ref, "gles"))
    {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
        SDL_Window* win = SDL_CreateWindow("ref", 0, 0, W, H, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
        p.sw = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : nullptr;
        if (!p.sw) { fprintf(stderr, "no OpenGL ES renderer: %s\n", SDL_GetError()); return 1; }
        SDL_RendererInfo ri;
        SDL_GetRendererInfo(p.sw, &ri);
        printf("reference: %s\n", ri.name);
    }
    else
        p.sw = SDL_CreateSoftwareRenderer(surface);
    const int size[6][2] = { { 256, 192 }, { 64, 64 }, { 96, 48 }, { 640, 360 }, { 16, 192 }, { 256, 192 } };
    for (int i = 0; i < 6; i++)
    {
        p.swt[i] = SDL_CreateTexture(p.sw, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, size[i][0], size[i][1]);
        p.vkt[i] = VkRender::CreateTexture(nullptr, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, size[i][0], size[i][1]);
        const std::vector<uint32_t> px = Pattern(size[i][0], size[i][1], i == 1 ? 1 : i == 2 ? 2 : i == 3 ? 3 : 0);
        BOTH_T(UpdateTexture, i, nullptr, px.data(), size[i][0] * 4);
    }
    const SDL_BlendMode premul = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    const SDL_BlendMode edge = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);

    // The game's screens: a DS screen three times its size, nearest, no blending.
    BOTH(SetRenderDrawColor, 20, 30, 60, 255);
    SDL_RenderClear(p.sw);
    VkRender::RenderClear(nullptr);
    BOTH_T(SetTextureBlendMode, 0, SDL_BLENDMODE_NONE);
    BOTH_T(SetTextureScaleMode, 0, SDL_ScaleModeNearest);
    SDL_Rect d = { 40, 20, 768, 576 };
    COPY(RenderCopy, 0, nullptr, &d);
    // Smoothed and at a fraction of a pixel, the picture darkened (colour modulation).
    BOTH_T(SetTextureScaleMode, 5, SDL_ScaleModeLinear);
    BOTH_T(SetTextureColorMod, 5, 180, 180, 180);
    SDL_FRect fd = { 830.5f, 20.25f, 400.0f, 300.0f };
    COPY(RenderCopyF, 5, nullptr, &fd);
    // A piece with alpha, flipped each way, blended.
    BOTH_T(SetTextureBlendMode, 1, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 4; i++)
    {
        SDL_Rect r = { 840 + i * 100, 360, 96, 96 };
        COPY(RenderCopyEx, 1, nullptr, &r, 0.0, nullptr, (SDL_RendererFlip)i);
    }
    // Premultiplied pieces with holes, and the edge mode.
    BOTH_T(SetTextureBlendMode, 2, premul);
    SDL_Rect r2 = { 840, 480, 288, 144 };
    COPY(RenderCopy, 2, nullptr, &r2);
    BOTH_T(SetTextureBlendMode, 4, edge);
    SDL_Rect r4 = { 1140, 480, 64, 192 };
    COPY(RenderCopy, 4, nullptr, &r4);
    // Filled rectangles, blended and not.
    BOTH(SetRenderDrawBlendMode, SDL_BLENDMODE_BLEND);
    BOTH(SetRenderDrawColor, 255, 200, 0, 128);
    SDL_Rect rects[2] = { { 100, 100, 200, 60 }, { 300, 400, 120, 120 } };
    BOTH(RenderFillRects, rects, 2);
    BOTH(SetRenderDrawBlendMode, SDL_BLENDMODE_NONE);
    BOTH(SetRenderDrawColor, 0, 255, 0, 255);
    SDL_Rect g = { 600, 620, 100, 50 };
    BOTH(RenderFillRect, &g);
    // A texture changed between two draws of it.
    const std::vector<uint32_t> other = Pattern(256, 192, 2);
    SDL_Rect part = { 64, 48, 128, 96 };
    BOTH_T(UpdateTexture, 0, &part, other.data() + 48 * 256 + 64, 256 * 4);
    SDL_Rect d2 = { 40, 600, 160, 120 };
    COPY(RenderCopy, 0, nullptr, &d2);
    // The pause menu: drawn at half the size and doubled, nearest, blended, with its alpha.
    BOTH_T(SetTextureBlendMode, 3, SDL_BLENDMODE_BLEND);
    BOTH_T(SetTextureScaleMode, 3, SDL_ScaleModeNearest);
    BOTH_T(SetTextureAlphaMod, 3, 200);
    COPY(RenderCopy, 3, nullptr, nullptr);
    // Triangles (the touch controls).
    SDL_Vertex tri[3] = { { { 300, 650 }, { 255, 0, 0, 160 }, { 0, 0 } }, { { 380, 700 }, { 0, 0, 255, 160 }, { 0, 0 } },
                          { { 260, 710 }, { 255, 255, 255, 160 }, { 0, 0 } } };
    BOTH(RenderGeometry, nullptr, tri, 3, nullptr, 0);

    std::vector<uint32_t> a((size_t)W * H), b((size_t)W * H);
    SDL_RenderReadPixels(p.sw, nullptr, SDL_PIXELFORMAT_ARGB8888, a.data(), W * 4);
    VkRender::RenderReadPixels(nullptr, nullptr, SDL_PIXELFORMAT_ARGB8888, b.data(), W * 4);
    WritePpm(out + "/sdl.ppm", a);
    WritePpm(out + "/vulkan.ppm", b);
    std::vector<uint32_t> map((size_t)W * H);
    long differ = 0, big = 0;
    int worst = 0;
    for (size_t i = 0; i < a.size(); i++)
    {
        int m = 0;
        for (int s = 0; s < 24; s += 8) m = std::max(m, std::abs((int)((a[i] >> s) & 255) - (int)((b[i] >> s) & 255)));
        differ += m > 0;
        big += m > 8;
        worst = std::max(worst, m);
        map[i] = m > 8 ? 0xFF0000 : m > 0 ? 0xFFFF00 : (a[i] >> 2) & 0x3F3F3F;
    }
    WritePpm(out + "/differ.ppm", map);
    printf("%ld of %d pixels differ, %ld by more than 8, at most by %d\n", differ, W * H, big, worst);
    return 0;
}
