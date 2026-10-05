#include "SdEngine.h"

#include <cstdio>
#include <cstring>

namespace sd {

const Tables TABLES{};

namespace {

const Tables& T = TABLES;

constexpr uint8_t TECH_TIER[TECH_COUNT] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3};
// Sudoku-Explainer-style ratings x10 (only the score uses them).
constexpr uint8_t TECH_RATING10[TECH_COUNT] = {10, 12, 15, 23, 26, 28, 30, 34, 32, 36,
                                               38, 40, 41, 42, 44, 44, 50, 52, 54, 70};
constexpr const char* TECH_NAME[TECH_COUNT] = {"Full house",
                                               "Hidden single (box)",
                                               "Hidden single (line)",
                                               "Naked single",
                                               "Pointing",
                                               "Claiming",
                                               "Naked pair",
                                               "Hidden pair",
                                               "X-wing",
                                               "Naked triple",
                                               "Swordfish",
                                               "Hidden triple",
                                               "Turbot fish",
                                               "XY-wing",
                                               "W-wing",
                                               "XYZ-wing",
                                               "Naked quad",
                                               "Jellyfish",
                                               "Hidden quad",
                                               "Trial"};
constexpr const char* TIER_KEY[TIER_COUNT] = {"easy", "medium", "hard", "expert"};

constexpr uint32_t FILL_NODE_CAP = 20000;

inline int lowDigit(const uint16_t m) { return __builtin_ctz(m) + 1; }

}  // namespace

const char* tierKey(const int tier) { return tier >= 0 && tier < TIER_COUNT ? TIER_KEY[tier] : nullptr; }

int tierFromKey(const char* key, const size_t len) {
  if (!key) return -1;
  for (int t = 0; t < TIER_COUNT; t++) {
    if (std::strlen(TIER_KEY[t]) == len && std::strncmp(TIER_KEY[t], key, len) == 0) return t;
  }
  return -1;
}

int techTier(const Tech tech) { return tech < TECH_COUNT ? TECH_TIER[tech] : Expert; }

const char* techName(const Tech tech) { return tech < TECH_COUNT ? TECH_NAME[tech] : ""; }

// ---- counter ------------------------------------------------------------------------------------

int countSolutions(const uint8_t* g, const int limit, uint8_t* firstSol, uint32_t* rng, const uint32_t nodeCap) {
  uint16_t rm[9] = {}, cm[9] = {}, bm[9] = {};
  uint8_t val[CELLS], cells[CELLS];
  uint16_t rem[CELLS];
  int ne = 0;
  for (int i = 0; i < CELLS; i++) {
    val[i] = g[i];
    if (g[i]) {
      if (g[i] > 9) return 0;
      const uint16_t bit = digitBit(g[i]);
      if ((rm[T.row[i]] | cm[T.col[i]] | bm[T.box[i]]) & bit) return 0;
      rm[T.row[i]] |= bit;
      cm[T.col[i]] |= bit;
      bm[T.box[i]] |= bit;
    } else {
      cells[ne++] = static_cast<uint8_t>(i);
    }
  }
  if (ne == 0) {
    if (firstSol) std::memcpy(firstSol, val, CELLS);
    return 1;
  }
  const auto freeMask = [&](const int i) -> uint16_t {
    return static_cast<uint16_t>(~(rm[T.row[i]] | cm[T.col[i]] | bm[T.box[i]]) & ALL_DIGITS);
  };
  // Moves the open cell with the fewest options to depth d.
  const auto choose = [&](const int d) {
    int best = d, bestCount = 10;
    for (int j = d; j < ne; j++) {
      const int p = T.pop[freeMask(cells[j])];
      if (p < bestCount) {
        bestCount = p;
        best = j;
        if (p <= 1) break;
      }
    }
    const uint8_t t = cells[d];
    cells[d] = cells[best];
    cells[best] = t;
    rem[d] = freeMask(cells[d]);
  };
  uint32_t nodes = 0;
  int count = 0;
  int d = 0;
  choose(0);
  for (;;) {
    const int i = cells[d];
    if (val[i]) {
      const uint16_t bit = digitBit(val[i]);
      rm[T.row[i]] &= static_cast<uint16_t>(~bit);
      cm[T.col[i]] &= static_cast<uint16_t>(~bit);
      bm[T.box[i]] &= static_cast<uint16_t>(~bit);
      val[i] = 0;
    }
    if (!rem[d]) {
      if (d == 0) break;
      d--;
      continue;
    }
    uint16_t bit;
    if (rng) {
      uint32_t k = randomBelow(*rng, T.pop[rem[d]]);
      uint16_t m = rem[d];
      while (k--) m &= static_cast<uint16_t>(m - 1);
      bit = static_cast<uint16_t>(m & (0u - m));
    } else {
      bit = static_cast<uint16_t>(rem[d] & (0u - rem[d]));
    }
    rem[d] ^= bit;
    val[i] = static_cast<uint8_t>(__builtin_ctz(bit) + 1);
    rm[T.row[i]] |= bit;
    cm[T.col[i]] |= bit;
    bm[T.box[i]] |= bit;
    if (++nodes > nodeCap) return -1;
    if (d + 1 == ne) {
      if (++count == 1 && firstSol) std::memcpy(firstSol, val, CELLS);
      if (count >= limit) break;
      continue;
    }
    d++;
    choose(d);
  }
  return count;
}

