#include "CrosswordDraw.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AppDraw.h"
#include "fontIds.h"
#include "sleepcards/CardDraw.h"

namespace cw::draw {

namespace {

constexpr char ELLIPSIS[] = "...";
constexpr size_t LINE_BUF = 240;  // one clue line's bytes (a 358 px line holds far fewer)
constexpr int KEY_RADIUS = 6;

// Where one square's ink may go: inside its lines. Every square has the same inside (the border
// is drawn outside the squares, over where the edge squares' outer lines would be).
struct Inner {
  int left, top, right, bottom;  // inclusive
};

Inner innerOf(const ScreenLayout& l, const int row, const int col) {
  const int x = l.cellX(col);
  const int y = l.cellY(row);
  return Inner{x + INNER_LINE, y + INNER_LINE, x + l.cell - 1, y + l.cell - 1};
}

void drawTinyNumber(GfxRenderer& r, const int number, const int x, const int y, const bool black, const bool halo) {
  uint8_t digits[3];
  const int count = numberDigits(number, digits);
  for (int pass = halo ? 0 : 1; pass < 2; pass++) {
    for (int i = 0; i < count; i++) {
      const int dx = x + i * (DIGIT_W + DIGIT_GAP);
      for (int py = 0; py < DIGIT_H; py++) {
        for (int px = 0; px < DIGIT_W; px++) {
          if (!digitPixel(digits[i], px, py)) continue;
          if (pass == 1) {
            r.drawPixel(dx + px, y + py, black);
            continue;
          }
          for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) r.drawPixel(dx + px + ox, y + py + oy, !black);
          }
        }
      }
    }
  }
}

// The cursor's checkerboard, CURSOR_CHECK px squares on the screen's own grid.
void fillChecker(GfxRenderer& r, const int x0, const int y0, const int x1, const int y1) {
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) r.drawPixel(x, y, ((x / CURSOR_CHECK + y / CURSOR_CHECK) & 1) != 0);
  }
}

// The SMALL_FONT digits' ink top below the drawText y.
int smallDigitInkTop(const GfxRenderer& r) {
  const EpdGlyph* zero = appdraw::glyphFor(r, SMALL_FONT_ID, '0', EpdFontFamily::REGULAR);
  return zero ? r.getFontAscenderSize(SMALL_FONT_ID) - zero->top : 0;
}

// SMALL_FONT digits with their ink's top at y.
void drawSmallNumber(GfxRenderer& r, const int number, const int x, const int y, const bool black, const bool halo) {
  char text[4];
  std::snprintf(text, sizeof(text), "%d", number);
  const int top = y - smallDigitInkTop(r);
  if (halo) {
    for (int oy = -1; oy <= 1; oy++) {
      for (int ox = -1; ox <= 1; ox++) {
        if (ox != 0 || oy != 0) r.drawText(SMALL_FONT_ID, x + ox, top + oy, text, !black);
      }
    }
  }
  r.drawText(SMALL_FONT_ID, x, top, text, black);
}

// A 1 px ring of radius rad around (cx, cy) (centres on half pixels when cx2/cy2 are odd).
void drawRing(GfxRenderer& r, const int cx2, const int cy2, const int rad, const bool black) {
  // In doubled coordinates, so an even-sized square's centre between two pixels is exact.
  const int outer = (2 * rad + 1) * (2 * rad + 1);
  const int inner = (2 * rad - 1) * (2 * rad - 1);
  for (int y = (cy2 - 2 * rad - 2) / 2; y <= (cy2 + 2 * rad + 2) / 2; y++) {
    for (int x = (cx2 - 2 * rad - 2) / 2; x <= (cx2 + 2 * rad + 2) / 2; x++) {
      const int dx = 2 * x + 1 - cx2;
      const int dy = 2 * y + 1 - cy2;
      const int d = dx * dx + dy * dy;
      if (d < outer && d >= inner) r.drawPixel(x, y, black);
    }
  }
}

