#pragma once

#if CROSSPOINT_BENCH_CONSOLE

#include "activities/Activity.h"
#include "sleepcards/SleepCard.h"

// Bench console CARD verb (dev builds): draws a sleep-screen card exactly as
// the sleep screen would (sleepcards::drawDeviceCard, the same HALF refresh)
// but stays awake; the next key press or tap returns to the screen below.
// CardId::None = "default", the logo screen the Dark/Light sleep screen draws.
class SleepCardPreviewActivity final : public Activity {
 public:
  static constexpr const char* NAME = "SleepCardPreview";

  struct Result {
    bool valid = false;
    sleepcards::CardId shown = sleepcards::CardId::None;
    const char* outcome = "";  // drawn | pictures | declined | logo
    unsigned long ms = 0;      // compute + draw, before the refresh
  };

  SleepCardPreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, sleepcards::CardId card)
      : Activity(NAME, renderer, mappedInput), card(card) {}
  void onEnter() override;
  void loop() override;
  // Draw another card in place (a second CARD while one is shown: no second preview stacked).
  void show(sleepcards::CardId next);

  // What the last preview showed (read by the bench console once the switch has happened).
  static Result lastResult();

 private:
  void draw();

  sleepcards::CardId card;
};

#endif  // CROSSPOINT_BENCH_CONSOLE