bool fillRandom(uint32_t& rng, uint8_t* out) {
  const uint8_t empty[CELLS] = {};
  return countSolutions(empty, 1, out, &rng, FILL_NODE_CAP) == 1;
}

// ---- board --------------------------------------------------------------------------------------

bool Board::load(const uint8_t* g) {
  for (int i = 0; i < CELLS; i++) {
    v[i] = 0;
    c[i] = ALL_DIGITS;
  }
  left = CELLS;
  for (int i = 0; i < CELLS; i++) {
    if (!g[i]) continue;
    if (g[i] > 9 || !(c[i] & digitBit(g[i]))) return false;
    place(i, g[i]);
  }
  return true;
}

void Board::place(const int i, const int d) {
  const uint16_t bit = digitBit(d);
  v[i] = static_cast<uint8_t>(d);
  c[i] = 0;
  left--;
  for (int k = 0; k < 20; k++) c[T.peers[i][k]] &= static_cast<uint16_t>(~bit);
}

bool Board::contradiction() const {
  for (int i = 0; i < CELLS; i++) {
    if (!v[i] && !c[i]) return true;
  }
  for (int u = 0; u < 27; u++) {
    uint16_t have = 0;
    for (int k = 0; k < 9; k++) {
      const int i = T.unit[u][k];
      have |= v[i] ? digitBit(v[i]) : c[i];
    }
    if (have != ALL_DIGITS) return true;
  }
  return false;
}

// ---- techniques ---------------------------------------------------------------------------------

