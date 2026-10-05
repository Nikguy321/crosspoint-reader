#pragma once

// Survival guide: gd::FontMetrics over the GfxRenderer and the built-in fonts, the one adapter the
// page screen and the host tests (test/guide's real-pack fit test, test/guide_preview) both use, so
// the screen breaks the tests check are the device's. Two font sets:
//
//              pageFonts() - every page        compactFonts() - a quick card (one screen)
//   Body       NotoSans 12 Regular (34 px)     UI 10 Regular (24 px)
//   Bold       NotoSans 12 Bold                UI 10 Bold
//   Title      NotoSans 14 Bold                UI 12 Bold
//   Lead       UI 12 Regular                   UI 10 Regular
//   Crumb      UI 10 Regular                   UI 10 Regular
//
// NotoSans 12 is the reader's smallest built-in size (its default is 14): on the 480 px screen a
// line holds ~37 characters. The quick cards' 140-170 words need about two screens at that size, so
// a quick card uses the compact set with gd::compactGeometry(); all 10 quick cards fit one screen
// that way (test/guide/GuidePackTest.cpp checks every one).

#include <EpdFontFamily.h>
#include <Guide.h>

class GfxRenderer;

namespace gd {

struct FontFace {
  int fontId;
  EpdFontFamily::Style style;
};

struct FontSet {
  FontFace face[FONT_COUNT];
  const FontFace& operator[](const Font f) const { return face[static_cast<int>(f)]; }
};
const FontSet& pageFonts();
const FontSet& compactFonts();

class RendererMetrics final : public FontMetrics {
 public:
  explicit RendererMetrics(const GfxRenderer& renderer, const FontSet& fonts = pageFonts())
      : renderer(renderer), fonts(fonts) {}
  int textWidth(Font font, const char* text, size_t len) const override;
  int spaceWidth(Font font) const override;
  int lineHeight(Font font) const override;
  // What the drawing code passes to GfxRenderer::drawText for a run in this font.
  const FontFace& face(const Font font) const { return fonts[font]; }

 private:
  const GfxRenderer& renderer;
  const FontSet& fonts;
};

}  // namespace gd
