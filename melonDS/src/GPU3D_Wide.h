// ssrecomp: widescreen, full-resolution rendering of the 3D scene.
//
// The DS draws its 3D scene 256x192 pixels large through a 4:3 view. For a wide display
// the frame's polygons are submitted a second time with the horizontal clip coordinate
// squeezed, so that what lies left and right of the original view survives clipping, and
// are then drawn by a separate rasteriser at the size of the output. The emulated state
// is not touched: the game sees exactly the polygon counts and results it would see
// without any of this.
#pragma once
#include <vector>
#include "types.h"

namespace melonDS
{
class GPU;
class GPU3D;

struct WideState
{
    GPU3D* Owner = nullptr;   // the console whose scene is drawn wide; nullptr turns it off
    int Width = 0, Height = 0; // size to draw at, set by the frontend
    float ViewWidth = 256;    // width of the view in DS pixels (256 = no extra width)
    // Polygons ending a little short of the view's edge are continued to it (GPU3D_Wide.cpp,
    // ExtendToEdges): for Black and White's skies and lookouts. Off for SoulSilver, HeartGold
    // and Platinum, whose overhead views have none and where it only stretched trees and opened
    // cracks between the ground's tiles.
    bool ExtendEdges = true;
    // Black and White's sunshine (their clear weather, set by the frontend while it runs): its
    // lens flare is one picture on a polygon over the view's upper left corner, which squeezed
    // like the world would shine from the DS's corner in the middle of the wide view. It keeps
    // its size and is moved to the wide view's corner instead (GPU3D_Wide.cpp, FlareAtCorner).
    bool FlareAtLeft = false;

    // The image that belongs to the frame the 2D renderer has just output: XRGB, with
    // the top byte 0 where no polygon was drawn. Three buffers take turns, so it stays as it
    // is until the one after the next is published: it can be read while the next frame is
    // emulated and drawn. (It may be memory the graphics chip writes, see GPU3D_WideGpu.h.)
    const u32* Front = nullptr;
    int FrontW = 0, FrontH = 0;
    u64 FrontSerial = 0;      // changes whenever Front does
};
extern WideState Wide;

// Blocks until the widescreen picture being drawn in the background is finished (the frontend
// calls it before shutting down).
void WideWait();
// Ends its drawing threads (the frontend calls it before shutting down, after WideWait).
void WideShutdown();
// Before the graphics chip's renderer goes (GPU3D_WideGpu.h): pictures in its memory are
// copied out.
void WideReleaseGpu();
}
