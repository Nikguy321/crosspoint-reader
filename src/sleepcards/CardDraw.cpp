#include "CardDraw.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "FooterText.h"
#include "fontIds.h"
#include "images/CardDigits.h"

namespace sleepcards::draw {
namespace {

void hspan(GfxRenderer& r, int x0, int x1, const int y, const bool black) {
  if (x1 < x0) return;
  r.fillRect(x0, y, x1 - x0 + 1, 1, black);
}

// Half-width of an ellipse (rx, ry) on row dy, rounded to the pixel grid.
int ellipseHalfWidth(const int rx, const int ry, const int dy) {
  if (ry <= 0 || std::abs(dy) > ry) return -1;
  const double t = 1.0 - (static_cast<double>(dy) * dy) / (static_cast<double>(ry) * ry);
  return static_cast<int>(std::floor(rx * std::sqrt(std::max(0.0, t)) + 0.5));
}

// A glyph's 2-bit or 1-bit coverage at (gx, gy), 0..3 (1-bit ink = 3). Outside = 0.
int glyphCoverage(const uint8_t* bitmap, const int w, const int h, const bool twoBit, const int gx, const int gy) {
  if (gx < 0 || gy < 0 || gx >= w || gy >= h) return 0;
  const int pos = gy * w + gx;
  if (twoBit) return (bitmap[pos >> 2] >> (6 - (pos & 3) * 2)) & 3;
  return ((bitmap[pos >> 3] >> (7 - (pos & 7))) & 1) ? 3 : 0;
}

const EpdFontFamily* familyFor(const GfxRenderer& r, const int fontId) {
  const auto& fonts = r.getFontMap();
  const auto it = fonts.find(fontId);
  return it == fonts.end() ? nullptr : &it->second;
}

}  // namespace

// ---- shapes -------------------------------------------------------------------------------------

void fillEllipse(GfxRenderer& r, const int cx, const int cy, const int rx, const int ry, const bool black) {
  for (int dy = -ry; dy <= ry; dy++) {
    const int hw = ellipseHalfWidth(rx, ry, dy);
    if (hw >= 0) hspan(r, cx - hw, cx + hw, cy + dy, black);
  }
}

void fillCircle(GfxRenderer& r, const int cx, const int cy, const int radius, const bool black) {
  fillEllipse(r, cx, cy, radius, radius, black);
}

void drawEllipse(GfxRenderer& r, const int cx, const int cy, const int rx, const int ry, const int thickness,
                 const bool black) {
  const int t = std::max(1, thickness);
  const int irx = rx - t;
  const int iry = ry - t;
  for (int dy = -ry; dy <= ry; dy++) {
    const int outer = ellipseHalfWidth(rx, ry, dy);
    if (outer < 0) continue;
    const int inner = (irx >= 0 && iry >= 0) ? ellipseHalfWidth(irx, iry, dy) : -1;
    if (inner < 0) {
      hspan(r, cx - outer, cx + outer, cy + dy, black);
    } else {
      hspan(r, cx - outer, cx - inner - 1, cy + dy, black);
      hspan(r, cx + inner + 1, cx + outer, cy + dy, black);
    }
  }
}

void drawCircle(GfxRenderer& r, const int cx, const int cy, const int radius, const int thickness, const bool black) {
  drawEllipse(r, cx, cy, radius, radius, thickness, black);
}

void drawDashedHLine(GfxRenderer& r, const int x0, const int x1, const int y, const int dash, const int gap,
                     const bool black) {
  const int step = std::max(1, dash) + std::max(0, gap);
  for (int x = x0; x <= x1; x += step) hspan(r, x, std::min(x1, x + std::max(1, dash) - 1), y, black);
}

void drawDashedVLine(GfxRenderer& r, const int x, const int y0, const int y1, const int dash, const int gap,
                     const bool black) {
  const int step = std::max(1, dash) + std::max(0, gap);
  for (int y = y0; y <= y1; y += step) {
    const int end = std::min(y1, y + std::max(1, dash) - 1);
    r.fillRect(x, y, 1, end - y + 1, black);
  }
}

// ---- dither -------------------------------------------------------------------------------------

bool ditherInk(const int x, const int y, const uint8_t level) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  if (level == 0) return false;
  if (level >= DITHER_LEVELS) return true;
  return BAYER_4X4[((y & 3) << 2) | (x & 3)] < level;
}

