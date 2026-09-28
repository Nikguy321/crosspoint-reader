#pragma once

class Bitmap;
class GfxRenderer;

// The Now Reading card's cover. It uses only the reader's 1-bit cover thumbnails (already dithered
// for the panel): the card draws in black and white, where a 2-bit cover's greys would all turn
// black. A thumbnail that fits is drawn 1:1; a smaller copy averages the ink under each output
// pixel and re-dithers it on the screen's ordered pattern - never shrunk by dropping rows and
// columns, which breaks a dither up into stripes.
namespace sleepcards::cover {

// The size a w x h cover is drawn at in a maxWidth x maxHeight box: as it is when it fits, else
// scaled down with its aspect kept (never up). False when the box or the cover is empty.
bool fitSize(int w, int h, int maxWidth, int maxHeight, int& outWidth, int& outHeight);

// Draw a parsed 1-bit bitmap at outWidth x outHeight (at most its own size), top-left at (x, y).
// Reads the rows once, in file order, into one row buffer. False on a read error, a size larger
// than the bitmap's, or anything but a 1-bit bitmap.
bool drawOneBitCover(GfxRenderer& renderer, const Bitmap& bitmap, int x, int y, int outWidth, int outHeight);

}  // namespace sleepcards::cover
