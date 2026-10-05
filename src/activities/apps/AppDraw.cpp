#include "AppDraw.h"

#include <GfxRenderer.h>

#include "fontIds.h"
#include "sleepcards/CardDraw.h"

namespace appdraw {

namespace {

const EpdFontFamily* familyFor(const GfxRenderer& r, const int fontId) {
  const auto& fonts = r.getFontMap();
  const auto it = fonts.find(fontId);
  return it == fonts.end() ? nullptr : &it->second;
}

}  // namespace

const EpdGlyph* glyphFor(const GfxRenderer& r, const int fontId, const unsigned char c,
                         const EpdFontFamily::Style style) {
  const EpdFontFamily* family = familyFor(r, fontId);
  return family ? family->getGlyph(c, style) : nullptr;
}

int capHeight(const GfxRenderer& r, const int fontId, const EpdFontFamily::Style style) {
  const EpdGlyph* h = glyphFor(r, fontId, 'H', style);
  return h ? h->top : r.getFontAscenderSize(fontId) * 3 / 4;
}

int capTopFor(const GfxRenderer& r, const int fontId, const EpdFontFamily::Style style, const int cy) {
  return cy + capHeight(r, fontId, style) / 2 - r.getFontAscenderSize(fontId);
}

void drawLetterOnBaseline(GfxRenderer& r, const int fontId, const int ascender, const char letter, const int cx,
                          const int baseline, const bool black, const bool halo) {
  drawLetterOnBaseline(r, fontId, ascender, letter, cx, baseline, black, halo, EpdFontFamily::BOLD);
}

void drawLetterOnBaseline(GfxRenderer& r, const int fontId, const int ascender, const char letter, const int cx,
                          const int baseline, const bool black, const bool halo, const EpdFontFamily::Style style) {
  const EpdGlyph* g = glyphFor(r, fontId, static_cast<unsigned char>(letter), style);
  if (!g) return;
  const char text[2] = {letter, '\0'};
  // Centre the ink horizontally on its own bounding box.
  const int x = cx - (g->left + g->width / 2);
  const int y = baseline - ascender;
  if (halo) {
    for (int oy = -1; oy <= 1; oy++) {
      for (int ox = -1; ox <= 1; ox++) {
        if (ox != 0 || oy != 0) r.drawText(fontId, x + ox, y + oy, text, !black, style);
      }
    }
  }
  r.drawText(fontId, x, y, text, black, style);
}

void drawLetter(GfxRenderer& r, const int fontId, const int capH, const int ascender, const char letter, const int cx,
                const int cy, const bool black, const bool halo) {
  drawLetterOnBaseline(r, fontId, ascender, letter, cx, cy + capH / 2, black, halo);
}

void drawButton(GfxRenderer& r, const int x, const int y, const int w, const int h, const char* label, int fontId) {
  if (fontId < 0) fontId = UI_12_FONT_ID;
  r.drawRoundedRect(x, y, w, h, 2, 10, true);
  const int top = capTopFor(r, fontId, EpdFontFamily::BOLD, y + h / 2);
  sleepcards::draw::drawTextCenteredAt(r, fontId, x + w / 2, top, label, true, EpdFontFamily::BOLD);
}

}  // namespace appdraw
