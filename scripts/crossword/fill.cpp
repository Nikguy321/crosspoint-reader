// fill - deterministic American-style crossword fill (Mac tool, never on the device).
//
// Backtracking with MRV (fewest candidates first) and forward checking over per-(position, letter) bitsets.
// Candidate order per slot is biased to HIGH score: words are grouped into bands of 10 of their ordering key (the
// score, plus 10 for a hand-reviewed word) highest band first, and shuffled inside a band by a splitmix32 stream seeded
// from --seed, so a seed always gives the same fill. No word appears twice in one grid. A node budget bounds the
// search.
//
// Usage:
//   fill --words words.txt --grid "#....|.....|.....|.....|....#" --seed 7 [--budget 2000000] [--min-score 50]
//        [--exclude excl.txt] [--ac 1|0]
// words.txt: "WORD score [key]" per line (prep.py); key (default = score) orders candidates by bands of 10. excl.txt:
// one WORD per line (not allowed in this fill). Output (stdout), one line, either
//   OK <nodes> <ms> <rows joined by |> <minScore> <meanScore> WORD:score,...  (across slots, then down)
// or
//   FAIL <nodes> <ms> <budget|exhausted>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
using namespace std;

static inline uint32_t nextRandom(uint32_t& s) {
  s += 0x9E3779B9u;
  uint32_t z = s;
  z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
  z = (z ^ (z >> 13)) * 0xC2B2AE35u;
  return z ^ (z >> 16);
}
static inline uint32_t randomBelow(uint32_t& s, uint32_t n) {
  return n ? (uint32_t)(((uint64_t)nextRandom(s) * n) >> 32) : 0;
}

struct Lex {  // all words of one length, index order = preference order for this run
  int L = 0, n = 0, W = 0;
  vector<string> w;
  vector<int> score;
  vector<uint32_t> bits;  // [(p*26+c)*W + k]
  vector<uint32_t> all;
  void build() {
    W = (n + 31) / 32;
    bits.assign((size_t)L * 26 * W, 0);
    all.assign(W, 0);
    for (int i = 0; i < n; i++) {
      all[i >> 5] |= 1u << (i & 31);
      for (int p = 0; p < L; p++) bits[((size_t)p * 26 + (w[i][p] - 'A')) * W + (i >> 5)] |= 1u << (i & 31);
    }
  }
};

struct Slot {
  int len;
  int cells[15];
};

struct Filler {
  int R = 0, C = 0;
  vector<char> blk, g;
  vector<Slot> slots;
  Lex* lex = nullptr;
  vector<int> chosen;
  long nodes = 0, budget = 0;
  vector<vector<uint32_t>> scratch;

