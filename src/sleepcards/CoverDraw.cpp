#include "CoverDraw.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "CardDraw.h"

namespace sleepcards::cover {

bool fitSize(const int w, const int h, const int maxWidth, const int maxHeight, int& outWidth, int& outHeight) {
  outWidth = 0;
  outHeight = 0;
  if (w <= 0 || h <= 0 || maxWidth <= 0 || maxHeight <= 0) return false;
  if (w <= maxWidth && h <= maxHeight) {
    outWidth = w;
    outHeight = h;
    return true;
  }
  // The tighter of the two limits, in integers: w * maxHeight vs h * maxWidth.
  if (static_cast<long>(w) * maxHeight >= static_cast<long>(h) * maxWidth) {
    outWidth = maxWidth;
    outHeight = std::max(1, static_cast<int>(static_cast<long>(h) * maxWidth / w));
  } else {
    outHeight = maxHeight;
    outWidth = std::max(1, static_cast<int>(static_cast<long>(w) * maxHeight / h));
  }
  return true;
}

bool drawOneBitCover(GfxRenderer& renderer, const Bitmap& bitmap, const int x, const int y, const int outWidth,
                     const int outHeight) {
  const int w = bitmap.getWidth();
  const int h = bitmap.getHeight();
  if (!bitmap.is1Bit() || outWidth <= 0 || outHeight <= 0 || outWidth > w || outHeight > h) return false;
  if (outWidth == w && outHeight == h) return renderer.drawBitmap(bitmap, x, y, 0, 0);

  // readNextRow's output (2 bits a pixel), the file's row, and per output column of the output row
  // being gathered: its inked and its total source pixels.
  const size_t outRow = static_cast<size_t>((w + 3) / 4);
  const size_t fileRow = static_cast<size_t>(bitmap.getRowBytes());
  const size_t cols = static_cast<size_t>(outWidth);
  auto scratch = makeUniqueNoThrow<uint8_t[]>(outRow + fileRow);
  auto counts = makeUniqueNoThrow<uint16_t[]>(2 * cols);  // its own array: uint16_t stays aligned
  if (!scratch || !counts) {
    LOG_ERR("CARD", "OOM: cover rows");
    return false;
  }
  uint8_t* row = scratch.get();
  uint8_t* raw = row + outRow;
  uint16_t* ink = counts.get();
  uint16_t* total = counts.get() + cols;

  int pendingRow = -1;  // the output row the counts belong to
  const auto flush = [&] {
    if (pendingRow < 0) return;
    const int sy = y + pendingRow;
    for (int ox = 0; ox < outWidth; ox++) {
      const uint16_t n = total[ox];
      if (n == 0) continue;
      // The share of inked source pixels as a dither level 0..16 on the screen's own pattern.
      const auto level = static_cast<uint8_t>((ink[ox] * draw::DITHER_LEVELS + n / 2) / n);
      if (draw::ditherInk(x + ox, sy, level)) renderer.drawPixel(x + ox, sy, true);
    }
  };
  for (int fileY = 0; fileY < h; fileY++) {
    if (bitmap.readNextRow(row, raw) != BmpReaderError::Ok) {
      LOG_ERR("CARD", "cover row %d unreadable", fileY);
      return false;
    }
    const int imageY = bitmap.isTopDown() ? fileY : h - 1 - fileY;
    const int oy = static_cast<int>(static_cast<long>(imageY) * outHeight / h);
    if (oy != pendingRow) {
      flush();
      pendingRow = oy;
      std::memset(counts.get(), 0, 2 * cols * sizeof(uint16_t));
    }
    for (int bx = 0; bx < w; bx++) {
      const auto ox = static_cast<size_t>(static_cast<long>(bx) * outWidth / w);
      const uint8_t v = (row[bx / 4] >> (6 - (bx % 4) * 2)) & 0x3;
      total[ox]++;
      if (v < 3) ink[ox]++;  // as drawBitmap1Bit: anything below white is ink
    }
  }
  flush();
  return true;
}

}  // namespace sleepcards::cover
