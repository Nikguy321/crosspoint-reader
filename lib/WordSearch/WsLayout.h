#pragma once

// Word Search screen geometry (portrait 480x800, logical pixels): the grid, the word list
// under it, the bottom bar with its Menu button and the completion banner. Pure: the word
// list's column/font choice takes a text-measuring callback, so the host tests can check it
// with fake widths and the drawing code (src/activities/apps/WordSearchDraw) with real fonts.
//
//   y 0..~94     the theme's header (GUI.drawHeader, drawn by the activity)
//   y 116..      the grid: never inside the top 112 px, where a downward swipe opens the
//                light panel; x within the 7 px bezel on each side
//   under it     the word list (3 or 2 columns), or the completion banner over it
//   y 750..796   the bottom bar: status text on the left, Menu button on the right

#include <cstdint>

#include "WsModel.h"

namespace ws {

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
constexpr int GRID_TOP_MIN = 116;  // > the light panel's 112 px top-edge band
constexpr int SIDE_INSET = 7;      // X4 Pro bezel, left and right
// Hard's 29 px (not 30) leaves room for 8 rows of the small list font when its long words need
// two columns.
constexpr int CELL_PX[DIFFICULTY_COUNT] = {44, 38, 29};
constexpr int LIST_GAP = 12;  // grid -> list, list -> bar
constexpr int LIST_SIDE = 12;
constexpr int LIST_COL_GAP = 18;  // the least space between two columns (strike bars stay apart)
constexpr int BAR_TOP = 750;
constexpr int BAR_BOTTOM = 796;
constexpr int BUTTON_W = 104;  // Menu (touch target >= 96x44)
constexpr int BUTTON_H = 44;
constexpr int NEW_PUZZLE_W = 200;

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  bool contains(const int px, const int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  int right() const { return x + w; }
  int bottom() const { return y + h; }
};

struct BoardLayout {
  int size = 0;  // cells a side
  int cell = 0;  // px a side
  Rect grid;
  Rect list;    // the word list area (and the banner's)
  Rect bar;     // the bottom bar
  Rect status;  // its text, left of the Menu button
  Rect menuButton;
  Rect newPuzzleButton;  // inside the banner, live only when the puzzle is complete

  int cellX(const int col) const { return grid.x + col * cell; }
  int cellY(const int row) const { return grid.y + row * cell; }
  int centerX(const int col) const { return grid.x + col * cell + cell / 2; }
  int centerY(const int row) const { return grid.y + row * cell + cell / 2; }
  // The cell under (x, y); false off the grid.
  bool cellAt(int x, int y, Cell& out) const;
  // cellAt, or the edge cell for a point up to half a cell left or right of the grid (the bare
  // margin out to the bezel): a contact aimed at the first or last column still starts on it.
  bool cellNear(int x, int y, Cell& out) const;
};

// contentTop: where the theme's content area starts under its header (the grid goes no
// higher than GRID_TOP_MIN either way).
BoardLayout computeLayout(Difficulty difficulty, int contentTop = 0);

enum class ButtonHit : uint8_t { None, Menu, NewPuzzle };
// A contact that began off the grid: which button holds both its start and its end?
ButtonHit hitButton(const BoardLayout& layout, bool complete, int x0, int y0, int x1, int y1);

// ---- the word list ------------------------------------------------------------------------------

enum class ListFont : uint8_t { Large = 0, Small = 1 };  // the device's UI_12 / UI_10
constexpr int LIST_FONT_COUNT = 2;

struct ListLayout {
  ListFont font = ListFont::Large;
  uint8_t columns = 3;
  uint8_t rows = 0;
  int colX[3] = {};  // each column's left edge
  int colW[3] = {};  // the room each column's text has (the drawing truncates beyond it)
  int rowPitch = 0;
  int top = 0;        // the first row's top (the block is centred in the area)
  bool fits = false;  // false: even 2 columns of the small font overflow (the drawing truncates)
};

// Width of text in px for a list font (drawing: the renderer; tests: a fake).
using MeasureText = int (*)(void* ctx, ListFont font, const char* text);

// Tries, in order, 3 then 2 columns of the large font, then 3 then 2 of the small one; the
// first where the columns (each as wide as its widest text, LIST_COL_GAP apart) fit the width
// and the rows fit the height wins. The columns are spread evenly and the block centred.
// pitch[font] = the row step for that font.
ListLayout layoutWordList(const char* const* texts, int count, const Rect& area, const int pitch[LIST_FONT_COUNT],
                          MeasureText measure, void* ctx);

// Top-left of item i (column-major: down the first column, then the next).
void listItemOrigin(const ListLayout& list, int index, int& x, int& y);

}  // namespace ws
