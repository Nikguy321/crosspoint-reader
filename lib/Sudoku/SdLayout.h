#pragma once

// Sudoku screen geometry (portrait 480x800, logical pixels, fixed: the grid is always 9x9) and
// the touch targets. Pure, so the host tests check that every target is reachable and that none
// lies in the bezel; the drawing (src/activities/apps/SudokuDraw) and the hit-testing both read it.
//
//   y 0..~94      the theme's header (GUI.drawHeader: title and back arrow)
//   y 116..574    the grid, x 10..468: a 3 px border and box lines, 1 px square lines, 49 px
//                 squares (3 + 9x49 + 6x1 + 2x3 + 3 = 459); never inside the top 112 px, where a
//                 downward swipe opens the light panel
//   y 578..608    the status line (no target)
//   y 614..701    digit keys 1-9, key k under column k (45 px wide, 2 px inside the square's span)
//   y 710..795    tool keys Notes / Erase / Undo / Menu at a 116.5 px pitch, drawn 112 px wide
// When solved, a banner covers y 578..795 with one button (the grid stays a target).
//
// Touch areas tile x 7..473 with no dead space between targets: a grid column takes its square
// and half of each line beside it, the edge columns reach the bezel, a digit key takes its
// column's span and y 609..705, a tool key its pitch cell and y 706..795. The bezel (x 0..6 and
// 474..479) is never a target, so the left-edge Back swipe keeps working.

#include <cstdint>

namespace sd {

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
constexpr int TARGET_LEFT = 7;     // x 0..6: the bezel
constexpr int TARGET_RIGHT = 474;  // x 474..479: the bezel (exclusive bound)

constexpr int GRID_X = 10;
constexpr int GRID_Y = 116;  // > the light panel's 112 px top-edge band
constexpr int BOX_LINE = 3;  // the border and the box lines
constexpr int CELL_LINE = 1;
constexpr int CELL_PX = 49;
constexpr int GRID_PX = 2 * BOX_LINE + 9 * CELL_PX + 6 * CELL_LINE + 2 * BOX_LINE;  // 459
constexpr int GRID_BOTTOM = GRID_Y + GRID_PX;                                       // 575, exclusive

constexpr int STATUS_TOP = 578;
constexpr int STATUS_BOTTOM = 609;  // exclusive
constexpr int DIGIT_TOP = 614;
constexpr int DIGIT_H = 88;  // y 614..701
constexpr int DIGIT_W = 45;
constexpr int DIGIT_HIT_TOP = 609;
constexpr int DIGIT_HIT_BOTTOM = 706;  // exclusive
constexpr int TOOL_TOP = 710;
constexpr int TOOL_H = 86;  // y 710..795
constexpr int TOOL_W = 112;
constexpr int TOOL_PITCH_TENTHS = 1165;  // 116.5 px
constexpr int TOOL_HIT_TOP = 706;
constexpr int TOOL_HIT_BOTTOM = 796;  // exclusive
constexpr int BANNER_TOP = 578;
constexpr int BANNER_BOTTOM = 796;  // exclusive
constexpr int BANNER_BUTTON_W = 240;
constexpr int BANNER_BUTTON_H = 56;

constexpr int CURSOR_FRAME = 5;   // the cursor: a frame this thick across a square line (2 px each side)
constexpr int SAME_RING = 2;      // the same-digit cue: a ring this thick ...
constexpr int SAME_RING_GAP = 2;  // ... this far inside the square
constexpr int CLASH_BAR = 2;      // the clash bar's height, under the digit

// Notes: a 3x3 of 7x11 digits (SdNotesFont); digit d's ink starts at (NOTE_X[(d-1)%3],
// NOTE_Y[(d-1)/3]) inside the square. A note matching the highlighted digit is drawn white in a
// 9x13 black box one pixel around its ink.
constexpr int NOTE_X[3] = {6, 21, 36};
constexpr int NOTE_Y[3] = {4, 19, 34};

enum class Tool : uint8_t { Notes = 0, Erase, Undo, Menu };
constexpr int TOOL_COUNT = 4;

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  constexpr bool contains(const int px, const int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  constexpr int right() const { return x + w; }
  constexpr int bottom() const { return y + h; }
};

// The left / top pixel of column / row k's square (inside the lines): 13, 63, 113, 165, ...
constexpr int cellLeft(const int col) {
  return GRID_X + BOX_LINE + col * (CELL_PX + CELL_LINE) + (col / 3) * (BOX_LINE - CELL_LINE);
}
constexpr int cellTop(const int row) {
  return GRID_Y + BOX_LINE + row * (CELL_PX + CELL_LINE) + (row / 3) * (BOX_LINE - CELL_LINE);
}
constexpr Rect gridRect() { return Rect{GRID_X, GRID_Y, GRID_PX, GRID_PX}; }
// A square, inside its lines (49x49).
constexpr Rect cellRect(const int cell) { return Rect{cellLeft(cell % 9), cellTop(cell / 9), CELL_PX, CELL_PX}; }
// The x span column k's taps cover (grid, digit row): [columnHitLeft, columnHitRight).
int columnHitLeft(int col);
int columnHitRight(int col);
int rowHitTop(int row);
int rowHitBottom(int row);

Rect digitKeyRect(int digit);  // drawn, 1..9
Rect digitKeyHit(int digit);
Rect toolKeyRect(Tool tool);
Rect toolKeyHit(Tool tool);
constexpr Rect statusRect() {
  return Rect{TARGET_LEFT, STATUS_TOP, TARGET_RIGHT - TARGET_LEFT, STATUS_BOTTOM - STATUS_TOP};
}
constexpr Rect bannerRect() {
  return Rect{TARGET_LEFT, BANNER_TOP, TARGET_RIGHT - TARGET_LEFT, BANNER_BOTTOM - BANNER_TOP};
}
constexpr Rect bannerButtonRect() {
  return Rect{(SCREEN_W - BANNER_BUTTON_W) / 2, BANNER_BOTTOM - 24 - BANNER_BUTTON_H, BANNER_BUTTON_W, BANNER_BUTTON_H};
}

// What a point on the screen is.
enum class TargetKind : uint8_t { None = 0, Cell, Digit, Tool, BannerButton };
struct Target {
  TargetKind kind = TargetKind::None;
  int8_t index = -1;  // the square (Cell, 0..80), the digit (Digit, 1..9) or the Tool
  bool operator==(const Target& o) const { return kind == o.kind && index == o.index; }
  bool operator!=(const Target& o) const { return !(*this == o); }
};
// banner: the completion banner is up (it hides the keys).
Target targetAt(bool banner, int x, int y);

}  // namespace sd
