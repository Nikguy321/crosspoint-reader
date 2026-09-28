#include "BrandScreen.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/X4ProLogo.h"

namespace sleepcards {

void drawX4ProLogoScreen(GfxRenderer& renderer, const char* caption) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int logoTop = (h - X4PRO_LOGO_SIZE) / 2 - 24;
  renderer.clearScreen();
  renderer.drawImage(X4ProLogo, (w - X4PRO_LOGO_SIZE) / 2, logoTop, X4PRO_LOGO_SIZE, X4PRO_LOGO_SIZE);
  const int nameY = logoTop + X4PRO_LOGO_SIZE + 22;
  renderer.drawCenteredText(UI_12_FONT_ID, nameY, tr(STR_X4_PRO), true, EpdFontFamily::BOLD);
  if (caption != nullptr && caption[0] != '\0') {
    renderer.drawCenteredText(SMALL_FONT_ID, nameY + renderer.getLineHeight(UI_12_FONT_ID) + 6, caption);
  }
}

}  // namespace sleepcards