  void cand(int s, uint32_t* out) const {
    const Slot& S = slots[s];
    const Lex& X = lex[S.len];
    memcpy(out, X.all.data(), X.W * 4);
    for (int p = 0; p < S.len; p++) {
      char ch = g[S.cells[p]];
      if (!ch) continue;
      const uint32_t* b = &X.bits[((size_t)p * 26 + (ch - 'A')) * X.W];
      for (int k = 0; k < X.W; k++) out[k] &= b[k];
    }
  }
  static int count(const uint32_t* v, int W) {
    int t = 0;
    for (int k = 0; k < W; k++) t += __builtin_popcount(v[k]);
    return t;
  }
  bool used(int s, int idx) const {
    for (size_t t = 0; t < slots.size(); t++)
      if ((int)t != s && chosen[t] >= 0 && slots[t].len == slots[s].len && chosen[t] == idx) return true;
    return false;
  }
  // Per node: candidate bitsets for every open slot, narrowed to a fixpoint by per-cell letter domains (arc
  // consistency over the crossings) when ac is on; then MRV picks the open slot with the fewest candidates.
  bool ac = true;
  vector<vector<uint32_t>> cur;  // [slot] candidate bitset at this node (recomputed per node)
  vector<uint32_t> dom;          // [cell] 26-bit letter domain
  uint32_t letterMask(int s, int p, const uint32_t* v) const {
    const Lex& X = lex[slots[s].len];
    uint32_t m = 0;
    int cnt = count(v, X.W);
    if (cnt * 2 < 26 * X.W) {  // few words: OR their letters
      for (int k = 0; k < X.W; k++) {
        uint32_t x = v[k];
        while (x) {
          int i = (k << 5) + __builtin_ctz(x);
          x &= x - 1;
          m |= 1u << (X.w[i][p] - 'A');
        }
      }
    } else {
      for (int c = 0; c < 26; c++) {
        const uint32_t* bb = &X.bits[((size_t)p * 26 + c) * X.W];
        for (int k = 0; k < X.W; k++)
          if (v[k] & bb[k]) {
            m |= 1u << c;
            break;
          }
      }
    }
    return m;
  }
  bool narrow() {  // returns false on a wipe-out
    for (size_t s = 0; s < slots.size(); s++)
      if (chosen[s] < 0) cand((int)s, cur[s].data());
    if (!ac) {
      for (size_t s = 0; s < slots.size(); s++)
        if (chosen[s] < 0 && !count(cur[s].data(), lex[slots[s].len].W)) return false;
      return true;
    }
    for (int iter = 0; iter < 4; iter++) {
      std::fill(dom.begin(), dom.end(), 0x3FFFFFFu);
      for (size_t s = 0; s < slots.size(); s++) {
        if (chosen[s] >= 0) continue;
        for (int p = 0; p < slots[s].len; p++) {
          int cell = slots[s].cells[p];
          if (g[cell]) continue;
          dom[cell] &= letterMask((int)s, p, cur[s].data());
          if (!dom[cell]) return false;
        }
      }
      bool changed = false;
      for (size_t s = 0; s < slots.size(); s++) {
        if (chosen[s] >= 0) continue;
        const Lex& X = lex[slots[s].len];
        uint32_t* v = cur[s].data();
        for (int p = 0; p < slots[s].len; p++) {
          int cell = slots[s].cells[p];
          if (g[cell] || dom[cell] == 0x3FFFFFFu) continue;
          uint32_t have = letterMask((int)s, p, v);
          if ((have & ~dom[cell]) == 0) continue;
          uint32_t* acc = tmpMask.data();
          memset(acc, 0, X.W * 4);
          for (int c = 0; c < 26; c++)
            if (dom[cell] >> c & 1) {
              const uint32_t* bb = &X.bits[((size_t)p * 26 + c) * X.W];
              for (int k = 0; k < X.W; k++) acc[k] |= bb[k];
            }
          for (int k = 0; k < X.W; k++) v[k] &= acc[k];
          changed = true;
        }
        if (!count(v, X.W)) return false;
      }
      if (!changed) break;
    }
    return true;
  }
  vector<uint32_t> tmpMask;
  bool rec(int depth) {
    if (++nodes > budget) return false;
    if (!narrow()) return false;
    int best = -1, bestCount = 1 << 30;
    for (size_t s = 0; s < slots.size(); s++) {
      if (chosen[s] >= 0) continue;
      int c = count(cur[s].data(), lex[slots[s].len].W);
      if (c < bestCount) {
        bestCount = c;
        best = (int)s;
      }
    }
    if (best < 0) return true;
    const Lex& X = lex[slots[best].len];
    vector<uint32_t>& v = scratch[depth + 1];
    memcpy(v.data(), cur[best].data(), X.W * 4);
    char save[15];
    for (int i = 0; i < X.n; i++) {  // index order = score band order (already seeded-shuffled within a band)
      if (!(v[i >> 5] & (1u << (i & 31)))) continue;
      if (used(best, i)) continue;
      for (int p = 0; p < slots[best].len; p++) {
        save[p] = g[slots[best].cells[p]];
        g[slots[best].cells[p]] = X.w[i][p];
      }
      chosen[best] = i;
      if (rec(depth + 1)) return true;
      chosen[best] = -1;
      for (int p = 0; p < slots[best].len; p++) g[slots[best].cells[p]] = save[p];
      if (nodes > budget) return false;
    }
    return false;
  }
};