// ---- clue wrapping --------------------------------------------------------------------------

struct Lines {
  int start[CLUE_LINES_MAX] = {};
  int len[CLUE_LINES_MAX] = {};
  int count = 0;
  bool cut = false;  // text was left over
};

int widthOf(const GfxRenderer& r, const int fontId, const char* s, const int n, const EpdFontFamily::Style style) {
  if (n <= 0) return 0;
  char buf[LINE_BUF];
  if (static_cast<size_t>(n) >= sizeof(buf)) return 1 << 20;
  std::memcpy(buf, s, static_cast<size_t>(n));
  buf[n] = '\0';
  return r.getTextWidth(fontId, buf, style);
}

int nextCodePoint(const char* s, int i, const int n) {
  i++;
  while (i < n && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) i++;
  return i;
}

// Greedy word wrap into at most maxLines lines; line 0 is `first` px wide, the rest `width`. A
// word wider than a whole line is split between code points.
Lines wrap(const GfxRenderer& r, const int fontId, const char* text, const int first, const int width,
           const int maxLines) {
  Lines out;
  const int n = static_cast<int>(std::strlen(text));
  int pos = 0;
  while (pos < n && text[pos] == ' ') pos++;
  while (pos < n) {
    if (out.count == maxLines) {
      out.cut = true;
      break;
    }
    const int room = out.count == 0 ? first : width;
    int end = pos;  // the line so far is text[pos, end)
    while (end < n) {
      int wordEnd = end;
      while (wordEnd < n && text[wordEnd] == ' ') wordEnd++;
      while (wordEnd < n && text[wordEnd] != ' ') wordEnd++;
      if (widthOf(r, fontId, text + pos, wordEnd - pos, EpdFontFamily::REGULAR) > room) break;
      end = wordEnd;
    }
    if (end == pos) {
      // Not even one word fits. On a short first line, the word starts the next line instead;
      // otherwise it is split.
      if (out.count == 0 && first < width) {
        out.start[out.count] = pos;
        out.len[out.count] = 0;
        out.count++;
        continue;
      }
      end = nextCodePoint(text, pos, n);
      while (end < n && text[end] != ' ') {
        const int next = nextCodePoint(text, end, n);
        if (widthOf(r, fontId, text + pos, next - pos, EpdFontFamily::REGULAR) > room) break;
        end = next;
      }
    }
    out.start[out.count] = pos;
    out.len[out.count] = end - pos;
    out.count++;
    pos = end;
    while (pos < n && text[pos] == ' ') pos++;
  }
  return out;
}

// text[start, start + len) as one line, cut to fit `room` with "..." when `ellipsis`.
void drawLine(GfxRenderer& r, const int fontId, const int x, const int y, const char* s, const int len, const int room,
              const bool ellipsis) {
  char buf[LINE_BUF];
  int n = len < static_cast<int>(sizeof(buf)) - 4 ? len : static_cast<int>(sizeof(buf)) - 4;
  // Back to a code point boundary.
  while (n > 0 && n < len && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
  std::memcpy(buf, s, static_cast<size_t>(n));
  buf[n] = '\0';
  if (ellipsis) {
    std::strcat(buf, ELLIPSIS);
    while (n > 0 && r.getTextWidth(fontId, buf) > room) {
      // Drop one code point (and any space before the dots).
      n--;
      while (n > 0 && (static_cast<unsigned char>(buf[n]) & 0xC0) == 0x80) n--;
      while (n > 0 && buf[n - 1] == ' ') n--;
      std::memcpy(buf + n, ELLIPSIS, sizeof(ELLIPSIS));
    }
  }
  r.drawText(fontId, x, y, buf);
}

struct Setting {
  int fontId;
  int maxLines;
  bool allowCut;
};
constexpr Setting SETTINGS[] = {{UI_12_FONT_ID, 2, false}, {UI_10_FONT_ID, 3, false}, {UI_10_FONT_ID, 3, true}};

Lines layoutClue(const GfxRenderer& r, const char* label, const char* text, const int width, ClueFit& fit) {
  Lines lines;
  for (const Setting& s : SETTINGS) {
    const int labelW = label && label[0] ? r.getTextWidth(s.fontId, label, EpdFontFamily::BOLD) +
                                               r.getSpaceWidth(s.fontId, EpdFontFamily::REGULAR)
                                         : 0;
    lines = wrap(r, s.fontId, text ? text : "", width - labelW, width, s.maxLines);
    if (!lines.cut || s.allowCut) {
      fit.fontId = s.fontId;
      fit.lines = lines.count > 0 ? lines.count : 1;
      fit.ellipsis = lines.cut;
      return lines;
    }
  }
  return lines;
}

}  // namespace

