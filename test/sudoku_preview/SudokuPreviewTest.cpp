// Sudoku on the host with the real renderer and fonts (SleepCardHost): writes the screen set to
// build/sudoku/*.png for a look, and checks with the real glyphs that digits, notes and marks stay
// inside their squares (and inside the cursor's frame), the key labels sit in their keys, the
// status line fits, and nothing below the header is drawn in the bezel (x 0..6, 474..479), where
// the left-edge Back swipe starts.
//
// The headers here are stand-ins (title, back arrow and underline where Classic and Lyra draw
// them): GUI.drawHeader is device-only. Everything below y 112 is the device's drawing
// (src/activities/apps/SudokuDraw).
#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include "CardPreview.h"
#include "Sudoku.h"
#include "src/activities/apps/AppDraw.h"
#include "src/activities/apps/SudokuDraw.h"
#include "src/fontIds.h"

using namespace sd;
using sleepcards::preview::renderer;

namespace {

enum class Theme { Classic, Lyra };

std::string outDir() {
  const std::string dir = std::string(CARD_PREVIEW_REPO_ROOT) + "/build/sudoku";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

// Stand-ins for GUI.drawHeader: Classic's band is y 5..89, Lyra's 10..94.
void stubHeader(GfxRenderer& r, const Theme theme, const char* title) {
  const int top = theme == Theme::Classic ? 5 : 10;
  r.drawText(UI_12_FONT_ID, 18, top + 36, "<", true, EpdFontFamily::BOLD);
  r.drawText(UI_12_FONT_ID, theme == Theme::Classic ? 60 : 56, top + 36, title, true, EpdFontFamily::BOLD);
  r.fillRect(0, top + 81, 480, theme == Theme::Classic ? 2 : 3, true);
}

// Numbered puzzle n of a tier, as New puzzle makes it.
std::unique_ptr<Model> numbered(const int tier, const uint32_t n) {
  auto m = std::make_unique<Model>();
  Generated gen;
  const uint32_t seed = puzzleSeed(tier, n);
  EXPECT_TRUE(generate(seed, tier, gen));
  startGame(m->game, gen, n, seed);
  m->resetSession();
  return m;
}

struct Texts {
  char status[96] = {};
  char title[48] = {};
  char detail[64] = {};
};

draw::View viewFor(const Game& g, const draw::Note& note, Texts& t) {
  draw::View v;
  v.game = &g;
  v.statusMessage = draw::formatStatus(g, note, t.status, sizeof(t.status));
  v.status = t.status;
  v.toolLabels[0] = tr(STR_SD_NOTES);
  v.toolLabels[1] = tr(STR_SD_ERASE);
  v.toolLabels[2] = tr(STR_SD_UNDO);
  v.toolLabels[3] = tr(STR_CW_MENU);
  if (g.solved) {
    draw::formatBanner(g, t.title, sizeof(t.title), t.detail, sizeof(t.detail));
    v.bannerTitle = t.title;
    v.bannerDetail = t.detail;
    v.bannerButton = tr(STR_WS_NEW_PUZZLE);
  }
  return v;
}

void render(const draw::View& view, const Theme theme, const std::string& name) {
  GfxRenderer& r = renderer();
  r.clearScreen();
  stubHeader(r, theme, tr(STR_SUDOKU));
  draw::drawScreen(r, view);
  ASSERT_TRUE(sleepcards::preview::writeFramePng(outDir() + "/" + name + ".png"));
}

struct Ink {
  int x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
  bool any() const { return x1 >= 0; }
};
Ink inkIn(const int left, const int top, const int right, const int bottom) {
  Ink ink;
  for (int y = top; y <= bottom; y++) {
    for (int x = left; x <= right; x++) {
      if (!renderer().readPixel(x, y)) continue;
      ink.x0 = std::min(ink.x0, x);
      ink.y0 = std::min(ink.y0, y);
      ink.x1 = std::max(ink.x1, x);
      ink.y1 = std::max(ink.y1, y);
    }
  }
  return ink;
}

// Nothing below the header in the bezel columns (the Back swipe's start).
void expectBezelClear() {
  EXPECT_FALSE(inkIn(0, 112, TARGET_LEFT - 1, SCREEN_H - 1).any());
  EXPECT_FALSE(inkIn(TARGET_RIGHT, 112, SCREEN_W - 1, SCREEN_H - 1).any());
}

// An empty non-given square whose candidates (from what the grid shows) include a digit that is
// not its solution: a wrong entry that clashes with nothing. Returns the cell, digit in d.
int wrongQuietSquare(const Game& g, int& d) {
  for (int i = 0; i < CELLS; i++) {
    if (g.value[i]) continue;
    const uint16_t c = static_cast<uint16_t>(candidatesOf(g, i) & ~digitBit(g.solution[i]));
    for (d = 1; d <= 9; d++) {
      if (c & digitBit(d)) return i;
    }
  }
  return -1;
}

// An empty square and a digit one of its peers shows: a clash.
int clashSquare(const Game& g, int& d, const int skip) {
  for (int i = CELLS - 1; i >= 0; i--) {
    if (g.value[i] || i == skip) continue;
    for (int k = 0; k < 20; k++) {
      const int v = g.value[peerOf(i, k)];
      if (v && isGiven(g, peerOf(i, k))) {
        d = v;
        return i;
      }
    }
  }
  return -1;
}

// A medium puzzle part-way: about half the empty squares solved, every other one's notes filled,
// one wrong entry checked, one revealed square, every 4 placed, a clash just made.
struct MidSolve {
  std::unique_ptr<Model> m;
  draw::Note note;
  int wrong = -1;
  int clash = -1;
  int revealed = -1;
};
MidSolve midSolve() {
  MidSolve s;
  s.m = numbered(Medium, 1);
  Model& m = *s.m;
  Game& g = m.game;
  int placed = 0;
  for (int i = 0; i < CELLS; i++) {
    if (g.value[i]) continue;
    if (g.solution[i] == 4 || (placed++ % 2 == 0 && i < 54)) putDigit(m, i, g.solution[i]);
  }
  fillAllNotes(m);
  int d = 0;
  s.wrong = wrongQuietSquare(g, d);
  EXPECT_GE(s.wrong, 0);
  putDigit(m, s.wrong, d);
  setCursor(m, s.wrong);
  check(m, Scope::Square);
  for (int i = 0; i < CELLS && s.revealed < 0; i++) {
    if (!g.value[i] && i != s.wrong) s.revealed = i;
  }
  setCursor(m, s.revealed);
  reveal(m, Scope::Square);
  s.clash = clashSquare(g, d, s.wrong);
  EXPECT_GE(s.clash, 0);
  putDigit(m, s.clash, d);
  EXPECT_TRUE(isClash(g, s.clash));
  // The status says where: the clash's row, column or box.
  for (int u = 0; u < 27 && !s.note.clashDigit; u++) {
    bool here = false;
    int same = 0;
    for (int k = 0; k < 9; k++) {
      here |= unitCell(u, k) == s.clash;
      same += g.value[unitCell(u, k)] == d;
    }
    if (here && same > 1) {
      s.note.clashDigit = static_cast<uint8_t>(d);
      s.note.clashUnit = static_cast<uint8_t>(u);
    }
  }
  return s;
}

}  // namespace

TEST(SudokuPreview, EachTierFresh) {
  const char* names[TIER_COUNT] = {"sd_easy_fresh", "sd_medium_fresh", "sd_hard_fresh", "sd_expert_fresh"};
  for (int tier = 0; tier < TIER_COUNT; tier++) {
    auto m = numbered(tier, 1);
    Texts t;
    const draw::View v = viewFor(m->game, draw::Note{}, t);
    EXPECT_FALSE(v.statusMessage);
    EXPECT_STREQ(t.status, tr(STR_SD_START_TIP));
    render(v, tier % 2 ? Theme::Lyra : Theme::Classic, names[tier]);
    expectBezelClear();
    // The grid's ink stays in x 10..468, y 116..574.
    const Ink grid = inkIn(0, GRID_Y - 4, SCREEN_W - 1, GRID_BOTTOM + 1);
    EXPECT_EQ(grid.x0, GRID_X);
    EXPECT_EQ(grid.x1, GRID_X + GRID_PX - 1);
    EXPECT_EQ(grid.y0, GRID_Y);
    EXPECT_EQ(grid.y1, GRID_BOTTOM - 1);
  }
}

TEST(SudokuPreview, MidSolveCursorOnADigit) {
  MidSolve s = midSolve();
  Game& g = s.m->game;
  // The cursor on a 7 that is placed: every 7 ringed, every 7 note inverted.
  int seven = -1;
  for (int i = 0; i < CELLS && seven < 0; i++) {
    if (g.value[i] == 7 && isGiven(g, i)) seven = i;
  }
  ASSERT_GE(seven, 0);
  setCursor(*s.m, seven);
  Texts t;
  const draw::View v = viewFor(g, s.note, t);
  EXPECT_TRUE(v.statusMessage);
  render(v, Theme::Classic, "sd_mid_cursor");
  expectBezelClear();
  char want[32];
  std::snprintf(want, sizeof(want), "Two %ds in", s.note.clashDigit);
  EXPECT_EQ(std::strncmp(t.status, want, std::strlen(want)), 0) << t.status;

  // The cursor: a frame centred on the square's lines, 2 px into it, white inside that.
  const Rect c = cellRect(seven);
  const int lineLeft = seven % 9 % 3 == 0 ? BOX_LINE : CELL_LINE;
  for (int x = c.x - lineLeft - draw::CURSOR_OUTSIDE; x < c.x + draw::CURSOR_INSIDE; x++) {
    EXPECT_TRUE(renderer().readPixel(x, c.y + c.h / 2)) << x;
  }
  EXPECT_FALSE(renderer().readPixel(c.x - lineLeft - draw::CURSOR_OUTSIDE - 1, c.y + c.h / 2));
  for (int k = 0; k < draw::CURSOR_INSIDE; k++) EXPECT_TRUE(renderer().readPixel(c.x + c.w / 2, c.y + c.h - 1 - k));
  EXPECT_FALSE(renderer().readPixel(c.x + draw::CURSOR_INSIDE, c.y + 8));
  // Another 7: the 2 px ring 2 px inside its square, the gap white.
  for (int i = 0; i < CELLS; i++) {
    if (i == seven || g.value[i] != 7) continue;
    const Rect o = cellRect(i);
    EXPECT_FALSE(renderer().readPixel(o.x + 1, o.y + o.h / 2)) << i;
    EXPECT_TRUE(renderer().readPixel(o.x + SAME_RING_GAP, o.y + o.h / 2)) << i;
    EXPECT_TRUE(renderer().readPixel(o.x + SAME_RING_GAP + 1, o.y + o.h / 2)) << i;
    EXPECT_FALSE(renderer().readPixel(o.x + SAME_RING_GAP + SAME_RING, o.y + o.h / 2)) << i;
  }
  // A 7 note: inverted (its box black where the digit has no ink).
  int noted = 0;
  for (int i = 0; i < CELLS; i++) {
    if (g.value[i] || !(g.notes[i] & digitBit(7))) continue;
    const Rect o = cellRect(i);
    const int nx = o.x + NOTE_X[0];
    const int ny = o.y + NOTE_Y[2];
    EXPECT_TRUE(renderer().readPixel(nx - 1, ny - 1)) << i;
    EXPECT_TRUE(renderer().readPixel(nx, ny + NOTE_DIGIT_H - 1)) << i;  // the 7's foot has no ink there
    noted++;
  }
  EXPECT_GT(noted, 0);
  // The clash bar: under the digit, clear of the cursor frame's reach.
  const Rect cl = cellRect(s.clash);
  const Ink bar = inkIn(cl.x + 13, cl.y + 38, cl.x + cl.w - 14, cl.y + cl.h - 1);
  ASSERT_TRUE(bar.any());
  EXPECT_LT(bar.y1, cl.y + cl.h - 1 - draw::CURSOR_INSIDE - 1);
  EXPECT_EQ(bar.y1 - bar.y0 + 1, CLASH_BAR);
  // The revealed triangle's corner, clear of the frame's reach.
  const Rect rv = cellRect(s.revealed);
  EXPECT_TRUE(renderer().readPixel(rv.x + rv.w - 1 - draw::MARK_INSET, rv.y + rv.h - 1 - draw::MARK_INSET));
  EXPECT_FALSE(renderer().readPixel(rv.x + rv.w - draw::MARK_INSET, rv.y + rv.h - draw::MARK_INSET));
  // The 4s are all placed: their key shows no count.
  EXPECT_EQ(remaining(g, 4), 0);
  const Rect k4 = digitKeyRect(4);
  EXPECT_FALSE(inkIn(k4.x + 4, k4.y + 56, k4.x + k4.w - 5, k4.y + k4.h - 6).any());
}

TEST(SudokuPreview, MarksOnTheCursor) {
  MidSolve s = midSolve();
  // The cursor on the checked wrong entry, then on the revealed square, then on full notes.
  setCursor(*s.m, s.wrong);
  Texts t;
  render(viewFor(s.m->game, draw::Note{}, t), Theme::Lyra, "sd_cursor_wrong");
  const Rect w = cellRect(s.wrong);
  // The slash runs inside the frame: its ends' pixels are black, the frame's inner edge too.
  EXPECT_TRUE(renderer().readPixel(w.x + w.w - 1 - draw::MARK_INSET, w.y + draw::MARK_INSET));
  setCursor(*s.m, s.revealed);
  render(viewFor(s.m->game, draw::Note{}, t), Theme::Classic, "sd_cursor_revealed");
  int full = -1;
  for (int i = 0; i < CELLS && full < 0; i++) {
    if (!s.m->game.value[i] && i != s.wrong && i != s.clash) full = i;
  }
  ASSERT_GE(full, 0);
  s.m->game.notes[full] = ALL_DIGITS;
  setCursor(*s.m, full);
  render(viewFor(s.m->game, draw::Note{}, t), Theme::Classic, "sd_cursor_notes");
  // The notes stay clear of the frame: white rows and columns between them and its inner edge.
  const Rect c = cellRect(full);
  const int in = draw::CURSOR_INSIDE;
  EXPECT_FALSE(inkIn(c.x + in, c.y + in, c.x + c.w - 1 - in, c.y + NOTE_Y[0] - 1).any());
  EXPECT_FALSE(inkIn(c.x + in, c.y + NOTE_Y[2] + NOTE_DIGIT_H, c.x + c.w - 1 - in, c.y + c.h - 1 - in).any());
  EXPECT_FALSE(inkIn(c.x + in, c.y + in, c.x + NOTE_X[0] - 1, c.y + c.h - 1 - in).any());
  EXPECT_FALSE(inkIn(c.x + NOTE_X[2] + NOTE_DIGIT_W, c.y + in, c.x + c.w - 1 - in, c.y + c.h - 1 - in).any());
  EXPECT_TRUE(inkIn(c.x + in, c.y + in, c.x + c.w - 1 - in, c.y + c.h - 1 - in).any());
}

TEST(SudokuPreview, LockedDigitAndNotesMode) {
  MidSolve s = midSolve();
  Model& m = *s.m;
  holdDigit(m, 5, 1000);
  toggleNotesMode(m, 2000);
  ASSERT_EQ(m.game.lock, 5);
  ASSERT_TRUE(m.game.notesMode);
  draw::Note note;
  note.msg = Msg::Hint;
  note.hint.kind = HintKind::Step;
  note.hint.step.tech = XWing;
  note.hint.step.digit = 7;
  Texts t;
  const draw::View v = viewFor(m.game, note, t);
  EXPECT_STREQ(t.status, "Needs an X-wing on 7s first");
  render(v, Theme::Lyra, "sd_locked_notes");
  expectBezelClear();
  // The locked key and the Notes key are inverted: black just inside their outline.
  const Rect k5 = digitKeyRect(5);
  EXPECT_TRUE(renderer().readPixel(k5.x + 4, k5.y + 4));
  const Rect notes = toolKeyRect(Tool::Notes);
  EXPECT_TRUE(renderer().readPixel(notes.x + 6, notes.y + 6));
  const Rect erase = toolKeyRect(Tool::Erase);
  EXPECT_FALSE(renderer().readPixel(erase.x + 6, erase.y + 6));

  // Erase locked instead.
  holdErase(m, 3000);
  render(viewFor(m.game, draw::Note{}, t), Theme::Classic, "sd_erase_locked");
  EXPECT_TRUE(renderer().readPixel(erase.x + 6, erase.y + 6));
}

TEST(SudokuPreview, KeyLabelsInsideTheirKeys) {
  auto m = numbered(Easy, 1);
  Texts t;
  render(viewFor(m->game, draw::Note{}, t), Theme::Classic, "sd_keys");
  for (int d = 1; d <= 9; d++) {
    const Rect k = digitKeyRect(d);
    // Inside the outline (2 px and the corner radius): the digit, then the count.
    const Ink digit = inkIn(k.x + 3, k.y + 3, k.x + k.w - 4, k.y + 52);
    ASSERT_TRUE(digit.any()) << d;
    EXPECT_NEAR((digit.x0 + digit.x1) / 2.0, k.x + k.w / 2.0, 1.5) << d;
    const Ink count = inkIn(k.x + 3, k.y + 56, k.x + k.w - 4, k.y + k.h - 4);
    EXPECT_TRUE(count.any()) << d;
    EXPECT_GT(count.y0, digit.y1 + 3) << d;
  }
  for (int i = 0; i < TOOL_COUNT; i++) {
    const Rect k = toolKeyRect(static_cast<Tool>(i));
    const Ink label = inkIn(k.x + 3, k.y + 3, k.x + k.w - 4, k.y + k.h - 4);
    ASSERT_TRUE(label.any()) << i;
    EXPECT_NEAR((label.x0 + label.x1) / 2.0, k.x + k.w / 2.0, 2.0) << i;
    EXPECT_NEAR((label.y0 + label.y1) / 2.0, k.y + k.h / 2.0, 3.0) << i;
  }
}

// Every digit, bold and regular, centred in a square and clear of the cursor's frame and of the
// clash bar's row; every note inside the square.
TEST(SudokuPreview, DigitsFitTheSquares) {
  for (const auto style : {EpdFontFamily::BOLD, EpdFontFamily::REGULAR}) {
    for (int d = 1; d <= 9; d++) {
      Game g;
      for (int i = 0; i < CELLS; i++) g.solution[i] = static_cast<uint8_t>(1 + (i * 4 + i / 9 + i / 27) % 9);
      const int cell = 40;
      g.value[cell] = static_cast<uint8_t>(d);
      if (style == EpdFontFamily::BOLD) g.givens[cell] = static_cast<uint8_t>(d);
      Texts t;
      GfxRenderer& r = renderer();
      r.clearScreen();
      draw::drawGrid(r, viewFor(g, draw::Note{}, t));
      const Rect c = cellRect(cell);
      const Ink ink = inkIn(c.x, c.y, c.x + c.w - 1, c.y + c.h - 1);
      ASSERT_TRUE(ink.any());
      EXPECT_GE(ink.x0, c.x + CURSOR_FRAME + 2) << d;
      EXPECT_LE(ink.x1, c.x + c.w - 1 - CURSOR_FRAME - 2) << d;
      EXPECT_GE(ink.y0, c.y + CURSOR_FRAME + 2) << d;
      EXPECT_LE(ink.y1, c.y + c.h - 1 - CURSOR_FRAME - 2 - CLASH_BAR - 1) << d;
      EXPECT_NEAR((ink.y0 + ink.y1) / 2.0, c.y + c.h / 2.0, 1.5) << d;
    }
  }
  Game g;
  for (int i = 0; i < CELLS; i++) g.notes[i] = ALL_DIGITS;
  Texts t;
  renderer().clearScreen();
  draw::drawGrid(renderer(), viewFor(g, draw::Note{}, t));
  const Rect c = cellRect(40);
  const Ink ink = inkIn(c.x, c.y, c.x + c.w - 1, c.y + c.h - 1);
  EXPECT_EQ(ink.x0, c.x + NOTE_X[0]);
  EXPECT_EQ(ink.y0, c.y + NOTE_Y[0]);
  EXPECT_LE(ink.x1, c.x + c.w - 1 - 2);
  EXPECT_LE(ink.y1, c.y + c.h - 1 - 2);
}

TEST(SudokuPreview, StatusLines) {
  auto m = numbered(Medium, 14);
  Game& g = m->game;
  char out[96];
  setCursor(*m, 0);
  EXPECT_FALSE(draw::formatStatus(g, draw::Note{}, out, sizeof(out)));
  char want[48];
  std::snprintf(want, sizeof(want), "Medium 14 - %d to go", emptyCount(g));
  EXPECT_STREQ(out, want);
  draw::Note n;
  n.msg = Msg::Hint;
  n.hint.kind = HintKind::Single;
  n.hint.digit = 7;
  n.hint.step.unit = 19;
  EXPECT_TRUE(draw::formatStatus(g, n, out, sizeof(out)));
  EXPECT_STREQ(out, "Only 7 fits here (box 2)");
  n.hint.step.unit = NO_UNIT;
  draw::formatStatus(g, n, out, sizeof(out));
  EXPECT_STREQ(out, "Only 7 fits here");
  n.hint.kind = HintKind::Step;
  n.hint.step.tech = NakedPair;
  n.hint.step.digits = static_cast<uint16_t>(digitBit(3) | digitBit(7));
  draw::formatStatus(g, n, out, sizeof(out));
  EXPECT_STREQ(out, "Needs a naked pair (3, 7) first");
  n.hint.kind = HintKind::Trial;
  draw::formatStatus(g, n, out, sizeof(out));
  EXPECT_STREQ(out, "No step short of a trial");
  n = draw::Note{};
  n.msg = Msg::Checked;
  n.count = 2;
  draw::formatStatus(g, n, out, sizeof(out));
  EXPECT_STREQ(out, "2 squares are wrong");
  n = draw::Note{};
  n.clashDigit = 7;
  n.clashUnit = 0;
  draw::formatStatus(g, n, out, sizeof(out));
  EXPECT_STREQ(out, "Two 7s in row 1");
  // Every hint and message fits the status line in UI 10 (never the smaller fallback).
  const int room = statusRect().w - 8;
  for (int tech = 0; tech < TECH_COUNT; tech++) {
    n = draw::Note{};
    n.msg = Msg::Hint;
    n.hint.kind =
        tech == Trial ? HintKind::Trial : (isSingle(static_cast<Tech>(tech)) ? HintKind::Single : HintKind::Step);
    n.hint.digit = 8;
    n.hint.step.tech = static_cast<Tech>(tech);
    n.hint.step.digit = 8;
    n.hint.step.unit = 22;
    n.hint.step.digits = 0x1E0;  // 6, 7, 8, 9
    ASSERT_TRUE(draw::formatStatus(g, n, out, sizeof(out)));
    EXPECT_LE(renderer().getTextWidth(UI_10_FONT_ID, out, EpdFontFamily::BOLD), room) << out;
  }
  g.number = 99999;
  g.tier = Expert;
  for (int i = 0; i < CELLS; i++) {
    if (!g.value[i]) {
      g.value[i] = g.solution[(i + 1) % CELLS];
    }
  }
  draw::formatStatus(g, draw::Note{}, out, sizeof(out));
  EXPECT_LE(renderer().getTextWidth(UI_10_FONT_ID, out, EpdFontFamily::REGULAR), room) << out;
}

TEST(SudokuPreview, SolvedBanner) {
  auto m = numbered(Medium, 14);
  Model& model = *m;
  Game& g = model.game;
  int first = -1;
  for (int i = 0; i < CELLS; i++) {
    if (!g.value[i] && first < 0) first = i;
  }
  setCursor(model, first);
  reveal(model, Scope::Square);
  hint(model);
  for (int i = 0; i < CELLS; i++) {
    if (!g.value[i]) putDigit(model, i, g.solution[i]);
  }
  ASSERT_TRUE(g.solved);
  g.elapsed = 724;
  Texts t;
  const draw::View v = viewFor(g, draw::Note{}, t);
  EXPECT_STREQ(t.title, "Solved in 12:04");
  EXPECT_STREQ(t.detail, "Medium 14 - 1 hint - 1 reveal");
  render(v, Theme::Classic, "sd_solved");
  expectBezelClear();
  // No cursor or ring once solved: the hinted square shows just its digit.
  const Rect c = cellRect(first);
  EXPECT_FALSE(renderer().readPixel(c.x + 1, c.y + c.h / 2));
  // The button where the touch target is.
  const Rect b = bannerButtonRect();
  EXPECT_TRUE(inkIn(b.x, b.y, b.x + b.w - 1, b.y + 3).any());
  render(v, Theme::Lyra, "sd_solved_lyra");
}
