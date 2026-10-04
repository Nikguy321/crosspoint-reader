#include "CwLayout.h"

#include "CwKeyboard.h"

namespace cw {

CellStyle cellStyleFor(const int cell) {
  CellStyle s;
  s.letterIndex = cell >= 60 ? 3 : cell >= 46 ? 2 : cell >= 37 ? 1 : 0;
  s.letterPoints = LETTER_POINTS[s.letterIndex];
  s.tinyDigits = cell < TINY_DIGIT_CELL;
  s.numberX = 2;
  s.numberY = s.tinyDigits ? 2 : 1;
  s.letterBottom = cell - 2;
  return s;
}

bool ScreenLayout::cellAt(const int x, const int y, int& index) const {
  if (cell <= 0 || !grid.contains(x, y)) return false;
  index = ((y - grid.y) / cell) * w + (x - grid.x) / cell;
  return true;
}

bool ScreenLayout::cellNear(const int x, const int y, int& index) const {
  if (cellAt(x, y, index)) return true;
  if (cell <= 0 || y < grid.y || y >= grid.bottom()) return false;
  const int slack = cell / 2;
  if (x < grid.x - slack || x >= grid.right() + slack) return false;
  return cellAt(x < grid.x ? grid.x : grid.right() - 1, y, index);
}

ScreenLayout computeLayout(const int w, const int h) {
  ScreenLayout l;
  if (w < 1 || h < 1) return l;
  l.w = w;
  l.h = h;
  const int areaW = GRID_RIGHT - GRID_LEFT;
  const int areaH = GRID_BOTTOM - GRID_TOP;
  int cell = areaW / w;
  if (areaH / h < cell) cell = areaH / h;
  if (cell > MAX_CELL_PX) cell = MAX_CELL_PX;
  l.cell = cell;
  l.grid = Rect{GRID_LEFT + (areaW - w * cell) / 2, GRID_TOP + (areaH - h * cell) / 2, w * cell, h * cell};
  l.clueBar = Rect{GRID_LEFT, BAR_TOP, areaW, BAR_BOTTOM - BAR_TOP};
  l.prevButton = Rect{GRID_LEFT, BAR_TOP, BAR_BUTTON_W, BAR_BOTTOM - BAR_TOP};
  l.nextButton = Rect{GRID_RIGHT - BAR_BUTTON_W, BAR_TOP, BAR_BUTTON_W, BAR_BOTTOM - BAR_TOP};
  l.clueText = Rect{BAR_TEXT_LEFT, BAR_TOP, BAR_TEXT_RIGHT - BAR_TEXT_LEFT, BAR_BOTTOM - BAR_TOP};
  l.keyboard = Rect{GRID_LEFT, KEYBOARD_TOP, areaW, KEYBOARD_BOTTOM - KEYBOARD_TOP};
  l.banner = Rect{GRID_LEFT, BAR_TOP, areaW, KEYBOARD_BOTTOM - BAR_TOP};
  l.bannerButton =
      Rect{(SCREEN_W - BANNER_BUTTON_W) / 2, KEYBOARD_BOTTOM - 24 - BANNER_BUTTON_H, BANNER_BUTTON_W, BANNER_BUTTON_H};
  l.style = cellStyleFor(cell);
  return l;
}

Target targetAt(const ScreenLayout& layout, const bool banner, const int x, const int y) {
  Target t;
  int index = -1;
  if (layout.cellNear(x, y, index)) {
    t.kind = TargetKind::Cell;
    t.index = static_cast<int16_t>(index);
    return t;
  }
  if (banner) {
    if (layout.bannerButton.contains(x, y)) t.kind = TargetKind::BannerButton;
    return t;
  }
  if (layout.prevButton.contains(x, y)) {
    t.kind = TargetKind::PrevClue;
  } else if (layout.nextButton.contains(x, y)) {
    t.kind = TargetKind::NextClue;
  } else if (layout.clueText.contains(x, y)) {
    t.kind = TargetKind::ClueText;
  } else if ((index = keyAt(x, y)) >= 0) {
    t.kind = TargetKind::Key;
    t.index = static_cast<int16_t>(index);
  }
  return t;
}

Target tapTarget(const ScreenLayout& layout, const bool banner, const int x0, const int y0, const int x1,
                 const int y1) {
  const Target a = targetAt(layout, banner, x0, y0);
  return a == targetAt(layout, banner, x1, y1) ? a : Target{};
}

}  // namespace cw
