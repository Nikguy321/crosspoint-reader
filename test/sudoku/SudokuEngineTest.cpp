// Sudoku engine on the host: the counter against a naive solver, one fixture per technique (the
// exact candidates each removes), the ladder's soundness along whole solves, seeds 0-199 of every
// tier unique and graded exactly, the pins (the ladder has no unique rectangle: the prototype's
// SD_NO_UR build), the numbered seeds, the out-of-tries fallback and the hint search.
#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Sudoku.h"

using namespace sd;

namespace {

constexpr int rc(const int r, const int c) { return r * 9 + c; }

// Every square empty with every candidate: the fixtures carve their pattern out of this.
Board blank() {
  Board b;
  for (int i = 0; i < CELLS; i++) {
    b.v[i] = 0;
    b.c[i] = ALL_DIGITS;
  }
  b.left = CELLS;
  return b;
}

uint16_t mask(const std::initializer_list<int> digits) {
  uint16_t m = 0;
  for (const int d : digits) m |= digitBit(d);
  return m;
}

void removeDigit(Board& b, const std::initializer_list<int> cells, const int d) {
  for (const int i : cells) b.c[i] &= static_cast<uint16_t>(~digitBit(d));
}

// The other cells of row r (not in keepCols).
std::vector<int> rowExcept(const int r, const std::initializer_list<int> keepCols) {
  std::vector<int> out;
  for (int c = 0; c < 9; c++) {
    bool keep = false;
    for (const int k : keepCols) keep |= k == c;
    if (!keep) out.push_back(rc(r, c));
  }
  return out;
}

void removeDigitFrom(Board& b, const std::vector<int>& cells, const int d) {
  for (const int i : cells) b.c[i] &= static_cast<uint16_t>(~digitBit(d));
}

using Elims = std::set<std::pair<int, int>>;  // (cell, digit)

// Runs one step and returns the candidates it removed (a placement's own square excluded).
Elims stepAndDiff(Board& b, Step& s) {
  Board before = b;
  EXPECT_TRUE(nextStep(b, Expert, s));
  Elims out;
  for (int i = 0; i < CELLS; i++) {
    if (s.place == i) continue;
    const uint16_t gone = before.c[i] & static_cast<uint16_t>(~b.c[i]);
    for (int d = 1; d <= 9; d++) {
      if (gone & digitBit(d)) out.insert({i, d});
    }
  }
  return out;
}

Elims cellsTimesDigits(const std::vector<int>& cells, const std::initializer_list<int> digits) {
  Elims out;
  for (const int i : cells) {
    for (const int d : digits) out.insert({i, d});
  }
  return out;
}

bool validComplete(const uint8_t* g) {
  for (int u = 0; u < 27; u++) {
    uint16_t seen = 0;
    for (int k = 0; k < 9; k++) {
      const int v = g[unitCell(u, k)];
      if (v < 1 || v > 9 || (seen & digitBit(v))) return false;
      seen |= digitBit(v);
    }
  }
  return true;
}

int naive(uint8_t* g, const int limit) {
  int i = 0;
  while (i < CELLS && g[i]) i++;
  if (i == CELLS) return 1;
  int n = 0;
  for (int d = 1; d <= 9 && n < limit; d++) {
    bool ok = true;
    for (int k = 0; k < 20 && ok; k++) ok = g[peerOf(i, k)] != d;
    if (!ok) continue;
    g[i] = static_cast<uint8_t>(d);
    n += naive(g, limit - n);
    g[i] = 0;
  }
  return n;
}

// Solves with the full ladder and checks every step against the solution: placements write the
// solution's digit, eliminations never remove it. Returns the techniques used.
std::set<int> soundSolve(const uint8_t* givens, const uint8_t* solution) {
  std::set<int> used;
  Board b;
  EXPECT_TRUE(b.load(givens));
  Step s;
  while (b.left) {
    const Board before = b;
    if (!nextStep(b, Expert, s)) break;
    used.insert(s.tech);
    if (s.place >= 0) {
      EXPECT_EQ(s.digit, solution[s.place]) << techName(s.tech);
    }
    for (int i = 0; i < CELLS; i++) {
      if (before.v[i]) continue;
      const uint16_t gone = before.c[i] & static_cast<uint16_t>(~b.c[i]);
      if (i != s.place) {
        EXPECT_FALSE(gone & digitBit(solution[i])) << techName(s.tech) << " removed the answer at " << i;
      }
    }
  }
  EXPECT_EQ(b.left, 0);
  EXPECT_EQ(std::memcmp(b.v, solution, CELLS), 0);
  return used;
}

}  // namespace

