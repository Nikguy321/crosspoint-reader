#pragma once

// Word Search: the puzzle model every other part shares. Pure C++ (no Arduino), so the host
// test suite builds it; the activities live in src/activities/apps.
//
// A puzzle is a square grid of 'A'..'Z' and up to MAX_WORDS words, each kept twice: the
// display text as written ("POLAR BEAR", uppercase, spaces/hyphens/apostrophes kept) and
// the grid letters ("POLARBEAR", A-Z only). A Puzzle is ~1.1 KB: allocate it once
// (makeUniqueNoThrow) on the device, never as a local.

#include <cstddef>
#include <cstdint>

namespace ws {

constexpr int MAX_GRID = 15;          // Hard's 15x15
constexpr int MAX_WORDS = 16;         // Hard's target
constexpr int MIN_WORD_LETTERS = 3;   // grid letters
constexpr int MAX_WORD_LETTERS = 15;  // = MAX_GRID
constexpr int MAX_DISPLAY_LEN = 20;   // display text bytes ("JACK-IN-THE-BOX" style)
constexpr int MAX_TITLE_LEN = 32;     // theme title bytes (UTF-8, cut on a code point)
constexpr int MAX_THEME_KEY = 47;     // "animals", or "file:<name>.words"

enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2 };
constexpr int DIFFICULTY_COUNT = 3;

// The eight directions, as (dRow, dCol). Index order: right, down-right, down, down-left,
// left, up-left, up, up-right (clockwise from east on screen); dir ^ 4 is the reverse.
constexpr int8_t DIR_DR[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int8_t DIR_DC[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr uint8_t DIR_RIGHT = 0, DIR_DOWN_RIGHT = 1, DIR_DOWN = 2, DIR_DOWN_LEFT = 3, DIR_LEFT = 4, DIR_UP_LEFT = 5,
                  DIR_UP = 6, DIR_UP_RIGHT = 7;
// The index of (dr, dc), each -1..1 and not both 0; -1 otherwise.
int dirIndex(int dr, int dc);

struct DifficultySpec {
  uint8_t size;        // grid side
  uint8_t words;       // target word count
  uint8_t minLetters;  // grid letters per word, inclusive
  uint8_t maxLetters;
  uint8_t dirMask;  // bit i = direction i may be used to PLACE a word (matching accepts all 8)
};
constexpr DifficultySpec DIFFICULTY_SPECS[DIFFICULTY_COUNT] = {
    {10, 8, 3, 10, (1u << DIR_RIGHT) | (1u << DIR_DOWN)},
    {12, 12, 3, 12, (1u << DIR_RIGHT) | (1u << DIR_DOWN) | (1u << DIR_DOWN_RIGHT) | (1u << DIR_UP_RIGHT)},
    {15, 16, 4, 15, 0xFF},
};
inline const DifficultySpec& specFor(const Difficulty d) { return DIFFICULTY_SPECS[static_cast<uint8_t>(d)]; }

// A grid cell; row/col -1 = none.
struct Cell {
  int8_t row = -1;
  int8_t col = -1;
  bool valid() const { return row >= 0 && col >= 0; }
  bool operator==(const Cell& o) const { return row == o.row && col == o.col; }
  bool operator!=(const Cell& o) const { return !(*this == o); }
};
inline Cell makeCell(const int row, const int col) {
  Cell c;
  c.row = static_cast<int8_t>(row);
  c.col = static_cast<int8_t>(col);
  return c;
}
inline bool inGrid(const Cell c, const int size) { return c.row >= 0 && c.col >= 0 && c.row < size && c.col < size; }

// A selection: two end cells (either order).
struct Line {
  Cell a;
  Cell b;
};
// True when a and b differ and sit on one of the 8 directions from each other.
bool aligned(Cell a, Cell b);
// Cells from a to b inclusive when aligned (1 when a == b), else 0.
int lineCells(Cell a, Cell b);

// Where the generator put a word: start cell, direction index, letter count.
struct Placement {
  int8_t row = 0;
  int8_t col = 0;
  uint8_t dir = 0;
  uint8_t len = 0;
  Cell start() const { return makeCell(row, col); }
  Cell end() const { return makeCell(row + DIR_DR[dir] * (len - 1), col + DIR_DC[dir] * (len - 1)); }
};

struct PuzzleWord {
  char display[MAX_DISPLAY_LEN + 1] = {};
  char letters[MAX_WORD_LETTERS + 1] = {};
  Placement place;
  bool found = false;
  Line foundLine;  // the endpoints the player selected (drawn as the capsule)
};

struct Puzzle {
  Difficulty difficulty = Difficulty::Medium;
  uint8_t size = 0;
  uint32_t seed = 0;
  char themeKey[MAX_THEME_KEY + 1] = {};
  char themeTitle[MAX_TITLE_LEN + 1] = {};
  char grid[MAX_GRID * MAX_GRID] = {};  // row-major, 'A'..'Z'
  PuzzleWord words[MAX_WORDS];
  uint8_t wordCount = 0;
  int8_t hintWord = -1;   // the word whose first letter wears the hint ring; -1 none
  uint8_t hintsUsed = 0;  // for the completion banner
  uint32_t elapsedSeconds = 0;
  Cell cursor = makeCell(0, 0);

  // Back to an empty puzzle in place (a Puzzle{} temporary would put ~1.1 KB on the stack).
  void reset();
  char at(const int row, const int col) const { return grid[row * MAX_GRID + col]; }
  char& at(const int row, const int col) { return grid[row * MAX_GRID + col]; }
  int foundCount() const;
  bool complete() const { return wordCount > 0 && foundCount() == wordCount; }
};

// Display text -> grid letters: keeps A-Z, drops spaces, hyphens and apostrophes. Returns the
// letter count, or 0 when any other byte appears or the count is outside 1..cap-1.
size_t gridLetters(const char* display, size_t displayLen, char* out, size_t cap);

// The letters along a line (aligned or a single cell) into out (NUL-terminated); 0 when the
// line is not aligned, leaves the grid, or does not fit cap.
size_t lettersOnLine(const Puzzle& p, Line line, char* out, size_t cap);

// "6:32", "1:02:03". Returns the length (0 when cap is too small).
size_t formatElapsed(uint32_t seconds, char* out, size_t cap);

}  // namespace ws