void fillRectDithered(GfxRenderer& r, const int x, const int y, const int w, const int h, const uint8_t level) {
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) r.drawPixel(xx, yy, ditherInk(xx, yy, level));
  }
}

void fillCircleDithered(GfxRenderer& r, const int cx, const int cy, const int radius, const uint8_t level) {
  for (int dy = -radius; dy <= radius; dy++) {
    const int hw = ellipseHalfWidth(radius, radius, dy);
    for (int dx = -hw; dx <= hw; dx++) r.drawPixel(cx + dx, cy + dy, ditherInk(cx + dx, cy + dy, level));
  }
}

// ---- the moon -----------------------------------------------------------------------------------

void moonLitSpan(double illum, const bool waxing, const bool southern, const int radius, const int dy, int& litLeft,
                 int& litRight) {
  litLeft = 1;
  litRight = 0;
  const int hw = ellipseHalfWidth(radius, radius, dy);
  if (hw < 0) return;
  illum = std::clamp(illum, 0.0, 1.0);
  // The terminator is a half-ellipse whose x on this row is k * hw with k running from +1 (new)
  // through 0 (quarter) to -1 (full), measured on the lit limb's side.
  const double k = 1.0 - 2.0 * illum;
  const int term = static_cast<int>(std::floor(k * hw + 0.5));
  // Lit on the right (waxing, northern): from the terminator to the right limb.
  int left = term;
  int right = hw;
  const bool litOnRight = waxing != southern;
  if (!litOnRight) {
    left = -hw;
    right = -term;
  }
  if (illum <= 0.0) return;
  litLeft = std::max(left, -hw);
  litRight = std::min(right, hw);
}

void drawMoon(GfxRenderer& r, const int cx, const int cy, const int radius, const double illum, const bool waxing,
              const bool southern, const uint8_t darkLevel) {
  for (int dy = -radius; dy <= radius; dy++) {
    const int hw = ellipseHalfWidth(radius, radius, dy);
    if (hw < 0) continue;
    int litL = 0;
    int litR = 0;
    moonLitSpan(illum, waxing, southern, radius, dy, litL, litR);
    for (int dx = -hw; dx <= hw; dx++) {
      const bool lit = dx >= litL && dx <= litR;
      const int x = cx + dx;
      const int y = cy + dy;
      r.drawPixel(x, y, lit ? false : ditherInk(x, y, darkLevel));
    }
  }
  const int ring = radius >= 24 ? 2 : 1;
  drawCircle(r, cx, cy, radius, ring, true);
  // Inside the ring (drawEllipse's inner edge): on a dark card the ring turns white and outlines
  // the dark limb against the black page.
  if (radius > ring) keepTonesDisc(cx, cy, radius - ring);
}

// ---- dark cards ---------------------------------------------------------------------------------

namespace {
struct KeptTones {
  bool disc;
  int x, y;  // disc: centre; rect: top-left
  int w, h;  // disc: w = radius
};
// A card has at most a cover or a few moons (the calendar's four phases, the sky's two).
constexpr int MAX_KEPT_TONES = 8;
KeptTones keptTones[MAX_KEPT_TONES];
int keptToneCount = 0;

void keepTones(const KeptTones& k) {
  if (keptToneCount < MAX_KEPT_TONES) keptTones[keptToneCount++] = k;
}

void invertPixel(GfxRenderer& r, const int x, const int y) { r.drawPixel(x, y, !r.readPixel(x, y)); }
}  // namespace

void clearKeptTones() { keptToneCount = 0; }

void keepTonesRect(const int x, const int y, const int w, const int h) {
  if (w > 0 && h > 0) keepTones({false, x, y, w, h});
}

void keepTonesDisc(const int cx, const int cy, const int radius) {
  if (radius >= 0) keepTones({true, cx, cy, radius, 0});
}

