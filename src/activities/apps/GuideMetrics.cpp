#include "GuideMetrics.h"

#include <GfxRenderer.h>

#include <cstring>

#include "fontIds.h"

namespace gd {

namespace {
// In Font order: Body, Bold, Title, Lead, Crumb.
const FontSet PAGE_FONTS = {{
    {NOTOSANS_12_FONT_ID, EpdFontFamily::REGULAR},
    {NOTOSANS_12_FONT_ID, EpdFontFamily::BOLD},
    {NOTOSANS_14_FONT_ID, EpdFontFamily::BOLD},
    {UI_12_FONT_ID, EpdFontFamily::REGULAR},
    {UI_10_FONT_ID, EpdFontFamily::REGULAR},
}};
const FontSet COMPACT_FONTS = {{
    {UI_10_FONT_ID, EpdFontFamily::REGULAR},
    {UI_10_FONT_ID, EpdFontFamily::BOLD},
    {UI_12_FONT_ID, EpdFontFamily::BOLD},
    {UI_10_FONT_ID, EpdFontFamily::REGULAR},
    {UI_10_FONT_ID, EpdFontFamily::REGULAR},
}};
}  // namespace

const FontSet& pageFonts() { return PAGE_FONTS; }
const FontSet& compactFonts() { return COMPACT_FONTS; }

int RendererMetrics::textWidth(const Font font, const char* text, const size_t len) const {
  const FontFace& f = fonts[font];
  // GfxRenderer measures NUL-terminated strings: copy the run in pieces (a run is one word, so one
  // piece in practice; a cut between pieces only loses a kerning pair).
  char buf[96];
  int width = 0;
  size_t done = 0;
  while (done < len) {
    size_t n = len - done < sizeof(buf) - 1 ? len - done : sizeof(buf) - 1;
    if (done + n < len) {
      while (n > 1 && (static_cast<uint8_t>(text[done + n]) & 0xC0) == 0x80) n--;  // whole characters
    }
    std::memcpy(buf, text + done, n);
    buf[n] = '\0';
    width += renderer.getTextWidth(f.fontId, buf, f.style);
    done += n;
  }
  return width;
}

int RendererMetrics::spaceWidth(const Font font) const {
  const FontFace& f = fonts[font];
  return renderer.getSpaceWidth(f.fontId, f.style);
}

int RendererMetrics::lineHeight(const Font font) const { return renderer.getLineHeight(fonts[font].fontId); }

}  // namespace gd
