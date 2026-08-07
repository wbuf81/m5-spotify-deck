#pragma once

// Draws Daisy onto the panel at an integer scale.
//
// The sprite data is 8-bit indexed pixel art in flash (see DaisyAssets.h);
// nothing here allocates. Frames redraw as a cell diff against the previous
// frame — the sprite is 55x41 = 2255 cells but between two frames of a walk
// cycle only the legs and tail move, so a diff is dozens of fillRects rather
// than thousands, and there is no flicker because nothing is cleared first.

#include <cstdint>

#include "DaisyAssets.h"

namespace daisy {

// Which frame an animation shows at time t. Wraps forever.
int frameAt(DaisyAnim anim, uint32_t t_ms);

// Full draw of one frame. Transparent cells are painted with `bg`, so the
// sprite's bounding box owns its rectangle and never needs a separate clear.
void draw(DaisyAnim anim, int frame, int x, int y, int scale, uint16_t bg);

// Redraw only the cells that differ between two frames of the SAME animation.
// prev < 0, or a different animation, falls back to a full draw.
void drawDiff(DaisyAnim prev_anim, int prev_frame, DaisyAnim anim, int frame,
              int x, int y, int scale, uint16_t bg);

}  // namespace daisy
