#include "WsSave.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "WsRandom.h"

namespace ws {

namespace {

// ---- writing ------------------------------------------------------------------------------------

struct Writer {
  char* out;
  size_t cap;
  size_t len = 0;
  bool ok = true;

  __attribute__((format(printf, 2, 3))) void add(const char* fmt, ...) {
    if (!ok) return;
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(out + len, cap - len, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= cap - len) {
      ok = false;
      return;
    }
    len += static_cast<size_t>(n);
  }
  size_t finish() const { return ok ? len : 0; }
};

// ---- reading ------------------------------------------------------------------------------------

struct Reader {
  const char* p;
  const char* end;

  // The next line without its '\n' (or '\r\n'). False at the end of the text.
  bool line(std::string_view& out) {
    if (p >= end) return false;
    const char* start = p;
    while (p < end && *p != '\n') p++;
    const char* stop = p;
    if (p < end) p++;
    if (stop > start && stop[-1] == '\r') stop--;
    out = std::string_view(start, static_cast<size_t>(stop - start));
    return true;
  }

  // The next line must be "<keyword>" or "<keyword> <rest>"; rest is what follows the space.
  bool field(const char* keyword, std::string_view& rest) {
    std::string_view l;
    if (!line(l)) return false;
    const size_t n = std::strlen(keyword);
    if (l.size() < n || l.compare(0, n, keyword) != 0) return false;
    if (l.size() == n) {
      rest = std::string_view();
      return true;
    }
    if (l[n] != ' ') return false;
    rest = l.substr(n + 1);
    return true;
  }
};

// Pops one space-separated token from s.
bool token(std::string_view& s, std::string_view& tok) {
  if (s.empty()) return false;
  const size_t sp = s.find(' ');
  tok = s.substr(0, sp);
  s = sp == std::string_view::npos ? std::string_view() : s.substr(sp + 1);
  return !tok.empty();
}

bool parseInt(const std::string_view tok, const int64_t lo, const int64_t hi, int64_t& out) {
  if (tok.empty() || tok.size() > 11) return false;
  size_t i = 0;
  bool negative = false;
  if (tok[0] == '-') {
    negative = true;
    i = 1;
    if (tok.size() == 1) return false;
  }
  int64_t v = 0;
  for (; i < tok.size(); i++) {
    if (tok[i] < '0' || tok[i] > '9') return false;
    v = v * 10 + (tok[i] - '0');
  }
  if (negative) v = -v;
  if (v < lo || v > hi) return false;
  out = v;
  return true;
}

bool intToken(std::string_view& s, const int64_t lo, const int64_t hi, int64_t& out) {
  std::string_view tok;
  return token(s, tok) && parseInt(tok, lo, hi, out);
}

// Printable text (UTF-8 allowed, no control bytes), 1..maxLen bytes, copied NUL-terminated.
bool copyText(const std::string_view s, const size_t maxLen, char* out) {
  if (s.empty() || s.size() > maxLen) return false;
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F) return false;
  }
  std::memcpy(out, s.data(), s.size());
  out[s.size()] = '\0';
  return true;
}

bool parseCell(std::string_view& s, Cell& out) {
  int64_t r = 0;
  int64_t c = 0;
  if (!intToken(s, -1, MAX_GRID - 1, r) || !intToken(s, -1, MAX_GRID - 1, c)) return false;
  out = (r < 0 || c < 0) ? Cell{} : makeCell(static_cast<int>(r), static_cast<int>(c));
  return true;
}

}  // namespace

// ---- prefs --------------------------------------------------------------------------------------

void resetPrefs(Prefs& prefs) {
  prefs.difficulty = Difficulty::Medium;
  std::memset(prefs.choice, 0, sizeof(prefs.choice));
  std::memset(prefs.recent, 0, sizeof(prefs.recent));
}

size_t formatPrefs(const Prefs& prefs, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  Writer w{out, cap};
  w.add("WP1\ndifficulty %u\nchoice %s\n", static_cast<unsigned>(prefs.difficulty),
        prefs.randomChoice() ? RANDOM_CHOICE : prefs.choice);
  for (const auto& key : prefs.recent) {
    if (key[0] != '\0') w.add("recent %s\n", key);
  }
  w.add("end\n");
  return w.finish();
}

