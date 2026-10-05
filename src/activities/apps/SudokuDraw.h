#pragma once

// Sudoku drawing: everything under the header, as free functions over GfxRenderer and the built-in
// fonts (fontIds.h), so the host preview test (test/sudoku_preview) renders the same screen to PNG.
// Pure 1-bit (Night mode's global inversion is all). Geometry is SdLayout's.
//
// The grid: a 3 px border and box lines, 1 px square lines. Givens NotoSans 18 Bold, the player's
// digits (and revealed ones) NotoSans 18 Regular, centred on their own ink. Notes: the 7x11 digits
// (SdNotesFont) in a 3x3; a note matching the highlighted digit is white in a black box. The
// highlighted digit (the locked one, else the cursor square's) puts a 2 px ring inside every square
// that shows it. The cursor: a 5 px frame centred on its square's lines (2 px in, the line, 2 px
// out; 7 px across a box line), clear of the notes. Marks: a 2 px bar under an entry that
// clashes with a peer (live), a 2 px slash behind a digit a check or hint found wrong, a 7 px
// triangle in the bottom-right corner of a revealed square (all three clear of the cursor's frame).
// Under the grid: the status line, the digit keys 1-9 (each with how many are left to place; a
// digit placed nine times is dimmed by a 1 px checker; the locked key is inverted) and the tool
// keys (Notes inverted while on, Erase while locked). Solved: no cursor, rings or keys, a banner.

#include <Sudoku.h>

#include <cstddef>

class GfxRenderer;

namespace sd::draw {

constexpr int CURSOR_INSIDE = 2;   // the cursor frame's reach into its square ...
constexpr int CURSOR_OUTSIDE = 2;  // ... and past the square's lines into the neighbours
static_assert(CURSOR_INSIDE + CELL_LINE + CURSOR_OUTSIDE == CURSOR_FRAME, "the cursor frame is 5 px on a square line");
constexpr int WRONG_LINE = 2;
constexpr int REVEALED_TRIANGLE = 7;
constexpr int MARK_INSET = CURSOR_INSIDE + 1;  // the slash and the triangle stay clear of the cursor frame
constexpr int KEY_RADIUS = 6;

// What the status line says instead of the steady text: an action's message (Change::msg and its
// details), or the clash the last edit made.
struct Note {
  Msg msg = Msg::None;
  uint8_t digit = 0;       // Msg::EraseFirst
  uint16_t count = 0;      // Msg::Checked / Msg::Revealed
  Hint hint;               // Msg::Hint
  uint8_t clashDigit = 0;  // with clashUnit: "Two 7s in row 1"
  uint8_t clashUnit = NO_UNIT;
};

struct View {
  const Game* game = nullptr;                             // required
  const char* status = "";                                // the status line
  bool statusMessage = false;                             // a message (bold), not the steady line
  const char* toolLabels[TOOL_COUNT] = {"", "", "", ""};  // Notes, Erase, Undo, Menu
  // The completion banner (over the status line and the keys while game->solved).
  const char* bannerTitle = "";   // "Solved in 12:04"
  const char* bannerDetail = "";  // "Medium 14 - 1 hint"
  const char* bannerButton = "";  // "New puzzle"
};

// The digit the same-digit cue follows: the locked digit, else the cursor square's; 0 for none
// (and always once solved).
int highlightDigit(const Game& g);

// "Easy" ... "Expert" (translated).
const char* tierName(int tier);
// "Medium 14".
void formatPuzzleName(const Game& g, char* out, size_t cap);
// "12:04" (or "1:02:03").
void formatElapsed(uint32_t seconds, char* out, size_t cap);
// The status line: the note's message when it has one (returns true: drawn bold), else the steady
// line ("Medium 14 - 41 to go", "Medium 14 - 3 clashes", or the first-frame tip).
bool formatStatus(const Game& g, const Note& note, char* out, size_t cap);
// The banner's title ("Solved in 12:04") and detail ("Medium 14 - 1 hint - 2 checks").
void formatBanner(const Game& g, char* title, size_t titleCap, char* detail, size_t detailCap);

void drawGrid(GfxRenderer& r, const View& view);
void drawStatus(GfxRenderer& r, const View& view);
void drawKeys(GfxRenderer& r, const View& view);
void drawBanner(GfxRenderer& r, const View& view);

// The grid, then the banner when solved or else the status line and the keys. The caller clears
// the screen and draws the header first.
void drawScreen(GfxRenderer& r, const View& view);

}  // namespace sd::draw