// ---- tables, PRNG, keys ---------------------------------------------------------------------------

TEST(SudokuEngine, TablesAndKeys) {
  EXPECT_EQ(boxOf(rc(4, 4)), 4);
  EXPECT_EQ(boxOf(rc(8, 0)), 6);
  EXPECT_EQ(unitCell(18 + 4, 0), rc(3, 3));
  EXPECT_EQ(unitCell(9 + 2, 5), rc(5, 2));
  for (int i = 0; i < CELLS; i++) {
    int n = 0;
    for (int j = 0; j < CELLS; j++) n += sees(i, j);
    EXPECT_EQ(n, 20);
    for (int k = 0; k < 20; k++) EXPECT_TRUE(sees(i, peerOf(i, k)));
  }
  EXPECT_EQ(popcount9(0x1FF), 9);
  EXPECT_EQ(popcount9(0x0A5), 4);
  for (int t = 0; t < TIER_COUNT; t++) {
    const char* key = tierKey(t);
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(tierFromKey(key, std::strlen(key)), t);
  }
  EXPECT_EQ(tierKey(4), nullptr);
  EXPECT_EQ(tierFromKey("Easy", 4), -1);
  EXPECT_EQ(tierFromKey("eas", 3), -1);
  EXPECT_EQ(techTier(NakedSingle), Easy);
  EXPECT_EQ(techTier(HiddenPair), Medium);
  EXPECT_EQ(techTier(XWing), Hard);
  EXPECT_EQ(techTier(HiddenQuad), Hard);
  EXPECT_EQ(techTier(Trial), Expert);
  // The ladder is tier-major.
  for (int t = 1; t < TECH_COUNT; t++) EXPECT_LE(techTier(static_cast<Tech>(t - 1)), techTier(static_cast<Tech>(t)));
}

TEST(SudokuEngine, SeedsAndFnv) {
  const char* text = "sudoku medium 14";
  EXPECT_EQ(puzzleSeed(Medium, 14), fnv1a(text, std::strlen(text)));
  EXPECT_EQ(puzzleSeed(Easy, 1), 0x2a98df61u);
  EXPECT_EQ(puzzleSeed(Expert, 3), 0x2ed46551u);
  EXPECT_EQ(fnv1a("", 0), FNV_OFFSET);
  uint32_t a = 7, b = 7;
  for (int i = 0; i < 100; i++) EXPECT_EQ(nextRandom(a), nextRandom(b));
  EXPECT_EQ(randomBelow(a, 0), 0u);
  for (int i = 0; i < 1000; i++) EXPECT_LT(randomBelow(a, 9), 9u);
}

// ---- counter and filler ----------------------------------------------------------------------------

TEST(SudokuEngine, FillRandomMakesValidGrids) {
  uint32_t rng = 11;
  for (int s = 0; s < 200; s++) {
    uint8_t g[CELLS];
    ASSERT_TRUE(fillRandom(rng, g));
    EXPECT_TRUE(validComplete(g));
    EXPECT_EQ(countSolutions(g, 2), 1);
  }
}

TEST(SudokuEngine, CounterMatchesNaiveSolver) {
  uint32_t rng = 5;
  int agree = 0, unique = 0, multiple = 0;
  constexpr int TOTAL = 1500;
  for (int s = 0; s < TOTAL; s++) {
    uint8_t sol[CELLS], p[CELLS], q[CELLS];
    ASSERT_TRUE(fillRandom(rng, sol));
    std::memcpy(p, sol, CELLS);
    const int holes = 45 + static_cast<int>(randomBelow(rng, 14));
    for (int h = 0; h < holes; h++) p[randomBelow(rng, CELLS)] = 0;
    std::memcpy(q, p, CELLS);
    uint8_t first[CELLS];
    const int a = countSolutions(p, 2, first);
    const int n = naive(q, 2);
    agree += a == n;
    if (a == 1) {
      unique++;
      EXPECT_EQ(std::memcmp(first, sol, CELLS), 0);
    } else if (a == 2) {
      multiple++;
    }
  }
  EXPECT_EQ(agree, TOTAL);
  EXPECT_GT(unique, 100);  // both outcomes are exercised
  EXPECT_GT(multiple, 100);
  // A clashing grid has none; an empty grid stops at the limit.
  uint8_t bad[CELLS] = {};
  bad[0] = 5;
  bad[1] = 5;
  EXPECT_EQ(countSolutions(bad, 2), 0);
  const uint8_t empty[CELLS] = {};
  EXPECT_EQ(countSolutions(empty, 2), 2);
}

