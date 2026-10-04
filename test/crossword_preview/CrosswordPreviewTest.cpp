// Crossword on the host with the real renderer and fonts (SleepCardHost): writes the screen set
// to build/crossword/*.png for a look, and checks with the real glyphs that letters and numbers
// stay inside their squares at every size, the marks land where they should, the keyboard
// labels sit centred in their keys and the clue bar picks its font and lines as specified.
//
// The headers here are stand-ins (title, back arrow and underline where Classic and Lyra draw
// them): GUI.drawHeader is device-only. Everything below y 112 is the device's drawing
// (src/activities/apps/CrosswordDraw).
#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "CardPreview.h"
#include "Crossword.h"
#include "samples.h"
#include "src/activities/apps/AppDraw.h"
#include "src/activities/apps/CrosswordDraw.h"
#include "src/fontIds.h"

using namespace cw;
using sleepcards::preview::renderer;

namespace {

enum class Theme { Classic, Lyra };

std::string outDir() {
  const std::string dir = std::string(CARD_PREVIEW_REPO_ROOT) + "/build/crossword";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

// A test-only sample (test/crossword/samples.h): the drawing tests never depend on the shipped set.
std::unique_ptr<Puzzle> sample(const char* text) {
  auto p = std::make_unique<Puzzle>();
  const TextStatus st = parseTextPuzzle(text, std::strlen(text), *p);
  EXPECT_EQ(st.error, Error::None) << "line " << st.line;
  return p;
}

// One of the shipped built-ins (scripts/crossword/builtin.txt), for a look at the real set.
std::unique_ptr<Puzzle> builtin(const char* id) {
  auto p = std::make_unique<Puzzle>();
  const int index = findBuiltin(id);
  EXPECT_GE(index, 0) << id;
  if (index < 0) return p;
  const BuiltinPuzzle& b = builtinPuzzle(static_cast<size_t>(index));
  const TextStatus st = parseTextPuzzle(b.text, std::strlen(b.text), *p);
  EXPECT_EQ(st.error, Error::None) << id << " line " << st.line;
  return p;
}

std::unique_ptr<Puzzle> fixtureIpuz(const char* name) {
  std::ifstream in(std::string(CROSSWORD_FIXTURES) + "/" + name, std::ios::binary);
  EXPECT_TRUE(in.good()) << name;
  std::stringstream s;
  s << in.rdbuf();
  const std::string data = s.str();
  auto p = std::make_unique<Puzzle>();
  MemoryReader reader(data.data(), data.size());
  const LoadStatus st = parseIpuz(reader, name, *p);
  EXPECT_TRUE(st.ok()) << name << " error " << static_cast<int>(st.error);
  return p;
}

// Stand-ins for GUI.drawHeader: Classic's band is y 5..89, Lyra's 10..94.
void stubHeader(GfxRenderer& r, const Theme theme, const char* title) {
  const int top = theme == Theme::Classic ? 5 : 10;
  r.drawText(UI_12_FONT_ID, 18, top + 36, "<", true, EpdFontFamily::BOLD);
  r.drawText(UI_12_FONT_ID, theme == Theme::Classic ? 60 : 56, top + 36, title, true, EpdFontFamily::BOLD);
  r.fillRect(0, top + 81, 480, theme == Theme::Classic ? 2 : 3, true);
}

struct Texts {
  char label[8] = {};
  char clue[MAX_CLUE_BYTES + 1] = {};
  char message[64] = {};
  char bannerTitle[48] = {};
  char bannerDetail[48] = {};
};

draw::View viewFor(const Puzzle& p, const Progress& prog, Texts& t) {
  draw::View v;
  v.prog = &prog;
  const int e = currentEntry(p, prog);
  formatClueLabel(p, e, t.label, sizeof(t.label));
  std::snprintf(t.clue, sizeof(t.clue), "%s", p.clue(e));
  v.clueLabel = t.label;
  v.clueText = t.clue;
  v.menuLabel = tr(STR_CW_MENU);
  v.delLabel = tr(STR_CW_DEL);
  char time[16];
  formatElapsed(prog.elapsed, time, sizeof(time));
  std::snprintf(t.bannerTitle, sizeof(t.bannerTitle), tr(STR_CW_SOLVED_IN), time);
  v.bannerTitle = t.bannerTitle;
  v.bannerButton = tr(STR_CW_NEXT_PUZZLE);
  return v;
}

void render(const Puzzle& p, const draw::View& view, const Theme theme, const std::string& name) {
  GfxRenderer& r = renderer();
  r.clearScreen();
  stubHeader(r, theme, p.title);
  draw::drawScreen(r, p, computeLayout(p.w, p.h), view);
  ASSERT_TRUE(sleepcards::preview::writeFramePng(outDir() + "/" + name + ".png"));
}

// The ink box of whatever is drawn inside a rect (inclusive bounds).
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

// The drawable inside of a square: within its 1 px lines (the border is outside the squares).
struct Inner {
  int left, top, right, bottom;
};
Inner innerOf(const ScreenLayout& l, const int row, const int col) {
  return Inner{l.cellX(col) + INNER_LINE, l.cellY(row) + INNER_LINE, l.cellX(col) + l.cell - 1,
               l.cellY(row) + l.cell - 1};
}

// A size x size grid with every square white (a synthetic numbering: every square of the top
// row and the left column starts an entry, numbers up to 2 * size - 1).
std::unique_ptr<Puzzle> openGrid(const int w, const int h, const char letter) {
  auto p = std::make_unique<Puzzle>();
  p->w = static_cast<uint8_t>(w);
  p->h = static_cast<uint8_t>(h);
  std::memset(p->solution, letter, static_cast<size_t>(w * h));
  EXPECT_EQ(numberGrid(*p), Error::None);
  std::snprintf(p->title, sizeof(p->title), "%dx%d", w, h);
  return p;
}

}  // namespace

// The cursor's checkerboard at (x, y) (CrosswordDraw's fillChecker).
static bool checkerInk(const int x, const int y) {
  return ((x / cw::draw::CURSOR_CHECK + y / cw::draw::CURSOR_CHECK) & 1) != 0;
}

TEST(CrosswordPreview, FreshMini) {
  auto p = sample(cw_samples::SHELTER);
  Progress prog;
  resetProgress(*p, prog);
  Texts t;
  render(*p, viewFor(*p, prog, t), Theme::Classic, "cw_5x5_fresh");
  const ScreenLayout l = computeLayout(5, 5);
  EXPECT_EQ(l.cell, 72);
  // The cursor square is a checkerboard (grey on the panel, never a block): its empty middle
  // follows the checker exactly.
  const Inner in = innerOf(l, p->rowOf(prog.cursor), p->colOf(prog.cursor));
  for (int y = (in.top + in.bottom) / 2 - 4; y <= (in.top + in.bottom) / 2 + 4; y++) {
    for (int x = (in.left + in.right) / 2 - 4; x <= (in.left + in.right) / 2 + 4; x++) {
      EXPECT_EQ(renderer().readPixel(x, y), checkerInk(x, y)) << x << "," << y;
    }
  }
  // ... inside a white frame (here on its side within the word: the outline covers the others).
  EXPECT_FALSE(renderer().readPixel(in.right, (in.top + in.bottom) / 2));
  // The frame is white on all four sides, even against the outline and the block beside it.
  const int midX = (in.left + in.right) / 2;
  const int midY = (in.top + in.bottom) / 2;
  EXPECT_FALSE(renderer().readPixel(in.left, midY));
  EXPECT_FALSE(renderer().readPixel(midX, in.top));
  EXPECT_FALSE(renderer().readPixel(midX, in.bottom));
  EXPECT_TRUE(renderer().readPixel(in.left - 1, midY));  // the block (and outline) beside it
  EXPECT_EQ(renderer().readPixel(in.left + 1, midY), checkerInk(in.left + 1, midY));  // the cursor's own checker
}

// The cursor on a revealed letter: the letter black on its white halo over the checker, the mark
// on a white backing.
TEST(CrosswordPreview, CursorOnLetter) {
  auto p = sample(cw_samples::SHELTER);
  Progress prog;
  resetProgress(*p, prog);
  const int at = p->index(2, 1);  // RAVEN's A
  prog.fill[at] = 'A';
  prog.flags[at] = FLAG_REVEALED;
  prog.fill[p->index(2, 0)] = 'R';
  ASSERT_TRUE(setCursor(*p, prog, at, ACROSS).changed || prog.cursor == at);
  Texts t;
  render(*p, viewFor(*p, prog, t), Theme::Classic, "cw_cursor_letter");
  const ScreenLayout l = computeLayout(5, 5);
  const Inner in = innerOf(l, 2, 1);
  // The letter's ink is there (the PNG is the check that it reads on the checker).
  const int cx = (in.left + in.right + 1) / 2;
  EXPECT_TRUE(inkIn(cx - 3, in.top + 20, cx + 3, in.bottom - 6).any());
  // The mark's corner pixel is black and the backing row just above-left of it white.
  EXPECT_TRUE(renderer().readPixel(in.right - 1, in.bottom - 1));
  EXPECT_FALSE(renderer().readPixel(in.right - cw::draw::REVEALED_TRIANGLE - 1, in.bottom));
  // Away from the letter, the number and the mark the square is still the checker.
  const int y = in.top + (in.bottom - in.top) / 2;
  EXPECT_EQ(renderer().readPixel(in.right - 3, y), checkerInk(in.right - 3, y));
}

TEST(CrosswordPreview, MidSolveMarks) {
  auto p = sample(cw_samples::SHELTER);
  // Shelter: ##HUT / #CASE / RAVEN / AGED# / PEN##
  p->setCircled(p->index(2, 4));
  Progress prog;
  resetProgress(*p, prog);
  const char* rows[5] = {"  H  ", " CX  ", "RA   ", "AGE  ", "P    "};
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 5; c++) {
      const int i = p->index(r, c);
      if (!p->isBlock(i) && rows[r][c] != ' ') prog.fill[i] = rows[r][c];
    }
  }
  prog.flags[p->index(1, 2)] = FLAG_WRONG;  // X where S belongs, marked by a check
  prog.fill[p->index(0, 3)] = 'U';
  prog.flags[p->index(0, 3)] = FLAG_REVEALED;
  ASSERT_TRUE(setCursor(*p, prog, p->index(2, 2), ACROSS).changed || prog.cursor == p->index(2, 2));
  Texts t;
  render(*p, viewFor(*p, prog, t), Theme::Classic, "cw_5x5_midsolve");

  const ScreenLayout l = computeLayout(5, 5);
  // Revealed: the bottom-right corner of (0,3) is ink.
  const Inner rev = innerOf(l, 0, 3);
  EXPECT_TRUE(renderer().readPixel(rev.right - 1, rev.bottom - 1));
  EXPECT_FALSE(renderer().readPixel(rev.right - 10, rev.bottom - 10));
  // Wrong: the slash crosses the top-right area of (1,2), clear of the letter.
  const Inner wrong = innerOf(l, 1, 2);
  EXPECT_TRUE(inkIn(wrong.right - 8, wrong.top + 2, wrong.right - 2, wrong.top + 8).any());
  // Circled: the ring touches the middle of the square's left side.
  const Inner circ = innerOf(l, 2, 4);
  EXPECT_TRUE(
      inkIn(circ.left, (circ.top + circ.bottom) / 2 - 1, circ.left + 3, (circ.top + circ.bottom) / 2 + 1).any());
  // The word outline: 3 px over the top edge of RAVEN's squares (row 2), outside the cursor.
  const int edgeY = l.cellY(2);
  for (int y = edgeY - 1; y <= edgeY + 1; y++) EXPECT_TRUE(renderer().readPixel(l.cellX(0) + 30, y)) << y;
  EXPECT_FALSE(renderer().readPixel(l.cellX(0) + 30, edgeY + 3));
  // Along the outline's bottom edge the letters keep a white row: no halo notches in the outline.
  const int bottomY = l.cellY(3);
  for (int col = 0; col < 2; col++) {
    const Inner sq = innerOf(l, 2, col);
    for (int x = sq.left + 1; x <= sq.right; x++) {  // past the grid border
      EXPECT_TRUE(renderer().readPixel(x, bottomY - 1)) << col << " x " << x;
      EXPECT_FALSE(renderer().readPixel(x, bottomY - 2)) << col << " x " << x;
    }
  }
}