int letterFontFor(const CellStyle& style) {
  switch (style.letterPoints) {
    case 18:
      return NOTOSANS_18_FONT_ID;
    case 16:
      return NOTOSANS_16_FONT_ID;
    case 14:
      return NOTOSANS_14_FONT_ID;
    default:
      return NOTOSANS_12_FONT_ID;
  }
}

ClueFit fitClue(const GfxRenderer& r, const char* label, const char* text, const int width) {
  ClueFit fit;
  layoutClue(r, label, text, width, fit);
  return fit;
}

void drawGrid(GfxRenderer& r, const Puzzle& p, const ScreenLayout& l, const View& view) {
  if (!view.prog || l.cell <= 0) return;
  const Progress& prog = *view.prog;
  const int cells = p.cells();
  const bool live = !prog.solved;  // a solved grid shows no cursor and no word

  // Blocks, then the lines between squares, then the border.
  for (int i = 0; i < cells; i++) {
    if (p.isBlock(i)) r.fillRect(l.cellX(p.colOf(i)), l.cellY(p.rowOf(i)), l.cell + 1, l.cell + 1, true);
  }
  for (int c = 1; c < l.w; c++) r.fillRect(l.cellX(c), l.grid.y, INNER_LINE, l.grid.h, true);
  for (int row = 1; row < l.h; row++) r.fillRect(l.grid.x, l.cellY(row), l.grid.w, INNER_LINE, true);
  // The border: the edge squares' own line and one pixel more outside.
  r.drawRect(l.grid.x - (OUTER_LINE - INNER_LINE), l.grid.y - (OUTER_LINE - INNER_LINE), l.grid.w + OUTER_LINE + 1,
             l.grid.h + OUTER_LINE + 1, OUTER_LINE, true);

  // The cursor: a checkered square inside a 1 px white frame - grey on the panel, so it never
  // reads as a block (a solid black cursor did: "It looks too much like the black no-type
  // squares"). Its letter and number stay black, on a white halo.
  const int cursor = prog.cursor < cells && !p.isBlock(prog.cursor) && live ? prog.cursor : -1;
  if (cursor >= 0) {
    const Inner in = innerOf(l, p.rowOf(cursor), p.colOf(cursor));
    fillChecker(r, in.left + 1, in.top + 1, in.right - 1, in.bottom - 1);
  }

  // The current word: a 3 px outline over its edge (one pixel in, the line, one pixel out).
  // The squares it runs along (the word's and their neighbours) draw their letters and numbers
  // on a halo, so the outline stops short of them.
  int word = -1;
  int nearTop = -1, nearLeft = -1, nearBottom = -2, nearRight = -2;
  if (cursor >= 0 && p.entryCount > 0) {
    word = currentEntry(p, prog);
    const Entry& e = p.entries[word];
    const int across = e.dir == ACROSS;
    const int rows = across ? 1 : e.len;
    const int cols = across ? e.len : 1;
    r.drawRect(l.cellX(e.col) - 1, l.cellY(e.row) - 1, cols * l.cell + 3, rows * l.cell + 3, WORD_OUTLINE, true);
    nearTop = e.row - 1;
    nearLeft = e.col - 1;
    nearBottom = e.row + rows;
    nearRight = e.col + cols;
  }
  // The cursor's white frame is drawn over the outline's inner pixel, so the checkered square stays
  // apart from the outline and from a block beside it (the outline is 2 px along the cursor).
  if (cursor >= 0) {
    const Inner in = innerOf(l, p.rowOf(cursor), p.colOf(cursor));
    r.drawRect(in.left, in.top, in.right - in.left + 1, in.bottom - in.top + 1, 1, false);
  }

  const CellStyle& style = l.style;
  const int fontId = letterFontFor(style);
  const int ascender = r.getFontAscenderSize(fontId);
  // The numbers' ink, from the square's top.
  const int numberTop = std::max(style.numberY, INNER_LINE + 1);
  int numberH = DIGIT_H;
  if (!style.tinyDigits) {
    const EpdGlyph* zero = appdraw::glyphFor(r, SMALL_FONT_ID, '0', EpdFontFamily::REGULAR);
    numberH = zero ? zero->height : 14;
  }
  for (int i = 0; i < cells; i++) {
    if (p.isBlock(i)) continue;
    const int row = p.rowOf(i);
    const int col = p.colOf(i);
    const Inner in = innerOf(l, row, col);
    constexpr bool ink = true;  // black, on the cursor's checker too (with a halo)
    const bool nearWord = row >= nearTop && row <= nearBottom && col >= nearLeft && col <= nearRight;
    const uint8_t flags = prog.flags[i];

    if (p.isCircled(i)) {
      const int rad = (in.right - in.left + 1) / 2 - 1;
      // On the checker a lone 1 px ring is lost in the grey: it stands on a white ring each side.
      if (i == cursor) {
        drawRing(r, in.left + in.right + 1, in.top + in.bottom + 1, rad - 1, !ink);
        drawRing(r, in.left + in.right + 1, in.top + in.bottom + 1, rad + 1, !ink);
      }
      drawRing(r, in.left + in.right + 1, in.top + in.bottom + 1, rad, ink);
    }
    const char letter = prog.fill[i];
    const bool hasLetter = letter >= 'A' && letter <= 'Z';
    const bool wrong = hasLetter && (flags & FLAG_WRONG) != 0;
    if (wrong) {
      constexpr int INSET = 3;
      // drawLine thickens by rows: 3 rows of a 45 degree line are WRONG_LINE (2) px across.
      // On the checker the slash stands on a white band two rows wider each side.
      if (i == cursor) {
        r.drawLine(in.right - INSET, in.top + INSET - 3, in.left + INSET, in.bottom - INSET - 3, WRONG_LINE + 5, !ink);
      }
      r.drawLine(in.right - INSET, in.top + INSET - 1, in.left + INSET, in.bottom - INSET - 1, WRONG_LINE + 1, ink);
    }
    // The number's ink box (its digits' widths: the tiny ones exactly, SMALL_FONT's measured).
    const int nx = l.cellX(col) + std::max(style.numberX, INNER_LINE + 1);
    const int ny = l.cellY(row) + numberTop;
    int numberRight = nx - 1;
    if (p.number[i] > 0) {
      if (style.tinyDigits) {
        numberRight = nx + numberWidth(p.number[i]) - 1;
      } else {
        char digits[4];
        std::snprintf(digits, sizeof(digits), "%d", p.number[i]);
        numberRight = nx + r.getTextWidth(SMALL_FONT_ID, digits) - 1;
      }
    }
    bool numberHalo = false;
    if (hasLetter) {
      const EpdGlyph* g = appdraw::glyphFor(r, fontId, static_cast<unsigned char>(letter), EpdFontFamily::BOLD);
      // Bottom-aligned: the ink's lowest row on the margin, so a letter with a tail below the
      // baseline (Q, J) is lifted by it.
      int baseline = std::min(l.cellY(row) + style.letterBottom, in.bottom - 1) + 1;
      // A square whose bottom edge carries the word's outline keeps a white row above it.
      const bool outlineBelow = col > nearLeft && col < nearRight && (row == nearBottom - 1 || row == nearTop);
      if (outlineBelow) baseline--;
      if (g && g->height > g->top) baseline -= g->height - g->top;
      const int cx = (in.left + in.right + 1) / 2;
      const bool halo = wrong || nearWord || p.isCircled(i) || i == cursor;
      appdraw::drawLetterOnBaseline(r, fontId, ascender, letter, cx, baseline, ink, halo);
      // A tall letter (Q, J) in a small square can reach the number: the number then goes on top
      // with a halo of its own.
      if (g && p.number[i] > 0) {
        const int inkLeft = cx - g->width / 2;
        const int inkTop = baseline - g->top;
        numberHalo = inkTop <= ny + numberH && inkLeft <= numberRight + 1;
      }
    }
    if (p.number[i] > 0) {
      numberHalo = numberHalo || nearWord || i == cursor;
      if (style.tinyDigits) {
        drawTinyNumber(r, p.number[i], nx, ny, ink, numberHalo);
      } else {
        drawSmallNumber(r, p.number[i], nx, ny, ink, numberHalo);
      }
    }
    if (hasLetter && (flags & FLAG_REVEALED) != 0) {
      if (i == cursor) {
        // On the checker the mark stands on a white triangle two pixels larger.
        constexpr int BACK = REVEALED_TRIANGLE + 2;
        const int bx[3] = {in.right - BACK, in.right, in.right};
        const int by[3] = {in.bottom, in.bottom, in.bottom - BACK};
        r.fillPolygon(bx, by, 3, false);
      }
      const int xs[3] = {in.right - REVEALED_TRIANGLE, in.right, in.right};
      const int ys[3] = {in.bottom, in.bottom, in.bottom - REVEALED_TRIANGLE};
      r.fillPolygon(xs, ys, 3, ink);
    }
  }
}

