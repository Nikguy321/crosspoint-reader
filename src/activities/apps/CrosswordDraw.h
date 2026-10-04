#pragma once

// Crossword drawing: everything under the header, as free functions over GfxRenderer and the
// built-in fonts (fontIds.h), so the host preview test (test/crossword_preview) renders the same
// screen to PNG. UI text comes in through the view (the activity passes tr() strings); titles
// and clues are content. Pure 1-bit: no grey levels (the cursor's checker is the one dithered
// element), so Night mode's global inversion is all.
//
// The grid: blocks solid black, 1 px lines between squares, a 2 px border. The cursor square is
// a 1 px checkerboard (CURSOR_CHECK; grey on the panel, so it never reads as a block) inside a
// 1 px white frame; its letter, number and marks stay black on a white halo (the ring on a
// white ring each side, the slash on a wider white band, the revealed triangle on a white
// triangle two px larger). The current word has a 3 px outline straddling its edge. Clue numbers sit top-left
// (SMALL_FONT, or the 5x7 digits under 37 px); letters are NotoSans Bold by square size, bottom-aligned and centred on
// their own ink. Marks: a 1 px circle (circled squares), a 2 px slash top-right to bottom-left behind the letter
// (checked wrong), a filled 7 px triangle in the bottom-right corner (revealed).

#include <Crossword.h>

class GfxRenderer;

namespace cw::draw {

struct View {
  const Progress* prog = nullptr;  // required: the fill, flags, cursor and direction
  const char* clueLabel = "";      // the bar's bold label ("14A")
  const char* clueText = "";       // the current clue (content)
  const char* barMessage = "";     // when set, the bar shows this instead ("Not quite - ...")
  const char* menuLabel = "";
  const char* delLabel = "";
  // The completion banner (drawn over the bar and the keyboard while prog->solved).
  const char* bannerTitle = "";   // "Solved in 4:12"
  const char* bannerDetail = "";  // "2 checks - 1 reveal", or empty
  const char* bannerButton = "";  // "Next puzzle"; empty = no button
  const char* bannerNote = "";    // shown where the button would be ("All solved here")
};

constexpr int CIRCLE_LINE = 1;
constexpr int WRONG_LINE = 2;
constexpr int REVEALED_TRIANGLE = 7;
constexpr int CURSOR_CHECK = 1;  // the cursor's checker squares (px)
constexpr int CLUE_LINES_MAX = 3;

// The letter font for a square size (NotoSans Bold 12/14/16/18).
int letterFontFor(const CellStyle& style);

// How the clue bar sets a clue: 2 lines of UI_12 when it fits, else 3 of UI_10, else 3 of
// UI_10 ending in "...". The label ("14A", bold) leads the first line.
struct ClueFit {
  int fontId = 0;
  int lines = 0;  // lines used
  bool ellipsis = false;
};
ClueFit fitClue(const GfxRenderer& r, const char* label, const char* text, int width);

void drawGrid(GfxRenderer& r, const Puzzle& p, const ScreenLayout& layout, const View& view);
void drawClueBar(GfxRenderer& r, const ScreenLayout& layout, const View& view);
void drawKeyboard(GfxRenderer& r, const View& view);
void drawBanner(GfxRenderer& r, const ScreenLayout& layout, const View& view);

// All of the above: the grid, then the banner when solved or else the clue bar and the
// keyboard. The caller clears the screen and draws the header first.
void drawScreen(GfxRenderer& r, const Puzzle& p, const ScreenLayout& layout, const View& view);

}  // namespace cw::draw