// Marks on the cursor: a circled square and a wrong letter keep their ring and slash readable
// on the checker, each standing on white.
TEST(CrosswordPreview, MarksOnTheCursor) {
  auto p = sample(cw_samples::SHELTER);
  // Shelter: ##HUT / #CASE / RAVEN / AGED# / PEN##
  const int circled = p->index(2, 4);  // RAVEN's N
  p->setCircled(circled);
  Progress prog;
  resetProgress(*p, prog);
  ASSERT_TRUE(setCursor(*p, prog, circled, ACROSS).changed || prog.cursor == circled);
  Texts t;
  render(*p, viewFor(*p, prog, t), Theme::Classic, "cw_cursor_circled");
  const ScreenLayout l = computeLayout(5, 5);
  const Inner in = innerOf(l, 2, 4);
  // In doubled coordinates as drawRing draws it: every pixel of the ring is black and every pixel of the rings one
  // inside and one outside it white (on the bare checker about half of them would be black).
  const int rad = (in.right - in.left + 1) / 2 - 1;
  const int cx2 = in.left + in.right + 1;
  const int cy2 = in.top + in.bottom + 1;
  auto onRing = [&](const int x, const int y, const int radius) {
    const int dx = 2 * x + 1 - cx2;
    const int dy = 2 * y + 1 - cy2;
    const int d = dx * dx + dy * dy;
    return d < (2 * radius + 1) * (2 * radius + 1) && d >= (2 * radius - 1) * (2 * radius - 1);
  };
  int ring = 0, halo = 0, wrongRing = 0, wrongHalo = 0;
  for (int y = in.top; y <= in.bottom; y++) {
    for (int x = in.left; x <= in.right; x++) {
      if (onRing(x, y, rad)) {
        ring++;
        if (!renderer().readPixel(x, y)) wrongRing++;
      } else if (onRing(x, y, rad - 1) || onRing(x, y, rad + 1)) {
        halo++;
        if (renderer().readPixel(x, y)) wrongHalo++;
      }
    }
  }
  EXPECT_GT(ring, 100);
  EXPECT_GT(halo, 100);
  EXPECT_EQ(wrongRing, 0);
  EXPECT_EQ(wrongHalo, 0);
  // Inside the halo the square is the checker again.
  const int midY = (in.top + in.bottom) / 2;
  EXPECT_EQ(renderer().readPixel(in.left + 8, midY), checkerInk(in.left + 8, midY));

  // A wrong letter on the cursor: the slash's black core is flanked by white rows.
  const int wrongAt = p->index(1, 2);  // CASE's A
  prog.fill[wrongAt] = 'X';
  prog.flags[wrongAt] = FLAG_WRONG;
  ASSERT_TRUE(setCursor(*p, prog, wrongAt, ACROSS).changed || prog.cursor == wrongAt);
  render(*p, viewFor(*p, prog, t), Theme::Classic, "cw_cursor_wrong");
  const Inner w = innerOf(l, 1, 2);
  // The slash runs from (right - 3, top + 2) down-left; one column in from its start, scan down for its core:
  // the first run of two or more black pixels (the checker never stacks two).
  const int x = w.right - 6;
  int firstBlack = -1;
  for (int y = w.top + 1; y < w.top + 16 && firstBlack < 0; y++) {
    if (renderer().readPixel(x, y) && renderer().readPixel(x, y + 1)) firstBlack = y;
  }
  ASSERT_GE(firstBlack, 0) << "no slash core";
  int lastBlack = firstBlack;
  while (renderer().readPixel(x, lastBlack + 1)) lastBlack++;
  EXPECT_GE(lastBlack - firstBlack + 1, cw::draw::WRONG_LINE);
  // Two white rows each side (the bare checker would give a black one in any two).
  for (int k = 1; k <= 2; k++) {
    EXPECT_FALSE(renderer().readPixel(x, firstBlack - k)) << k;
    EXPECT_FALSE(renderer().readPixel(x, lastBlack + k)) << k;
  }
}