// ---- one fixture per technique ---------------------------------------------------------------------

TEST(SudokuTechniques, FullHouse) {
  Board b = blank();
  for (int c = 0; c < 8; c++) b.place(rc(0, c), c + 1);
  Step s;
  stepAndDiff(b, s);
  EXPECT_EQ(s.tech, FullHouse);
  EXPECT_EQ(s.place, rc(0, 8));
  EXPECT_EQ(s.digit, 9);
  EXPECT_EQ(s.unit, 0);
}

TEST(SudokuTechniques, HiddenSingleBox) {
  Board b = blank();
  removeDigit(b, {rc(0, 1), rc(0, 2), rc(1, 0), rc(1, 1), rc(1, 2), rc(2, 0), rc(2, 1), rc(2, 2)}, 5);
  Step s;
  stepAndDiff(b, s);
  EXPECT_EQ(s.tech, HiddenSingleBox);
  EXPECT_EQ(s.place, rc(0, 0));
  EXPECT_EQ(s.digit, 5);
  EXPECT_EQ(s.unit, 18);
}

TEST(SudokuTechniques, HiddenSingleLine) {
  Board b = blank();
  removeDigitFrom(b, rowExcept(0, {4}), 5);
  Step s;
  stepAndDiff(b, s);
  EXPECT_EQ(s.tech, HiddenSingleLine);
  EXPECT_EQ(s.place, rc(0, 4));
  EXPECT_EQ(s.digit, 5);
  EXPECT_EQ(s.unit, 0);
}

TEST(SudokuTechniques, NakedSingle) {
  Board b = blank();
  b.c[rc(4, 4)] = mask({7});
  Step s;
  stepAndDiff(b, s);
  EXPECT_EQ(s.tech, NakedSingle);
  EXPECT_EQ(s.place, rc(4, 4));
  EXPECT_EQ(s.digit, 7);
  EXPECT_EQ(s.unit, NO_UNIT);
}

TEST(SudokuTechniques, Pointing) {
  Board b = blank();
  removeDigit(b, {rc(0, 2), rc(1, 0), rc(1, 1), rc(1, 2), rc(2, 0), rc(2, 1), rc(2, 2)}, 3);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, Pointing);
  EXPECT_EQ(s.digit, 3);
  EXPECT_EQ(s.unit, 18);
  EXPECT_EQ(s.pattern[0], 0);  // row 1
  EXPECT_EQ(e, cellsTimesDigits(rowExcept(0, {0, 1, 2}), {3}));
}

TEST(SudokuTechniques, Claiming) {
  Board b = blank();
  removeDigitFrom(b, rowExcept(0, {0, 1}), 3);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, Claiming);
  EXPECT_EQ(s.unit, 0);
  EXPECT_EQ(s.pattern[0], 18);
  EXPECT_EQ(e, cellsTimesDigits({rc(1, 0), rc(1, 1), rc(1, 2), rc(2, 0), rc(2, 1), rc(2, 2)}, {3}));
}

TEST(SudokuTechniques, NakedPair) {
  Board b = blank();
  b.c[rc(0, 0)] = mask({1, 2});
  b.c[rc(0, 4)] = mask({1, 2});
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, NakedPair);
  EXPECT_EQ(s.digits, mask({1, 2}));
  EXPECT_EQ(e, cellsTimesDigits(rowExcept(0, {0, 4}), {1, 2}));
}

TEST(SudokuTechniques, HiddenPair) {
  Board b = blank();
  removeDigitFrom(b, rowExcept(0, {0, 4}), 1);
  removeDigitFrom(b, rowExcept(0, {0, 4}), 2);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, HiddenPair);
  EXPECT_EQ(s.digits, mask({1, 2}));
  EXPECT_EQ(e, cellsTimesDigits({rc(0, 0), rc(0, 4)}, {3, 4, 5, 6, 7, 8, 9}));
}

