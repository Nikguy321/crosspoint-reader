#include "WsGenerator.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "WsRandom.h"

namespace ws {

namespace {

// English letter frequencies (per mille, rounded; A..Z) for the filler.
constexpr uint8_t LETTER_WEIGHTS[26] = {82, 15, 28, 43, 127, 22, 20, 61, 70, 2,  8, 40, 24,
                                        67, 75, 19, 1,  60,  63, 91, 28, 10, 24, 2, 20, 1};
constexpr uint32_t letterWeightTotal() {
  uint32_t t = 0;
  for (const uint8_t w : LETTER_WEIGHTS) t += w;
  return t;
}
constexpr uint32_t LETTER_WEIGHT_TOTAL = letterWeightTotal();

char randomLetter(uint32_t& rng) {
  uint32_t pick = randomBelow(rng, LETTER_WEIGHT_TOTAL);
  for (int i = 0; i < 26; i++) {
    if (pick < LETTER_WEIGHTS[i]) return static_cast<char>('A' + i);
    pick -= LETTER_WEIGHTS[i];
  }
  return 'E';
}

// The placed-cell mask (MAX_GRID^2 bits), so the filler can tell its own cells.
struct CellMask {
  uint32_t bits[(MAX_GRID * MAX_GRID + 31) / 32] = {};
  void set(const int r, const int c) { bits[(r * MAX_GRID + c) / 32] |= 1u << ((r * MAX_GRID + c) % 32); }
  bool test(const int r, const int c) const {
    return bits[(r * MAX_GRID + c) / 32] & (1u << ((r * MAX_GRID + c) % 32));
  }
};

bool onPlacement(const Placement& p, const int r, const int c) {
  for (int i = 0; i < p.len; i++) {
    if (p.row + DIR_DR[p.dir] * i == r && p.col + DIR_DC[p.dir] * i == c) return true;
  }
  return false;
}

void reverseInto(const char* s, const size_t n, char* out) {
  for (size_t i = 0; i < n; i++) out[i] = s[n - 1 - i];
  out[n] = '\0';
}

// True when either word is a substring of the other or of the other's reverse (equal included).
bool conflicts(const char* a, const char* b) {
  char rev[MAX_WORD_LETTERS + 1];
  reverseInto(a, std::strlen(a), rev);
  if (std::strstr(a, b) || std::strstr(rev, b)) return true;
  reverseInto(b, std::strlen(b), rev);
  return std::strstr(b, a) || std::strstr(rev, a);
}

// Copies one theme entry into w (display + letters). False when it is not a usable word or its
// letter count is outside [minLen, maxLen].
bool loadWord(const std::string_view text, const int minLen, const int maxLen, PuzzleWord& w) {
  if (text.empty() || text.size() > static_cast<size_t>(MAX_DISPLAY_LEN)) return false;
  const size_t n = gridLetters(text.data(), text.size(), w.letters, sizeof(w.letters));
  if (n < static_cast<size_t>(minLen) || n > static_cast<size_t>(maxLen)) return false;
  std::memcpy(w.display, text.data(), text.size());
  w.display[text.size()] = '\0';
  w.found = false;
  w.foundLine = Line{};
  return true;
}

// Longest first; equal lengths by their letters, so every standard library orders them alike.
bool longerFirst(const PuzzleWord& a, const PuzzleWord& b) {
  const size_t la = std::strlen(a.letters);
  const size_t lb = std::strlen(b.letters);
  if (la != lb) return la > lb;
  return std::strcmp(a.letters, b.letters) < 0;
}

bool displayOrder(const PuzzleWord& a, const PuzzleWord& b) { return std::strcmp(a.display, b.display) < 0; }

// Tries random starts for words[index], in `preferred` for the first half of the tries (the
// puzzle's direction deck) and then in any allowed direction; on success writes its letters and
// placement.
bool placeWord(Puzzle& p, const int index, const uint8_t dirMask, const uint8_t preferred, uint32_t& rng) {
  PuzzleWord& w = p.words[index];
  const int n = static_cast<int>(std::strlen(w.letters));
  const int size = p.size;
  uint8_t dirs[8];
  int dirCount = 0;
  for (uint8_t d = 0; d < 8; d++) {
    if (dirMask & (1u << d)) dirs[dirCount++] = d;
  }
  if (dirCount == 0 || n < 1 || n > size) return false;
  for (int attempt = 0; attempt < PLACE_TRIES_PER_WORD; attempt++) {
    const uint8_t dir = attempt < PLACE_TRIES_PER_WORD / 2 && (dirMask & (1u << preferred))
                            ? preferred
                            : dirs[randomBelow(rng, static_cast<uint32_t>(dirCount))];
    const int dr = DIR_DR[dir];
    const int dc = DIR_DC[dir];
    const int rowLo = dr < 0 ? n - 1 : 0;
    const int rowHi = dr > 0 ? size - n : size - 1;
    const int colLo = dc < 0 ? n - 1 : 0;
    const int colHi = dc > 0 ? size - n : size - 1;
    const int r0 = rowLo + static_cast<int>(randomBelow(rng, static_cast<uint32_t>(rowHi - rowLo + 1)));
    const int c0 = colLo + static_cast<int>(randomBelow(rng, static_cast<uint32_t>(colHi - colLo + 1)));
    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
      const int r = r0 + dr * i;
      const int c = c0 + dc * i;
      const char have = p.at(r, c);
      if (have == '\0') continue;
      if (have != w.letters[i]) {
        ok = false;
        break;
      }
      // A crossing: never along another word's own line (the capsules would merge).
      for (int k = 0; k < index && ok; k++) {
        const Placement& other = p.words[k].place;
        if (other.dir % 4 == dir % 4 && onPlacement(other, r, c)) ok = false;
      }
    }
    if (!ok) continue;
    for (int i = 0; i < n; i++) p.at(r0 + dr * i, c0 + dc * i) = w.letters[i];
    w.place.row = static_cast<int8_t>(r0);
    w.place.col = static_cast<int8_t>(c0);
    w.place.dir = dir;
    w.place.len = static_cast<uint8_t>(n);
    return true;
  }
  return false;
}

// Deals the difficulty's directions from a shuffled deck, one per placed word, so a puzzle mixes
// them evenly (an independent pick left ~2 % of Medium puzzles with 9 of 12 words across).
struct DirectionDeck {
  uint8_t dirs[8] = {};
  int count = 0;
  int next = 0;

