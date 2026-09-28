#pragma once

#include "SleepCard.h"

// The device side of the sleep cards (never built on the host: the preview
// harness has its own context and CardIo). SleepActivity and the bench
// console's CARD verb both go through drawDeviceCard(), so a card previewed
// on the bench is exactly the card the reader sleeps with.
namespace sleepcards {

enum class CardOutcome : uint8_t {
  Drawn,     // the frame holds the card and its footer (not yet refreshed)
  Pictures,  // Shuffle picked the picture frame: the caller shows the CUSTOM sleep screen
  Declined,  // the card had nothing true to show; the frame is cleared: draw the logo screen
};

// Fill ctx from the live device: settings, RTC, battery, the open book, the device CardIo.
void buildDeviceContext(CardContext& ctx);

// Build the context and draw `requested` (Shuffle is resolved here; shown = what was picked).
// Logs the time it took.
CardOutcome drawDeviceCard(GfxRenderer& renderer, CardId requested, CardId* shown = nullptr);

}  // namespace sleepcards
