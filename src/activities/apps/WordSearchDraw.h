#pragma once

// Word Search drawing: everything under the header, as free functions over GfxRenderer, the
// built-in fonts (fontIds.h) and sleepcards::draw, so the host preview test
// (test/word_search_preview) renders the same screen to PNG. UI text comes in through the
// view (the activity passes tr() strings); the words and the theme title are content.
//
// Drawing order on the grid: the drag preview (light dither, hairline edge), found-word
// capsules (3 px outlines), the anchor square, the letters (those near a capsule on a 1 px white
// halo, so a crossing capsule stops short of the letter instead of running through it), the
// dashed hint ring, the cursor box.

#include <WordSearch.h>

class GfxRenderer;

namespace ws::draw {

struct BoardView {
  Cell anchor;              // the tapped first cell / drag start: black cell, white letter
  Cell dragStart;           // a drag in progress: preview capsule dragStart -> dragEnd
  Cell dragEnd;             //   (none when either is invalid or they are equal)
  Cell cursor;              // the key cursor's cell
  bool showCursor = false;  // only after a key was used; hidden again after a touch
  const char* status = "";  // bottom bar, left ("5 of 12 found", "No match", ...)
  const char* menuLabel = "";
  // Completion banner (drawn over the word list when the puzzle is complete).
  const char* bannerTitle = "";   // "All 12 found in 6:32"
  const char* bannerDetail = "";  // "1 hint", or empty
  const char* newPuzzleLabel = "";
};

constexpr int CAPSULE_LINE = 3;  // found-word outline, px
constexpr int CURSOR_LINE = 3;

// The letter font for a cell size (NotoSans bold).
int letterFontFor(int cell);
// The cursor box's line for a cell size: CURSOR_LINE, one less on Hard's small cells. The box
// is the cell grown by 1 px on every side, its line drawn inward.
int cursorLineFor(int cell);
// The list font's id for a ListFont.
int listFontId(ListFont font);
// The word list's columns and font for this puzzle, measured with the real fonts.
ListLayout layoutWordList(const GfxRenderer& r, const Puzzle& p, const BoardLayout& layout);
// The same for any texts (the host test checks worst cases with it).
ListLayout layoutTexts(const GfxRenderer& r, const char* const* texts, int count, const BoardLayout& layout);

// A capsule (stadium) around the segment (x0, y0) -> (x1, y1): an outline `thickness` px wide
// whose outer edge is `radius` from the segment, or (thickness 0) filled with a light dither.
void drawCapsule(GfxRenderer& r, int x0, int y0, int x1, int y1, int radius, int thickness);

void drawGrid(GfxRenderer& r, const Puzzle& p, const BoardLayout& layout, const BoardView& view);
void drawWordList(GfxRenderer& r, const Puzzle& p, const BoardLayout& layout);
void drawBottomBar(GfxRenderer& r, const BoardLayout& layout, const BoardView& view);
void drawBanner(GfxRenderer& r, const BoardLayout& layout, const BoardView& view);

// All of the above: the grid, then the banner when complete or else the word list, then the
// bar. The caller clears the screen and draws the header first.
void drawBoard(GfxRenderer& r, const Puzzle& p, const BoardLayout& layout, const BoardView& view);

}  // namespace ws::draw
