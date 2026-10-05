#include "SdSave.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sd {

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

  // The next line must be "<keyword> <rest>"; rest is what follows the space.
  bool field(const char* keyword, std::string_view& rest) {
    std::string_view l;
    if (!line(l)) return false;
    const size_t n = std::strlen(keyword);
    if (l.size() <= n || l.compare(0, n, keyword) != 0 || l[n] != ' ') return false;
    rest = l.substr(n + 1);
    return true;
  }

  // Only an empty remainder may follow "end".
  bool atEnd() {
    std::string_view l;
    return !line(l) || (l.empty() && p >= end);
  }
};

bool token(std::string_view& s, std::string_view& tok) {
  if (s.empty()) return false;
  const size_t sp = s.find(' ');
  tok = s.substr(0, sp);
  s = sp == std::string_view::npos ? std::string_view() : s.substr(sp + 1);
  return !tok.empty();
}

bool parseUint(const std::string_view tok, const uint64_t hi, uint64_t& out) {
  if (tok.empty() || tok.size() > 10) return false;
  uint64_t v = 0;
  for (const char c : tok) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  if (v > hi) return false;
  out = v;
  return true;
}

bool uintToken(std::string_view& s, const uint64_t hi, uint64_t& out) {
  std::string_view tok;
  return token(s, tok) && parseUint(tok, hi, out);
}

int hexDigit(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool parseHex32(const std::string_view tok, uint32_t& out) {
  if (tok.size() != 8) return false;
  uint32_t v = 0;
  for (const char c : tok) {
    const int d = hexDigit(c);
    if (d < 0) return false;
    v = (v << 4) | static_cast<uint32_t>(d);
  }
  out = v;
  return true;
}

bool parseTier(const std::string_view tok, uint8_t& out) {
  const int t = tierFromKey(tok.data(), tok.size());
  if (t < 0) return false;
  out = static_cast<uint8_t>(t);
  return true;
}

// 81 squares: '.' (0 when allowDot) or '1'..'9'.
bool parseGrid(const std::string_view s, const bool allowDot, uint8_t* out) {
  if (s.size() != CELLS) return false;
  for (int i = 0; i < CELLS; i++) {
    const char c = s[i];
    if (c == '.' && allowDot) {
      out[i] = 0;
    } else if (c >= '1' && c <= '9') {
      out[i] = static_cast<uint8_t>(c - '0');
    } else {
      return false;
    }
  }
  return true;
}

void gridText(const uint8_t* grid, char* out) {
  for (int i = 0; i < CELLS; i++) out[i] = grid[i] ? static_cast<char>('0' + grid[i]) : '.';
  out[CELLS] = '\0';
}

}  // namespace

// ---- prefs --------------------------------------------------------------------------------------

void resetPrefs(Prefs& prefs) {
  prefs.tier = Easy;
  for (uint32_t& n : prefs.next) n = 1;
  prefs.removeNotes = true;
}

size_t formatPrefs(const Prefs& prefs, char* out, const size_t cap) {
  if (!out || cap == 0 || !tierKey(prefs.tier)) return 0;
  Writer w{out, cap};
  w.add("SP1\ntier %s\nnext %lu %lu %lu %lu\nremovenotes %d\nend\n", tierKey(prefs.tier),
        static_cast<unsigned long>(prefs.next[0]), static_cast<unsigned long>(prefs.next[1]),
        static_cast<unsigned long>(prefs.next[2]), static_cast<unsigned long>(prefs.next[3]),
        prefs.removeNotes ? 1 : 0);
  return w.finish();
}

bool parsePrefs(const char* text, const size_t len, Prefs& out) {
  const auto fail = [&out] {
    resetPrefs(out);
    return false;
  };
  resetPrefs(out);
  if (!text) return false;
  Reader in{text, text + len};
  std::string_view l;
  std::string_view rest;
  uint64_t v = 0;
  if (!in.line(l) || l != "SP1") return fail();
  if (!in.field("tier", rest) || !parseTier(rest, out.tier)) return fail();
  if (!in.field("next", rest)) return fail();
  for (uint32_t& n : out.next) {
    if (!uintToken(rest, 0xFFFFFFFFull, v) || v == 0) return fail();
    n = static_cast<uint32_t>(v);
  }
  if (!rest.empty()) return fail();
  if (!in.field("removenotes", rest) || !parseUint(rest, 1, v)) return fail();
  out.removeNotes = v == 1;
  if (!in.line(l) || l != "end" || !in.atEnd()) return fail();
  return true;
}

