#include "SudokuDraw.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AppDraw.h"
#include "fontIds.h"
#include "sleepcards/CardDraw.h"

namespace sd::draw {

namespace {

constexpr int DIGIT_FONT = NOTOSANS_18_FONT_ID;
constexpr int NOTE_BOX_PAD = 1;   // a highlighted note's black box reaches this far past its ink
constexpr int KEY_DIGIT_CY = 32;  // a digit key's digit, centred this far below the key's top
constexpr int KEY_COUNT_CY = 68;  // and its count

// A square's digit drawn centred (both ways) on its own ink; returns the ink's bottom row.
int drawCellDigit(GfxRenderer& r, const Rect& cell, const int digit, const EpdFontFamily::Style style,
                  const bool halo) {
  const char c = static_cast<char>('0' + digit);
  const EpdGlyph* g = appdraw::glyphFor(r, DIGIT_FONT, static_cast<unsigned char>(c), style);
  if (!g) return cell.y + cell.h / 2;
  const int inkTop = cell.y + (cell.h - g->height) / 2;
  appdraw::drawLetterOnBaseline(r, DIGIT_FONT, r.getFontAscenderSize(DIGIT_FONT), c, cell.x + cell.w / 2,
                                inkTop + g->top, true, halo, style);
  return inkTop + g->height - 1;
}

// The cursor: a frame centred on the square's own lines, CURSOR_INSIDE px into the square and
// CURSOR_OUTSIDE into its neighbours (5 px across a 1 px line, 7 px across a box line). It never
// touches what a square holds: notes start 4 px in, digits further.
void drawCursor(GfxRenderer& r, const int cell) {
  const int col = cell % 9;
  const int row = cell / 9;
  const auto lineBefore = [](const int k) { return k % 3 == 0 ? BOX_LINE : CELL_LINE; };
  const Rect c = cellRect(cell);
  const int x0 = c.x - lineBefore(col) - CURSOR_OUTSIDE;
  const int y0 = c.y - lineBefore(row) - CURSOR_OUTSIDE;
  const int x1 = c.x + c.w - 1 + lineBefore(col + 1) + CURSOR_OUTSIDE;
  const int y1 = c.y + c.h - 1 + lineBefore(row + 1) + CURSOR_OUTSIDE;
  r.fillRect(x0, y0, x1 - x0 + 1, c.y + CURSOR_INSIDE - y0, true);
  r.fillRect(x0, c.y + c.h - CURSOR_INSIDE, x1 - x0 + 1, y1 - (c.y + c.h - CURSOR_INSIDE) + 1, true);
  r.fillRect(x0, y0, c.x + CURSOR_INSIDE - x0, y1 - y0 + 1, true);
  r.fillRect(c.x + c.w - CURSOR_INSIDE, y0, x1 - (c.x + c.w - CURSOR_INSIDE) + 1, y1 - y0 + 1, true);
}

// The 7x11 note digit d with its top-left at (x, y).
void drawNoteDigit(GfxRenderer& r, const int d, const int x, const int y, const bool black) {
  for (int py = 0; py < NOTE_DIGIT_H; py++) {
    for (int px = 0; px < NOTE_DIGIT_W; px++) {
      if (notePixel(d, px, py)) r.drawPixel(x + px, y + py, black);
    }
  }
}

// Every other pixel of a box turned to the background (a 1 px checker: a dimmed glyph).
void dimBox(GfxRenderer& r, const int x0, const int y0, const int x1, const int y1, const bool background) {
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      if (((x + y) & 1) != 0) r.drawPixel(x, y, background);
    }
  }
}

void unitText(const int unit, char* out, const size_t cap) {
  if (unit < 9) {
    std::snprintf(out, cap, tr(STR_SD_ROW), unit + 1);
  } else if (unit < 18) {
    std::snprintf(out, cap, tr(STR_SD_COLUMN), unit - 9 + 1);
  } else {
    std::snprintf(out, cap, tr(STR_SD_BOX), unit - 18 + 1);
  }
}

