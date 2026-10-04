#pragma once

// Crossword keyboard: QWERTY 10 / 9 / 7 at a 46.6 px pitch inside x 7..473, rows of 52 px from
// y 632 with 4 px between them. Row 2 is indented half a key; row 3 is [Menu 70] Z..M [Del 70].
// A key is drawn 1 px inside its pitch cell (so neighbours are 2 px apart). The touch areas tile
// the keyboard: each key takes its whole pitch cell and half of the 4 px row gaps (the first row
// starts and the last ends at the drawn keys), and A and L also take row 2's half-key margins, so
// a tap in a gap types the nearer key instead of nothing.

#include <cstdint>

#include "CwLayout.h"

namespace cw {

enum class KeyKind : uint8_t { Letter = 0, Menu, Del };

struct Key {
  KeyKind kind = KeyKind::Letter;
  char letter = 0;  // 'A'..'Z' for a letter key
  Rect rect;        // drawn
  Rect hit;         // touched (the pitch cell and half the row gaps; wider for A and L)
};

constexpr int KEY_COUNT = 28;
constexpr int KEY_ROWS = 3;
constexpr int KEY_PITCH_TENTHS = 466;  // 46.6 px
constexpr int SIDE_KEY_W = 70;         // Menu and Del pitch cells

const Key& keyboardKey(int index);
// The key under (x, y), or -1.
int keyAt(int x, int y);
// The index of a letter's key, or -1.
int keyForLetter(char letter);
inline int menuKey() { return 19; }
inline int delKey() { return 27; }

}  // namespace cw