  explicit DirectionDeck(const uint8_t dirMask) {
    for (uint8_t d = 0; d < 8; d++) {
      if (dirMask & (1u << d)) dirs[count++] = d;
    }
    next = count;
  }

  uint8_t deal(uint32_t& rng) {
    if (count == 0) return 0;
    if (next >= count) {
      for (int i = count; i > 1; i--) std::swap(dirs[i - 1], dirs[randomBelow(rng, static_cast<uint32_t>(i))]);
      next = 0;
    }
    return dirs[next++];
  }
};

// Re-rolls filler under any extra occurrence of a placed word. False when an extra occurrence
// is made only of placed cells (no filler to change) or the rounds run out.
bool clearExtraOccurrences(Puzzle& p, const CellMask& placed, uint32_t& rng) {
  const int size = p.size;
  for (int round = 0; round < FILLER_FIX_ROUNDS; round++) {
    bool clean = true;
    for (int k = 0; k < p.wordCount; k++) {
      const PuzzleWord& w = p.words[k];
      const int n = w.place.len;
      const Cell s = w.place.start();
      const Cell e = w.place.end();
      for (int r = 0; r < size; r++) {
        for (int c = 0; c < size; c++) {
          if (p.at(r, c) != w.letters[0]) continue;
          for (uint8_t d = 0; d < 8; d++) {
            const int er = r + DIR_DR[d] * (n - 1);
            const int ec = c + DIR_DC[d] * (n - 1);
            if (er < 0 || ec < 0 || er >= size || ec >= size) continue;
            int i = 1;
            while (i < n && p.at(r + DIR_DR[d] * i, c + DIR_DC[d] * i) == w.letters[i]) i++;
            if (i < n) continue;
            const bool own =
                (r == s.row && c == s.col && d == w.place.dir) || (r == e.row && c == e.col && d == (w.place.dir ^ 4));
            if (own) continue;
            clean = false;
            bool rerolled = false;
            for (i = 0; i < n; i++) {
              const int rr = r + DIR_DR[d] * i;
              const int cc = c + DIR_DC[d] * i;
              if (placed.test(rr, cc)) continue;
              p.at(rr, cc) = randomLetter(rng);
              rerolled = true;
            }
            if (!rerolled) return false;
          }
        }
      }
    }
    if (clean) return true;
  }
  return false;
}

}  // namespace

