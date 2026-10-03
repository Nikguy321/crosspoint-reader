#include "WordSearchDraw.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "fontIds.h"
#include "sleepcards/CardDraw.h"

namespace ws::draw {

namespace {

constexpr uint8_t PREVIEW_DITHER = 5;  // of sleepcards::draw::DITHER_LEVELS: a light grey
constexpr int LIST_GAP_LARGE = 9;      // px between rows of capitals (no descenders to clear)
constexpr int LIST_GAP_SMALL = 7;
constexpr int STRIKE = 2;  // found word's bar, px
constexpr int HINT_LINE = 2;
constexpr int HINT_DASHES = 8;

const EpdFontFamily* familyFor(const GfxRenderer& r, const int fontId) {
  const auto& fonts = r.getFontMap();
  const auto it = fonts.find(fontId);
  return it == fonts.end() ? nullptr : &it->second;
}

// Height of the capitals above the baseline ('H'), for centring upper-case text.
int capHeight(const GfxRenderer& r, const int fontId, const EpdFontFamily::Style style) {
  const EpdFontFamily* family = familyFor(r, fontId);
  const EpdGlyph* h = family ? family->getGlyph('H', style) : nullptr;
  return h ? h->top : r.getFontAscenderSize(fontId) * 3 / 4;
}

// The top y for drawText so capitals sit centred on cy.
int capTopFor(const GfxRenderer& r, const int fontId, const EpdFontFamily::Style style, const int cy) {
  return cy + capHeight(r, fontId, style) / 2 - r.getFontAscenderSize(fontId);
}

int measureList(void* ctx, const ListFont font, const char* text) {
  return static_cast<const GfxRenderer*>(ctx)->getTextWidth(listFontId(font), text);
}

// Row y's span of a capsule of radius rad around a->b (convex, so one interval a row).
bool capsuleSpan(const float ax, const float ay, const float bx, const float by, const float rad, const int y,
                 float& lo, float& hi) {
  lo = 1e9f;
  hi = -1e9f;
  bool any = false;
  const auto disc = [&](const float cx, const float cy) {
    const float dy = static_cast<float>(y) - cy;
    const float s = rad * rad - dy * dy;
    if (s < 0) return;
    const float h = std::sqrt(s);
    lo = std::min(lo, cx - h);
    hi = std::max(hi, cx + h);
    any = true;
  };
  disc(ax, ay);
  disc(bx, by);
  const float dx = bx - ax;
  const float dy = by - ay;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len > 0) {
    const float ux = dx / len;
    const float uy = dy / len;
    float slo = -1e9f;
    float shi = 1e9f;
    // mn <= k * (x - ax) + c <= mx, as an x interval.
    const auto clampInterval = [&](const float k, const float c, const float mn, const float mx) {
      if (std::fabs(k) < 1e-6f) return c >= mn && c <= mx;
      float x1 = ax + (mn - c) / k;
      float x2 = ax + (mx - c) / k;
      if (x1 > x2) std::swap(x1, x2);
      slo = std::max(slo, x1);
      shi = std::min(shi, x2);
      return true;
    };
    const float ry = static_cast<float>(y) - ay;
    // Within rad of the line (normal (-uy, ux)) and between the ends.
    if (clampInterval(-uy, ry * ux, -rad, rad) && clampInterval(ux, ry * uy, 0, len) && slo <= shi) {
      lo = std::min(lo, slo);
      hi = std::max(hi, shi);
      any = true;
    }
  }
  return any && lo <= hi;
}

void hspan(GfxRenderer& r, const int x0, const int x1, const int y) {
  if (x1 >= x0) r.fillRect(x0, y, x1 - x0 + 1, 1, true);
}

// Is the cell's centre within reach of the segment a->b (so a capsule around it may cross the
// cell's letter)?
bool nearSegment(const BoardLayout& l, const int row, const int col, const Cell a, const Cell b) {
  const float px = static_cast<float>(l.centerX(col));
  const float py = static_cast<float>(l.centerY(row));
  const float ax = static_cast<float>(l.centerX(a.col));
  const float ay = static_cast<float>(l.centerY(a.row));
  const float dx = static_cast<float>(l.centerX(b.col)) - ax;
  const float dy = static_cast<float>(l.centerY(b.row)) - ay;
  const float len2 = dx * dx + dy * dy;
  const float t = len2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f) : 0.0f;
  const float ex = ax + t * dx - px;
  const float ey = ay + t * dy - py;
  const float reach = static_cast<float>(l.cell) * 1.25f;
  return ex * ex + ey * ey < reach * reach;
}

