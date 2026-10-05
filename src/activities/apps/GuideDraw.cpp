#include "GuideDraw.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "fontIds.h"

namespace gd::draw {

namespace {

constexpr int TITLE_FONT = UI_12_FONT_ID;
constexpr int SMALL_FONT = UI_10_FONT_ID;
constexpr int CRUMB_TEXT_Y = 9;
constexpr int CRUMB_RULE_Y = CRUMB_H - 3;  // 2 px: y 41..42
constexpr int DASH = 4;
constexpr int DASH_GAP = 3;

// Text centred on cx with its top at y, cut to maxWidth.
void centredText(GfxRenderer& r, const int font, const int cx, const int y, const char* text, const int maxWidth,
                 const EpdFontFamily::Style style) {
  if (!text || !*text) return;
  std::string fitted = r.truncatedText(font, text, maxWidth, style);
  const int w = r.getTextWidth(font, fitted.c_str(), style);
  r.drawText(font, cx - w / 2, y, fitted.c_str(), true, style);
}

// A 2 px dashed frame (the NOTE box), drawn edge by edge (flags: which horizontal edges).
void dashedH(GfxRenderer& r, const int x, const int y, const int w, const int t) {
  for (int i = 0; i < w; i += DASH + DASH_GAP) r.fillRect(x + i, y, std::min(DASH, w - i), t, true);
}
void dashedV(GfxRenderer& r, const int x, const int y, const int h, const int t) {
  for (int i = 0; i < h; i += DASH + DASH_GAP) r.fillRect(x, y + i, t, std::min(DASH, h - i), true);
}

void drawBox(GfxRenderer& r, const Shape& s, const bool dashed) {
  constexpr int t = 2;
  if (dashed) {
    dashedV(r, s.x, s.y, s.h, t);
    dashedV(r, s.x + s.w - t, s.y, s.h, t);
    if (s.flags & EDGE_TOP) dashedH(r, s.x, s.y, s.w, t);
    if (s.flags & EDGE_BOTTOM) dashedH(r, s.x, s.y + s.h - t, s.w, t);
    return;
  }
  r.fillRect(s.x, s.y, t, s.h, true);
  r.fillRect(s.x + s.w - t, s.y, t, s.h, true);
  if (s.flags & EDGE_TOP) r.fillRect(s.x, s.y, s.w, t, true);
  if (s.flags & EDGE_BOTTOM) r.fillRect(s.x, s.y + s.h - t, s.w, t, true);
  // The WARNING's extra weight: a 4 px bar inside its left edge.
  r.fillRect(s.x + t, s.y, 2, s.h, true);
}

// The figure placeholder: a dashed frame with its label centred.
void drawMissing(GfxRenderer& r, const int x, const int y, const int w, const int h, const char* label) {
  dashedH(r, x, y, w, 1);
  dashedH(r, x, y + h - 1, w, 1);
  dashedV(r, x, y, h, 1);
  dashedV(r, x + w - 1, y, h, 1);
  const int lh = r.getLineHeight(SMALL_FONT);
  if (h >= lh) centredText(r, SMALL_FONT, x + w / 2, y + (h - lh) / 2, label, w - 8, EpdFontFamily::REGULAR);
}

void drawFigure(GfxRenderer& r, FigureFn fn, void* ctx, const char* name, const int x, const int y, const int w,
                const int h, const bool missing, const char* missingLabel) {
  if (!missing && fn && name && fn(ctx, r, name, x, y, w, h)) return;
  drawMissing(r, x, y, w, h, missingLabel);
}

}  // namespace

// ---- the bar ----------------------------------------------------------------------------------------

BarButton barButtonAt(const int x, const int y) {
  if (y < BAR_TOP || y >= SCREEN_H || x < 0 || x >= SCREEN_W) return BarButton::None;
  if (x < BAR_THIRD) return BarButton::Prev;
  if (x < 2 * BAR_THIRD) return BarButton::Middle;
  return BarButton::Next;
}

void drawBar(GfxRenderer& r, const BarView& v) {
  r.fillRect(BEZEL, BAR_TOP, SCREEN_W - 2 * BEZEL, 2, true);
  // Thin dividers between the thirds, inset from the rule.
  r.fillRect(BAR_THIRD, BAR_TOP + 12, 1, BAR_H - 24, true);
  r.fillRect(2 * BAR_THIRD, BAR_TOP + 12, 1, BAR_H - 24, true);
  const int labelH = r.getLineHeight(TITLE_FONT);
  const int detailH = r.getLineHeight(SMALL_FONT);
  const int top = BAR_TOP + 2 + (BAR_H - 2 - labelH - detailH) / 2;
  const int maxW = BAR_THIRD - 16;
  const auto third = [&](const int i, const char* label, const char* detail, const bool enabled, const bool bold) {
    if (!enabled) return;
    const int cx = i * BAR_THIRD + BAR_THIRD / 2;
    const bool hasDetail = detail && *detail;
    const int y = hasDetail ? top : BAR_TOP + 2 + (BAR_H - 2 - labelH) / 2;
    centredText(r, TITLE_FONT, cx, y, label, maxW, EpdFontFamily::BOLD);
    if (hasDetail) {
      centredText(r, SMALL_FONT, cx, top + labelH, detail, maxW, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
  };
  third(0, v.prevLabel, v.prevDetail, v.prevEnabled, false);
  third(1, v.middleLabel, v.middleDetail, true, v.middleDetailBold);
  third(2, v.nextLabel, v.nextDetail, v.nextEnabled, false);
}

// ---- the page screen --------------------------------------------------------------------------------

int crumbWidth() { return TEXT_RIGHT - TEXT_LEFT - RIBBON_W - 8; }

void drawCrumb(GfxRenderer& r, const char* text, const bool marked) {
  if (text && *text) r.drawText(SMALL_FONT, TEXT_LEFT, CRUMB_TEXT_Y, text, true, EpdFontFamily::REGULAR);
  r.fillRect(TEXT_LEFT - 8, CRUMB_RULE_Y, TEXT_RIGHT - TEXT_LEFT + 16, 2, true);
  if (!marked) return;
  // A bookmark ribbon hanging from the top edge: a bar with a notched foot.
  const int x = TEXT_RIGHT - RIBBON_W;
  constexpr int h = 30;
  r.fillRect(x, 0, RIBBON_W, h - RIBBON_W / 2, true);
  for (int i = 0; i < RIBBON_W / 2; i++) {
    const int y = h - RIBBON_W / 2 + i;
    r.fillRect(x, y, RIBBON_W / 2 - i, 1, true);
    r.fillRect(x + RIBBON_W / 2 + i, y, RIBBON_W / 2 - i, 1, true);
  }
}

int drawPageBody(GfxRenderer& r, const PageView& v) {
  if (!v.layout || !v.fonts || v.screen < 0 || v.screen >= v.layout->screenCount()) return -1;
  const ScreenSpan& span = v.layout->screen(v.screen);
  int figure = -1;
  for (int i = span.firstShape; i < span.firstShape + span.shapeCount; i++) {
    const Shape& s = v.layout->shape(i);
    switch (s.kind) {
      case ShapeKind::Figure:
        drawFigure(r, v.figure, v.figureCtx, s.ref, s.x, s.y, s.w, s.h, (s.flags & FIGURE_MISSING) != 0,
                   v.missingLabel);
        figure = i;
        break;
      case ShapeKind::WarningBox:
        drawBox(r, s, false);
        break;
      case ShapeKind::NoteBox:
        drawBox(r, s, true);
        break;
      case ShapeKind::Dot:
        r.fillRect(s.x, s.y, s.w, s.h, true);
        break;
    }
  }
  char buf[160];
  for (int i = span.firstRun; i < span.firstRun + span.runCount; i++) {
    const TextRun& run = v.layout->run(i);
    if (copyRun(run, buf, sizeof(buf)) == 0) continue;
    const FontFace& face = (*v.fonts)[run.font];
    r.drawText(face.fontId, run.x, run.y, buf, true, face.style);
  }
  return figure;
}

void drawFigureScreen(GfxRenderer& r, const FigureView& v) {
  static_assert(FIGURE_BOX_W == MAX_FIG_XL_W && FIGURE_BOX_H == MAX_FIG_XL_H, "the XL raster fills the box");
  static_assert(SCREEN_W - FIGURE_BOX_W >= 2 * BEZEL + 8, "the box stays clear of the bezel columns");
  constexpr int maxW = FIGURE_BOX_W;
  constexpr int maxH = FIGURE_BOX_H;
  constexpr int captionTop = FIGURE_BOX_TOP + FIGURE_BOX_H + 12;
  constexpr int captionW = SCREEN_W - 2 * TEXT_LEFT;
  int w = maxW;
  int h = maxH / 2;
  bool missing = v.figW <= 0 || v.figH <= 0;
  if (!missing) {
    int k = std::min(maxW / v.figW, maxH / v.figH);
    if (k >= 1) {
      w = v.figW * k;
      h = v.figH * k;
    } else {
      fitFigure(v.figW, v.figH, maxW, maxH, w, h);
    }
  }
  const int x = (SCREEN_W - w) / 2;
  const int y = FIGURE_BOX_TOP + (maxH - h) / 2;
  drawFigure(r, v.figure, v.figureCtx, v.name, x, y, w, h, missing, v.missingLabel);
  if (v.caption && *v.caption) {
    const auto lines = r.wrappedText(SMALL_FONT, v.caption, captionW, 3, EpdFontFamily::REGULAR);
    int ly = captionTop;
    for (const auto& line : lines) {
      centredText(r, SMALL_FONT, SCREEN_W / 2, ly, line.c_str(), captionW, EpdFontFamily::REGULAR);
      ly += r.getLineHeight(SMALL_FONT);
    }
  }
  r.fillRect(BEZEL, BAR_TOP, SCREEN_W - 2 * BEZEL, 2, true);
  centredText(r, TITLE_FONT, SCREEN_W / 2, BAR_TOP + 2 + (BAR_H - 2 - r.getLineHeight(TITLE_FONT)) / 2, v.hint,
              SCREEN_W - 40, EpdFontFamily::BOLD);
}

// ---- the list screens -------------------------------------------------------------------------------

int rowsPerPage(const int listTop) {
  const int n = (BAR_TOP - 4 - listTop) / ROW_H;
  return n > 0 ? n : 1;
}

int rowAt(const int listTop, const int rowsOnPage, const int x, const int y) {
  if (x < ROW_LEFT || x >= ROW_RIGHT || y < listTop) return -1;
  const int i = (y - listTop) / ROW_H;
  return i < rowsOnPage ? i : -1;
}

void drawList(GfxRenderer& r, const ListView& v) {
  const int titleH = r.getLineHeight(TITLE_FONT);
  const int smallH = r.getLineHeight(SMALL_FONT);
  const int pad = (ROW_H - titleH - smallH) / 2;
  for (int i = 0; i < v.rowCount; i++) {
    const ListRow& row = v.rows[i];
    const int y = v.listTop + i * ROW_H;
    const bool ink = !row.emphasis;  // the text colour
    if (row.emphasis) r.fillRoundedRect(ROW_LEFT + 4, y + 3, ROW_RIGHT - ROW_LEFT - 8, ROW_H - 6, 6, Color::Black);
    if (i == v.selected && row.enabled) r.drawRoundedRect(ROW_LEFT, y, ROW_RIGHT - ROW_LEFT, ROW_H, 2, 8, true);
    // Title, with the value at the right on the same line.
    int valueW = 0;
    if (row.value && *row.value) {
      valueW = r.getTextWidth(SMALL_FONT, row.value, EpdFontFamily::REGULAR);
      r.drawText(SMALL_FONT, TEXT_RIGHT - valueW, y + pad + (titleH - smallH), row.value, ink, EpdFontFamily::REGULAR);
      valueW += 12;
    }
    const int titleW = TEXT_RIGHT - TEXT_LEFT - valueW;
    const auto title = r.truncatedText(TITLE_FONT, row.title, titleW, EpdFontFamily::BOLD);
    r.drawText(TITLE_FONT, TEXT_LEFT, y + pad, title.c_str(), ink, EpdFontFamily::BOLD);
    if (row.subtitle && *row.subtitle) {
      const auto sub = r.truncatedText(SMALL_FONT, row.subtitle, TEXT_RIGHT - TEXT_LEFT, EpdFontFamily::REGULAR);
      r.drawText(SMALL_FONT, TEXT_LEFT, y + pad + titleH, sub.c_str(), ink, EpdFontFamily::REGULAR);
    }
    // A hairline between rows (not next to an inverted or framed row).
    const bool nextFramed = i + 1 < v.rowCount && (v.rows[i + 1].emphasis || i + 1 == v.selected);
    if (i + 1 < v.rowCount && !row.emphasis && i != v.selected && !nextFramed) {
      r.fillRect(TEXT_LEFT, y + ROW_H - 1, TEXT_RIGHT - TEXT_LEFT, 1, true);
    }
  }
  drawBar(r, v.bar);
}

void drawMessage(GfxRenderer& r, const int top, const char* title, const char* const* paragraphs, const int count) {
  constexpr int width = TEXT_RIGHT - TEXT_LEFT;
  int y = top + 16;
  const auto lines = r.wrappedText(NOTOSANS_14_FONT_ID, title, width, 3, EpdFontFamily::BOLD);
  for (const auto& line : lines) {
    r.drawText(NOTOSANS_14_FONT_ID, TEXT_LEFT, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += r.getLineHeight(NOTOSANS_14_FONT_ID);
  }
  y += 12;
  const int lh = r.getLineHeight(UI_12_FONT_ID);
  for (int p = 0; p < count; p++) {
    if (!paragraphs[p] || !*paragraphs[p]) continue;
    const int linesLeft = (BAR_TOP - 8 - y) / lh;
    if (linesLeft <= 0) break;
    const auto para = r.wrappedText(UI_12_FONT_ID, paragraphs[p], width, linesLeft, EpdFontFamily::REGULAR);
    for (const auto& line : para) {
      r.drawText(UI_12_FONT_ID, TEXT_LEFT, y, line.c_str(), true, EpdFontFamily::REGULAR);
      y += lh;
    }
    y += 12;
  }
}

// ---- the guide menu -----------------------------------------------------------------------------------

namespace {
constexpr int MENU_TITLE_H = 52;
int menuHeight(const int count) { return MENU_TITLE_H + count * MENU_ROW_H + 8; }
}  // namespace

int menuTop(const int count) { return (SCREEN_H - menuHeight(count)) / 2; }

int menuRowAt(const int count, const int x, const int y) {
  const int top = menuTop(count);
  if (x < MENU_LEFT || x >= MENU_LEFT + MENU_W || y < top || y >= top + menuHeight(count)) return -2;
  const int rowsTop = top + MENU_TITLE_H;
  if (y < rowsTop) return -1;
  const int i = (y - rowsTop) / MENU_ROW_H;
  return i < count ? i : -1;
}

void drawMenu(GfxRenderer& r, const MenuView& v) {
  const int top = menuTop(v.count);
  const int h = menuHeight(v.count);
  // A white margin round the box keeps the screen beneath from touching its frame.
  r.fillRect(MENU_LEFT - 6, top - 6, MENU_W + 12, h + 12, false);
  r.fillRoundedRect(MENU_LEFT, top, MENU_W, h, 10, Color::White);
  r.drawRoundedRect(MENU_LEFT, top, MENU_W, h, 3, 10, true);
  const int titleH = r.getLineHeight(TITLE_FONT);
  centredText(r, TITLE_FONT, SCREEN_W / 2, top + (MENU_TITLE_H - titleH) / 2, v.title, MENU_W - 24,
              EpdFontFamily::BOLD);
  r.fillRect(MENU_LEFT + 12, top + MENU_TITLE_H - 2, MENU_W - 24, 2, true);
  for (int i = 0; i < v.count; i++) {
    const int y = top + MENU_TITLE_H + i * MENU_ROW_H;
    if (i == v.selected) r.drawRoundedRect(MENU_LEFT + 8, y + 4, MENU_W - 16, MENU_ROW_H - 8, 2, 8, true);
    r.drawText(TITLE_FONT, MENU_LEFT + 28, y + (MENU_ROW_H - titleH) / 2, v.labels[i] ? v.labels[i] : "", true,
               EpdFontFamily::REGULAR);
  }
}

}  // namespace gd::draw
