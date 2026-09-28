#pragma once

// Host stand-in for the e-ink HAL: an 800x480 1-bit framebuffer in memory
// (bit 1 = white, MSB first, the panel's native landscape rows) that the real
// GfxRenderer draws into. Refreshes are counted, nothing else happens.
#include <Arduino.h>
#include <BoardConfig.h>
#include <GrayscaleCapabilities.h>

#include <cstdint>

class HalDisplay {
 public:
  using Controller = BoardConfig::DisplayController;
  using GrayscaleMode = freeink::GrayscaleMode;
  using GrayscaleCapabilities = freeink::GrayscaleCapabilities;
  using GrayscaleBase = freeink::GrayscaleBase;
  using GrayscaleEncoding = freeink::GrayscaleEncoding;

  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };

  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  Controller getController() const { return Controller::SSD1677; }
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode = GrayscaleMode::Overlay) const { return {}; }

  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  void displayBuffer(RefreshMode = FAST_REFRESH, bool = false) { refreshes++; }
  void displayBufferAsync(RefreshMode = FAST_REFRESH) { refreshes++; }
  void waitRefreshComplete() {}
  bool supportsAsyncRefresh() const { return false; }
  bool supportsAsyncGrayscaleBase() const { return false; }
  void refreshDisplay(RefreshMode = FAST_REFRESH, bool = false) { refreshes++; }
  void setInverted(bool inverted) { inverted_ = inverted; }
  bool isInverted() const { return inverted_; }
  void deepSleep() {}

  uint8_t* getFrameBuffer() const;
  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut);
  void returnFrameBufferStorage() {}

  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
  void displayGrayscaleBase(RefreshMode = HALF_REFRESH, bool = false) { refreshes++; }
  bool displayGrayscaleBase(GrayscaleMode, RefreshMode = HALF_REFRESH, bool = false) { return false; }
  void copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t*) {}
  void grayscaleRevert() {}
  void displayGrayBuffer(bool = false) {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
  bool supportsStripGrayscale() const { return false; }
  bool combinesGrayscaleBase() const { return false; }

  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }

  int refreshes = 0;

 private:
  void blit(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool transparent) const;
  bool inverted_ = false;
};

extern HalDisplay display;