// Does any capsule (found words, the drag preview) come near this cell? Only those letters
// get the halo: it costs eight extra glyph draws.
bool needsHalo(const Puzzle& p, const BoardLayout& l, const BoardView& view, const bool dragging, const int row,
               const int col) {
  if (dragging && nearSegment(l, row, col, view.dragStart, view.dragEnd)) return true;
  for (int k = 0; k < p.wordCount; k++) {
    const PuzzleWord& w = p.words[k];
    if (w.found && nearSegment(l, row, col, w.foundLine.a, w.foundLine.b)) return true;
  }
  return false;
}

void drawLetter(GfxRenderer& r, const int fontId, const int capH, const int ascender, const char letter, const int cx,
                const int cy, const bool black, const bool halo) {
  const EpdFontFamily* family = familyFor(r, fontId);
  const EpdGlyph* g = family ? family->getGlyph(static_cast<unsigned char>(letter), EpdFontFamily::BOLD) : nullptr;
  if (!g) return;
  const char text[2] = {letter, '\0'};
  // Centre the ink: horizontally on its own bounding box, vertically on the capital height.
  const int x = cx - (g->left + g->width / 2);
  const int y = cy + capH / 2 - ascender;
  if (halo) {
    // A white outline a pixel wide, so a capsule line crossing this cell stops short of the
    // letter instead of running through it.
    for (int oy = -1; oy <= 1; oy++) {
      for (int ox = -1; ox <= 1; ox++) {
        if (ox != 0 || oy != 0) r.drawText(fontId, x + ox, y + oy, text, !black, EpdFontFamily::BOLD);
      }
    }
  }
  r.drawText(fontId, x, y, text, black, EpdFontFamily::BOLD);
}

// A ring `thickness` px wide (outer edge `radius`) in HINT_DASHES dashes with equal gaps.
void drawDashedRing(GfxRenderer& r, const int cx, const int cy, const int radius, const int thickness) {
  const int outer2 = radius * radius;
  const int inner = radius - thickness;
  const int inner2 = inner * inner;
  constexpr float SECTOR = 3.14159265f / HINT_DASHES;  // a dash or a gap
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      const int d2 = dx * dx + dy * dy;
      if (d2 > outer2 || d2 <= inner2) continue;
      const float a = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) + 3.14159265f;
      if (static_cast<int>(a / SECTOR) % 2 == 0) r.drawPixel(cx + dx, cy + dy, true);
    }
  }
}

void drawButton(GfxRenderer& r, const Rect& b, const char* label) {
  r.drawRoundedRect(b.x, b.y, b.w, b.h, 2, 10, true);
  const int top = capTopFor(r, UI_12_FONT_ID, EpdFontFamily::BOLD, b.y + b.h / 2);
  sleepcards::draw::drawTextCenteredAt(r, UI_12_FONT_ID, b.x + b.w / 2, top, label, true, EpdFontFamily::BOLD);
}

}  // namespace

int letterFontFor(const int cell) {
  if (cell >= 42) return NOTOSANS_16_FONT_ID;
  if (cell >= 36) return NOTOSANS_14_FONT_ID;
  return NOTOSANS_12_FONT_ID;
}

int cursorLineFor(const int cell) { return cell >= 36 ? CURSOR_LINE : CURSOR_LINE - 1; }

int listFontId(const ListFont font) { return font == ListFont::Large ? UI_12_FONT_ID : UI_10_FONT_ID; }

ListLayout layoutTexts(const GfxRenderer& r, const char* const* texts, const int count, const BoardLayout& layout) {
  const int pitch[LIST_FONT_COUNT] = {
      capHeight(r, UI_12_FONT_ID, EpdFontFamily::REGULAR) + LIST_GAP_LARGE,
      capHeight(r, UI_10_FONT_ID, EpdFontFamily::REGULAR) + LIST_GAP_SMALL,
  };
  return ws::layoutWordList(texts, count, layout.list, pitch, measureList, const_cast<GfxRenderer*>(&r));
}

ListLayout layoutWordList(const GfxRenderer& r, const Puzzle& p, const BoardLayout& layout) {
  const char* texts[MAX_WORDS];
  for (int k = 0; k < p.wordCount; k++) texts[k] = p.words[k].display;
  return layoutTexts(r, texts, p.wordCount, layout);
}