int main(int argc, char** argv) {
  string wordsPath, gridArg, exclPath;
  uint32_t seed = 1;
  long budget = 2000000;
  int minScore = 50;
  bool acOn = true;
  for (int i = 1; i + 1 < argc; i += 2) {
    string k = argv[i], v = argv[i + 1];
    if (k == "--words")
      wordsPath = v;
    else if (k == "--grid")
      gridArg = v;
    else if (k == "--seed")
      seed = (uint32_t)strtoul(v.c_str(), nullptr, 10);
    else if (k == "--budget")
      budget = atol(v.c_str());
    else if (k == "--min-score")
      minScore = atoi(v.c_str());
    else if (k == "--exclude")
      exclPath = v;
    else if (k == "--ac")
      acOn = atoi(v.c_str()) != 0;
    else {
      fprintf(stderr, "unknown arg %s\n", k.c_str());
      return 1;
    }
  }
  if (wordsPath.empty() || gridArg.empty()) {
    fprintf(stderr, "need --words and --grid\n");
    return 1;
  }
  auto t0 = chrono::steady_clock::now();

  vector<string> rows;
  {
    stringstream ss(gridArg);
    string r;
    while (getline(ss, r, '|'))
      if (!r.empty()) rows.push_back(r);
  }
  Filler F;
  F.R = (int)rows.size();
  F.C = (int)rows[0].size();
  F.blk.assign(F.R * F.C, 0);
  for (int r = 0; r < F.R; r++) {
    if ((int)rows[r].size() != F.C) {
      fprintf(stderr, "ragged grid\n");
      return 1;
    }
    for (int c = 0; c < F.C; c++) F.blk[r * F.C + c] = rows[r][c] == '#';
  }
  for (int dir = 0; dir < 2; dir++)
    for (int a = 0; a < (dir ? F.C : F.R); a++) {
      int lim = dir ? F.R : F.C, b = 0;
      auto cell = [&](int k) { return dir ? k * F.C + a : a * F.C + k; };
      while (b < lim) {
        if (F.blk[cell(b)]) {
          b++;
          continue;
        }
        int e = b;
        while (e < lim && !F.blk[cell(e)]) e++;
        if (e - b >= 2) {
          Slot S;
          S.len = e - b;
          for (int k = b; k < e; k++) S.cells[k - b] = cell(k);
          F.slots.push_back(S);
        }
        b = e;
      }
    }

  unordered_set<string> excl;
  if (!exclPath.empty()) {
    ifstream f(exclPath);
    string s;
    while (f >> s) excl.insert(s);
  }

  // Load words, keep those >= minScore and not excluded; per length: bands of 10 (high first), seeded shuffle inside.
  static Lex lex[16];
  {
    vector<vector<pair<string, int>>> byLen(16);
    ifstream f(wordsPath);
    string line;
    while (getline(f, line)) {
      istringstream ls(line);
      string w;
      int s = 0, key = -1;
      if (!(ls >> w >> s)) continue;
      if (!(ls >> key)) key = s;
      if ((int)w.size() < 2 || (int)w.size() > 15 || s < minScore || excl.count(w)) continue;
      byLen[w.size()].push_back({w, s * 1000 + key});  // packed: real score, ordering key
    }
    uint32_t rng = seed * 2654435761u + 0x5EEDu;
    for (int L = 0; L < 16; L++) {
      auto& v = byLen[L];
      auto band = [](int packed) { return (packed % 1000) / 10; };
      sort(v.begin(), v.end(), [&](const pair<string, int>& a, const pair<string, int>& b) {
        if (band(a.second) != band(b.second)) return band(a.second) > band(b.second);
        return a.first < b.first;
      });
      for (size_t i = 0; i < v.size();) {  // Fisher-Yates inside each band
        size_t j = i;
        while (j < v.size() && band(v[j].second) == band(v[i].second)) j++;
        for (size_t k = j - 1; k > i; k--) swap(v[k], v[i + randomBelow(rng, (uint32_t)(k - i + 1))]);
        i = j;
      }
      lex[L].L = L;
      lex[L].n = (int)v.size();
      for (auto& p : v) {
        lex[L].w.push_back(p.first);
        lex[L].score.push_back(p.second / 1000);
      }
      lex[L].build();
    }
  }
  size_t maxW = 1;
  for (int L = 0; L < 16; L++) maxW = max(maxW, (size_t)lex[L].W);
  F.lex = lex;
  F.scratch.assign(F.slots.size() + 2, vector<uint32_t>(maxW));
  F.cur.assign(F.slots.size(), vector<uint32_t>(maxW));
  F.tmpMask.assign(maxW, 0);
  F.dom.assign(F.R * F.C, 0);
  F.ac = acOn;
  F.g.assign(F.R * F.C, 0);
  F.chosen.assign(F.slots.size(), -1);
  F.budget = budget;
  bool ok = F.rec(0);
  double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
  if (!ok) {
    printf("FAIL %ld %.1f %s\n", F.nodes, ms, F.nodes > budget ? "budget" : "exhausted");
    return 0;
  }
  // verify: every slot's letters spell its chosen word, no duplicates
  unordered_set<string> seen;
  int mn = 1000;
  double sum = 0;
  string list;
  for (size_t s = 0; s < F.slots.size(); s++) {
    const Lex& X = lex[F.slots[s].len];
    const string& w = X.w[F.chosen[s]];
    for (int p = 0; p < F.slots[s].len; p++)
      if (F.g[F.slots[s].cells[p]] != w[p]) {
        fprintf(stderr, "BAD FILL\n");
        return 2;
      }
    if (!seen.insert(w).second) {
      fprintf(stderr, "DUPLICATE %s\n", w.c_str());
      return 2;
    }
    int sc = X.score[F.chosen[s]];
    mn = min(mn, sc);
    sum += sc;
    if (!list.empty()) list += ',';
    list += w + ":" + to_string(sc);
  }
  string out;
  for (int r = 0; r < F.R; r++) {
    if (r) out += '|';
    for (int c = 0; c < F.C; c++) out += F.blk[r * F.C + c] ? '#' : F.g[r * F.C + c];
  }
  printf("OK %ld %.1f %s %d %.1f %s\n", F.nodes, ms, out.c_str(), mn, sum / F.slots.size(), list.c_str());
  return 0;
}