uint32_t takeNumber(Prefs& prefs, const int tier) {
  if (tier < 0 || tier >= TIER_COUNT) return 0;
  uint32_t& next = prefs.next[tier];
  if (next == 0) next = 1;
  const uint32_t n = next;
  next = next == 0xFFFFFFFFu ? 1 : next + 1;
  return n;
}

// ---- the puzzle ---------------------------------------------------------------------------------

size_t formatGame(const Game& g, char* out, const size_t cap) {
  if (!out || cap == 0 || !tierKey(g.tier)) return 0;
  Writer w{out, cap};
  char grid[CELLS + 1];
  w.add("SD1\ntier %s\nnumber %lu\nseed %08lx\nfnv %08lx\n", tierKey(g.tier), static_cast<unsigned long>(g.number),
        static_cast<unsigned long>(g.seed), static_cast<unsigned long>(g.fnv));
  gridText(g.givens, grid);
  w.add("givens %s\n", grid);
  gridText(g.solution, grid);
  w.add("solution %s\n", grid);
  for (int i = 0; i < CELLS; i++) grid[i] = (g.value[i] && !g.givens[i]) ? static_cast<char>('0' + g.value[i]) : '.';
  w.add("entries %s\n", grid);
  char notes[CELLS * 3 + 1];
  for (int i = 0; i < CELLS; i++)
    std::snprintf(notes + i * 3, 4, "%03x", static_cast<unsigned>(g.notes[i] & ALL_DIGITS));
  w.add("notes %s\n", notes);
  for (int i = 0; i < CELLS; i++) {
    grid[i] = (g.flags[i] & FLAG_REVEALED) ? 'r' : (g.flags[i] & FLAG_WRONG) ? 'w' : '.';
  }
  w.add("flags %s\n", grid);
  if (g.cursor == NO_CELL) {
    w.add("cursor -\n");
  } else {
    w.add("cursor %u\n", static_cast<unsigned>(g.cursor));
  }
  if (g.lock == LOCK_ERASE) {
    w.add("mode %d e\n", g.notesMode ? 1 : 0);
  } else {
    w.add("mode %d %u\n", g.notesMode ? 1 : 0, static_cast<unsigned>(g.lock));
  }
  w.add("elapsed %lu\ncounts %u %u %u\nsolved %d\nend\n", static_cast<unsigned long>(g.elapsed),
        static_cast<unsigned>(g.checks), static_cast<unsigned>(g.hints), static_cast<unsigned>(g.reveals),
        g.solved ? 1 : 0);
  return w.finish();
}

bool parseGame(const char* text, const size_t len, Game& out) {
  out.clear();
  if (!text) return false;
  Reader in{text, text + len};
  std::string_view l;
  std::string_view rest;
  std::string_view tok;
  uint64_t v = 0;
  if (!in.line(l) || l != "SD1") return false;
  if (!in.field("tier", rest) || !parseTier(rest, out.tier)) return false;
  if (!in.field("number", rest) || !parseUint(rest, 0xFFFFFFFFull, v)) return false;
  out.number = static_cast<uint32_t>(v);
  if (!in.field("seed", rest) || !parseHex32(rest, out.seed)) return false;
  if (!in.field("fnv", rest) || !parseHex32(rest, out.fnv)) return false;
  if (!in.field("givens", rest) || !parseGrid(rest, true, out.givens)) return false;
  if (!in.field("solution", rest) || !parseGrid(rest, false, out.solution)) return false;
  if (!in.field("entries", rest) || !parseGrid(rest, true, out.value)) return false;
  for (int i = 0; i < CELLS; i++) {
    if (out.givens[i]) {
      if (out.value[i]) return false;  // a given's square is '.' in entries
      out.value[i] = out.givens[i];
    }
  }
  if (!in.field("notes", rest) || rest.size() != CELLS * 3) return false;
  for (int i = 0; i < CELLS; i++) {
    int m = 0;
    for (int k = 0; k < 3; k++) {
      const int d = hexDigit(rest[i * 3 + k]);
      if (d < 0) return false;
      m = (m << 4) | d;
    }
    if (m > ALL_DIGITS) return false;
    out.notes[i] = static_cast<uint16_t>(m);
  }
  if (!in.field("flags", rest) || rest.size() != CELLS) return false;
  for (int i = 0; i < CELLS; i++) {
    const char c = rest[i];
    if (c == 'r') {
      out.flags[i] = FLAG_REVEALED;
    } else if (c == 'w') {
      out.flags[i] = FLAG_WRONG;
    } else if (c != '.') {
      return false;
    }
  }
  if (!in.field("cursor", rest)) return false;
  if (rest == "-") {
    out.cursor = NO_CELL;
  } else {
    if (!parseUint(rest, CELLS - 1, v)) return false;
    out.cursor = static_cast<uint8_t>(v);
  }
  if (!in.field("mode", rest) || !uintToken(rest, 1, v) || !token(rest, tok) || !rest.empty()) return false;
  out.notesMode = v == 1;
  if (tok == "e") {
    out.lock = LOCK_ERASE;
  } else {
    if (!parseUint(tok, 9, v)) return false;
    out.lock = static_cast<uint8_t>(v);
  }
  if (!in.field("elapsed", rest) || !parseUint(rest, ELAPSED_MAX, v)) return false;
  out.elapsed = static_cast<uint32_t>(v);
  if (!in.field("counts", rest)) return false;
  uint64_t counts[3];
  for (uint64_t& n : counts) {
    if (!uintToken(rest, COUNT_MAX, n)) return false;
  }
  if (!rest.empty()) return false;
  out.checks = static_cast<uint16_t>(counts[0]);
  out.hints = static_cast<uint16_t>(counts[1]);
  out.reveals = static_cast<uint16_t>(counts[2]);
  if (!in.field("solved", rest) || !parseUint(rest, 1, v)) return false;
  out.solved = v == 1;
  if (!in.line(l) || l != "end" || !in.atEnd()) return false;
  return validGame(out);
}

