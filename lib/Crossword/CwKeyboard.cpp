#include "CwKeyboard.h"

namespace cw {

namespace {

constexpr char ROW_LETTERS[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
constexpr int ROW_KEYS[KEY_ROWS] = {10, 9, 7};

constexpr int px(const int tenths) { return (tenths + 5) / 10; }
constexpr int rowTop(const int row) { return KEYBOARD_TOP + row * (KEY_ROW_H + KEY_ROW_GAP); }

// A key 1 px inside its pitch cell [left, right).
constexpr Rect keyRect(const int left, const int right, const int row) {
  return Rect{left + 1, rowTop(row), right - left - 2, KEY_ROW_H};
}

constexpr Key makeKey(const int index) {
  Key k;
  const int left10 = GRID_LEFT * 10;
  if (index < 10) {
    k.letter = ROW_LETTERS[index];
    const int l = left10 + index * KEY_PITCH_TENTHS;
    k.rect = keyRect(px(l), px(l + KEY_PITCH_TENTHS), 0);
  } else if (index < 19) {
    k.letter = ROW_LETTERS[index];
    const int l = left10 + KEY_PITCH_TENTHS / 2 + (index - 10) * KEY_PITCH_TENTHS;
    k.rect = keyRect(px(l), px(l + KEY_PITCH_TENTHS), 1);
  } else if (index == 19) {
    k.kind = KeyKind::Menu;
    k.rect = keyRect(GRID_LEFT, GRID_LEFT + SIDE_KEY_W, 2);
  } else if (index < 27) {
    k.letter = ROW_LETTERS[index - 1];
    const int l = (GRID_LEFT + SIDE_KEY_W) * 10 + (index - 20) * KEY_PITCH_TENTHS;
    k.rect = keyRect(px(l), px(l + KEY_PITCH_TENTHS), 2);
  } else {
    k.kind = KeyKind::Del;
    k.rect = keyRect(GRID_RIGHT - SIDE_KEY_W, GRID_RIGHT, 2);
  }
  // The touch area: the whole pitch cell, and half of each 4 px row gap.
  k.hit = Rect{k.rect.x - 1, k.rect.y, k.rect.w + 2, k.rect.h};
  const int row = index < 10 ? 0 : (index < 19 ? 1 : 2);
  if (row > 0) {
    k.hit.y -= KEY_ROW_GAP / 2;
    k.hit.h += KEY_ROW_GAP / 2;
  }
  if (row < KEY_ROWS - 1) k.hit.h += KEY_ROW_GAP - KEY_ROW_GAP / 2;
  if (index == 10) {  // A takes the row's left margin
    k.hit.w += k.hit.x - GRID_LEFT;
    k.hit.x = GRID_LEFT;
  } else if (index == 18) {  // L takes the right one
    k.hit.w = GRID_RIGHT - k.hit.x;
  }
  return k;
}

struct KeyTable {
  Key keys[KEY_COUNT];
  constexpr KeyTable() : keys() {
    for (int i = 0; i < KEY_COUNT; i++) keys[i] = makeKey(i);
  }
};
constexpr KeyTable KEYS;

static_assert(ROW_KEYS[0] + ROW_KEYS[1] + ROW_KEYS[2] + 2 == KEY_COUNT, "Menu and Del complete row 3");

}  // namespace

const Key& keyboardKey(const int index) { return KEYS.keys[index < 0 || index >= KEY_COUNT ? 0 : index]; }

int keyAt(const int x, const int y) {
  if (y < KEYBOARD_TOP || y >= KEYBOARD_BOTTOM) return -1;
  for (int i = 0; i < KEY_COUNT; i++) {
    if (KEYS.keys[i].hit.contains(x, y)) return i;
  }
  return -1;
}

int keyForLetter(char letter) {
  if (letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
  for (int i = 0; i < KEY_COUNT; i++) {
    if (KEYS.keys[i].kind == KeyKind::Letter && KEYS.keys[i].letter == letter) return i;
  }
  return -1;
}

}  // namespace cw