namespace {

inline int elim(Board& b, const int i, const uint16_t m) {
  const uint16_t x = b.c[i] & m;
  if (!x) return 0;
  b.c[i] &= static_cast<uint16_t>(~m);
  return T.pop[x];
}

// The next k-combination of 0..n-1 in a (ascending); false after the last.
bool nextCombo(int* a, const int k, const int n) {
  int j = k - 1;
  while (j >= 0 && a[j] == n - k + j) j--;
  if (j < 0) return false;
  a[j]++;
  for (int t = j + 1; t < k; t++) a[t] = a[t - 1] + 1;
  return true;
}

Step makeStep(const Tech t) {
  Step s;
  s.tech = t;
  return s;
}

bool fullHouse(Board& b, Step& s) {
  for (int u = 0; u < 27; u++) {
    int n = 0, empty = -1;
    uint16_t placed = 0;
    for (int k = 0; k < 9; k++) {
      const int i = T.unit[u][k];
      if (b.v[i]) {
        placed |= digitBit(b.v[i]);
      } else {
        n++;
        empty = i;
      }
    }
    if (n != 1) continue;
    const uint16_t m = static_cast<uint16_t>(ALL_DIGITS & ~placed);
    if (!(b.c[empty] & m)) continue;
    const int d = lowDigit(m);
    b.place(empty, d);
    s = makeStep(FullHouse);
    s.place = static_cast<int8_t>(empty);
    s.digit = static_cast<uint8_t>(d);
    s.unit = static_cast<uint8_t>(u);
    return true;
  }
  return false;
}

bool hiddenSingle(Board& b, Step& s, const int u0, const int u1, const Tech t) {
  for (int u = u0; u < u1; u++) {
    uint16_t once = 0, twice = 0;
    for (int k = 0; k < 9; k++) {
      const uint16_t c = b.c[T.unit[u][k]];
      twice |= once & c;
      once |= c;
    }
    const uint16_t singles = once & static_cast<uint16_t>(~twice);
    if (!singles) continue;
    const int d = lowDigit(singles);
    const uint16_t bit = digitBit(d);
    for (int k = 0; k < 9; k++) {
      const int i = T.unit[u][k];
      if (b.c[i] & bit) {
        b.place(i, d);
        s = makeStep(t);
        s.place = static_cast<int8_t>(i);
        s.digit = static_cast<uint8_t>(d);
        s.unit = static_cast<uint8_t>(u);
        return true;
      }
    }
  }
  return false;
}

bool nakedSingle(Board& b, Step& s) {
  for (int i = 0; i < CELLS; i++) {
    if (!b.v[i] && T.pop[b.c[i]] == 1) {
      const int d = lowDigit(b.c[i]);
      b.place(i, d);
      s = makeStep(NakedSingle);
      s.place = static_cast<int8_t>(i);
      s.digit = static_cast<uint8_t>(d);
      return true;
    }
  }
  return false;
}

bool pointing(Board& b, Step& s) {
  for (int bx = 0; bx < 9; bx++) {
    const int u = 18 + bx;
    for (int d = 1; d <= 9; d++) {
      const uint16_t bit = digitBit(d);
      uint16_t pm = 0;
      for (int k = 0; k < 9; k++) {
        if (b.c[T.unit[u][k]] & bit) pm |= static_cast<uint16_t>(1u << k);
      }
      if (T.pop[pm] < 2) continue;
      for (int g = 0; g < 3; g++) {
        int line = -1;
        if (!(pm & ~(0x7u << (3 * g)))) {
          line = (bx / 3) * 3 + g;  // a row
        } else if (!(pm & ~(0x49u << g))) {
          line = 9 + (bx % 3) * 3 + g;  // a column
        }
        if (line < 0) continue;
        int n = 0;
        for (int k = 0; k < 9; k++) {
          const int i = T.unit[line][k];
          if (T.box[i] != bx) n += elim(b, i, bit);
        }
        if (n) {
          s = makeStep(Pointing);
          s.unit = static_cast<uint8_t>(u);
          s.digit = static_cast<uint8_t>(d);
          s.elims = static_cast<uint8_t>(n);
          s.pattern[0] = static_cast<uint8_t>(line);
          return true;
        }
      }
    }
  }
  return false;
}

bool claiming(Board& b, Step& s) {
  for (int u = 0; u < 18; u++) {
    for (int d = 1; d <= 9; d++) {
      const uint16_t bit = digitBit(d);
      int bx = -1, n = 0;
      bool same = true;
      for (int k = 0; k < 9 && same; k++) {
        const int i = T.unit[u][k];
        if (!(b.c[i] & bit)) continue;
        n++;
        if (bx < 0) {
          bx = T.box[i];
        } else if (bx != T.box[i]) {
          same = false;
        }
      }
      if (!same || n < 2) continue;
      int e = 0;
      for (int k = 0; k < 9; k++) {
        const int i = T.unit[18 + bx][k];
        const bool inLine = u < 9 ? T.row[i] == u : T.col[i] == u - 9;
        if (!inLine) e += elim(b, i, bit);
      }
      if (e) {
        s = makeStep(Claiming);
        s.unit = static_cast<uint8_t>(u);
        s.digit = static_cast<uint8_t>(d);
        s.elims = static_cast<uint8_t>(e);
        s.pattern[0] = static_cast<uint8_t>(18 + bx);
        return true;
      }
    }
  }
  return false;
}

bool nakedSubset(Board& b, Step& s, const int k, const Tech t) {
  for (int u = 0; u < 27; u++) {
    uint8_t cells[9];
    int n = 0, unsolved = 0;
    for (int kk = 0; kk < 9; kk++) {
      const int i = T.unit[u][kk];
      if (b.v[i]) continue;
      unsolved++;
      const int p = T.pop[b.c[i]];
      if (p >= 2 && p <= k) cells[n++] = static_cast<uint8_t>(i);
    }
    if (unsolved <= k || n < k) continue;
    int a[4] = {0, 1, 2, 3};
    do {
      uint16_t all = 0;
      for (int j = 0; j < k; j++) all |= b.c[cells[a[j]]];
      if (T.pop[all] != k) continue;
      int e = 0;
      for (int kk = 0; kk < 9; kk++) {
        const int i = T.unit[u][kk];
        bool in = false;
        for (int j = 0; j < k; j++) in |= (cells[a[j]] == i);
        if (!in) e += elim(b, i, all);
      }
      if (e) {
        s = makeStep(t);
        s.unit = static_cast<uint8_t>(u);
        s.digits = all;
        s.elims = static_cast<uint8_t>(e);
        for (int j = 0; j < k; j++) s.pattern[j] = cells[a[j]];
        return true;
      }
    } while (nextCombo(a, k, n));
  }
  return false;
}

bool hiddenSubset(Board& b, Step& s, const int k, const Tech t) {
  for (int u = 0; u < 27; u++) {
    uint16_t pm[9] = {};  // per digit: the unit positions holding it
    int unsolved = 0;
    for (int kk = 0; kk < 9; kk++) {
      const int i = T.unit[u][kk];
      if (!b.v[i]) unsolved++;
      for (uint16_t m = b.c[i]; m; m &= static_cast<uint16_t>(m - 1)) {
        pm[__builtin_ctz(m)] |= static_cast<uint16_t>(1u << kk);
      }
    }
    if (unsolved <= k) continue;
    uint8_t digits[9];
    int n = 0;
    for (int d = 0; d < 9; d++) {
      if (T.pop[pm[d]] >= 2 && T.pop[pm[d]] <= k) digits[n++] = static_cast<uint8_t>(d);
    }
    if (n < k) continue;
    int a[4] = {0, 1, 2, 3};
    do {
      uint16_t where = 0, keep = 0;
      for (int j = 0; j < k; j++) {
        where |= pm[digits[a[j]]];
        keep |= static_cast<uint16_t>(1u << digits[a[j]]);
      }
      if (T.pop[where] != k) continue;
      int e = 0;
      for (int kk = 0; kk < 9; kk++) {
        if (where & (1u << kk)) e += elim(b, T.unit[u][kk], static_cast<uint16_t>(ALL_DIGITS & ~keep));
      }
      if (e) {
        s = makeStep(t);
        s.unit = static_cast<uint8_t>(u);
        s.digits = keep;
        s.elims = static_cast<uint8_t>(e);
        int j = 0;
        for (int kk = 0; kk < 9 && j < 4; kk++) {
          if (where & (1u << kk)) s.pattern[j++] = T.unit[u][kk];
        }
        return true;
      }
    } while (nextCombo(a, k, n));
  }
  return false;
}

bool fish(Board& b, Step& s, const int k, const Tech t) {
  for (int d = 1; d <= 9; d++) {
    const uint16_t bit = digitBit(d);
    for (int orient = 0; orient < 2; orient++) {  // base rows, then base columns
      uint16_t m[9];
      uint8_t lines[9];
      int n = 0;
      for (int l = 0; l < 9; l++) {
        uint16_t mm = 0;
        for (int x = 0; x < 9; x++) {
          const int i = orient == 0 ? l * 9 + x : x * 9 + l;
          if (b.c[i] & bit) mm |= static_cast<uint16_t>(1u << x);
        }
        m[l] = mm;
        if (T.pop[mm] >= 2 && T.pop[mm] <= k) lines[n++] = static_cast<uint8_t>(l);
      }
      if (n < k) continue;
      int a[4] = {0, 1, 2, 3};
      do {
        uint16_t cover = 0, base = 0;
        for (int j = 0; j < k; j++) {
          cover |= m[lines[a[j]]];
          base |= static_cast<uint16_t>(1u << lines[a[j]]);
        }
        if (T.pop[cover] != k) continue;
        int e = 0;
        for (int x = 0; x < 9; x++) {
          if (!(cover & (1u << x))) continue;
          for (int l = 0; l < 9; l++) {
            if (base & (1u << l)) continue;
            e += elim(b, orient == 0 ? l * 9 + x : x * 9 + l, bit);
          }
        }
        if (e) {
          s = makeStep(t);
          s.digit = static_cast<uint8_t>(d);
          s.elims = static_cast<uint8_t>(e);
          for (int j = 0; j < k; j++) s.pattern[j] = static_cast<uint8_t>(orient * 9 + lines[a[j]]);
          return true;
        }
      } while (nextCombo(a, k, n));
    }
  }
  return false;
}

// Turbot fish (skyscraper / 2-string kite / turbot): two strong links on one digit joined by a
// weak link, p=q-r=t: p or t holds the digit, so a cell seeing both loses it.
bool turbotFish(Board& b, Step& s) {
  for (int d = 1; d <= 9; d++) {
    const uint16_t bit = digitBit(d);
    uint8_t first[27], second[27];
    int n = 0;
    for (int u = 0; u < 27; u++) {
      int cnt = 0, a = -1, c = -1;
      for (int k = 0; k < 9 && cnt <= 2; k++) {
        const int i = T.unit[u][k];
        if (!(b.c[i] & bit)) continue;
        if (++cnt == 1) {
          a = i;
        } else {
          c = i;
        }
      }
      if (cnt == 2) {
        first[n] = static_cast<uint8_t>(a);
        second[n] = static_cast<uint8_t>(c);
        n++;
      }
    }
    for (int x = 0; x < n; x++) {
      for (int y = 0; y < n; y++) {
        if (x == y) continue;
        for (int ox = 0; ox < 2; ox++) {
          for (int oy = 0; oy < 2; oy++) {
            const int p = ox ? second[x] : first[x], q = ox ? first[x] : second[x];
            const int r = oy ? second[y] : first[y], t = oy ? first[y] : second[y];
            if (p == r || p == t || q == r || q == t || !sees(q, r)) continue;
            int e = 0;
            for (int j = 0; j < CELLS; j++) {
              if ((b.c[j] & bit) && j != p && j != q && j != r && j != t && sees(j, p) && sees(j, t)) {
                e += elim(b, j, bit);
              }
            }
            if (e) {
              s = makeStep(TurbotFish);
              s.digit = static_cast<uint8_t>(d);
              s.elims = static_cast<uint8_t>(e);
              s.pattern[0] = static_cast<uint8_t>(p);
              s.pattern[1] = static_cast<uint8_t>(q);
              s.pattern[2] = static_cast<uint8_t>(r);
              s.pattern[3] = static_cast<uint8_t>(t);
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

bool xyWing(Board& b, Step& s) {
  for (int i = 0; i < CELLS; i++) {
    if (b.v[i] || T.pop[b.c[i]] != 2) continue;
    const uint16_t ci = b.c[i];
    for (int pa = 0; pa < 20; pa++) {
      const int a = T.peers[i][pa];
      const uint16_t ca = b.c[a];
      if (T.pop[ca] != 2 || T.pop[ca & ci] != 1) continue;
      const uint16_t x = ca & ci;
      const uint16_t z = ca & static_cast<uint16_t>(~ci);
      const uint16_t want = static_cast<uint16_t>((ci & ~x) | z);
      for (int pb = 0; pb < 20; pb++) {
        const int bb = T.peers[i][pb];
        if (bb == a || b.c[bb] != want) continue;
        int e = 0;
        for (int j = 0; j < CELLS; j++) {
          if (j != a && j != bb && j != i && sees(j, a) && sees(j, bb)) e += elim(b, j, z);
        }
        if (e) {
          s = makeStep(XYWing);
          s.digit = static_cast<uint8_t>(lowDigit(z));
          s.elims = static_cast<uint8_t>(e);
          s.pattern[0] = static_cast<uint8_t>(i);
          s.pattern[1] = static_cast<uint8_t>(a);
          s.pattern[2] = static_cast<uint8_t>(bb);
          return true;
        }
      }
    }
  }
  return false;
}

// W-wing: twin bivalue cells {x,y} that do not see each other, bridged by a strong link on x
// (p sees one twin, q the other): one twin is y, so cells seeing both lose y.
bool wWing(Board& b, Step& s) {
  for (int a = 0; a < CELLS; a++) {
    if (T.pop[b.c[a]] != 2) continue;
    for (int bb = a + 1; bb < CELLS; bb++) {
      if (b.c[bb] != b.c[a] || sees(a, bb)) continue;
      for (uint16_t xm = b.c[a]; xm; xm &= static_cast<uint16_t>(xm - 1)) {
        const uint16_t x = static_cast<uint16_t>(xm & (0u - xm));
        const uint16_t y = static_cast<uint16_t>(b.c[a] & ~x);
        for (int u = 0; u < 27; u++) {
          int cnt = 0, p = -1, q = -1;
          for (int k = 0; k < 9 && cnt <= 2; k++) {
            const int i = T.unit[u][k];
            if (!(b.c[i] & x)) continue;
            if (++cnt == 1) {
              p = i;
            } else {
              q = i;
            }
          }
          if (cnt != 2 || p == a || p == bb || q == a || q == bb) continue;
          if (!((sees(p, a) && sees(q, bb)) || (sees(p, bb) && sees(q, a)))) continue;
          int e = 0;
          for (int j = 0; j < CELLS; j++) {
            if ((b.c[j] & y) && j != a && j != bb && sees(j, a) && sees(j, bb)) e += elim(b, j, y);
          }
          if (e) {
            s = makeStep(WWing);
            s.digit = static_cast<uint8_t>(lowDigit(y));
            s.elims = static_cast<uint8_t>(e);
            s.unit = static_cast<uint8_t>(u);
            s.pattern[0] = static_cast<uint8_t>(a);
            s.pattern[1] = static_cast<uint8_t>(bb);
            s.pattern[2] = static_cast<uint8_t>(p);
            s.pattern[3] = static_cast<uint8_t>(q);
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool xyzWing(Board& b, Step& s) {
  for (int i = 0; i < CELLS; i++) {
    if (b.v[i] || T.pop[b.c[i]] != 3) continue;
    const uint16_t ci = b.c[i];
    for (int pa = 0; pa < 20; pa++) {
      const int a = T.peers[i][pa];
      const uint16_t ca = b.c[a];
      if (T.pop[ca] != 2 || (ca & ~ci)) continue;
      for (int pb = pa + 1; pb < 20; pb++) {
        const int bb = T.peers[i][pb];
        const uint16_t cb = b.c[bb];
        if (T.pop[cb] != 2 || (cb & ~ci) || cb == ca || (ca | cb) != ci) continue;
        const uint16_t z = ca & cb;
        int e = 0;
        for (int j = 0; j < CELLS; j++) {
          if (j != i && j != a && j != bb && sees(j, i) && sees(j, a) && sees(j, bb)) e += elim(b, j, z);
        }
        if (e) {
          s = makeStep(XYZWing);
          s.digit = static_cast<uint8_t>(lowDigit(z));
          s.elims = static_cast<uint8_t>(e);
          s.pattern[0] = static_cast<uint8_t>(i);
          s.pattern[1] = static_cast<uint8_t>(a);
          s.pattern[2] = static_cast<uint8_t>(bb);
          return true;
        }
      }
    }
  }
  return false;
}

// Singles to a fixpoint; false on a contradiction.
bool propagateSingles(Board& t) {
  for (;;) {
    bool changed = false;
    for (int i = 0; i < CELLS; i++) {
      if (t.v[i]) continue;
      if (!t.c[i]) return false;
      if (T.pop[t.c[i]] == 1) {
        t.place(i, lowDigit(t.c[i]));
        changed = true;
      }
    }
    for (int u = 0; u < 27; u++) {
      uint16_t once = 0, twice = 0, placed = 0;
      for (int k = 0; k < 9; k++) {
        const int i = T.unit[u][k];
        if (t.v[i]) placed |= digitBit(t.v[i]);
        twice |= once & t.c[i];
        once |= t.c[i];
      }
      if ((placed | once) != ALL_DIGITS) return false;
      uint16_t singles = once & static_cast<uint16_t>(~twice);
      while (singles) {
        const int d = lowDigit(singles);
        singles &= static_cast<uint16_t>(singles - 1);
        const uint16_t bit = digitBit(d);
        int at = -1;
        for (int k = 0; k < 9 && at < 0; k++) {
          if (t.c[T.unit[u][k]] & bit) at = T.unit[u][k];
        }
        if (at < 0) return false;  // its only cell took another digit
        t.place(at, d);
        changed = true;
      }
    }
    if (!t.left || !changed) return true;
  }
}

bool trial(Board& b, Step& s) {
  for (int p = 2; p <= 9; p++) {
    for (int i = 0; i < CELLS; i++) {
      if (b.v[i] || T.pop[b.c[i]] != p) continue;
      for (uint16_t m = b.c[i]; m; m &= static_cast<uint16_t>(m - 1)) {
        const int d = __builtin_ctz(m) + 1;
        Board t = b;
        t.place(i, d);
        if (!propagateSingles(t)) {
          b.c[i] &= static_cast<uint16_t>(~digitBit(d));
          s = makeStep(Trial);
          s.pattern[0] = static_cast<uint8_t>(i);
          s.digit = static_cast<uint8_t>(d);
          s.elims = 1;
          return true;
        }
      }
    }
  }
  return false;
}

bool apply(Board& b, const Tech t, Step& s) {
  switch (t) {
    case FullHouse:
      return fullHouse(b, s);
    case HiddenSingleBox:
      return hiddenSingle(b, s, 18, 27, HiddenSingleBox);
    case HiddenSingleLine:
      return hiddenSingle(b, s, 0, 18, HiddenSingleLine);
    case NakedSingle:
      return nakedSingle(b, s);
    case Pointing:
      return pointing(b, s);
    case Claiming:
      return claiming(b, s);
    case NakedPair:
      return nakedSubset(b, s, 2, NakedPair);
    case HiddenPair:
      return hiddenSubset(b, s, 2, HiddenPair);
    case XWing:
      return fish(b, s, 2, XWing);
    case NakedTriple:
      return nakedSubset(b, s, 3, NakedTriple);
    case Swordfish:
      return fish(b, s, 3, Swordfish);
    case HiddenTriple:
      return hiddenSubset(b, s, 3, HiddenTriple);
    case TurbotFish:
      return turbotFish(b, s);
    case XYWing:
      return xyWing(b, s);
    case WWing:
      return wWing(b, s);
    case XYZWing:
      return xyzWing(b, s);
    case NakedQuad:
      return nakedSubset(b, s, 4, NakedQuad);
    case Jellyfish:
      return fish(b, s, 4, Jellyfish);
    case HiddenQuad:
      return hiddenSubset(b, s, 4, HiddenQuad);
    case Trial:
      return trial(b, s);
  }
  return false;
}

}  // namespace

bool nextStep(Board& b, const int maxTier, Step& out) {
  for (int t = 0; t < TECH_COUNT; t++) {
    if (TECH_TIER[t] > maxTier) break;  // the ladder is tier-major
    if (apply(b, static_cast<Tech>(t), out)) return true;
  }
  return false;
}

Grade grade(const uint8_t* puzzle, const int maxTier) {
  Grade g;
  Board b;
  if (!b.load(puzzle)) return g;
  while (b.left) {
    Step s;
    if (!nextStep(b, maxTier, s)) break;
    g.steps++;
    if (s.tech > g.hardest) g.hardest = s.tech;
    if (TECH_TIER[s.tech] > 0) g.score = static_cast<uint16_t>(g.score + TECH_RATING10[s.tech]);
  }
  g.solved = b.left == 0;
  g.tier = TECH_TIER[g.hardest];
  return g;
}

// ---- generator ----------------------------------------------------------------------------------

int Generated::givenCount() const {
  int n = 0;
  for (const uint8_t g : givens) n += g != 0;
  return n;
}

bool generateWith(const uint32_t seed, const int tier, const int maxTries, const int minGivens, Generated& out) {
  uint32_t rng = seed;
  bool haveLast = false;
  out.tries = 0;
  out.exact = false;
  for (int attempt = 0; attempt < maxTries; attempt++) {
    out.tries++;
    uint8_t sol[CELLS], p[CELLS];
    if (!fillRandom(rng, sol)) continue;
    std::memcpy(p, sol, CELLS);
    // The 41 orbits of the 180-degree turn (40 pairs and the centre), shuffled.
    uint8_t order[41];
    for (int k = 0; k < 41; k++) order[k] = static_cast<uint8_t>(k);
    for (int k = 40; k > 0; k--) {
      const int j = static_cast<int>(randomBelow(rng, static_cast<uint32_t>(k + 1)));
      const uint8_t t = order[k];
      order[k] = order[j];
      order[j] = t;
    }
    int givens = CELLS;
    uint8_t removed[41];
    int nRemoved = 0;
    for (int k = 0; k < 41; k++) {
      const int a = order[k], b = CELLS - 1 - order[k];
      const int size = a == b ? 1 : 2;
      if (minGivens && givens - size < minGivens) continue;
      const uint8_t va = p[a], vb = p[b];
      p[a] = 0;
      p[b] = 0;
      if (countSolutions(p, 2) == 1) {
        givens -= size;
        removed[nRemoved++] = static_cast<uint8_t>(a);
      } else {
        p[a] = va;
        p[b] = vb;
      }
    }
    Grade g = grade(p, Expert);
    if (tier < Expert && (!g.solved || g.tier > tier)) {
      // Too hard: put removed pairs back (newest first) until the tier's ladder gets through,
      // then take it only if it did not become too easy.
      while (nRemoved > 0) {
        const int a = removed[--nRemoved];
        p[a] = sol[a];
        p[CELLS - 1 - a] = sol[CELLS - 1 - a];
        if (grade(p, tier).solved) break;
      }
      g = grade(p, Expert);
    }
    if (!g.solved) continue;  // beyond the ladder: never kept
    // Counter-minimal and re-filled puzzles are unique by construction; this guards the code.
    uint8_t check[CELLS];
    if (countSolutions(p, 2, check) != 1 || std::memcmp(check, sol, CELLS) != 0) continue;
    std::memcpy(out.givens, p, CELLS);
    std::memcpy(out.solution, sol, CELLS);
    out.grade = g;
    out.tier = g.tier;
    haveLast = true;
    if (g.tier == tier) {
      out.exact = true;
      return true;
    }
  }
  return haveLast;
}

bool generate(const uint32_t seed, const int tier, Generated& out) {
  return generateWith(seed, tier, MAX_TRIES, tier == Easy ? EASY_MIN_GIVENS : 0, out);
}

uint32_t fnv1a(const void* data, const size_t len, uint32_t h) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

uint32_t puzzleSeed(const int tier, const uint32_t number) {
  char text[40];
  const char* key = tierKey(tier);
  const int n = std::snprintf(text, sizeof(text), "sudoku %s %lu", key ? key : "?", static_cast<unsigned long>(number));
  return fnv1a(text, n > 0 ? static_cast<size_t>(n) : 0);
}

// ---- hints --------------------------------------------------------------------------------------

bool findNextPlacement(const uint8_t* grid, NextPlacement& out) {
  out = NextPlacement{};
  Board b;
  if (!b.load(grid) || b.left == 0) return false;
  // Every step either places a digit or removes a candidate, so this ends.
  for (int guard = 0; guard < CELLS * 9; guard++) {
    Step s;
    if (!nextStep(b, Expert, s)) return false;
    if (s.place >= 0) {
      out.place = s;
      if (out.singlesOnly) out.hardest = s;
      return true;
    }
    if (out.singlesOnly || s.tech > out.hardest.tech) out.hardest = s;
    out.singlesOnly = false;
    if (out.eliminations < 255) out.eliminations++;
  }
  return false;
}

namespace {

void unitName(const int u, char* out, const size_t cap) {
  if (u < 9) {
    std::snprintf(out, cap, "row %d", u + 1);
  } else if (u < 18) {
    std::snprintf(out, cap, "column %d", u - 8);
  } else {
    std::snprintf(out, cap, "box %d", u - 17);
  }
}

}  // namespace

size_t describe(const Step& s, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  char un[16] = "";
  char pn[16] = "";
  if (s.unit != NO_UNIT) unitName(s.unit, un, sizeof(un));
  const char* name = techName(s.tech);
  int n = 0;
  if (s.place >= 0) {
    n = std::snprintf(out, cap, "%s: %d at r%dc%d%s%s", name, s.digit, s.place / 9 + 1, s.place % 9 + 1,
                      un[0] ? " in " : "", un);
  } else if (s.tech == Pointing || s.tech == Claiming) {
    unitName(s.pattern[0], pn, sizeof(pn));
    n = std::snprintf(out, cap, "%s: the %ds of %s lie in %s; %d removed", name, s.digit, un, pn, s.elims);
  } else if (s.tech == Trial) {
    n = std::snprintf(out, cap, "%s: %d at r%dc%d breaks the grid; removed", name, s.digit, s.pattern[0] / 9 + 1,
                      s.pattern[0] % 9 + 1);
  } else if (s.tech == XWing || s.tech == Swordfish || s.tech == Jellyfish) {
    char lines[48] = "";
    size_t ln = 0;
    for (int j = 0; j < 4 && s.pattern[j] != 0xFF; j++) {
      char one[16];
      unitName(s.pattern[j], one, sizeof(one));
      const int k = std::snprintf(lines + ln, sizeof(lines) - ln, "%s%s", j ? ", " : "", one);
      if (k < 0 || static_cast<size_t>(k) >= sizeof(lines) - ln) break;
      ln += static_cast<size_t>(k);
    }
    n = std::snprintf(out, cap, "%s on %d (%s): %d removed", name, s.digit, lines, s.elims);
  } else if (s.tech == TurbotFish || s.tech == WWing) {
    const int other = s.tech == WWing ? s.pattern[1] : s.pattern[3];
    n = std::snprintf(out, cap, "%s on %d: r%dc%d or r%dc%d is %d, so %d removed from cells seeing both", name, s.digit,
                      s.pattern[0] / 9 + 1, s.pattern[0] % 9 + 1, other / 9 + 1, other % 9 + 1, s.digit, s.elims);
  } else if (s.tech == XYWing || s.tech == XYZWing) {
    n = std::snprintf(out, cap, "%s: pivot r%dc%d, pincers r%dc%d r%dc%d: %d removed from %d cell(s)", name,
                      s.pattern[0] / 9 + 1, s.pattern[0] % 9 + 1, s.pattern[1] / 9 + 1, s.pattern[1] % 9 + 1,
                      s.pattern[2] / 9 + 1, s.pattern[2] % 9 + 1, s.digit, s.elims);
  } else {
    char digits[12] = "";
    int dn = 0;
    for (int d = 0; d < 9; d++) {
      if (s.digits & (1u << d)) digits[dn++] = static_cast<char>('1' + d);
    }
    n = std::snprintf(out, cap, "%s {%s} in %s: %d candidate(s) removed", name, digits, un, s.elims);
  }
  if (n < 0) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : cap - 1;
}

}  // namespace sd
