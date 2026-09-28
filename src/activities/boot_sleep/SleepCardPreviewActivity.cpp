#include "SleepCardPreviewActivity.h"

#if CROSSPOINT_BENCH_CONSOLE

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "sleepcards/BrandScreen.h"
#include "sleepcards/DeviceCards.h"

namespace {
SleepCardPreviewActivity::Result last;
}

SleepCardPreviewActivity::Result SleepCardPreviewActivity::lastResult() { return last; }

void SleepCardPreviewActivity::onEnter() {
  Activity::onEnter();
  draw();
}

void SleepCardPreviewActivity::show(const sleepcards::CardId next) {
  card = next;
  draw();
}

void SleepCardPreviewActivity::draw() {
  RenderLock lock;
  const auto previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  // Like SleepActivity: fresh content in normal polarity.
  display.setInverted(false);

  const unsigned long start = millis();
  last = Result{};
  last.valid = true;
  last.shown = card;
  bool invert = false;
  if (card == sleepcards::CardId::None) {
    last.outcome = "logo";
  } else {
    switch (sleepcards::drawDeviceCard(renderer, card, &last.shown)) {
      case sleepcards::CardOutcome::Drawn:
        last.outcome = "drawn";
        break;
      case sleepcards::CardOutcome::Pictures:
        last.outcome = "pictures";  // the picture frame reads the card; the preview shows the logo
        break;
      case sleepcards::CardOutcome::Declined:
      default:
        last.outcome = "declined";
        break;
    }
  }
  if (strcmp(last.outcome, "drawn") != 0) {
    // What the sleep screen falls back to: the logo, dark only for the classic Dark screen.
    sleepcards::drawX4ProLogoScreen(renderer, tr(STR_SLEEPING));
    invert = card == sleepcards::CardId::None && SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::DARK;
  }
  if (invert) renderer.invertScreen();
  last.ms = millis() - start;
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  renderer.setOrientation(previousOrientation);
}

void SleepCardPreviewActivity::loop() {
  // The main loop has already updated the input for this pass.
  int x = 0;
  int y = 0;
  if (mappedInput.wasAnyPressed() || mappedInput.wasScreenTapped(x, y)) finish();
}

#endif  // CROSSPOINT_BENCH_CONSOLE