void invertKeepingTones(GfxRenderer& r) {
  r.invertScreen();
  for (int i = 0; i < keptToneCount; i++) {
    const KeptTones& k = keptTones[i];
    if (k.disc) {
      for (int dy = -k.w; dy <= k.w; dy++) {
        const int hw = ellipseHalfWidth(k.w, k.w, dy);
        for (int dx = -hw; dx <= hw; dx++) invertPixel(r, k.x + dx, k.y + dy);
      }
    } else {
      for (int y = k.y; y < k.y + k.h; y++) {
        for (int x = k.x; x < k.x + k.w; x++) invertPixel(r, x, y);
      }
    }
  }
}

// ---- bars ---------------------------------------------------------------------------------------

void drawProgressBar(GfxRenderer& r, const int x, const int y, const int w, const int h, float fraction) {
  if (w <= 2 || h <= 2) return;
  fraction = std::clamp(fraction, 0.0f, 1.0f);
  r.drawRect(x, y, w, h, true);
  const int inner = w - 4;
  const int filled = static_cast<int>(inner * fraction + 0.5f);
  if (filled > 0) r.fillRect(x + 2, y + 2, filled, h - 4, true);
}

// ---- text ---------------------------------------------------------------------------------------

int textWidthScaled(const GfxRenderer& r, const int fontId, const char* text, const float scale,
                    const EpdFontFamily::Style style) {
  const EpdFontFamily* family = familyFor(r, fontId);
  if (!family || !text) return 0;
  int32_t advanceFp = 0;
  uint32_t prev = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    const EpdGlyph* glyph = family->getGlyph(cp, style);
    if (!glyph) continue;
    if (prev) advanceFp += family->getKerning(prev, cp, style);
    advanceFp += glyph->advanceX;
    prev = cp;
  }
  return static_cast<int>(std::lround(fp4::toFloat(advanceFp) * scale));
}

void drawTextScaled(GfxRenderer& r, const int fontId, const int x, const int y, const char* text, const float scale,
                    const bool black, const EpdFontFamily::Style style) {
  const EpdFontFamily* family = familyFor(r, fontId);
  if (!family || !text || scale <= 0.0f) return;
  const EpdFontData* data = family->getData(style);
  const float baseline = static_cast<float>(y) + data->ascender * scale;
  int32_t penFp = 0;  // pen position in unscaled 12.4 fixed point
  uint32_t prev = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    const EpdGlyph* glyph = family->getGlyph(cp, style);
    if (!glyph) continue;
    if (prev) penFp += family->getKerning(prev, cp, style);
    prev = cp;
    const int gw = glyph->width;
    const int gh = glyph->height;
    if (gw > 0 && gh > 0) {
      // The bitmap pointer is only valid until the next glyph is fetched: consume it here.
      const uint8_t* bitmap = r.getGlyphBitmap(data, glyph);
      if (bitmap) {
        const float gx0 = x + (fp4::toFloat(penFp) + glyph->left) * scale;
        const float gy0 = baseline - glyph->top * scale;
        const int ox0 = static_cast<int>(std::floor(gx0));
        const int oy0 = static_cast<int>(std::floor(gy0));
        const int ox1 = static_cast<int>(std::ceil(gx0 + gw * scale));
        const int oy1 = static_cast<int>(std::ceil(gy0 + gh * scale));
        for (int oy = oy0; oy < oy1; oy++) {
          // Sample the source at the output pixel's centre, bilinearly over the coverage grid.
          const float sy = (oy + 0.5f - gy0) / scale - 0.5f;
          const int sy0 = static_cast<int>(std::floor(sy));
          const float fy = sy - sy0;
          for (int ox = ox0; ox < ox1; ox++) {
            const float sx = (ox + 0.5f - gx0) / scale - 0.5f;
            const int sx0 = static_cast<int>(std::floor(sx));
            const float fx = sx - sx0;
            const float c00 = glyphCoverage(bitmap, gw, gh, data->is2Bit, sx0, sy0);
            const float c10 = glyphCoverage(bitmap, gw, gh, data->is2Bit, sx0 + 1, sy0);
            const float c01 = glyphCoverage(bitmap, gw, gh, data->is2Bit, sx0, sy0 + 1);
            const float c11 = glyphCoverage(bitmap, gw, gh, data->is2Bit, sx0 + 1, sy0 + 1);
            const float c = (c00 * (1 - fx) + c10 * fx) * (1 - fy) + (c01 * (1 - fx) + c11 * fx) * fy;
            if (c >= 1.5f) r.drawPixel(ox, oy, black);
          }
        }
      }
    }
    penFp += glyph->advanceX;
  }
}