TEST(SudokuTechniques, XWing) {
  Board b = blank();
  removeDigitFrom(b, rowExcept(1, {1, 7}), 4);
  removeDigitFrom(b, rowExcept(4, {1, 7}), 4);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, XWing);
  EXPECT_EQ(s.digit, 4);
  std::vector<int> cells;
  for (int r = 0; r < 9; r++) {
    if (r == 1 || r == 4) continue;
    cells.push_back(rc(r, 1));
    cells.push_back(rc(r, 7));
  }
  EXPECT_EQ(e, cellsTimesDigits(cells, {4}));
}

TEST(SudokuTechniques, NakedTriple) {
  Board b = blank();
  b.c[rc(0, 0)] = mask({1, 2});
  b.c[rc(0, 4)] = mask({2, 3});
  b.c[rc(0, 8)] = mask({1, 3});
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, NakedTriple);
  EXPECT_EQ(e, cellsTimesDigits(rowExcept(0, {0, 4, 8}), {1, 2, 3}));
}

TEST(SudokuTechniques, Swordfish) {
  Board b = blank();
  removeDigitFrom(b, rowExcept(0, {0, 3}), 6);
  removeDigitFrom(b, rowExcept(3, {3, 6}), 6);
  removeDigitFrom(b, rowExcept(6, {0, 6}), 6);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, Swordfish);
  EXPECT_EQ(s.digit, 6);
  std::vector<int> cells;
  for (const int r : {1, 2, 4, 5, 7, 8}) {
    for (const int c : {0, 3, 6}) cells.push_back(rc(r, c));
  }
  EXPECT_EQ(e, cellsTimesDigits(cells, {6}));
}

TEST(SudokuTechniques, HiddenTriple) {
  Board b = blank();
  for (const int d : {1, 2, 3}) removeDigitFrom(b, rowExcept(0, {0, 4, 8}), d);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, HiddenTriple);
  EXPECT_EQ(s.digits, mask({1, 2, 3}));
  EXPECT_EQ(e, cellsTimesDigits({rc(0, 0), rc(0, 4), rc(0, 8)}, {4, 5, 6, 7, 8, 9}));
}

TEST(SudokuTechniques, TurbotFish) {
  // A skyscraper on 5: rows 1 and 5 share column 2; their tops r1c5 and r5c6 see r4c5, r6c5, r2c6
  // and r3c6.
  Board b = blank();
  removeDigitFrom(b, rowExcept(0, {1, 4}), 5);
  removeDigitFrom(b, rowExcept(4, {1, 5}), 5);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, TurbotFish);
  EXPECT_EQ(s.digit, 5);
  EXPECT_EQ(s.pattern[0], rc(0, 4));
  EXPECT_EQ(s.pattern[3], rc(4, 5));
  EXPECT_EQ(e, cellsTimesDigits({rc(3, 4), rc(5, 4), rc(1, 5), rc(2, 5)}, {5}));
}

TEST(SudokuTechniques, XYWing) {
  Board b = blank();
  b.c[rc(0, 0)] = mask({1, 2});
  b.c[rc(0, 4)] = mask({1, 3});
  b.c[rc(4, 0)] = mask({2, 3});
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, XYWing);
  EXPECT_EQ(s.digit, 3);
  EXPECT_EQ(s.pattern[0], rc(0, 0));
  EXPECT_EQ(e, cellsTimesDigits({rc(4, 4)}, {3}));
}

TEST(SudokuTechniques, WWing) {
  Board b = blank();
  b.c[rc(0, 0)] = mask({1, 2});
  b.c[rc(4, 4)] = mask({1, 2});
  removeDigitFrom(b, rowExcept(8, {0, 4}), 1);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, WWing);
  EXPECT_EQ(s.digit, 2);
  EXPECT_EQ(s.unit, 8);
  EXPECT_EQ(e, cellsTimesDigits({rc(0, 4), rc(4, 0)}, {2}));
}

TEST(SudokuTechniques, XYZWing) {
  Board b = blank();
  b.c[rc(0, 0)] = mask({1, 2, 3});
  b.c[rc(0, 4)] = mask({1, 3});
  b.c[rc(1, 1)] = mask({2, 3});
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, XYZWing);
  EXPECT_EQ(s.digit, 3);
  EXPECT_EQ(e, cellsTimesDigits({rc(0, 1), rc(0, 2)}, {3}));
}