TEST(CrosswordPreview, SevenBySevenLyra) {
  auto p = sample(cw_samples::OASIS);
  Progress prog;
  resetProgress(*p, prog);
  typeLetter(*p, prog, 'Y', true);
  typeLetter(*p, prog, 'E', true);
  Texts t;
  render(*p, viewFor(*p, prog, t), Theme::Lyra, "cw_7x7_lyra");
  EXPECT_EQ(computeLayout(7, 7).cell, 62);
}

TEST(CrosswordPreview, NineByNine) {
  auto p = fixtureIpuz("nine.ipuz");
  Progress prog;
  resetProgress(*p, prog);
  for (int i = 0; i < 12; i++) typeLetter(*p, prog, p->solution[prog.cursor], true);
  Texts t;
  render(*p, viewFor(*p, prog, t), Theme::Classic, "cw_9x9");
  EXPECT_FALSE(computeLayout(9, 9).style.tinyDigits);
}

TEST(CrosswordPreview, FifteenTinyDigits) {
  auto p = fixtureIpuz("fifteen.ipuz");
  Progress prog;
  resetProgress(*p, prog);
  // Every other square filled, a few marked.
  for (int i = 0; i < p->cells(); i++) {
    if (!p->isBlock(i) && i % 2 == 0) prog.fill[i] = p->solution[i];
  }
  for (int i = 0; i < p->cells(); i += 17) {
    if (!p->isBlock(i) && prog.fill[i] != EMPTY) prog.flags[i] = (i / 17) % 2 ? FLAG_WRONG : FLAG_REVEALED;
  }
  // The cursor in the middle, going down.
  int middle = p->index(7, 7);
  while (p->isBlock(middle)) middle++;
  setCursor(*p, prog, middle, DOWN);
  ASSERT_EQ(prog.cursor, middle);
  Texts t;
  render(*p, viewFor(*p, prog, t), Theme::Classic, "cw_15x15");
  const ScreenLayout l = computeLayout(15, 15);
  EXPECT_EQ(l.cell, 29);
  EXPECT_TRUE(l.style.tinyDigits);
}