void drawCenteredTextScaled(GfxRenderer& r, const int fontId, const int cx, const int y, const char* text,
                            const float scale, const bool black, const EpdFontFamily::Style style) {
  drawTextScaled(r, fontId, cx - textWidthScaled(r, fontId, text, scale, style) / 2, y, text, scale, black, style);
}

int digitsWidth(const CardDigitFont& font, const char* digits) {
  if (!digits) return 0;
  int first = -1;
  int last = -1;
  int count = 0;
  for (const char* p = digits; *p; p++) {
    if (*p < '0' || *p > '9') continue;
    if (first < 0) first = *p - '0';
    last = *p - '0';
    count++;
  }
  if (count == 0) return 0;
  return (count - 1) * font.advance + font.left[last] + font.width[last] - font.left[first];
}

void drawDigits(GfxRenderer& r, const CardDigitFont& font, const int x, const int top, const char* digits,
                const bool black) {
  if (!digits) return;
  int pen = x;
  bool first = true;
  for (const char* p = digits; *p; p++) {
    if (*p < '0' || *p > '9') continue;
    const int d = *p - '0';
    if (first) {
      pen -= font.left[d];  // x is the first digit's ink edge
      first = false;
    }
    const int w = font.width[d];
    const int rowBytes = (w + 7) / 8;
    const uint8_t* bits = font.bits + font.offset[d];
    const int x0 = pen + font.left[d];
    for (int row = 0; row < font.height; row++) {
      const uint8_t* line = bits + row * rowBytes;
      int col = 0;
      while (col < w) {
        // Runs of ink as one span each.
        while (col < w && ((line[col >> 3] >> (7 - (col & 7))) & 1) == 0) col++;
        const int start = col;
        while (col < w && ((line[col >> 3] >> (7 - (col & 7))) & 1) != 0) col++;
        if (col > start) r.fillRect(x0 + start, top + row, col - start, 1, black);
      }
    }
    pen += font.advance;
  }
}

void drawCenteredDigits(GfxRenderer& r, const CardDigitFont& font, const int cx, const int top, const char* digits,
                        const bool black) {
  drawDigits(r, font, cx - digitsWidth(font, digits) / 2, top, digits, black);
}

void drawTextCenteredAt(GfxRenderer& r, const int fontId, const int cx, const int y, const char* text, const bool black,
                        const EpdFontFamily::Style style) {
  r.drawText(fontId, cx - r.getTextWidth(fontId, text, style) / 2, y, text, black, style);
}

void drawTextRight(GfxRenderer& r, const int fontId, const int right, const int y, const char* text, const bool black,
                   const EpdFontFamily::Style style) {
  r.drawText(fontId, right - r.getTextWidth(fontId, text, style), y, text, black, style);
}

int drawWrapped(GfxRenderer& r, const int fontId, const int x, const int y, const int width, const char* text,
                const int maxLines, const Align align, const EpdFontFamily::Style style, const int lineHeight) {
  if (!text || !*text || maxLines <= 0 || width <= 0) return 0;
  const int lh = lineHeight > 0 ? lineHeight : r.getLineHeight(fontId);
  // wrappedText() allocates one std::string per line (maxLines bounds it).
  const std::vector<std::string> lines = r.wrappedText(fontId, text, width, maxLines, style);
  int yy = y;
  for (const auto& line : lines) {
    int lx = x;
    if (align != Align::Left) {
      const int w = r.getTextWidth(fontId, line.c_str(), style);
      lx = align == Align::Center ? x + (width - w) / 2 : x + width - w;
    }
    r.drawText(fontId, lx, yy, line.c_str(), true, style);
    yy += lh;
  }
  return yy - y;
}

// ---- footer -------------------------------------------------------------------------------------