TEST(SudokuTechniques, NakedQuad) {
  Board b = blank();
  b.c[rc(0, 0)] = mask({1, 2});
  b.c[rc(0, 2)] = mask({2, 3});
  b.c[rc(0, 4)] = mask({3, 4});
  b.c[rc(0, 6)] = mask({1, 4});
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, NakedQuad);
  EXPECT_EQ(e, cellsTimesDigits(rowExcept(0, {0, 2, 4, 6}), {1, 2, 3, 4}));
}

TEST(SudokuTechniques, Jellyfish) {
  // 7 in rows 1, 4, 7, 8 on three of columns 1, 2, 4, 7 each: no strong link, no smaller fish.
  Board b = blank();
  removeDigitFrom(b, rowExcept(0, {0, 1, 3}), 7);
  removeDigitFrom(b, rowExcept(3, {1, 3, 6}), 7);
  removeDigitFrom(b, rowExcept(6, {0, 1, 6}), 7);
  removeDigitFrom(b, rowExcept(7, {0, 3, 6}), 7);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, Jellyfish);
  EXPECT_EQ(s.digit, 7);
  std::vector<int> cells;
  for (const int r : {1, 2, 4, 5, 8}) {
    for (const int c : {0, 1, 3, 6}) cells.push_back(rc(r, c));
  }
  EXPECT_EQ(e, cellsTimesDigits(cells, {7}));
}

TEST(SudokuTechniques, HiddenQuad) {
  Board b = blank();
  const std::initializer_list<int> cols[4] = {{0, 2, 4}, {2, 4, 6}, {0, 4, 6}, {0, 2, 6}};
  for (int d = 1; d <= 4; d++) removeDigitFrom(b, rowExcept(0, cols[d - 1]), d);
  Step s;
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, HiddenQuad);
  EXPECT_EQ(s.digits, mask({1, 2, 3, 4}));
  EXPECT_EQ(e, cellsTimesDigits({rc(0, 0), rc(0, 2), rc(0, 4), rc(0, 6)}, {5, 6, 7, 8, 9}));
}

TEST(SudokuTechniques, Trial) {
  // An Expert puzzle: the Hard ladder gets stuck, the next step is a trial, and the candidate it
  // removes is never the answer.
  Generated gen;
  ASSERT_TRUE(generate(1234, Expert, gen));
  ASSERT_EQ(gen.tier, Expert);
  Board b;
  ASSERT_TRUE(b.load(gen.givens));
  Step s;
  while (nextStep(b, Hard, s)) {
  }
  ASSERT_GT(b.left, 0);
  const Elims e = stepAndDiff(b, s);
  EXPECT_EQ(s.tech, Trial);
  ASSERT_EQ(e.size(), 1u);
  const auto [cell, digit] = *e.begin();
  EXPECT_EQ(cell, s.pattern[0]);
  EXPECT_EQ(digit, s.digit);
  EXPECT_NE(digit, gen.solution[cell]);
}

TEST(SudokuTechniques, DescribeEveryKind) {
  Board b = blank();
  b.c[rc(0, 0)] = mask({1, 2});
  b.c[rc(0, 4)] = mask({1, 3});
  b.c[rc(4, 0)] = mask({2, 3});
  Step s;
  stepAndDiff(b, s);
  char text[160];
  EXPECT_GT(describe(s, text, sizeof(text)), 0u);
  EXPECT_STREQ(text, "XY-wing: pivot r1c1, pincers r1c5 r5c1: 3 removed from 1 cell(s)");
  Board f = blank();
  for (int c = 0; c < 8; c++) f.place(rc(0, c), c + 1);
  stepAndDiff(f, s);
  describe(s, text, sizeof(text));
  EXPECT_STREQ(text, "Full house: 9 at r1c9 in row 1");
  // Truncation keeps a terminated string.
  char tiny[8];
  EXPECT_EQ(describe(s, tiny, sizeof(tiny)), sizeof(tiny) - 1);
  EXPECT_EQ(std::strlen(tiny), sizeof(tiny) - 1);
}

// ---- the ladder along whole solves -------------------------------------------------------------------