TEST(CrosswordPreview, LongClueEllipsis) {
  auto p = fixtureIpuz("long_clue.ipuz");
  Progress prog;
  resetProgress(*p, prog);
  // The long clue is 8 Across.
  const int e = findEntry(*p, ACROSS, 8);
  ASSERT_GE(e, 0);
  gotoEntry(*p, prog, e);
  Texts t;
  const draw::View v = viewFor(*p, prog, t);
  const draw::ClueFit fit = draw::fitClue(renderer(), v.clueLabel, v.clueText, computeLayout(5, 5).clueText.w);
  EXPECT_EQ(fit.fontId, UI_10_FONT_ID);
  EXPECT_EQ(fit.lines, 3);
  EXPECT_TRUE(fit.ellipsis);
  render(*p, v, Theme::Classic, "cw_long_clue");
}

TEST(CrosswordPreview, ClueFitSteps) {
  const int width = computeLayout(5, 5).clueText.w;
  const draw::ClueFit shortFit = draw::fitClue(renderer(), "1A", "Simple shelter in the woods", width);
  EXPECT_EQ(shortFit.fontId, UI_12_FONT_ID);
  EXPECT_EQ(shortFit.lines, 1);
  EXPECT_FALSE(shortFit.ellipsis);
  const draw::ClueFit two = draw::fitClue(renderer(), "12D", "Found at the back of a drawer in the winter", width);
  EXPECT_EQ(two.fontId, UI_12_FONT_ID);
  EXPECT_EQ(two.lines, 2);
  const draw::ClueFit three = draw::fitClue(
      renderer(), "12D", "Found at the back of a kitchen drawer after a long winter, with some odd socks", width);
  EXPECT_EQ(three.fontId, UI_10_FONT_ID);
  EXPECT_EQ(three.lines, 3);
  EXPECT_FALSE(three.ellipsis);
}