void drawBatteryIcon(GfxRenderer& r, const int x, const int y, const int w, const int h, int percent,
                     const bool charging) {
  constexpr int NUB_W = 2;
  // The theme's bolt (BaseTheme::drawBatteryLightningBolt) cut to the fill's 6 rows (from y + 2):
  // a stroke down to the left, the bar, and a stroke on down to the left, both tips kept.
  constexpr int BOLT_W = 6;
  constexpr int BOLT_H = 6;
  constexpr int BOLT_ROWS[BOLT_H][2] = {{4, 5}, {3, 4}, {2, 5}, {3, 4}, {2, 3}, {1, 2}};
  const int bodyW = w - NUB_W;
  if (bodyW <= 4 || h <= 4) return;
  percent = std::clamp(percent, 0, 100);
  r.drawRect(x, y, bodyW, h, true);
  r.fillRect(x + bodyW, y + h / 4, NUB_W, h - 2 * (h / 4), true);
  const int inner = bodyW - 4;
  int filled = (inner * percent + 50) / 100;
  const bool bolt = charging && inner >= BOLT_W + 2;
  if (bolt) filled = std::max(filled, BOLT_W + 2);
  if (filled > 0) r.fillRect(x + 2, y + 2, filled, h - 4, true);
  if (!bolt) return;
  for (int row = 0; row < BOLT_H && row < h - 4; row++) {
    r.drawLine(x + 3 + BOLT_ROWS[row][0], y + 2 + row, x + 3 + BOLT_ROWS[row][1], y + 2 + row, false);
  }
}

namespace {
int footerTextWidth(void* user, const char* text) {
  return static_cast<GfxRenderer*>(user)->getTextWidth(UI_10_FONT_ID, text);
}
}  // namespace

void drawSleepFooter(const CardContext& ctx, GfxRenderer& r) {
  const int w = r.getScreenWidth();
  const int h = r.getScreenHeight();
  const int top = h - FOOTER_HEIGHT;
  r.fillRect(SCREEN_MARGIN, top, w - 2 * SCREEN_MARGIN, 1, true);

  char battery[8] = "";
  if (ctx.batteryPercent >= 0) std::snprintf(battery, sizeof(battery), "%d%%", ctx.batteryPercent);

  const int font = UI_10_FONT_ID;
  const int textY = top + (FOOTER_HEIGHT - r.getLineHeight(font)) / 2 + 1;
  const int dotGap = 8;  // each side of the separator dot
  // The battery reads as a battery, never as another percentage on the card.
  constexpr int ICON_W = 20;
  constexpr int ICON_H = 10;
  constexpr int ICON_GAP = 5;
  const int batteryW = battery[0] ? ICON_W + ICON_GAP + r.getTextWidth(font, battery) : 0;

  // The "when", in the longest form that fits between the margins beside the battery.
  char when[96] = "";
  if (ctx.timeValid) {
    const int room = w - 2 * SCREEN_MARGIN - (batteryW > 0 ? batteryW + 2 * dotGap + 4 : 0);
    footer::fitUpdated(ctx.localNow, ctx.clock12h, room, &footerTextWidth, &r, when, sizeof(when));
  }
  const int whenW = when[0] ? r.getTextWidth(font, when) : 0;
  const bool both = whenW > 0 && batteryW > 0;
  const int total = whenW + batteryW + (both ? 2 * dotGap + 4 : 0);
  int x = (w - total) / 2;
  if (whenW > 0) {
    r.drawText(font, x, textY, when, true);
    x += whenW;
  }
  if (both) {
    fillCircle(r, x + dotGap + 2, textY + r.getFontAscenderSize(font) / 2 + 1, 2, true);
    x += 2 * dotGap + 4;
  }
  if (batteryW > 0) {
    const int asc = r.getFontAscenderSize(font);
    // Centred on the figures (lining digits stand ~0.72 of the ascender).
    drawBatteryIcon(r, x, textY + asc - asc * 36 / 100 - ICON_H / 2, ICON_W, ICON_H, ctx.batteryPercent, ctx.charging);
    r.drawText(font, x + ICON_W + ICON_GAP, textY, battery, true);
  }
}

}  // namespace sleepcards::draw
