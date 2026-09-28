#include "HalDisplay.h"

#include <cstring>

namespace {
uint8_t frameBuffer[HalDisplay::BUFFER_SIZE];
}

HalDisplay display;

uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  if (sizeOut) *sizeOut = BUFFER_SIZE;
  return frameBuffer;
}

void HalDisplay::clearScreen(const uint8_t color) const { std::memset(frameBuffer, color, BUFFER_SIZE); }

// Per-pixel copy of FreeInkDisplay::blitImage: 1 bit per pixel, MSB first, 1 = white.
void HalDisplay::blit(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                      const bool transparent) const {
  const uint16_t imageWidthBytes = (w + 7) / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint32_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) break;
    for (uint16_t col = 0; col < w; col++) {
      const uint32_t destX = x + col;
      if (destX >= DISPLAY_WIDTH) break;
      const bool white = (imageData[row * imageWidthBytes + col / 8] >> (7 - (col & 7))) & 1;
      if (transparent && white) continue;
      uint8_t& cell = frameBuffer[destY * DISPLAY_WIDTH_BYTES + destX / 8];
      const uint8_t mask = static_cast<uint8_t>(0x80 >> (destX & 7));
      if (white)
        cell |= mask;
      else
        cell &= static_cast<uint8_t>(~mask);
    }
  }
}

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, bool) const {
  blit(imageData, x, y, w, h, false);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                                      const uint16_t h, bool) const {
  blit(imageData, x, y, w, h, true);
}