TEST(CrosswordPreview, FullButWrongBar) {
  auto p = sample(cw_samples::KEEN);
  Progress prog;
  resetProgress(*p, prog);
  for (int i = 0; i < p->cells(); i++) {
    if (!p->isBlock(i)) prog.fill[i] = p->solution[i];
  }
  prog.fill[p->index(2, 2)] = 'Z';
  prog.fill[p->index(3, 4)] = 'Q';
  Texts t;
  draw::View v = viewFor(*p, prog, t);
  std::snprintf(t.message, sizeof(t.message), tr(STR_CW_NOT_QUITE), 2);
  v.barMessage = t.message;
  render(*p, v, Theme::Classic, "cw_full_wrong");
}

TEST(CrosswordPreview, SolvedBanner) {
  auto p = sample(cw_samples::KEEN);
  Progress prog;
  resetProgress(*p, prog);
  for (int i = 0; i < p->cells(); i++) {
    if (!p->isBlock(i)) prog.fill[i] = p->solution[i];
  }
  prog.flags[p->index(1, 1)] = FLAG_REVEALED;
  prog.solved = true;
  prog.elapsed = 252;
  prog.checks = 2;
  prog.reveals = 1;
  Texts t;
  draw::View v = viewFor(*p, prog, t);
  std::snprintf(t.bannerDetail, sizeof(t.bannerDetail), "%s - %s", "2 checks", tr(STR_CW_ONE_REVEAL));
  v.bannerDetail = t.bannerDetail;
  render(*p, v, Theme::Lyra, "cw_solved");
  EXPECT_STREQ(t.bannerTitle, "Solved in 4:12");

  v.bannerButton = "";
  v.bannerNote = tr(STR_CW_ALL_SOLVED);
  render(*p, v, Theme::Classic, "cw_solved_all");
}