LoadFrom loadFrom(const bool mainExists, const bool tmpExists) {
  if (mainExists) return LoadFrom::Main;
  return tmpExists ? LoadFrom::Tmp : LoadFrom::None;
}

size_t tmpPathOf(const char* path, char* out, const size_t cap) {
  if (!path || !out || cap == 0) return 0;
  const int n = std::snprintf(out, cap, "%s%s", path, TMP_SUFFIX);
  return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

// ---- solved list --------------------------------------------------------------------------------

SolvedEntry solvedEntryOf(const Game& g) {
  SolvedEntry e;
  e.fnv = g.fnv;
  e.tier = g.tier;
  e.number = g.number;
  e.elapsed = g.elapsed;
  e.checks = g.checks;
  e.hints = g.hints;
  e.reveals = g.reveals;
  return e;
}

size_t formatSolvedLine(const SolvedEntry& e, char* out, const size_t cap) {
  if (!out || cap == 0 || !tierKey(e.tier)) return 0;
  const int n =
      std::snprintf(out, cap, "%08lx %s %lu %lu %u %u %u\n", static_cast<unsigned long>(e.fnv), tierKey(e.tier),
                    static_cast<unsigned long>(e.number), static_cast<unsigned long>(e.elapsed),
                    static_cast<unsigned>(e.checks), static_cast<unsigned>(e.hints), static_cast<unsigned>(e.reveals));
  return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

bool parseSolvedLine(std::string_view line, SolvedEntry& out) {
  out = SolvedEntry{};
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  std::string_view tok;
  uint64_t v = 0;
  if (!token(line, tok) || !parseHex32(tok, out.fnv)) return false;
  if (!token(line, tok) || !parseTier(tok, out.tier)) return false;
  if (!uintToken(line, 0xFFFFFFFFull, v)) return false;
  out.number = static_cast<uint32_t>(v);
  if (!uintToken(line, ELAPSED_MAX, v)) return false;
  out.elapsed = static_cast<uint32_t>(v);
  uint64_t counts[3];
  for (uint64_t& n : counts) {
    if (!uintToken(line, COUNT_MAX, n)) return false;
  }
  out.checks = static_cast<uint16_t>(counts[0]);
  out.hints = static_cast<uint16_t>(counts[1]);
  out.reveals = static_cast<uint16_t>(counts[2]);
  return line.empty();
}

bool solvedListHas(const char* text, const size_t len, const uint32_t fnv) {
  if (!text) return false;
  Reader in{text, text + len};
  std::string_view l;
  while (in.line(l)) {
    SolvedEntry e;
    if (parseSolvedLine(l, e) && e.fnv == fnv) return true;
  }
  return false;
}

size_t solvedKeepOffset(const char* text, const size_t len, const int keepLines) {
  if (!text || len == 0 || keepLines <= 0) return len;
  // Walk back over keepLines line starts (a final line without '\n' counts too).
  int lines = 0;
  size_t i = len;
  if (text[i - 1] == '\n') i--;
  while (i > 0) {
    if (text[i - 1] == '\n') {
      if (++lines == keepLines) return i;
    }
    i--;
  }
  return 0;
}

}  // namespace sd