void drawClueBar(GfxRenderer& r, const ScreenLayout& l, const View& view) {
  appdraw::drawButton(r, l.prevButton.x, l.prevButton.y, l.prevButton.w, l.prevButton.h, "<");
  appdraw::drawButton(r, l.nextButton.x, l.nextButton.y, l.nextButton.w, l.nextButton.h, ">");
  const Rect& box = l.clueText;
  if (view.barMessage && view.barMessage[0]) {
    ClueFit fit;
    const Lines lines = layoutClue(r, "", view.barMessage, box.w, fit);
    const int lineH = r.getLineHeight(fit.fontId);
    int y = box.y + (box.h - fit.lines * lineH) / 2;
    for (int i = 0; i < lines.count; i++, y += lineH) {
      drawLine(r, fit.fontId, box.x, y, view.barMessage + lines.start[i], lines.len[i], box.w,
               fit.ellipsis && i == lines.count - 1);
    }
    return;
  }
  const char* text = view.clueText ? view.clueText : "";
  ClueFit fit;
  const Lines lines = layoutClue(r, view.clueLabel, text, box.w, fit);
  const int lineH = r.getLineHeight(fit.fontId);
  int y = box.y + (box.h - fit.lines * lineH) / 2;
  int x = box.x;
  if (view.clueLabel && view.clueLabel[0]) {
    r.drawText(fit.fontId, x, y, view.clueLabel, true, EpdFontFamily::BOLD);
    x += r.getTextWidth(fit.fontId, view.clueLabel, EpdFontFamily::BOLD) +
         r.getSpaceWidth(fit.fontId, EpdFontFamily::REGULAR);
  }
  for (int i = 0; i < lines.count; i++, y += lineH) {
    const int lx = i == 0 ? x : box.x;
    drawLine(r, fit.fontId, lx, y, text + lines.start[i], lines.len[i], box.right() - lx,
             fit.ellipsis && i == lines.count - 1);
  }
}