void drawCapsule(GfxRenderer& r, const int x0, const int y0, const int x1, const int y1, const int radius,
                 const int thickness) {
  const float ax = static_cast<float>(x0);
  const float ay = static_cast<float>(y0);
  const float bx = static_cast<float>(x1);
  const float by = static_cast<float>(y1);
  const float outer = static_cast<float>(radius);
  const float inner = static_cast<float>(radius - thickness);
  const int top = std::min(y0, y1) - radius;
  const int bottom = std::max(y0, y1) + radius;
  for (int y = top; y <= bottom; y++) {
    float lo = 0;
    float hi = 0;
    if (!capsuleSpan(ax, ay, bx, by, outer, y, lo, hi)) continue;
    const int olo = static_cast<int>(std::ceil(lo));
    const int ohi = static_cast<int>(std::floor(hi));
    if (thickness <= 0) {
      for (int x = olo; x <= ohi; x++) r.drawPixel(x, y, sleepcards::draw::ditherInk(x, y, PREVIEW_DITHER));
      continue;
    }
    float ilo = 0;
    float ihi = 0;
    if (inner > 0 && capsuleSpan(ax, ay, bx, by, inner, y, ilo, ihi)) {
      const int a = static_cast<int>(std::ceil(ilo));
      const int b = static_cast<int>(std::floor(ihi));
      if (a <= b) {
        hspan(r, olo, a - 1, y);
        hspan(r, b + 1, ohi, y);
        continue;
      }
    }
    hspan(r, olo, ohi, y);
  }
}

void drawGrid(GfxRenderer& r, const Puzzle& p, const BoardLayout& l, const BoardView& view) {
  const int radius = l.cell / 2 - 1;
  // The drag preview under everything.
  const bool dragging = view.dragStart.valid() && view.dragEnd.valid() && view.dragStart != view.dragEnd &&
                        inGrid(view.dragStart, l.size) && inGrid(view.dragEnd, l.size);
  if (dragging) {
    const int x0 = l.centerX(view.dragStart.col);
    const int y0 = l.centerY(view.dragStart.row);
    const int x1 = l.centerX(view.dragEnd.col);
    const int y1 = l.centerY(view.dragEnd.row);
    drawCapsule(r, x0, y0, x1, y1, radius, 0);
    drawCapsule(r, x0, y0, x1, y1, radius, 1);  // a hairline edge: the shape reads at a glance
  }
  for (int k = 0; k < p.wordCount; k++) {
    const PuzzleWord& w = p.words[k];
    if (!w.found || !inGrid(w.foundLine.a, l.size) || !inGrid(w.foundLine.b, l.size)) continue;
    drawCapsule(r, l.centerX(w.foundLine.a.col), l.centerY(w.foundLine.a.row), l.centerX(w.foundLine.b.col),
                l.centerY(w.foundLine.b.row), radius, CAPSULE_LINE);
  }
  // The anchor (or the drag's start): the whole cell in black, the letter white on it. A disc
  // inside the cell is narrower than Hard's W and M, whose outer strokes would run off it.
  const Cell disc = dragging ? view.dragStart : view.anchor;
  if (inGrid(disc, l.size)) {
    r.fillRoundedRect(l.cellX(disc.col), l.cellY(disc.row), l.cell, l.cell, l.cell / 6, Color::Black);
  }

  const int fontId = letterFontFor(l.cell);
  const int capH = capHeight(r, fontId, EpdFontFamily::BOLD);
  const int ascender = r.getFontAscenderSize(fontId);
  for (int row = 0; row < l.size; row++) {
    for (int col = 0; col < l.size; col++) {
      const bool onDisc = inGrid(disc, l.size) && disc.row == row && disc.col == col;
      const bool halo = !onDisc && needsHalo(p, l, view, dragging, row, col);
      drawLetter(r, fontId, capH, ascender, p.at(row, col), l.centerX(col), l.centerY(row), !onDisc, halo);
    }
  }

  // The hint: a dashed ring around the first letter of one unfound word (dashed, so it never
  // reads as a found word's capsule).
  if (p.hintWord >= 0 && p.hintWord < p.wordCount && !p.words[p.hintWord].found) {
    const Cell h = p.words[p.hintWord].place.start();
    if (inGrid(h, l.size)) drawDashedRing(r, l.centerX(h.col), l.centerY(h.row), radius, HINT_LINE);
  }
  if (view.showCursor && inGrid(view.cursor, l.size)) {
    // Straddling the cell's edge, so it clears the widest letters and the tails of Q and J.
    r.drawRect(l.cellX(view.cursor.col) - 1, l.cellY(view.cursor.row) - 1, l.cell + 2, l.cell + 2,
               cursorLineFor(l.cell), true);
  }
}

