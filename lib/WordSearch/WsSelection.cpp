#include "WsSelection.h"

#include <cstdlib>

namespace ws {

namespace {

int clampSteps(int steps, const int pos, const int d, const int size) {
  if (d > 0 && steps > size - 1 - pos) steps = size - 1 - pos;
  if (d < 0 && steps > pos) steps = pos;
  return steps;
}

}  // namespace

Cell snapEnd(const int size, const int cell, const Cell start, const int dx, const int dy) {
  if (!inGrid(start, size) || cell <= 0) return start;
  const int ax = std::abs(dx);
  const int ay = std::abs(dy);
  if (ax == 0 && ay == 0) return start;
  // Sector edges at 22.5 degrees from each axis: tan(22.5) ~ 29/70.
  int dr = 0;
  int dc = 0;
  int along = 0;  // px along the direction, in step units of `cell`
  if (70 * ay <= 29 * ax) {
    dc = dx > 0 ? 1 : -1;
    along = ax;
  } else if (70 * ax <= 29 * ay) {
    dr = dy > 0 ? 1 : -1;
    along = ay;
  } else {
    dc = dx > 0 ? 1 : -1;
    dr = dy > 0 ? 1 : -1;
    // Projection on the unit diagonal is (ax + ay) / sqrt 2 and a step is sqrt 2 cells long,
    // so steps = (ax + ay) / 2 cells.
    along = (ax + ay) / 2;
  }
  int steps = (along + cell / 2) / cell;
  steps = clampSteps(steps, start.row, dr, size);
  steps = clampSteps(steps, start.col, dc, size);
  return makeCell(start.row + dr * steps, start.col + dc * steps);
}

Cell snapEndAt(const BoardLayout& layout, const Cell start, const int x, const int y) {
  return snapEnd(layout.size, layout.cell, start, x - layout.centerX(start.col), y - layout.centerY(start.row));
}

ContactEvent tapCell(Cell& anchor, const Cell cell) {
  ContactEvent ev;
  if (!anchor.valid()) {
    anchor = cell;
    ev.kind = ContactEvent::Kind::Anchored;
  } else if (anchor == cell) {
    anchor = Cell{};
    ev.kind = ContactEvent::Kind::AnchorCleared;
  } else if (aligned(anchor, cell)) {
    ev.kind = ContactEvent::Kind::Line;
    ev.line = Line{anchor, cell};
    anchor = Cell{};
  } else {
    anchor = cell;
    ev.kind = ContactEvent::Kind::Anchored;
  }
  return ev;
}

ContactEvent trackContact(Contact& c, Cell& anchor, const BoardLayout& layout, const bool held, const int x,
                          const int y, const bool released, const uint32_t nowMs) {
  ContactEvent ev;
  if (held && !released) {
    if (!c.active) {
      c.active = true;
      c.startX = c.lastX = static_cast<int16_t>(x);
      c.startY = c.lastY = static_cast<int16_t>(y);
      c.onGrid = layout.cellNear(x, y, c.start);
      if (!c.onGrid) c.start = Cell{};
      c.end = c.start;
      c.rested = Cell{};
      c.endSinceMs = nowMs;
      if (c.onGrid) ev.kind = ContactEvent::Kind::Began;
      return ev;
    }
    c.lastX = static_cast<int16_t>(x);
    c.lastY = static_cast<int16_t>(y);
    if (!c.onGrid) return ev;
    // A finger that rolls less than half a cell from where it landed is still a tap.
    const int moved = std::abs(x - c.startX) > std::abs(y - c.startY) ? std::abs(x - c.startX) : std::abs(y - c.startY);
    Cell end = moved * 2 < layout.cell ? c.start : snapEndAt(layout, c.start, x, y);
    if (lineCells(c.start, end) < MIN_WORD_LETTERS) end = c.start;
    if (end != c.end) {
      c.rested = nowMs - c.endSinceMs >= SETTLE_MS ? c.end : Cell{};
      c.end = end;
      c.endSinceMs = nowMs;
      ev.kind = ContactEvent::Kind::EndMoved;
    }
    return ev;
  }
  if (!c.active) return ev;
  // The contact ended: the release edge, or the first frame it is no longer held.
  c.active = false;
  if (!c.onGrid) {
    ev.kind = ContactEvent::Kind::OutsideEnded;
    ev.x0 = c.startX;
    ev.y0 = c.startY;
    ev.x1 = c.lastX;
    ev.y1 = c.lastY;
    return ev;
  }
  // A one-step jump off a rested cell just before the end is the lift's drift, not the player.
  Cell end = c.end;
  if (c.rested.valid() && nowMs - c.endSinceMs < SETTLE_MS && lineCells(c.rested, c.end) == 2) end = c.rested;
  if (end != c.start) {
    ev.kind = ContactEvent::Kind::Line;
    ev.line = Line{c.start, end};
    anchor = Cell{};
  } else {
    ev = tapCell(anchor, c.start);
  }
  ev.gridEnded = true;
  return ev;
}

Cell stepCursor(const Cell c, const int delta, const int size) {
  if (size <= 0) return c;
  const int total = size * size;
  int index = inGrid(c, size) ? c.row * size + c.col : 0;
  index = ((index + delta) % total + total) % total;
  return makeCell(index / size, index % size);
}

Cell moveCursor(const Cell c, const int dRow, const int dCol, const int size) {
  if (size <= 0) return c;
  const Cell from = inGrid(c, size) ? c : makeCell(0, 0);
  int r = from.row + dRow;
  int col = from.col + dCol;
  r = r < 0 ? 0 : (r >= size ? size - 1 : r);
  col = col < 0 ? 0 : (col >= size ? size - 1 : col);
  return makeCell(r, col);
}

}  // namespace ws