void drawKeyboard(GfxRenderer& r, const View& view) {
  const int letterFont = NOTOSANS_14_FONT_ID;
  for (int i = 0; i < KEY_COUNT; i++) {
    const Key& k = keyboardKey(i);
    r.drawRoundedRect(k.rect.x, k.rect.y, k.rect.w, k.rect.h, 2, KEY_RADIUS, true);
    char letter[2] = {k.letter, '\0'};
    const char* label = letter;
    if (k.kind == KeyKind::Menu) label = view.menuLabel ? view.menuLabel : "";
    if (k.kind == KeyKind::Del) label = view.delLabel ? view.delLabel : "";
    int fontId = letterFont;
    // A translated Menu / Del that is too wide drops to the smaller UI font.
    if (k.kind != KeyKind::Letter && r.getTextWidth(fontId, label, EpdFontFamily::BOLD) > k.rect.w - 8) {
      fontId = UI_10_FONT_ID;
    }
    const int top = appdraw::capTopFor(r, fontId, EpdFontFamily::BOLD, k.rect.y + k.rect.h / 2);
    if (k.kind == KeyKind::Letter) {
      // Centred on the letter's ink (J and Q sit off their advance).
      appdraw::drawLetterOnBaseline(r, fontId, r.getFontAscenderSize(fontId), k.letter, k.rect.x + k.rect.w / 2,
                                    top + r.getFontAscenderSize(fontId), true, false);
    } else {
      sleepcards::draw::drawTextCenteredAt(r, fontId, k.rect.x + k.rect.w / 2, top, label, true, EpdFontFamily::BOLD);
    }
  }
}