// The technique as the hint names it ("an X-wing"); nullptr for a single or a trial.
const char* techPhrase(const Tech tech) {
  switch (tech) {
    case Pointing:
      return tr(STR_SD_TECH_POINTING);
    case Claiming:
      return tr(STR_SD_TECH_CLAIMING);
    case NakedPair:
      return tr(STR_SD_TECH_NAKED_PAIR);
    case HiddenPair:
      return tr(STR_SD_TECH_HIDDEN_PAIR);
    case XWing:
      return tr(STR_SD_TECH_X_WING);
    case NakedTriple:
      return tr(STR_SD_TECH_NAKED_TRIPLE);
    case Swordfish:
      return tr(STR_SD_TECH_SWORDFISH);
    case HiddenTriple:
      return tr(STR_SD_TECH_HIDDEN_TRIPLE);
    case TurbotFish:
      return tr(STR_SD_TECH_TURBOT_FISH);
    case XYWing:
      return tr(STR_SD_TECH_XY_WING);
    case WWing:
      return tr(STR_SD_TECH_W_WING);
    case XYZWing:
      return tr(STR_SD_TECH_XYZ_WING);
    case NakedQuad:
      return tr(STR_SD_TECH_NAKED_QUAD);
    case Jellyfish:
      return tr(STR_SD_TECH_JELLYFISH);
    case HiddenQuad:
      return tr(STR_SD_TECH_HIDDEN_QUAD);
    default:
      return nullptr;
  }
}

// A subset technique: the hint names its digits, not one digit.
bool namesDigits(const Tech tech) {
  return tech == NakedPair || tech == HiddenPair || tech == NakedTriple || tech == HiddenTriple || tech == NakedQuad ||
         tech == HiddenQuad;
}

void formatHint(const Hint& h, char* out, const size_t cap) {
  char part[24];
  switch (h.kind) {
    case HintKind::Wrong:
      std::snprintf(out, cap, "%s", tr(STR_SD_HINT_WRONG));
      return;
    case HintKind::Single:
      if (h.step.unit == NO_UNIT) {
        std::snprintf(out, cap, tr(STR_SD_HINT_ONLY), h.digit);
      } else {
        unitText(h.step.unit, part, sizeof(part));
        std::snprintf(out, cap, tr(STR_SD_HINT_ONLY_IN), h.digit, part);
      }
      return;
    case HintKind::Step: {
      const char* phrase = techPhrase(h.step.tech);
      if (!phrase) break;
      if (namesDigits(h.step.tech)) {
        // "3, 7"
        size_t n = 0;
        part[0] = '\0';
        for (int d = 1; d <= 9 && n + 4 < sizeof(part); d++) {
          if (!(h.step.digits & digitBit(d))) continue;
          n += static_cast<size_t>(std::snprintf(part + n, sizeof(part) - n, n ? ", %d" : "%d", d));
        }
        std::snprintf(out, cap, tr(STR_SD_HINT_NEEDS_DIGITS), phrase, part);
      } else {
        std::snprintf(out, cap, tr(STR_SD_HINT_NEEDS_DIGIT), phrase, h.step.digit);
      }
      return;
    }
    case HintKind::Trial:
      break;
    case HintKind::Revealed:
      std::snprintf(out, cap, tr(STR_SD_HINT_REVEALED), h.digit);
      return;
    case HintKind::None:
      out[0] = '\0';
      return;
  }
  std::snprintf(out, cap, "%s", tr(STR_SD_HINT_TRIAL));
}

// Nothing done yet: the first frame's tip.
bool untouched(const Game& g) {
  if (g.solved || g.cursor != NO_CELL || g.lock != LOCK_NONE || g.notesMode) return false;
  for (int i = 0; i < CELLS; i++) {
    if (g.value[i] != g.givens[i] || g.notes[i]) return false;
  }
  return true;
}

// Text centred in a key, UI_12 Bold, or UI_10 Bold when it does not fit.
void drawKeyLabel(GfxRenderer& r, const Rect& key, const char* label, const bool black) {
  int fontId = UI_12_FONT_ID;
  if (r.getTextWidth(fontId, label, EpdFontFamily::BOLD) > key.w - 8) fontId = UI_10_FONT_ID;
  const int top = appdraw::capTopFor(r, fontId, EpdFontFamily::BOLD, key.y + key.h / 2);
  sleepcards::draw::drawTextCenteredAt(r, fontId, key.x + key.w / 2, top, label, black, EpdFontFamily::BOLD);
}