TEST(SudokuEngine, LadderIsSoundAndCoversTheTechniques) {
  std::set<int> used;
  for (int tier = 0; tier < TIER_COUNT; tier++) {
    for (uint32_t s = 0; s < 60; s++) {
      Generated gen;
      ASSERT_TRUE(generate(9000 + s * 7 + tier, tier, gen));
      const std::set<int> u = soundSolve(gen.givens, gen.solution);
      used.insert(u.begin(), u.end());
    }
  }
  // Every technique but the free quads / jellyfish shows up in real puzzles.
  for (int t = 0; t < TECH_COUNT; t++) {
    if (t == NakedQuad || t == Jellyfish || t == HiddenQuad) continue;
    EXPECT_TRUE(used.count(t)) << techName(static_cast<Tech>(t)) << " never used";
  }
}

// ---- the generator ------------------------------------------------------------------------------------

TEST(SudokuGenerator, Seeds0To199EveryTierUniqueAndGradedExactly) {
  for (int tier = 0; tier < TIER_COUNT; tier++) {
    int maxTries = 0;
    int roomy = 0;
    for (uint32_t seed = 0; seed < 200; seed++) {
      Generated gen;
      ASSERT_TRUE(generate(seed, tier, gen)) << tier << " " << seed;
      EXPECT_TRUE(gen.exact) << tier << " " << seed;
      EXPECT_EQ(gen.tier, tier);
      EXPECT_LE(gen.tries, MAX_TRIES);
      if (gen.tries > maxTries) maxTries = gen.tries;
      // Unique, and its solution is the stored one.
      uint8_t first[CELLS];
      ASSERT_EQ(countSolutions(gen.givens, 2, first), 1) << tier << " " << seed;
      EXPECT_EQ(std::memcmp(first, gen.solution, CELLS), 0);
      EXPECT_TRUE(validComplete(gen.solution));
      for (int i = 0; i < CELLS; i++) {
        if (gen.givens[i]) EXPECT_EQ(gen.givens[i], gen.solution[i]);
        EXPECT_EQ(gen.givens[i] != 0, gen.givens[CELLS - 1 - i] != 0);  // 180-degree symmetric
      }
      // Graded exactly: its tier's ladder finishes it, the one below does not.
      const Grade g = grade(gen.givens, Expert);
      EXPECT_TRUE(g.solved);
      EXPECT_EQ(g.tier, tier);
      EXPECT_TRUE(grade(gen.givens, tier).solved);
      if (tier > 0) EXPECT_FALSE(grade(gen.givens, tier - 1).solved) << tier << " " << seed;
      if (tier == Easy) {
        EXPECT_GE(gen.givenCount(), EASY_MIN_GIVENS);
        roomy += gen.givenCount() > EASY_MIN_GIVENS + 1;  // pairs put back by the repair
      }
    }
    EXPECT_LT(maxTries, MAX_TRIES / 2) << "tier " << tier;
    EXPECT_LT(roomy, 30) << "Easy mostly stops at the floor (36-37 givens)";
  }
}

TEST(SudokuGenerator, Deterministic) {
  for (int tier = 0; tier < TIER_COUNT; tier++) {
    Generated a, b;
    ASSERT_TRUE(generate(puzzleSeed(tier, 42), tier, a));
    ASSERT_TRUE(generate(puzzleSeed(tier, 42), tier, b));
    EXPECT_EQ(std::memcmp(a.givens, b.givens, CELLS), 0);
    EXPECT_EQ(std::memcmp(a.solution, b.solution, CELLS), 0);
    EXPECT_EQ(a.tries, b.tries);
  }
}