bool generatePuzzle(const std::string_view* words, const size_t count, const Difficulty difficulty, const uint32_t seed,
                    Puzzle& out) {
  const DifficultySpec& spec = specFor(difficulty);
  const int maxLen = spec.maxLetters < spec.size ? spec.maxLetters : spec.size;
  const int target = spec.words < MAX_WORDS ? spec.words : MAX_WORDS;
  if (!words || count == 0) return false;

  // The usable entries once (index into words); each try shuffles a copy.
  std::vector<uint16_t> eligible;
  eligible.reserve(count < 0xFFFF ? count : 0xFFFF);
  PuzzleWord probe;
  for (size_t i = 0; i < count && i < 0xFFFF; i++) {
    if (loadWord(words[i], spec.minLetters, maxLen, probe)) eligible.push_back(static_cast<uint16_t>(i));
  }
  if (eligible.size() < static_cast<size_t>(minimumWords(difficulty))) return false;
  std::vector<uint16_t> order;
  order.reserve(eligible.size());

  for (int tryIndex = 0; tryIndex < GENERATE_SEED_TRIES; tryIndex++) {
    uint32_t rng = seed + static_cast<uint32_t>(tryIndex);
    out.difficulty = difficulty;
    out.size = spec.size;
    out.seed = seed;
    std::memset(out.grid, 0, sizeof(out.grid));
    out.wordCount = 0;
    out.hintWord = -1;
    out.hintsUsed = 0;
    out.elapsedSeconds = 0;
    out.cursor = makeCell(0, 0);

    order = eligible;
    for (size_t i = order.size(); i > 1; i--) {
      std::swap(order[i - 1], order[randomBelow(rng, static_cast<uint32_t>(i))]);
    }

    // Choose up to the target, then place them longest first.
    size_t next = 0;
    int chosen = 0;
    while (chosen < target && next < order.size()) {
      PuzzleWord& w = out.words[chosen];
      loadWord(words[order[next++]], spec.minLetters, maxLen, w);
      bool clash = false;
      for (int k = 0; k < chosen && !clash; k++) clash = conflicts(out.words[k].letters, w.letters);
      if (!clash) chosen++;
    }
    std::sort(out.words, out.words + chosen, longerFirst);
    DirectionDeck deck(spec.dirMask);
    uint8_t preferred = deck.deal(rng);
    int placed = 0;
    for (int k = 0; k < chosen; k++) {
      if (k != placed) out.words[placed] = out.words[k];
      if (placeWord(out, placed, spec.dirMask, preferred, rng)) {
        placed++;
        preferred = deck.deal(rng);
      }
    }
    // Words that would not fit leave room for further ones from the theme (bounded).
    int extraTries = 0;
    while (placed < target && next < order.size() && extraTries < 3 * target) {
      PuzzleWord& w = out.words[placed];
      loadWord(words[order[next++]], spec.minLetters, maxLen, w);
      bool clash = false;
      for (int k = 0; k < placed && !clash; k++) clash = conflicts(out.words[k].letters, w.letters);
      if (clash) continue;
      extraTries++;
      if (placeWord(out, placed, spec.dirMask, preferred, rng)) {
        placed++;
        preferred = deck.deal(rng);
      }
    }
    out.wordCount = static_cast<uint8_t>(placed);
    if (placed < minimumWords(difficulty)) continue;

    CellMask mask;
    for (int k = 0; k < placed; k++) {
      const Placement& pl = out.words[k].place;
      for (int i = 0; i < pl.len; i++) mask.set(pl.row + DIR_DR[pl.dir] * i, pl.col + DIR_DC[pl.dir] * i);
    }
    for (int r = 0; r < out.size; r++) {
      for (int c = 0; c < out.size; c++) {
        if (!mask.test(r, c)) out.at(r, c) = randomLetter(rng);
      }
    }
    if (!clearExtraOccurrences(out, mask, rng)) continue;

    std::sort(out.words, out.words + placed, displayOrder);
    for (int k = placed; k < MAX_WORDS; k++) {
      std::memset(out.words[k].display, 0, sizeof(out.words[k].display));
      std::memset(out.words[k].letters, 0, sizeof(out.words[k].letters));
      out.words[k].place = Placement{};
    }
    return true;
  }
  out.wordCount = 0;
  return false;
}

