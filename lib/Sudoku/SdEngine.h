#pragma once

// Sudoku engine, pure (no Arduino, no heap, no recursion): the seeded PRNG, the geometry tables,
// a bitmask solution counter (which also fills random grids), a human-style step solver graded by
// a technique ladder, the generator and the hint search. The same seed gives the same puzzle on
// the host and on the reader: integer maths only, try counts and never time budgets.
//
// Cells are row-major 0..80; digits 1..9, 0 = empty; a candidate mask holds digit d at bit d - 1.
// Units: 0-8 rows, 9-17 columns, 18-26 boxes (box b covers rows 3*(b/3).., columns 3*(b%3)..).
//
// The ladder (easiest first; a step is always the easiest one available, so the hardest step a
// solve needs is also the lowest tier whose ladder finishes it):
//   Easy    full house, hidden single (box), hidden single (row/column), naked single
//   Medium  pointing, claiming, naked pair, hidden pair
//   Hard    X-wing, naked triple, swordfish, hidden triple, turbot fish (skyscraper / 2-string
//           kite), XY-wing, W-wing, XYZ-wing, naked quad, jellyfish, hidden quad
//   Expert  trial: assume one candidate, follow singles; a contradiction removes it
// Within a tier the order follows Sudoku-Explainer-style ratings only loosely (X-wing, rated 3.2,
// is Hard while hidden pair, 3.4, is Medium): the tiers are what the player sees. There is no
// unique rectangle (it would lean on uniqueness and moved only 0.5 % of puzzles); the generator
// prototype's pins were made with that ladder (SD_NO_UR).
//
// Generation, method (c): fill a random grid, remove 180-degree symmetric pairs while the counter
// proves the puzzle unique, grade it; when it grades harder than the tier, put removed pairs back
// (newest first) until the tier's ladder finishes it, and keep it only when it grades exactly the
// tier. Easy stops removing at EASY_MIN_GIVENS givens. Out of MAX_TRIES, the last solvable graded
// puzzle is kept with its REAL tier, so generation never fails. Stack peak ~1.3 KB.

#include <cstddef>
#include <cstdint>

namespace sd {

constexpr int CELLS = 81;
constexpr uint16_t ALL_DIGITS = 0x1FF;
constexpr uint8_t NO_UNIT = 0xFF;

// ---- PRNG: splitmix32 (Word Search's WsRandom, copied so the game stands alone) -----------------

inline uint32_t nextRandom(uint32_t& state) {
  state += 0x9E3779B9u;
  uint32_t z = state;
  z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
  z = (z ^ (z >> 13)) * 0xC2B2AE35u;
  return z ^ (z >> 16);
}

// Uniform-enough in [0, n) by multiply-shift; 0 when n is 0.
inline uint32_t randomBelow(uint32_t& state, const uint32_t n) {
  if (n == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(nextRandom(state)) * n) >> 32);
}

// ---- geometry tables (constant-initialised: they sit in flash, 2.6 KB) ---------------------------

struct Tables {
  uint8_t row[CELLS], col[CELLS], box[CELLS];
  uint8_t unit[27][9];
  uint8_t peers[CELLS][20];
  uint8_t pop[512];  // popcount of a 9-bit mask (the S3 has no popcount instruction)
  constexpr Tables() : row(), col(), box(), unit(), peers(), pop() {
    for (int i = 0; i < CELLS; i++) {
      row[i] = static_cast<uint8_t>(i / 9);
      col[i] = static_cast<uint8_t>(i % 9);
      box[i] = static_cast<uint8_t>((i / 27) * 3 + (i % 9) / 3);
    }
    for (int u = 0; u < 9; u++) {
      for (int k = 0; k < 9; k++) {
        unit[u][k] = static_cast<uint8_t>(u * 9 + k);
        unit[9 + u][k] = static_cast<uint8_t>(k * 9 + u);
        unit[18 + u][k] = static_cast<uint8_t>(((u / 3) * 3 + k / 3) * 9 + (u % 3) * 3 + k % 3);
      }
    }
    for (int i = 0; i < CELLS; i++) {
      int n = 0;
      for (int j = 0; j < CELLS; j++) {
        if (j != i && (row[j] == row[i] || col[j] == col[i] || box[j] == box[i]))
          peers[i][n++] = static_cast<uint8_t>(j);
      }
    }
    for (int m = 0; m < 512; m++) {
      int c = 0;
      for (int b = 0; b < 9; b++) c += (m >> b) & 1;
      pop[m] = static_cast<uint8_t>(c);
    }
  }
};
extern const Tables TABLES;

inline int rowOf(const int cell) { return TABLES.row[cell]; }
inline int colOf(const int cell) { return TABLES.col[cell]; }
inline int boxOf(const int cell) { return TABLES.box[cell]; }
// The k-th (0..19) cell sharing a row, column or box with cell, ascending.
inline int peerOf(const int cell, const int k) { return TABLES.peers[cell][k]; }
inline int unitCell(const int unit, const int k) { return TABLES.unit[unit][k]; }
inline int popcount9(const uint16_t mask) { return TABLES.pop[mask & ALL_DIGITS]; }
inline uint16_t digitBit(const int d) { return static_cast<uint16_t>(1u << (d - 1)); }
// Two different cells in one row, column or box.
inline bool sees(const int a, const int b) {
  return a != b && (TABLES.row[a] == TABLES.row[b] || TABLES.col[a] == TABLES.col[b] || TABLES.box[a] == TABLES.box[b]);
}

// ---- tiers and techniques ------------------------------------------------------------------------