void drawWordList(GfxRenderer& r, const Puzzle& p, const BoardLayout& l) {
  const ListLayout list = layoutWordList(r, p, l);
  const int fontId = listFontId(list.font);
  const int capH = capHeight(r, fontId, EpdFontFamily::REGULAR);
  const int ascender = r.getFontAscenderSize(fontId);
  for (int k = 0; k < p.wordCount; k++) {
    int x = 0;
    int y = 0;
    listItemOrigin(list, k, x, y);
    const int col = k / (list.rows > 0 ? list.rows : 1);
    const int room = list.colW[col < 3 ? col : 2];
    const PuzzleWord& w = p.words[k];
    int width = r.getTextWidth(fontId, w.display);
    if (width <= room) {
      r.drawText(fontId, x, y, w.display);
    } else {
      const std::string cut = r.truncatedText(fontId, w.display, room);
      r.drawText(fontId, x, y, cut.c_str());
      width = r.getTextWidth(fontId, cut.c_str());
    }
    if (w.found) {
      const int mid = y + ascender - capH / 2 - STRIKE / 2;
      r.fillRect(x - 2, mid, width + 4, STRIKE, true);
    }
  }
}

void drawBottomBar(GfxRenderer& r, const BoardLayout& l, const BoardView& view) {
  r.drawLine(l.bar.x + SIDE_INSET, l.bar.y - 2, l.bar.right() - SIDE_INSET - 1, l.bar.y - 2, true);
  if (view.status && view.status[0]) {
    const int top = capTopFor(r, UI_12_FONT_ID, EpdFontFamily::REGULAR, l.menuButton.y + l.menuButton.h / 2);
    if (r.getTextWidth(UI_12_FONT_ID, view.status) <= l.status.w) {
      r.drawText(UI_12_FONT_ID, l.status.x, top, view.status);
    } else {
      const std::string text = r.truncatedText(UI_12_FONT_ID, view.status, l.status.w);
      r.drawText(UI_12_FONT_ID, l.status.x, top, text.c_str());
    }
  }
  if (view.menuLabel && view.menuLabel[0]) drawButton(r, l.menuButton, view.menuLabel);
}

void drawBanner(GfxRenderer& r, const BoardLayout& l, const BoardView& view) {
  const Rect& b = l.list;
  r.fillRect(b.x, b.y, b.w, b.h, false);
  r.drawRoundedRect(b.x, b.y, b.w, b.h, 3, 14, true);
  const int buttonTop = l.newPuzzleButton.y;
  const bool detail = view.bannerDetail && view.bannerDetail[0];
  const int titleH = r.getLineHeight(UI_12_FONT_ID);
  const int detailH = detail ? r.getLineHeight(UI_10_FONT_ID) + 2 : 0;
  // The text block centred between the banner's top and the button.
  int y = b.y + (buttonTop - b.y - titleH - detailH) / 2;
  if (view.bannerTitle && view.bannerTitle[0]) {
    sleepcards::draw::drawTextCenteredAt(r, UI_12_FONT_ID, b.x + b.w / 2, y, view.bannerTitle, true,
                                         EpdFontFamily::BOLD);
  }
  y += titleH + 2;
  if (detail) sleepcards::draw::drawTextCenteredAt(r, UI_10_FONT_ID, b.x + b.w / 2, y, view.bannerDetail);
  if (view.newPuzzleLabel && view.newPuzzleLabel[0]) drawButton(r, l.newPuzzleButton, view.newPuzzleLabel);
}

void drawBoard(GfxRenderer& r, const Puzzle& p, const BoardLayout& layout, const BoardView& view) {
  drawGrid(r, p, layout, view);
  if (p.complete()) {
    drawBanner(r, layout, view);
  } else {
    drawWordList(r, p, layout);
  }
  drawBottomBar(r, layout, view);
}

}  // namespace ws::draw