// The shipped set: every clue fits the clue bar whole (two lines of UI 12, or three of UI 10),
// never cut with an ellipsis.
TEST(CrosswordPreview, EveryBuiltinClueFitsTheBar) {
  int checked = 0;
  int small = 0;
  for (size_t i = 0; i < builtinCount(); i++) {
    const BuiltinPuzzle& b = builtinPuzzle(i);
    auto p = builtin(b.id);
    const int width = computeLayout(p->w, p->h).clueText.w;
    for (int e = 0; e < p->entryCount; e++) {
      char label[8];
      formatClueLabel(*p, e, label, sizeof(label));
      const draw::ClueFit fit = draw::fitClue(renderer(), label, p->clue(e), width);
      EXPECT_FALSE(fit.ellipsis) << b.id << " " << label << ": " << p->clue(e);
      if (fit.fontId == UI_12_FONT_ID) {
        EXPECT_LE(fit.lines, 2) << b.id << " " << label;
      } else {
        EXPECT_EQ(fit.fontId, UI_10_FONT_ID) << b.id << " " << label;
        EXPECT_LE(fit.lines, 3) << b.id << " " << label;
        small++;
      }
      checked++;
    }
  }
  EXPECT_EQ(checked, 678);
  std::printf("[ built-in clues: %d checked, %d drop to UI 10 ]\n", checked, small);
}

