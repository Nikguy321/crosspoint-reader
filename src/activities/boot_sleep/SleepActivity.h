#pragma once
#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "sleepcards/CardId.h"

class Bitmap;
class HalFile;

// The sleep screen. Normally drawn once on entry, after which main.cpp deep-sleeps. Live (X4 Pro
// on external power, util/LiveSleepPolicy.h): it stays up, loop() redraws it on the minute every
// Charging Updates interval and ticks the Wi-Fi station keeper, until main.cpp ends it with a
// key (a restart to where the reader was) or an unplug (a final redraw, then deep sleep).
class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false,
                         bool live = false, bool popup = true)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout), live(live), popup(popup) {}
  ~SleepActivity() override;
  void onEnter() override;
  void loop() override;
  // Home on a sleep screen is not a way out (a live one wakes on a physical key only).
  bool handleHomeGesture() override { return true; }

  // The live screen, for main.cpp and the bench console (all no-ops without one).
  struct LiveStatus {
    bool active = false;
    uint32_t nextRedrawInMs = 0;  // 0 when no redraw is scheduled (a picture without the cycle)
    const char* card = "none";    // sleepcards::cardName of the last card drawn
    const char* screen = "none";  // card | picture | logo
    uint32_t redraws = 0;         // since entry
  };
  static LiveStatus liveStatus();
  // Redraw at the next loop() pass (bench REDRAW, a bench SLEEP on the live screen, a new forecast
  // for the Weather card). halfRefresh: a clean HALF pass even for the same card - its data changed
  // (a black alert band or dithered bars would ghost under FAST).
  static void requestLiveRedraw(bool halfRefresh = false);
  // The last redraw before deep sleep: the card on screen again with a fresh time and battery
  // (nothing for a picture or the logo, which show neither).
  static void finalLiveRedraw();

 private:
  enum class LiveScreen : uint8_t { None, Card, Picture, Logo };

  void renderDefaultSleepScreen() const;
  // The logo screen (renderDefaultSleepScreen), with a FAST refresh for a live same-screen redraw.
  // cardStyle: the cards' polarity (the live screen's fallback), whatever the Sleep Screen mode.
  void renderLogoScreen(bool halfRefresh, bool cardStyle) const;
  void renderCustomSleepScreen() const;
  // A picture from /sleep.bmp, /.sleep or /sleep; false (nothing drawn) when there is none. The
  // live cycle prefers the folders, so /sleep.bmp does not show every time.
  bool renderPictureFrame(bool cycling) const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;
  void renderCardSleepScreen() const;

  // Live: draw the next screen (entry: the first one; final: the card on screen again).
  void drawLive(bool entry, bool final);
  void scheduleNextRedraw();
  bool liveRedrawWanted() const;

  bool fromTimeout = false;
  bool live = false;
  bool popup = true;
  sleepcards::CardId lastShown = sleepcards::CardId::None;
  LiveScreen lastScreen = LiveScreen::None;
  uint8_t fastSinceHalf = 0;
  bool picturesEmpty = false;  // the picture frame found no picture this session
  uint32_t nextRedrawAt = 0;
  uint32_t redraws = 0;
};
