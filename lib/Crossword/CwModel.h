#pragma once

// Crossword: the puzzle model every other part shares. Pure C++ (no Arduino), so the host test
// suite builds it; the activities live in src/activities/apps.
//
// An American-style grid of up to 15x15: 'A'..'Z' squares and '#' blocks, numbered entries
// (runs of 2 or more white squares), one clue each. Cells are row-major with the puzzle's own
// width as the stride (index = row * w + col). A Puzzle is ~18.4 KB (most of it the clue pool):
// allocate it once (makeUniqueNoThrow) on the device, never as a local.

#include <cstddef>
#include <cstdint>

namespace cw {

constexpr int MIN_SIDE = 3;
constexpr int MAX_SIDE = 15;
constexpr int MAX_CELLS = MAX_SIDE * MAX_SIDE;
constexpr int MAX_ENTRIES = 100;
constexpr size_t CLUE_POOL = 16384;     // all clue text, each NUL-terminated
constexpr size_t MAX_CLUE_BYTES = 400;  // longer clues are cut (on a code point)
constexpr size_t MAX_TITLE = 48;        // bytes of UTF-8
constexpr size_t MAX_AUTHOR = 64;       // author, and copyright, each
constexpr size_t MAX_SOURCE_KEY = 96;   // "builtin:<id>" or the card path
constexpr char BLOCK = '#';
constexpr char EMPTY = ' ';  // an empty square in Progress::fill
constexpr uint8_t NO_ENTRY = 0xFF;

enum Dir : uint8_t { ACROSS = 0, DOWN = 1 };

// Why a source was refused (or None). The activity maps each to a tr() line for the picker.
enum class Error : uint8_t {
  None = 0,
  TooBig,        // wider or taller than MAX_SIDE (the status carries the size)
  TooSmall,      // narrower or shorter than MIN_SIDE
  FileTooLarge,  // over MAX_IPUZ_BYTES / MAX_PUZ_BYTES (checked by the caller)
  Rebus,         // a square whose answer is not one letter A-Z
  Locked,        // a scrambled .puz
  Diagramless,   // a diagramless .puz / ipuz kind
  Barred,        // an ipuz with bars between squares
  NoSolution,    // no answer grid, or a white square without an answer
  NotCrossword,  // not an ipuz crossword / not a .puz / a white square in no entry
  Damaged,       // truncated or malformed
  TooManyClues,  // more than MAX_ENTRIES entries, or the clue text overflows CLUE_POOL
  BadNumbering,  // the file's numbers disagree with the standard numbering
  ClueMismatch,  // an entry without a clue, or a clue for no entry, or a clue given twice
  OutOfMemory,   // the JSON parse ran out of memory
};

struct Entry {
  uint8_t row = 0;
  uint8_t col = 0;
  uint8_t dir = ACROSS;
  uint8_t len = 0;
  uint8_t number = 0;
  uint16_t clueOff = 0;  // into Puzzle::clues
  uint16_t clueLen = 0;  // bytes, without the NUL
};

struct Puzzle {
  uint8_t w = 0;
  uint8_t h = 0;
  char solution[MAX_CELLS] = {};   // 'A'..'Z' or BLOCK
  uint8_t number[MAX_CELLS] = {};  // 0 = none
  uint8_t circled[(MAX_CELLS + 7) / 8] = {};
  // Across entries in number order, then Down entries in number order: the order "<" / ">"
  // walk and the clue list shows.
  Entry entries[MAX_ENTRIES];
  uint8_t entryCount = 0;
  uint8_t acrossCount = 0;
  uint8_t entryAt[2][MAX_CELLS] = {};  // [dir][cell] -> entry index, or NO_ENTRY
  uint16_t clueUsed = 1;               // clues[0] is the empty clue (offset 0) every entry starts with
  char clues[CLUE_POOL] = {};
  char title[MAX_TITLE + 1] = {};
  char author[MAX_AUTHOR + 1] = {};
  char copyright[MAX_AUTHOR + 1] = {};
  char sourceKey[MAX_SOURCE_KEY + 1] = {};
  uint32_t fnv = 0;  // puzzleFnv() of the solution

  Puzzle() { reset(); }
  // Back to an empty puzzle in place (a Puzzle{} temporary would put ~18 KB on the stack).
  void reset();
  int cells() const { return w * h; }
  int index(const int row, const int col) const { return row * w + col; }
  int rowOf(const int cell) const { return w ? cell / w : 0; }
  int colOf(const int cell) const { return w ? cell % w : 0; }
  bool isBlock(const int cell) const { return solution[cell] == BLOCK; }
  bool isCircled(const int cell) const { return (circled[cell >> 3] >> (cell & 7)) & 1; }
  void setCircled(const int cell) { circled[cell >> 3] |= static_cast<uint8_t>(1u << (cell & 7)); }
  const char* clue(const int entry) const { return clues + entries[entry].clueOff; }
  // The cell index of letter i of an entry.
  int entryCell(const int entry, const int i) const {
    const Entry& e = entries[entry];
    return e.dir == ACROSS ? index(e.row, e.col + i) : index(e.row + i, e.col);
  }
  int whiteCount() const;
};

// Progress flags, per cell.
constexpr uint8_t FLAG_WRONG = 1;     // marked by a check; cleared when the square is edited
constexpr uint8_t FLAG_REVEALED = 2;  // written by a reveal; locked

struct Progress {
  char sourceKey[MAX_SOURCE_KEY + 1] = {};
  uint32_t fnv = 0;
  uint8_t w = 0;
  uint8_t h = 0;
  char fill[MAX_CELLS] = {};  // EMPTY, 'A'..'Z', or BLOCK on a block
  uint8_t flags[MAX_CELLS] = {};
  uint8_t cursor = 0;  // cell index
  uint8_t dir = ACROSS;
  uint32_t elapsed = 0;  // seconds
  uint16_t checks = 0;
  uint16_t reveals = 0;
  bool solved = false;
  uint32_t seq = 0;  // save order, for pruning old progress files

  // Back to all zero in place (a Progress{} temporary would put ~580 B on the stack).
  void clear();
};

// FNV-1a 32 of a byte run, continuing from h.
constexpr uint32_t FNV_OFFSET = 2166136261u;
uint32_t fnv1a(const char* data, size_t len, uint32_t h = FNV_OFFSET);
// FNV-1a 32 over "WxH:" + the solution (blocks as '#'), the puzzle's identity.
uint32_t puzzleFnv(int w, int h, const char* solution);

// Fresh progress for a puzzle: empty squares, no flags, cursor on the first cell of the first
// entry (Across when it has one), counters 0. Copies sourceKey, fnv, w, h.
void resetProgress(const Puzzle& p, Progress& out);
// The saved progress fits this puzzle: same fnv, size and blocks, letters A-Z or empty, every
// REVEALED square holds its answer, WRONG only on filled unlocked squares, the cursor on a
// white square. Anything else means the source changed: start fresh.
bool progressMatches(const Puzzle& p, const Progress& prog);

// "6:32", "1:02:03". Returns the length (0 when cap is too small).
size_t formatElapsed(uint32_t seconds, char* out, size_t cap);
// "14A" / "3D" into out. Returns the length (0 when cap is too small).
size_t formatClueLabel(const Puzzle& p, int entry, char* out, size_t cap);

}  // namespace cw