// The real set on screen: a 7x7 and a 9x9 mid-solve with the checkered cursor, and the first mini.
TEST(CrosswordPreview, RealBuiltins) {
  struct Shot {
    const char* id;
    const char* name;
    int typed;
    Theme theme;
  };
  const Shot shots[] = {{"mini-001", "cw_real_mini_001", 3, Theme::Classic},
                        {"midi-001", "cw_real_midi_001", 6, Theme::Lyra},
                        {"maxi-006", "cw_real_maxi_006", 14, Theme::Classic}};
  for (const Shot& shot : shots) {
    auto p = builtin(shot.id);
    ASSERT_GT(p->w, 0) << shot.id;
    Progress prog;
    resetProgress(*p, prog);
    for (int i = 0; i < shot.typed; i++) typeLetter(*p, prog, p->solution[prog.cursor], true);
    // One revealed square and one wrong one, away from the cursor.
    for (int i = p->cells() - 1; i >= 0; i--) {
      if (p->isBlock(i) || i == prog.cursor) continue;
      if (prog.fill[i] == EMPTY) {
        prog.fill[i] = p->solution[i];
        prog.flags[i] = FLAG_REVEALED;
        break;
      }
    }
    for (int i = p->cells() / 2; i < p->cells(); i++) {
      if (p->isBlock(i) || i == prog.cursor || prog.fill[i] != EMPTY) continue;
      prog.fill[i] = p->solution[i] == 'E' ? 'A' : 'E';
      prog.flags[i] = FLAG_WRONG;
      break;
    }
    Texts t;
    render(*p, viewFor(*p, prog, t), shot.theme, shot.name);
    // The cursor square is the checkerboard (sampled in its middle, clear of letter and number).
    const ScreenLayout l = computeLayout(p->w, p->h);
    const Inner in = innerOf(l, p->rowOf(prog.cursor), p->colOf(prog.cursor));
    if (prog.fill[prog.cursor] == EMPTY) {
      const int x = (in.left + in.right) / 2;
      const int y = (in.top + in.bottom) / 2;
      EXPECT_EQ(renderer().readPixel(x, y), checkerInk(x, y)) << shot.id;
      EXPECT_EQ(renderer().readPixel(x + 1, y), checkerInk(x + 1, y)) << shot.id;
    }
  }
}