int countOccurrences(const Puzzle& p, const char* letters) {
  const int n = letters ? static_cast<int>(std::strlen(letters)) : 0;
  if (n == 0) return 0;
  int matches = 0;
  for (int r = 0; r < p.size; r++) {
    for (int c = 0; c < p.size; c++) {
      if (p.at(r, c) != letters[0]) continue;
      for (int d = 0; d < 8; d++) {
        const int er = r + DIR_DR[d] * (n - 1);
        const int ec = c + DIR_DC[d] * (n - 1);
        if (er < 0 || ec < 0 || er >= p.size || ec >= p.size) continue;
        int i = 1;
        while (i < n && p.at(r + DIR_DR[d] * i, c + DIR_DC[d] * i) == letters[i]) i++;
        if (i == n) matches++;
        if (n == 1) break;  // one cell is one run, whatever the direction
      }
    }
  }
  bool palindrome = n > 1;
  for (int i = 0; i < n / 2 && palindrome; i++) palindrome = letters[i] == letters[n - 1 - i];
  return palindrome ? matches / 2 : matches;
}

MatchResult matchLine(const Puzzle& p, const Line line) {
  MatchResult result;
  char buf[MAX_GRID + 1];
  const size_t n = lettersOnLine(p, line, buf, sizeof(buf));
  if (n < 2) return result;
  for (int k = 0; k < p.wordCount; k++) {
    const PuzzleWord& w = p.words[k];
    if (std::strlen(w.letters) != n) continue;
    bool forward = true;
    bool backward = true;
    for (size_t i = 0; i < n; i++) {
      forward = forward && buf[i] == w.letters[i];
      backward = backward && buf[i] == w.letters[n - 1 - i];
    }
    if (!forward && !backward) continue;
    if (!w.found) {
      result.kind = MatchKind::Found;
      result.word = static_cast<int8_t>(k);
      return result;
    }
    result.kind = MatchKind::AlreadyFound;
    result.word = static_cast<int8_t>(k);
  }
  return result;
}

void markFound(Puzzle& p, const int word, const Line line) {
  if (word < 0 || word >= p.wordCount) return;
  p.words[word].found = true;
  p.words[word].foundLine = line;
  if (p.hintWord == word) p.hintWord = -1;
}

int pickHintWord(const Puzzle& p, uint32_t& rng) {
  const int unfound = p.wordCount - p.foundCount();
  if (unfound <= 0) return -1;
  int pick = static_cast<int>(randomBelow(rng, static_cast<uint32_t>(unfound)));
  for (int k = 0; k < p.wordCount; k++) {
    if (p.words[k].found) continue;
    if (pick-- == 0) return k;
  }
  return -1;
}

}  // namespace ws