void drawBanner(GfxRenderer& r, const ScreenLayout& l, const View& view) {
  const Rect& b = l.banner;
  r.fillRect(b.x, b.y, b.w, b.h, false);
  r.drawRoundedRect(b.x, b.y, b.w, b.h, 3, 14, true);
  const bool detail = view.bannerDetail && view.bannerDetail[0];
  const int titleH = r.getLineHeight(UI_12_FONT_ID);
  const int detailH = detail ? r.getLineHeight(UI_10_FONT_ID) + 4 : 0;
  // The text block centred between the banner's top and the button.
  const int buttonTop = l.bannerButton.y;
  int y = b.y + (buttonTop - b.y - titleH - detailH) / 2;
  if (view.bannerTitle && view.bannerTitle[0]) {
    sleepcards::draw::drawTextCenteredAt(r, UI_12_FONT_ID, b.x + b.w / 2, y, view.bannerTitle, true,
                                         EpdFontFamily::BOLD);
  }
  y += titleH + 4;
  if (detail) sleepcards::draw::drawTextCenteredAt(r, UI_10_FONT_ID, b.x + b.w / 2, y, view.bannerDetail);
  const Rect& button = l.bannerButton;
  if (view.bannerButton && view.bannerButton[0]) {
    appdraw::drawButton(r, button.x, button.y, button.w, button.h, view.bannerButton);
  } else if (view.bannerNote && view.bannerNote[0]) {
    const int top = appdraw::capTopFor(r, UI_12_FONT_ID, EpdFontFamily::REGULAR, button.y + button.h / 2);
    sleepcards::draw::drawTextCenteredAt(r, UI_12_FONT_ID, button.x + button.w / 2, top, view.bannerNote);
  }
}

void drawScreen(GfxRenderer& r, const Puzzle& p, const ScreenLayout& layout, const View& view) {
  drawGrid(r, p, layout, view);
  if (view.prog && view.prog->solved) {
    drawBanner(r, layout, view);
  } else {
    drawClueBar(r, layout, view);
    drawKeyboard(r, view);
  }
}

}  // namespace cw::draw