// Letters and numbers stay inside their squares, apart, at every size 3x3 .. 15x15 (and
// lopsided ones), with the widest letter and the ones that reach below the baseline.
TEST(CrosswordPreview, LettersAndNumbersFitEverySize) {
  GfxRenderer& r = renderer();
  const int sizes[][2] = {{3, 3},   {4, 4},   {5, 5},   {6, 6},   {7, 7},   {8, 8},  {9, 9},  {10, 10},
                          {11, 11}, {12, 12}, {13, 13}, {14, 14}, {15, 15}, {15, 5}, {5, 15}, {15, 9}};
  for (const auto& s : sizes) {
    for (const char letter : {'W', 'Q', 'J', 'M', 'I'}) {
      auto p = openGrid(s[0], s[1], letter);
      const ScreenLayout l = computeLayout(p->w, p->h);
      Progress prog;
      resetProgress(*p, prog);
      for (int i = 0; i < p->cells(); i++) prog.fill[i] = letter;
      prog.solved = true;  // no cursor, no outline: only lines, numbers, letters
      draw::View v;
      v.prog = &prog;

      // Numbers alone.
      Progress empty = prog;
      std::memset(empty.fill, EMPTY, sizeof(empty.fill));
      v.prog = &empty;
      r.clearScreen();
      draw::drawGrid(r, *p, l, v);
      std::vector<Ink> numbers(static_cast<size_t>(p->cells()));
      for (int i = 0; i < p->cells(); i++) {
        const Inner in = innerOf(l, p->rowOf(i), p->colOf(i));
        numbers[i] = inkIn(in.left, in.top, in.right, in.bottom);
        EXPECT_EQ(numbers[i].any(), p->number[i] > 0) << s[0] << "x" << s[1] << " cell " << i;
        if (numbers[i].any()) {
          EXPECT_GT(numbers[i].x0, in.left) << s[0] << "x" << s[1];
          EXPECT_GT(numbers[i].y0, in.top) << s[0] << "x" << s[1];
        }
      }

      // Letters alone (numbers cleared).
      uint8_t saved[MAX_CELLS];
      std::memcpy(saved, p->number, sizeof(saved));
      std::memset(p->number, 0, sizeof(p->number));
      v.prog = &prog;
      r.clearScreen();
      draw::drawGrid(r, *p, l, v);
      std::vector<Ink> letters(static_cast<size_t>(p->cells()));
      for (int i = 0; i < p->cells(); i++) {
        const Inner in = innerOf(l, p->rowOf(i), p->colOf(i));
        const Ink ink = inkIn(in.left, in.top, in.right, in.bottom);
        letters[i] = ink;
        ASSERT_TRUE(ink.any()) << s[0] << "x" << s[1] << " " << letter;
        // A clear pixel all round (the line is next to it otherwise).
        EXPECT_GT(ink.x0, in.left) << s[0] << "x" << s[1] << " " << letter;
        EXPECT_LT(ink.x1, in.right) << s[0] << "x" << s[1] << " " << letter;
        EXPECT_LT(ink.y1, in.bottom) << s[0] << "x" << s[1] << " " << letter;
        // Centred on its ink (within a pixel).
        EXPECT_LE(std::abs((ink.x0 + ink.x1) - (in.left + in.right)), 2) << s[0] << "x" << s[1] << " " << letter;
      }
      std::memcpy(p->number, saved, sizeof(saved));

      // Both: a letter is clear of its number, except a tall one (Q, J) in a small square, where
      // the number stays whole on a white halo.
      std::vector<uint8_t> numbersOnly(static_cast<size_t>(480 * 800));
      r.clearScreen();
      v.prog = &empty;
      draw::drawGrid(r, *p, l, v);
      for (int y = 0; y < 800; y++) {
        for (int x = 0; x < 480; x++) numbersOnly[static_cast<size_t>(y * 480 + x)] = r.readPixel(x, y) ? 1 : 0;
      }
      v.prog = &prog;
      r.clearScreen();
      draw::drawGrid(r, *p, l, v);
      for (int i = 0; i < p->cells(); i++) {
        const Ink& n = numbers[i];
        const Ink& ink = letters[i];
        if (!n.any() || ink.y0 > n.y1 || ink.x0 > n.x1) continue;
        EXPECT_TRUE(letter == 'Q' || letter == 'J')
            << s[0] << "x" << s[1] << " " << letter << " cell " << i << " letter " << ink.x0 << "," << ink.y0
            << " number to " << n.x1 << "," << n.y1;
        for (int y = n.y0 - 1; y <= n.y1 + 1; y++) {
          for (int x = n.x0 - 1; x <= n.x1 + 1; x++) {
            bool nearInk = false;
            for (int oy = -1; oy <= 1 && !nearInk; oy++) {
              for (int ox = -1; ox <= 1 && !nearInk; ox++) {
                nearInk = numbersOnly[static_cast<size_t>((y + oy) * 480 + x + ox)] != 0;
              }
            }
            if (!nearInk) continue;
            const bool expected = numbersOnly[static_cast<size_t>(y * 480 + x)] != 0;
            ASSERT_EQ(r.readPixel(x, y), expected) << s[0] << "x" << s[1] << " " << letter << " at " << x << "," << y;
          }
        }
      }
    }
  }
}

// Every key's label sits inside its key, centred.
TEST(CrosswordPreview, KeyLabelsCentred) {
  GfxRenderer& r = renderer();
  r.clearScreen();
  draw::View v;
  v.menuLabel = tr(STR_CW_MENU);
  v.delLabel = tr(STR_CW_DEL);
  draw::drawKeyboard(r, v);
  for (int i = 0; i < KEY_COUNT; i++) {
    const Key& k = keyboardKey(i);
    // Inside the 2 px outline and its rounded corners.
    const Ink ink = inkIn(k.rect.x + 4, k.rect.y + 4, k.rect.right() - 5, k.rect.bottom() - 5);
    ASSERT_TRUE(ink.any()) << i;
    const int cx2 = 2 * k.rect.x + k.rect.w - 1;
    const int cy2 = 2 * k.rect.y + k.rect.h - 1;
    EXPECT_LE(std::abs(ink.x0 + ink.x1 - cx2), 4) << "key " << i;
    EXPECT_LE(std::abs(ink.y0 + ink.y1 - cy2), 6) << "key " << i;
  }
}
