#pragma once

// Word Search selection: snapping a drag to one of the 8 directions, the per-contact state
// machine, the tap/anchor rules and the key cursor. Pure; the activity feeds it one frame at
// a time from MappedInputManager (isScreenTouchHeld + wasScreenTouchReleased).
//
// A contact's start cell S is the cell under its first held frame (or the edge cell, for a
// landing in the bare margin beside the grid); its end cell E is the snapped cell of the LAST
// held position (cached every frame, since the release frame carries no position). A snapped
// end less than a shortest word from S (one step) counts as S: no such line can match, and the
// touch panel's centroid drifts 10-20 px as a finger rolls off at lift. For the same drift, an
// E that moved one step off a cell the finger rested on (SETTLE_MS) less than SETTLE_MS before
// the contact ended falls back to that cell. It ends on the release edge OR on the first frame
// the touch is no longer held (a second finger silences the single-contact queries but not the
// release).
//   E != S: the line S->E is evaluated; the anchor is cleared.
//   E == S (a tap): no anchor -> anchor = S; anchor == S -> anchor cleared;
//                   anchor A aligned with S -> the line A->S is evaluated, anchor cleared;
//                   A not aligned -> anchor = S.
// Contacts that start off the grid are reported at their end for the buttons.

#include <cstdint>

#include "WsLayout.h"
#include "WsModel.h"

namespace ws {

// The end cell of a drag from start's centre by (dx, dy) px on a grid of `size` cells of
// `cell` px: the nearest of the 8 directions (45 degree sectors), the projection on it rounded
// to whole steps (a diagonal step is sqrt 2 cells), clamped to the grid.
Cell snapEnd(int size, int cell, Cell start, int dx, int dy);
// The same from a screen point, through the layout.
Cell snapEndAt(const BoardLayout& layout, Cell start, int x, int y);

// How long a finger must rest on an end cell for a one-step jump at lift to fall back to it.
constexpr uint32_t SETTLE_MS = 80;

struct Contact {
  bool active = false;  // a finger is down and being tracked
  bool onGrid = false;  // it began on a cell
  Cell start;
  Cell end;
  Cell rested;              // the end before `end`, when the finger rested on it (else invalid)
  uint32_t endSinceMs = 0;  // when `end` took its value
  int16_t startX = 0;
  int16_t startY = 0;
  int16_t lastX = 0;
  int16_t lastY = 0;
  bool gridActive() const { return active && onGrid; }
};

struct ContactEvent {
  enum class Kind : uint8_t {
    None,
    Began,          // a grid contact started (nothing to draw yet)
    EndMoved,       // the drag's snapped end cell changed: redraw the preview S->E
    Line,           // evaluate `line` (a drag, or a tap completing an aligned anchor)
    Anchored,       // a tap set the anchor
    AnchorCleared,  // a tap on the anchor cleared it
    OutsideEnded,   // a contact that began off the grid ended: (x0, y0) -> (x1, y1)
  };
  Kind kind = Kind::None;
  bool gridEnded = false;  // a grid contact ended this frame (do not read Back on this frame)
  Line line;
  int16_t x0 = 0;
  int16_t y0 = 0;
  int16_t x1 = 0;
  int16_t y1 = 0;
};

// One input frame: held/x/y = isScreenTouchHeld(x, y), released = wasScreenTouchReleased(),
// nowMs = millis().
ContactEvent trackContact(Contact& contact, Cell& anchor, const BoardLayout& layout, bool held, int x, int y,
                          bool released, uint32_t nowMs);

// The tap rules on one cell (a touch tap, or the key cursor's "tap"). Kind is Line, Anchored
// or AnchorCleared.
ContactEvent tapCell(Cell& anchor, Cell cell);

// The key cursor: delta cells in reading order, wrapping around the grid.
Cell stepCursor(Cell c, int delta, int size);
// The key cursor in 2D, clamped to the grid.
Cell moveCursor(Cell c, int dRow, int dCol, int size);

}  // namespace ws