void drawKeyFrame(GfxRenderer& r, const Rect& key, const bool inverted) {
  if (inverted) {
    r.fillRoundedRect(key.x, key.y, key.w, key.h, KEY_RADIUS, Color::Black);
  } else {
    r.drawRoundedRect(key.x, key.y, key.w, key.h, 2, KEY_RADIUS, true);
  }
}

}  // namespace

int highlightDigit(const Game& g) {
  if (g.solved) return 0;
  if (g.lock >= 1 && g.lock <= 9) return g.lock;
  return g.cursor < CELLS ? g.value[g.cursor] : 0;
}

const char* tierName(const int tier) {
  switch (tier) {
    case Medium:
      return tr(STR_WS_MEDIUM);
    case Hard:
      return tr(STR_WS_HARD);
    case Expert:
      return tr(STR_SD_EXPERT);
    default:
      return tr(STR_WS_EASY);
  }
}

void formatPuzzleName(const Game& g, char* out, const size_t cap) {
  std::snprintf(out, cap, tr(STR_SD_PUZZLE_NAME), tierName(g.tier), static_cast<unsigned>(g.number));
}

void formatElapsed(const uint32_t seconds, char* out, const size_t cap) {
  const unsigned h = seconds / 3600;
  const unsigned m = (seconds / 60) % 60;
  const unsigned s = seconds % 60;
  if (h > 0) {
    std::snprintf(out, cap, "%u:%02u:%02u", h, m, s);
  } else {
    std::snprintf(out, cap, "%u:%02u", m, s);
  }
}

bool formatStatus(const Game& g, const Note& note, char* out, const size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  switch (note.msg) {
    case Msg::Given:
      std::snprintf(out, cap, "%s", tr(STR_SD_GIVEN));
      return true;
    case Msg::Locked:
      std::snprintf(out, cap, "%s", tr(STR_SD_REVEALED_SQUARE));
      return true;
    case Msg::EraseFirst:
      std::snprintf(out, cap, tr(STR_SD_ERASE_FIRST), note.digit);
      return true;
    case Msg::NothingToUndo:
      std::snprintf(out, cap, "%s", tr(STR_SD_NOTHING_TO_UNDO));
      return true;
    case Msg::NoSquare:
      std::snprintf(out, cap, "%s", tr(STR_SD_TAP_SQUARE));
      return true;
    case Msg::Checked:
      if (note.count == 0) {
        std::snprintf(out, cap, "%s", tr(STR_SD_NO_MISTAKES));
      } else if (note.count == 1) {
        std::snprintf(out, cap, "%s", tr(STR_SD_ONE_WRONG));
      } else {
        std::snprintf(out, cap, tr(STR_SD_WRONG), note.count);
      }
      return true;
    case Msg::Revealed:
      if (note.count == 0) {
        std::snprintf(out, cap, "%s", tr(STR_SD_NOTHING_TO_REVEAL));
      } else if (note.count == 1) {
        std::snprintf(out, cap, "%s", tr(STR_SD_ONE_REVEALED));
      } else {
        std::snprintf(out, cap, tr(STR_SD_REVEALED), note.count);
      }
      return true;
    case Msg::Hint:
      formatHint(note.hint, out, cap);
      if (out[0]) return true;
      break;
    case Msg::None:
      break;
  }
  if (note.clashDigit >= 1 && note.clashDigit <= 9 && note.clashUnit < 27) {
    char unit[24];
    unitText(note.clashUnit, unit, sizeof(unit));
    std::snprintf(out, cap, tr(STR_SD_TWO_IN), note.clashDigit, unit);
    return true;
  }
  if (untouched(g)) {
    std::snprintf(out, cap, "%s", tr(STR_SD_START_TIP));
    return false;
  }
  char name[32];
  formatPuzzleName(g, name, sizeof(name));
  const int clashes = clashCount(g);
  if (clashes == 1) {
    std::snprintf(out, cap, tr(STR_SD_ONE_CLASH), name);
  } else if (clashes > 1) {
    std::snprintf(out, cap, tr(STR_SD_CLASHES), name, clashes);
  } else {
    std::snprintf(out, cap, tr(STR_SD_TO_GO), name, emptyCount(g));
  }
  return false;
}