bool parsePrefs(const char* text, const size_t len, Prefs& out) {
  // Parsed in place (no second Prefs on the stack); any failure leaves the defaults.
  const auto fail = [&out] {
    resetPrefs(out);
    return false;
  };
  resetPrefs(out);
  if (!text) return false;
  Reader in{text, text + len};
  std::string_view l;
  std::string_view rest;
  int64_t v = 0;
  if (!in.line(l) || l != "WP1") return fail();
  if (!in.field("difficulty", rest) || !parseInt(rest, 0, DIFFICULTY_COUNT - 1, v)) return fail();
  out.difficulty = static_cast<Difficulty>(v);
  if (!in.field("choice", rest)) return fail();
  if (rest != RANDOM_CHOICE && !copyText(rest, MAX_THEME_KEY, out.choice)) return fail();
  int recent = 0;
  while (true) {
    if (!in.line(l)) return fail();  // truncated before "end"
    if (l == "end") break;
    if (recent >= RECENT_THEMES || l.size() < 8 || l.compare(0, 7, "recent ") != 0) return fail();
    if (!copyText(l.substr(7), MAX_THEME_KEY, out.recent[recent])) return fail();
    recent++;
  }
  return true;
}

bool isRecentTheme(const Prefs& prefs, const std::string_view key) {
  for (const auto& r : prefs.recent) {
    if (r[0] != '\0' && key == r) return true;
  }
  return false;
}

void rememberTheme(Prefs& prefs, const char* key) {
  if (!key || key[0] == '\0' || std::strlen(key) > static_cast<size_t>(MAX_THEME_KEY)) return;
  char keep[RECENT_THEMES][MAX_THEME_KEY + 1] = {};
  std::snprintf(keep[0], sizeof(keep[0]), "%s", key);
  int n = 1;
  for (int i = 0; i < RECENT_THEMES && n < RECENT_THEMES; i++) {
    if (prefs.recent[i][0] == '\0' || std::strcmp(prefs.recent[i], key) == 0) continue;
    std::memcpy(keep[n++], prefs.recent[i], sizeof(keep[0]));
  }
  std::memcpy(prefs.recent, keep, sizeof(keep));
}

int pickRandomTheme(uint32_t& rng, const std::string_view* keys, const size_t count, const Prefs& prefs) {
  if (!keys || count == 0) return -1;
  size_t fresh = 0;
  for (size_t i = 0; i < count; i++) fresh += isRecentTheme(prefs, keys[i]) ? 0 : 1;
  if (fresh == 0) return static_cast<int>(randomBelow(rng, static_cast<uint32_t>(count)));
  size_t pick = randomBelow(rng, static_cast<uint32_t>(fresh));
  for (size_t i = 0; i < count; i++) {
    if (isRecentTheme(prefs, keys[i])) continue;
    if (pick-- == 0) return static_cast<int>(i);
  }
  return -1;
}

// ---- puzzle -------------------------------------------------------------------------------------

size_t formatPuzzle(const Puzzle& p, char* out, const size_t cap) {
  if (!out || cap == 0 || p.size == 0 || p.size > MAX_GRID) return 0;
  Writer w{out, cap};
  w.add("WS1\ndifficulty %u\nseed %lu\nelapsed %lu\ncursor %d %d\nhint %u %d\ntheme %s\ntitle %s\n",
        static_cast<unsigned>(p.difficulty), static_cast<unsigned long>(p.seed),
        static_cast<unsigned long>(p.elapsedSeconds), p.cursor.row, p.cursor.col, static_cast<unsigned>(p.hintsUsed),
        p.hintWord, p.themeKey, p.themeTitle);
  for (int r = 0; r < p.size; r++) w.add("row %.*s\n", p.size, p.grid + r * MAX_GRID);
  for (int k = 0; k < p.wordCount; k++) {
    const PuzzleWord& word = p.words[k];
    const Line l = word.found ? word.foundLine : Line{};
    w.add("word %d %d %u %u %d %d %d %d %d %s\n", word.place.row, word.place.col, static_cast<unsigned>(word.place.dir),
          static_cast<unsigned>(word.place.len), word.found ? 1 : 0, l.a.row, l.a.col, l.b.row, l.b.col, word.display);
  }
  w.add("end\n");
  return w.finish();
}

