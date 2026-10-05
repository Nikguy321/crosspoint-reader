#include "SdLayout.h"

namespace sd {

namespace {

// The line before column / row k (1..8) is BOX_LINE or CELL_LINE wide; the square before it takes
// its first half (rounded up), k the rest.
constexpr int lineBefore(const int k) { return k % 3 == 0 ? BOX_LINE : CELL_LINE; }
constexpr int splitBefore(const int start, const int k) { return start - lineBefore(k) / 2; }

constexpr int px(const int tenths) { return (tenths + 5) / 10; }
constexpr int toolLeft(const int k) { return px(TARGET_LEFT * 10 + k * TOOL_PITCH_TENTHS); }
constexpr int toolRight(const int k) { return k == TOOL_COUNT - 1 ? TARGET_RIGHT : toolLeft(k + 1); }

int indexOf(const int v, int (*lo)(int), int (*hi)(int)) {
  for (int k = 0; k < 9; k++) {
    if (v >= lo(k) && v < hi(k)) return k;
  }
  return -1;
}

}  // namespace

int columnHitLeft(const int col) { return col <= 0 ? TARGET_LEFT : splitBefore(cellLeft(col), col); }
int columnHitRight(const int col) { return col >= 8 ? TARGET_RIGHT : columnHitLeft(col + 1); }
int rowHitTop(const int row) { return row <= 0 ? GRID_Y : splitBefore(cellTop(row), row); }
int rowHitBottom(const int row) { return row >= 8 ? GRID_BOTTOM : rowHitTop(row + 1); }

Rect digitKeyRect(const int digit) {
  const int col = digit < 1 ? 0 : digit > 9 ? 8 : digit - 1;
  return Rect{cellLeft(col) + (CELL_PX - DIGIT_W) / 2, DIGIT_TOP, DIGIT_W, DIGIT_H};
}

Rect digitKeyHit(const int digit) {
  const int col = digit < 1 ? 0 : digit > 9 ? 8 : digit - 1;
  return Rect{columnHitLeft(col), DIGIT_HIT_TOP, columnHitRight(col) - columnHitLeft(col),
              DIGIT_HIT_BOTTOM - DIGIT_HIT_TOP};
}

Rect toolKeyRect(const Tool tool) {
  const int k = static_cast<int>(tool);
  const int left = toolLeft(k);
  const int pitch = toolLeft(k + 1) - left;
  return Rect{left + (pitch - TOOL_W) / 2, TOOL_TOP, TOOL_W, TOOL_H};
}

Rect toolKeyHit(const Tool tool) {
  const int k = static_cast<int>(tool);
  return Rect{toolLeft(k), TOOL_HIT_TOP, toolRight(k) - toolLeft(k), TOOL_HIT_BOTTOM - TOOL_HIT_TOP};
}

Target targetAt(const bool banner, const int x, const int y) {
  Target t;
  if (x < TARGET_LEFT || x >= TARGET_RIGHT) return t;
  if (y >= GRID_Y && y < GRID_BOTTOM) {
    const int col = indexOf(x, columnHitLeft, columnHitRight);
    const int row = indexOf(y, rowHitTop, rowHitBottom);
    if (col >= 0 && row >= 0) {
      t.kind = TargetKind::Cell;
      t.index = static_cast<int8_t>(row * 9 + col);
    }
    return t;
  }
  if (banner) {
    if (bannerButtonRect().contains(x, y)) t.kind = TargetKind::BannerButton;
    return t;
  }
  if (y >= DIGIT_HIT_TOP && y < DIGIT_HIT_BOTTOM) {
    const int col = indexOf(x, columnHitLeft, columnHitRight);
    if (col >= 0) {
      t.kind = TargetKind::Digit;
      t.index = static_cast<int8_t>(col + 1);
    }
  } else if (y >= TOOL_HIT_TOP && y < TOOL_HIT_BOTTOM) {
    for (int k = 0; k < TOOL_COUNT; k++) {
      if (x >= toolLeft(k) && x < toolRight(k)) {
        t.kind = TargetKind::Tool;
        t.index = static_cast<int8_t>(k);
        break;
      }
    }
  }
  return t;
}

}  // namespace sd