void formatBanner(const Game& g, char* title, const size_t titleCap, char* detail, const size_t detailCap) {
  char time[16];
  formatElapsed(g.elapsed, time, sizeof(time));
  std::snprintf(title, titleCap, tr(STR_CW_SOLVED_IN), time);
  formatPuzzleName(g, detail, detailCap);
  size_t n = std::strlen(detail);
  char part[24];
  const auto add = [&](const uint16_t count, const char* one, const char* many) {
    if (count == 0 || n + 4 >= detailCap) return;
    if (count == 1) {
      std::snprintf(part, sizeof(part), "%s", one);
    } else {
      std::snprintf(part, sizeof(part), many, count);
    }
    const int k = std::snprintf(detail + n, detailCap - n, " - %s", part);
    if (k > 0) n = std::min(detailCap - 1, n + static_cast<size_t>(k));
  };
  add(g.hints, tr(STR_WS_ONE_HINT), tr(STR_WS_HINTS));
  add(g.checks, tr(STR_CW_ONE_CHECK), tr(STR_CW_CHECKS));
  add(g.reveals, tr(STR_CW_ONE_REVEAL), tr(STR_CW_REVEALS));
}

void drawGrid(GfxRenderer& r, const View& view) {
  if (!view.game) return;
  const Game& g = *view.game;
  const bool live = !g.solved;  // a solved grid shows no cursor and no highlight

  // The border, then the lines between squares (box lines as thick as the border).
  r.drawRect(GRID_X, GRID_Y, GRID_PX, GRID_PX, BOX_LINE, true);
  for (int k = 1; k < 9; k++) {
    const int w = k % 3 == 0 ? BOX_LINE : CELL_LINE;
    r.fillRect(cellLeft(k) - w, GRID_Y, w, GRID_PX, true);
    r.fillRect(GRID_X, cellTop(k) - w, GRID_PX, w, true);
  }

  const int hl = highlightDigit(g);
  const int cursor = live && g.cursor < CELLS ? g.cursor : -1;
  const EpdGlyph* eight = appdraw::glyphFor(r, DIGIT_FONT, '8', EpdFontFamily::REGULAR);
  const int digitH = eight ? eight->height : 29;
  for (int i = 0; i < CELLS; i++) {
    const Rect cell = cellRect(i);
    const uint8_t v = g.value[i];
    if (i == cursor) {
      drawCursor(r, i);
    } else if (hl && v == hl) {
      r.drawRect(cell.x + SAME_RING_GAP, cell.y + SAME_RING_GAP, cell.w - 2 * SAME_RING_GAP, cell.h - 2 * SAME_RING_GAP,
                 SAME_RING, true);
    }
    if (v == 0) {
      const uint16_t notes = g.notes[i];
      for (int d = 1; d <= 9 && notes; d++) {
        if (!(notes & digitBit(d))) continue;
        const int nx = cell.x + NOTE_X[(d - 1) % 3];
        const int ny = cell.y + NOTE_Y[(d - 1) / 3];
        const bool inverted = live && d == hl;
        if (inverted) {
          r.fillRect(nx - NOTE_BOX_PAD, ny - NOTE_BOX_PAD, NOTE_DIGIT_W + 2 * NOTE_BOX_PAD,
                     NOTE_DIGIT_H + 2 * NOTE_BOX_PAD, true);
        }
        drawNoteDigit(r, d, nx, ny, !inverted);
      }
      continue;
    }
    const bool given = isGiven(g, i);
    const bool wrong = !given && isWrongMarked(g, i);
    const int right = cell.x + cell.w - 1 - MARK_INSET;
    const int bottom = cell.y + cell.h - 1 - MARK_INSET;
    if (wrong) {
      // drawLine thickens by rows: 3 rows of a 45 degree line are WRONG_LINE (2) px across.
      r.drawLine(right, cell.y + MARK_INSET, cell.x + MARK_INSET, bottom, WRONG_LINE + 1, true);
    }
    drawCellDigit(r, cell, v, given ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR, wrong);
    if (live && isClash(g, i)) {
      // Under the digit, as wide as a digit is.
      const int barTop = cell.y + (cell.h - digitH) / 2 + digitH + 2;
      r.fillRect(cell.x + 13, barTop, cell.w - 26, CLASH_BAR, true);
    }
    if (isRevealed(g, i)) {
      const int xs[3] = {right - REVEALED_TRIANGLE + 1, right, right};
      const int ys[3] = {bottom, bottom, bottom - REVEALED_TRIANGLE + 1};
      r.fillPolygon(xs, ys, 3, true);
    }
  }
}

