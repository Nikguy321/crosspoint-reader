#pragma once

// Crossword: 5x7 px digits for clue numbers in squares under 37 px, where SMALL_FONT's ~14 px
// digits would collide with the letter. Each row is 5 bits, bit 4 = the leftmost column.

#include <cstdint>

namespace cw {

constexpr int DIGIT_W = 5;
constexpr int DIGIT_H = 7;
constexpr int DIGIT_GAP = 1;  // between digits of one number

constexpr uint8_t DIGITS_5X7[10][DIGIT_H] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},  // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  // 9
};

// Ink at (x, y) of digit d (0..9), x 0..4 left to right, y 0..6 top to bottom.
constexpr bool digitPixel(const int d, const int x, const int y) {
  return d >= 0 && d <= 9 && x >= 0 && x < DIGIT_W && y >= 0 && y < DIGIT_H &&
         ((DIGITS_5X7[d][y] >> (DIGIT_W - 1 - x)) & 1) != 0;
}

// The px width of a number drawn with these digits (0 for n <= 0).
constexpr int numberWidth(int n) {
  if (n <= 0) return 0;
  int digits = 0;
  for (; n > 0; n /= 10) digits++;
  return digits * DIGIT_W + (digits - 1) * DIGIT_GAP;
}

// The digits of n, most significant first, into out (at most 3 for a clue number). Returns the
// count (0 for n <= 0 or n > 999).
inline int numberDigits(const int n, uint8_t out[3]) {
  if (n <= 0 || n > 999) return 0;
  const int count = n >= 100 ? 3 : n >= 10 ? 2 : 1;
  int v = n;
  for (int i = count - 1; i >= 0; i--) {
    out[i] = static_cast<uint8_t>(v % 10);
    v /= 10;
  }
  return count;
}

}  // namespace cw