bool parsePuzzle(const char* text, const size_t len, Puzzle& out) {
  out.reset();
  if (!text) return false;
  Reader in{text, text + len};
  std::string_view l;
  std::string_view rest;
  int64_t v = 0;
  if (!in.line(l) || l != "WS1") return false;
  if (!in.field("difficulty", rest) || !parseInt(rest, 0, DIFFICULTY_COUNT - 1, v)) return false;
  out.difficulty = static_cast<Difficulty>(v);
  out.size = specFor(out.difficulty).size;
  if (!in.field("seed", rest) || !parseInt(rest, 0, 0xFFFFFFFFLL, v)) return false;
  out.seed = static_cast<uint32_t>(v);
  if (!in.field("elapsed", rest) || !parseInt(rest, 0, ELAPSED_MAX, v)) return false;
  out.elapsedSeconds = static_cast<uint32_t>(v);
  if (!in.field("cursor", rest) || !parseCell(rest, out.cursor) || !rest.empty()) return false;
  if (!in.field("hint", rest) || !intToken(rest, 0, 255, v)) return false;
  out.hintsUsed = static_cast<uint8_t>(v);
  if (!intToken(rest, -1, MAX_WORDS - 1, v) || !rest.empty()) return false;
  out.hintWord = static_cast<int8_t>(v);
  if (!in.field("theme", rest) || !copyText(rest, MAX_THEME_KEY, out.themeKey)) return false;
  if (!in.field("title", rest) || !copyText(rest, MAX_TITLE_LEN, out.themeTitle)) return false;
  for (int r = 0; r < out.size; r++) {
    if (!in.field("row", rest) || rest.size() != out.size) return false;
    std::memcpy(out.grid + r * MAX_GRID, rest.data(), rest.size());
  }
  while (true) {
    if (!in.line(l)) return false;  // truncated before "end"
    if (l == "end") break;
    if (out.wordCount >= MAX_WORDS || l.size() < 6 || l.compare(0, 5, "word ") != 0) return false;
    rest = l.substr(5);
    PuzzleWord& w = out.words[out.wordCount];
    int64_t row = 0;
    int64_t col = 0;
    int64_t dir = 0;
    int64_t n = 0;
    int64_t found = 0;
    if (!intToken(rest, 0, MAX_GRID - 1, row) || !intToken(rest, 0, MAX_GRID - 1, col) || !intToken(rest, 0, 7, dir) ||
        !intToken(rest, 2, MAX_WORD_LETTERS, n) || !intToken(rest, 0, 1, found)) {
      return false;
    }
    w.place.row = static_cast<int8_t>(row);
    w.place.col = static_cast<int8_t>(col);
    w.place.dir = static_cast<uint8_t>(dir);
    w.place.len = static_cast<uint8_t>(n);
    w.found = found == 1;
    if (!parseCell(rest, w.foundLine.a) || !parseCell(rest, w.foundLine.b)) return false;
    if (!copyText(rest, MAX_DISPLAY_LEN, w.display)) return false;
    if (gridLetters(w.display, rest.size(), w.letters, sizeof(w.letters)) == 0) return false;
    if (!w.found) w.foundLine = Line{};
    out.wordCount++;
  }
  // Nothing may follow "end".
  if (in.line(l) && !(l.empty() && in.p >= in.end)) return false;
  return validatePuzzle(out);
}

bool validatePuzzle(const Puzzle& p) {
  if (static_cast<uint8_t>(p.difficulty) >= DIFFICULTY_COUNT) return false;
  const int size = p.size;
  if (size != specFor(p.difficulty).size) return false;
  for (int r = 0; r < size; r++) {
    for (int c = 0; c < size; c++) {
      if (p.at(r, c) < 'A' || p.at(r, c) > 'Z') return false;
    }
  }
  if (p.wordCount < 1 || p.wordCount > MAX_WORDS) return false;
  if (p.themeKey[0] == '\0' || p.themeTitle[0] == '\0') return false;
  if (!inGrid(p.cursor, size)) return false;
  if (p.elapsedSeconds > ELAPSED_MAX) return false;
  if (p.hintWord < -1 || p.hintWord >= p.wordCount || (p.hintWord >= 0 && p.words[p.hintWord].found)) return false;
  char letters[MAX_WORD_LETTERS + 1];
  char line[MAX_GRID + 1];
  for (int k = 0; k < p.wordCount; k++) {
    const PuzzleWord& w = p.words[k];
    const size_t displayLen = strnlen(w.display, sizeof(w.display));
    const size_t n = gridLetters(w.display, displayLen, letters, sizeof(letters));
    if (n < 2 || n != w.place.len || std::strcmp(letters, w.letters) != 0) return false;
    if (w.place.dir >= 8 || !inGrid(w.place.start(), size) || !inGrid(w.place.end(), size)) return false;
    for (size_t i = 0; i < n; i++) {
      if (p.at(w.place.row + DIR_DR[w.place.dir] * static_cast<int>(i),
               w.place.col + DIR_DC[w.place.dir] * static_cast<int>(i)) != letters[i]) {
        return false;
      }
    }
    if (!w.found) continue;
    if (static_cast<size_t>(lineCells(w.foundLine.a, w.foundLine.b)) != n) return false;
    if (lettersOnLine(p, w.foundLine, line, sizeof(line)) != n) return false;
    bool forward = true;
    bool backward = true;
    for (size_t i = 0; i < n; i++) {
      forward = forward && line[i] == letters[i];
      backward = backward && line[i] == letters[n - 1 - i];
    }
    if (!forward && !backward) return false;
  }
  return true;
}

}  // namespace ws