void drawStatus(GfxRenderer& r, const View& view) {
  const char* text = view.status ? view.status : "";
  if (!text[0]) return;
  const Rect box = statusRect();
  const EpdFontFamily::Style style = view.statusMessage ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  int fontId = UI_10_FONT_ID;
  if (r.getTextWidth(fontId, text, style) > box.w - 8) fontId = SMALL_FONT_ID;
  const int top = appdraw::capTopFor(r, fontId, style, box.y + box.h / 2);
  sleepcards::draw::drawTextCenteredAt(r, fontId, box.x + box.w / 2, top, text, true, style);
}

void drawKeys(GfxRenderer& r, const View& view) {
  if (!view.game) return;
  const Game& g = *view.game;
  const int ascender = r.getFontAscenderSize(DIGIT_FONT);
  for (int d = 1; d <= 9; d++) {
    const Rect key = digitKeyRect(d);
    const bool locked = g.lock == d;
    drawKeyFrame(r, key, locked);
    const char c = static_cast<char>('0' + d);
    const EpdGlyph* glyph = appdraw::glyphFor(r, DIGIT_FONT, static_cast<unsigned char>(c), EpdFontFamily::BOLD);
    if (!glyph) continue;
    const int cx = key.x + key.w / 2;
    const int inkTop = key.y + KEY_DIGIT_CY - glyph->height / 2;
    appdraw::drawLetterOnBaseline(r, DIGIT_FONT, ascender, c, cx, inkTop + glyph->top, !locked, false,
                                  EpdFontFamily::BOLD);
    const int left = remaining(g, d);
    if (left == 0) {
      // All nine placed: the digit dimmed, no count.
      const int inkLeft = cx - glyph->width / 2;
      dimBox(r, inkLeft - 1, inkTop - 1, inkLeft + glyph->width, inkTop + glyph->height, locked);
      continue;
    }
    char count[4];
    std::snprintf(count, sizeof(count), "%d", left);
    const int top = appdraw::capTopFor(r, SMALL_FONT_ID, EpdFontFamily::REGULAR, key.y + KEY_COUNT_CY);
    sleepcards::draw::drawTextCenteredAt(r, SMALL_FONT_ID, cx, top, count, !locked);
  }
  for (int t = 0; t < TOOL_COUNT; t++) {
    const Tool tool = static_cast<Tool>(t);
    const Rect key = toolKeyRect(tool);
    const bool inverted = (tool == Tool::Notes && g.notesMode) || (tool == Tool::Erase && g.lock == LOCK_ERASE);
    drawKeyFrame(r, key, inverted);
    drawKeyLabel(r, key, view.toolLabels[t] ? view.toolLabels[t] : "", !inverted);
  }
}

void drawBanner(GfxRenderer& r, const View& view) {
  const Rect b = bannerRect();
  r.fillRect(b.x, b.y, b.w, b.h, false);
  r.drawRoundedRect(b.x, b.y, b.w, b.h, 3, 14, true);
  const bool detail = view.bannerDetail && view.bannerDetail[0];
  const int titleH = r.getLineHeight(UI_12_FONT_ID);
  const int detailH = detail ? r.getLineHeight(UI_10_FONT_ID) + 4 : 0;
  const Rect button = bannerButtonRect();
  // The text block centred between the banner's top and the button.
  int y = b.y + (button.y - b.y - titleH - detailH) / 2;
  if (view.bannerTitle && view.bannerTitle[0]) {
    sleepcards::draw::drawTextCenteredAt(r, UI_12_FONT_ID, b.x + b.w / 2, y, view.bannerTitle, true,
                                         EpdFontFamily::BOLD);
  }
  y += titleH + 4;
  if (detail) sleepcards::draw::drawTextCenteredAt(r, UI_10_FONT_ID, b.x + b.w / 2, y, view.bannerDetail);
  if (view.bannerButton && view.bannerButton[0]) {
    appdraw::drawButton(r, button.x, button.y, button.w, button.h, view.bannerButton);
  }
}

void drawScreen(GfxRenderer& r, const View& view) {
  drawGrid(r, view);
  if (view.game && view.game->solved) {
    drawBanner(r, view);
  } else {
    drawStatus(r, view);
    drawKeys(r, view);
  }
}

}  // namespace sd::draw
