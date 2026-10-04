#pragma once

// Crossword screen geometry (portrait 480x800, logical pixels) and the touch targets. Pure, so
// the host tests check every size from 3x3 to 15x15; the drawing (src/activities/apps/
// CrosswordDraw) and the activity's hit-testing both read it.
//
//   y 0..~94     the theme's header (GUI.drawHeader: title and back arrow)
//   y 116..552   the grid: cell = min(466 / w, 436 / h, 72), centred in x 7..473, y 116..552
//                (never inside the top 112 px, where a downward swipe opens the light panel)
//   y 556..628   the clue bar: "<" x 7..55, the clue text x 61..419, ">" x 425..473
//   y 632..796   the keyboard (CwKeyboard): 3 rows of 52 px, 4 px apart
// When solved, a banner covers the clue bar and the keyboard (y 556..796) with one button.

#include <cstdint>

#include "CwModel.h"

namespace cw {

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
constexpr int GRID_LEFT = 7;  // the X4 Pro bezel
constexpr int GRID_RIGHT = 473;
constexpr int GRID_TOP = 116;  // > the light panel's 112 px top-edge band
constexpr int GRID_BOTTOM = 552;
constexpr int MAX_CELL_PX = 72;
constexpr int BAR_TOP = 556;
constexpr int BAR_BOTTOM = 628;
constexpr int BAR_BUTTON_W = 48;
constexpr int BAR_TEXT_LEFT = 61;
constexpr int BAR_TEXT_RIGHT = 419;
constexpr int KEYBOARD_TOP = 632;
constexpr int KEY_ROW_H = 52;
constexpr int KEY_ROW_GAP = 4;
constexpr int KEYBOARD_BOTTOM = 796;
constexpr int BANNER_BUTTON_W = 240;
constexpr int BANNER_BUTTON_H = 56;
constexpr int OUTER_LINE = 2;        // the grid's border
constexpr int INNER_LINE = 1;        // between squares
constexpr int WORD_OUTLINE = 3;      // around the active word
constexpr int TINY_DIGIT_CELL = 37;  // below this, numbers use the 5x7 digits (CwDigits)

// Nominal font metrics for the fit checks (cap height / widest capital 'W' in px, from the
// built-in NotoSans Bold tables; SMALL_FONT digits are ~14 px tall). The preview tests draw the
// real glyphs.
constexpr int LETTER_POINTS[4] = {12, 14, 16, 18};
constexpr int LETTER_CAP_PX[4] = {18, 21, 24, 27};
constexpr int LETTER_W_PX[4] = {25, 29, 33, 37};
constexpr int SMALL_DIGIT_PX = 14;

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  constexpr bool contains(const int px, const int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  constexpr int right() const { return x + w; }
  constexpr int bottom() const { return y + h; }
};

// How a square of a given size is drawn.
struct CellStyle {
  int letterPoints = 12;   // NotoSans Bold 12/14/16/18
  int letterIndex = 0;     // into LETTER_POINTS / LETTER_CAP_PX
  bool tinyDigits = true;  // numbers in the 5x7 digits (else SMALL_FONT)
  int numberX = 2;         // the number's top-left, inside the square
  int numberY = 2;
  int letterBottom = 0;  // the letter's lowest ink row, from the square's top (1 px above the line)
};
CellStyle cellStyleFor(int cell);

struct ScreenLayout {
  int w = 0;     // squares across
  int h = 0;     // squares down
  int cell = 0;  // px a side
  Rect grid;     // the squares (the border is drawn over their outer pixels)
  Rect clueBar;
  Rect prevButton;
  Rect nextButton;
  Rect clueText;
  Rect keyboard;
  Rect banner;
  Rect bannerButton;
  CellStyle style;

  int cellX(const int col) const { return grid.x + col * cell; }
  int cellY(const int row) const { return grid.y + row * cell; }
  Rect cellRect(const int index) const { return w > 0 ? Rect{cellX(index % w), cellY(index / w), cell, cell} : Rect{}; }
  // The square under (x, y); false off the grid.
  bool cellAt(int x, int y, int& index) const;
  // cellAt, or the edge square for a point up to half a square left or right of the grid.
  bool cellNear(int x, int y, int& index) const;
};

ScreenLayout computeLayout(int w, int h);

// What a point on the screen is.
enum class TargetKind : uint8_t { None = 0, Cell, PrevClue, NextClue, ClueText, Key, BannerButton };
struct Target {
  TargetKind kind = TargetKind::None;
  int16_t index = -1;  // the square (Cell) or the key (Key)
  bool operator==(const Target& o) const { return kind == o.kind && index == o.index; }
  bool operator!=(const Target& o) const { return !(*this == o); }
};
// banner: the completion banner is up (it hides the clue bar and the keyboard).
Target targetAt(const ScreenLayout& layout, bool banner, int x, int y);
// A contact is a tap when it starts and ends on the same target; None otherwise.
Target tapTarget(const ScreenLayout& layout, bool banner, int x0, int y0, int x1, int y1);

}  // namespace cw