// The generator prototype's pins (seed 1234, method (c), Easy floor 36, no unique rectangle) and
// the first numbered puzzles of each tier. A ladder or generator change moves these ON PURPOSE:
// saves keep their grids, so puzzles in progress never change; only new numbers would.
TEST(SudokuGenerator, Pins) {
  struct Pin {
    int tier;
    uint32_t seed;
    uint32_t fnv;
    int givens;
    Tech hardest;
  };
  const Pin pins[] = {
      {Easy, 1234, 0x5505AD9Au, 36, HiddenSingleBox},
      {Medium, 1234, 0x7DB44385u, 26, HiddenPair},
      {Hard, 1234, 0x7D59D8BAu, 26, XYWing},
      {Expert, 1234, 0x94927FB0u, 29, Trial},
  };
  for (const Pin& p : pins) {
    Generated gen;
    ASSERT_TRUE(generate(p.seed, p.tier, gen));
    EXPECT_EQ(givensFnv(gen.givens), p.fnv) << p.tier;
    EXPECT_EQ(gen.givenCount(), p.givens) << p.tier;
    EXPECT_EQ(gen.grade.hardest, p.hardest) << p.tier;
  }
  struct Numbered {
    int tier;
    uint32_t number;
    uint32_t fnv;
  };
  const Numbered numbered[] = {
      {Easy, 1, 0x6664abdau}, {Easy, 2, 0x6248328cu}, {Medium, 1, 0x84eb5493u}, {Medium, 2, 0x9f33196fu},
      {Hard, 1, 0x6112887au}, {Hard, 2, 0x3b3fa2deu}, {Expert, 1, 0xde3fe77bu}, {Expert, 2, 0x6409d240u},
  };
  for (const Numbered& n : numbered) {
    Generated gen;
    ASSERT_TRUE(generate(puzzleSeed(n.tier, n.number), n.tier, gen));
    EXPECT_TRUE(gen.exact);
    EXPECT_EQ(givensFnv(gen.givens), n.fnv) << tierKey(n.tier) << " " << n.number;
  }
}

TEST(SudokuGenerator, OutOfTriesKeepsTheLastPuzzleWithItsRealTier) {
  int fallbacks = 0;
  for (uint32_t seed = 0; seed < 40; seed++) {
    for (const int tier : {Medium, Hard, Expert}) {
      Generated gen;
      ASSERT_TRUE(generateWith(seed, tier, 1, 0, gen));
      EXPECT_EQ(gen.tries, 1);
      uint8_t first[CELLS];
      EXPECT_EQ(countSolutions(gen.givens, 2, first), 1);
      EXPECT_EQ(std::memcmp(first, gen.solution, CELLS), 0);
      const Grade g = grade(gen.givens, Expert);
      EXPECT_TRUE(g.solved);
      EXPECT_EQ(gen.tier, g.tier);  // labelled with its REAL tier
      EXPECT_EQ(gen.exact, gen.tier == tier);
      fallbacks += !gen.exact;
    }
  }
  EXPECT_GT(fallbacks, 0);  // the path ran
}

// ---- the hint search ----------------------------------------------------------------------------------

TEST(SudokuEngine, NextPlacementFromGivens) {
  Generated easy;
  ASSERT_TRUE(generate(puzzleSeed(Easy, 1), Easy, easy));
  NextPlacement next;
  ASSERT_TRUE(findNextPlacement(easy.givens, next));
  EXPECT_TRUE(next.singlesOnly);
  EXPECT_TRUE(isSingle(next.place.tech));
  EXPECT_EQ(next.place.digit, easy.solution[next.place.place]);
  EXPECT_EQ(next.eliminations, 0);
  // A full grid and a clashing grid have no next placement.
  EXPECT_FALSE(findNextPlacement(easy.solution, next));
  uint8_t bad[CELLS];
  std::memcpy(bad, easy.givens, CELLS);
  bool clashed = false;
  for (int i = 0; i < CELLS && !clashed; i++) {
    for (int k = 0; k < 20 && bad[i] && !clashed; k++) {
      if (!bad[peerOf(i, k)]) {
        bad[peerOf(i, k)] = bad[i];  // the same digit twice in a unit
        clashed = true;
      }
    }
  }
  ASSERT_TRUE(clashed);
  EXPECT_FALSE(findNextPlacement(bad, next));
  // From an Expert puzzle the first placement needs something harder than a single.
  Generated expert;
  ASSERT_TRUE(generate(puzzleSeed(Expert, 1), Expert, expert));
  Board b;
  ASSERT_TRUE(b.load(expert.givens));
  Step s;
  while (nextStep(b, Hard, s)) {
  }
  uint8_t stuck[CELLS];
  std::memcpy(stuck, b.v, CELLS);
  ASSERT_TRUE(findNextPlacement(stuck, next));
  EXPECT_FALSE(next.singlesOnly);
  EXPECT_EQ(next.hardest.tech, Trial);
  EXPECT_EQ(next.place.digit, expert.solution[next.place.place]);
}
