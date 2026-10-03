#include "WsLayout.h"

namespace ws {

bool BoardLayout::cellAt(const int x, const int y, Cell& out) const {
  if (cell <= 0 || !grid.contains(x, y)) return false;
  out = makeCell((y - grid.y) / cell, (x - grid.x) / cell);
  return true;
}

bool BoardLayout::cellNear(const int x, const int y, Cell& out) const {
  if (cellAt(x, y, out)) return true;
  if (cell <= 0 || y < grid.y || y >= grid.bottom()) return false;
  const int slack = cell / 2;
  if (x < grid.x - slack || x >= grid.right() + slack) return false;
  return cellAt(x < grid.x ? grid.x : grid.right() - 1, y, out);
}

BoardLayout computeLayout(const Difficulty difficulty, const int contentTop) {
  BoardLayout l;
  const DifficultySpec& spec = specFor(difficulty);
  l.size = spec.size;
  l.cell = CELL_PX[static_cast<uint8_t>(difficulty)];
  const int side = l.size * l.cell;
  l.grid = Rect{(SCREEN_W - side) / 2, contentTop > GRID_TOP_MIN ? contentTop : GRID_TOP_MIN, side, side};
  l.bar = Rect{0, BAR_TOP, SCREEN_W, BAR_BOTTOM - BAR_TOP};
  const int top = l.grid.bottom() + LIST_GAP;
  l.list = Rect{LIST_SIDE, top, SCREEN_W - 2 * LIST_SIDE, BAR_TOP - LIST_GAP / 2 - top};
  l.menuButton = Rect{SCREEN_W - SIDE_INSET - 5 - BUTTON_W, BAR_TOP + 1, BUTTON_W, BUTTON_H};
  l.status = Rect{LIST_SIDE, BAR_TOP, l.menuButton.x - 10 - LIST_SIDE, l.bar.h};
  l.newPuzzleButton = Rect{(SCREEN_W - NEW_PUZZLE_W) / 2, l.list.bottom() - BUTTON_H - 14, NEW_PUZZLE_W, BUTTON_H + 4};
  return l;
}

ButtonHit hitButton(const BoardLayout& layout, const bool complete, const int x0, const int y0, const int x1,
                    const int y1) {
  if (layout.menuButton.contains(x0, y0) && layout.menuButton.contains(x1, y1)) return ButtonHit::Menu;
  if (complete && layout.newPuzzleButton.contains(x0, y0) && layout.newPuzzleButton.contains(x1, y1)) {
    return ButtonHit::NewPuzzle;
  }
  return ButtonHit::None;
}

ListLayout layoutWordList(const char* const* texts, const int count, const Rect& area, const int pitch[LIST_FONT_COUNT],
                          const MeasureText measure, void* ctx) {
  constexpr ListFont FONTS[4] = {ListFont::Large, ListFont::Large, ListFont::Small, ListFont::Small};
  constexpr uint8_t COLUMNS[4] = {3, 2, 3, 2};
  ListLayout l;
  for (int option = 0; option < 4; option++) {
    l = ListLayout{};
    l.font = FONTS[option];
    l.columns = COLUMNS[option];
    l.rows = static_cast<uint8_t>(count > 0 ? (count + l.columns - 1) / l.columns : 0);
    l.rowPitch = pitch[static_cast<uint8_t>(l.font)];
    if (l.rows * l.rowPitch > area.h) continue;
    int total = 0;
    for (int c = 0; c < l.columns; c++) {
      for (int i = c * l.rows; i < (c + 1) * l.rows && i < count; i++) {
        const int w = measure(ctx, l.font, texts[i]);
        if (w > l.colW[c]) l.colW[c] = w;
      }
      total += l.colW[c];
    }
    const int free = area.w - total;
    if (free < (l.columns - 1) * LIST_COL_GAP) continue;
    // Even spacing between the columns (and as margins), at most 40 px, the block centred.
    int space = free / (l.columns + 1);
    if (space > 40) space = 40;
    if (space < LIST_COL_GAP) space = LIST_COL_GAP;
    const int block = total + (l.columns - 1) * space;
    int x = area.x + (area.w - block) / 2;
    for (int c = 0; c < l.columns; c++) {
      l.colX[c] = x;
      x += l.colW[c] + space;
    }
    l.fits = true;
    break;
  }
  if (!l.fits) {
    // Nothing fits: two equal columns of the small font, the drawing truncates.
    l = ListLayout{};
    l.font = ListFont::Small;
    l.columns = 2;
    l.rows = static_cast<uint8_t>((count + 1) / 2);
    l.rowPitch = pitch[static_cast<uint8_t>(l.font)];
    for (int c = 0; c < 2; c++) {
      l.colX[c] = area.x + c * (area.w / 2);
      l.colW[c] = area.w / 2 - LIST_COL_GAP;
    }
  }
  const int used = l.rows * l.rowPitch;
  l.top = area.y + (used < area.h ? (area.h - used) / 2 : 0);
  return l;
}

void listItemOrigin(const ListLayout& list, const int index, int& x, int& y) {
  const int rows = list.rows > 0 ? list.rows : 1;
  const int col = index / rows;
  x = list.colX[col < 3 ? col : 2];
  y = list.top + (index % rows) * list.rowPitch;
}

}  // namespace ws