enum Tier : uint8_t { Easy = 0, Medium = 1, Hard = 2, Expert = 3 };
constexpr int TIER_COUNT = 4;
// "easy" / "medium" / "hard" / "expert" (seeds, saves, the bench); nullptr out of range.
const char* tierKey(int tier);
// The tier of a key above; -1 for anything else.
int tierFromKey(const char* key, size_t len);

// Ladder order (tier-major); the numbering is part of no file format.
enum Tech : uint8_t {
  FullHouse,
  HiddenSingleBox,
  HiddenSingleLine,
  NakedSingle,
  Pointing,
  Claiming,
  NakedPair,
  HiddenPair,
  XWing,
  NakedTriple,
  Swordfish,
  HiddenTriple,
  TurbotFish,
  XYWing,
  WWing,
  XYZWing,
  NakedQuad,
  Jellyfish,
  HiddenQuad,
  Trial,
};
constexpr int TECH_COUNT = Trial + 1;
int techTier(Tech tech);
// English technique name ("X-wing"), for the bench and logs; the UI translates by Tech.
const char* techName(Tech tech);
// A single: the step places a digit.
inline bool isSingle(const Tech tech) { return tech <= NakedSingle; }

// One applied step. Placements (singles) set place/digit (and unit, except a naked single);
// eliminations set elims and the pattern:
//   Pointing   unit = the box, pattern[0] = the line          digit
//   Claiming   unit = the line, pattern[0] = the box           digit
//   subsets    unit, digits, pattern = the cells
//   fish       pattern = the base lines (unit numbers)          digit
//   TurbotFish pattern = p, q, r, t (p or t holds digit)        digit
//   XY/XYZ     pattern = pivot, pincer, pincer                  digit = the one removed
//   WWing      pattern = the twins, then the link's cells; unit = the link's unit; digit removed
//   Trial      pattern[0] = the cell, digit = the candidate removed
struct Step {
  Tech tech = FullHouse;
  int8_t place = -1;  // the cell placed, or -1 for an elimination
  uint8_t digit = 0;
  uint8_t unit = NO_UNIT;
  uint8_t elims = 0;  // candidates removed
  uint8_t pattern[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint16_t digits = 0;  // a subset's digits
};

// A solving board: placed digits and the candidates of the empty cells.
struct Board {
  uint8_t v[CELLS];
  uint16_t c[CELLS];
  uint8_t left;  // empty cells

  // From a grid (0 = empty). False when two digits clash.
  bool load(const uint8_t* grid);
  // Places d at i and removes it from the peers' candidates.
  void place(int i, int d);
  // An empty cell without candidates, or a unit missing a digit.
  bool contradiction() const;
};

// Applies the easiest available step whose tier is <= maxTier. False when stuck.
bool nextStep(Board& board, int maxTier, Step& out);

struct Grade {
  bool solved = false;  // the ladder up to maxTier finished it
  Tech hardest = FullHouse;
  uint8_t tier = 0;  // techTier(hardest)
  uint16_t steps = 0;
  uint16_t score = 0;  // sum of ratings x10 over the non-single steps (a tie-breaker within a tier)
};
Grade grade(const uint8_t* puzzle, int maxTier);

// ---- counter and filler --------------------------------------------------------------------------

// Solutions of a grid, stopping at limit; -1 when the node budget runs out (never on a real
// puzzle). firstSol (optional) receives the first solution. With rng the digit order is random.
int countSolutions(const uint8_t* grid, int limit, uint8_t* firstSol = nullptr, uint32_t* rng = nullptr,
                   uint32_t nodeCap = 200000);
// A random complete grid.
bool fillRandom(uint32_t& rng, uint8_t* out);

// ---- generator -----------------------------------------------------------------------------------

constexpr int MAX_TRIES = 160;
constexpr int EASY_MIN_GIVENS = 36;

struct Generated {
  uint8_t givens[CELLS] = {};
  uint8_t solution[CELLS] = {};
  Grade grade;
  uint8_t tier = 0;    // the REAL tier (== the asked tier unless the tries ran out)
  uint16_t tries = 0;  // grids filled
  bool exact = false;  // graded exactly the asked tier
  int givenCount() const;
};

// Method (c) for a tier with the product settings (MAX_TRIES, the Easy floor). Always yields a
// unique puzzle (false only if no try produced one, which does not happen).
bool generate(uint32_t seed, int tier, Generated& out);
// The same with explicit knobs (the tests, the bench): minGivens 0 = no floor.
bool generateWith(uint32_t seed, int tier, int maxTries, int minGivens, Generated& out);

// FNV-1a 32.
constexpr uint32_t FNV_OFFSET = 2166136261u;
uint32_t fnv1a(const void* data, size_t len, uint32_t h = FNV_OFFSET);
// A puzzle's identity: FNV-1a over its 81 givens (raw bytes 0..9).
inline uint32_t givensFnv(const uint8_t* givens) { return fnv1a(givens, CELLS); }
// The seed of numbered puzzle n of a tier: fnv1a("sudoku <tierKey> <n>").
uint32_t puzzleSeed(int tier, uint32_t number);

// ---- hints ---------------------------------------------------------------------------------------

// From a grid (givens plus the player's digits, assumed correct; no notes), the steps up to the
// first placement. place = that placement; hardest = the hardest step before it (== place when no
// elimination was needed). False when the ladder is stuck or the grid is full or clashes.
struct NextPlacement {
  Step place;
  Step hardest;
  bool singlesOnly = true;   // the placement needed nothing but singles
  uint8_t eliminations = 0;  // steps before the placement
};
bool findNextPlacement(const uint8_t* grid, NextPlacement& out);

// "Hidden single (box): 2 at r1c1 in box 1" and so on: English, for the bench and logs. Returns the
// length (truncated to cap - 1).
size_t describe(const Step& step, char* out, size_t cap);

}  // namespace sd
