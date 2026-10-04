#include "CwSave.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace cw {

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

bool parseHex32(const std::string_view tok, uint32_t& out) {
  if (tok.size() != 8) return false;
  uint32_t v = 0;
  for (const char c : tok) {
    uint32_t d = 0;
    if (c >= '0' && c <= '9') {
      d = static_cast<uint32_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = static_cast<uint32_t>(c - 'a' + 10);
    } else {
      return false;
    }
    v = (v << 4) | d;
  }
  out = v;
  return true;
}

bool printable(const std::string_view s) {
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F) return false;
  }
  return true;
}

// Printable text (UTF-8 allowed), 1..maxLen bytes, copied NUL-terminated.
bool copyText(const std::string_view s, const size_t maxLen, char* out) {
  if (s.empty() || s.size() > maxLen || !printable(s)) return false;
  std::memcpy(out, s.data(), s.size());
  out[s.size()] = '\0';
  return true;
}

// A key or "-" for none.
bool copyOptional(const std::string_view s, const size_t maxLen, char* out) {
  if (s == "-") {
    out[0] = '\0';
    return true;
  }
  return copyText(s, maxLen, out);
}

}  // namespace

const char* builtinIdFromKey(const char* sourceKey) {
  const size_t n = sizeof(BUILTIN_KEY_PREFIX) - 1;
  if (!sourceKey || std::strncmp(sourceKey, BUILTIN_KEY_PREFIX, n) != 0 || sourceKey[n] == '\0') return nullptr;
  return sourceKey + n;
}

// ---- prefs --------------------------------------------------------------------------------------

void resetPrefs(Prefs& prefs) {
  std::memset(prefs.current, 0, sizeof(prefs.current));
  prefs.skipFilled = true;
  std::memset(prefs.collection, 0, sizeof(prefs.collection));
  prefs.seq = 0;
}

size_t formatPrefs(const Prefs& prefs, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  Writer w{out, cap};
  w.add("CP1\ncurrent %s\nskip %d\ncollection %s\nseq %lu\nend\n", prefs.current[0] ? prefs.current : "-",
        prefs.skipFilled ? 1 : 0, prefs.collection[0] ? prefs.collection : "-", static_cast<unsigned long>(prefs.seq));
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
  if (!in.line(l) || l != "CP1") return fail();
  if (!in.field("current", rest) || !copyOptional(rest, MAX_SOURCE_KEY, out.current)) return fail();
  if (!in.field("skip", rest) || !parseUint(rest, 1, v)) return fail();
  out.skipFilled = v == 1;
  if (!in.field("collection", rest) || !copyOptional(rest, MAX_COLLECTION_KEY, out.collection)) return fail();
  if (!in.field("seq", rest) || !parseUint(rest, 0xFFFFFFFFull, v)) return fail();
  out.seq = static_cast<uint32_t>(v);
  if (!in.line(l) || l != "end" || !in.atEnd()) return fail();
  return true;
}

// ---- progress -----------------------------------------------------------------------------------

size_t formatProgress(const Progress& prog, char* out, const size_t cap) {
  if (!out || cap == 0 || prog.w < MIN_SIDE || prog.h < MIN_SIDE || prog.w > MAX_SIDE || prog.h > MAX_SIDE) return 0;
  if (prog.sourceKey[0] == '\0') return 0;
  Writer w{out, cap};
  w.add("CW1\nkey %s\nfnv %08lx\nsize %u %u\ncursor %d %d %c\nelapsed %lu\ncounts %u %u\nsolved %d\nseq %lu\n",
        prog.sourceKey, static_cast<unsigned long>(prog.fnv), static_cast<unsigned>(prog.w),
        static_cast<unsigned>(prog.h), prog.cursor / prog.w, prog.cursor % prog.w, prog.dir == DOWN ? 'D' : 'A',
        static_cast<unsigned long>(prog.elapsed), static_cast<unsigned>(prog.checks),
        static_cast<unsigned>(prog.reveals), prog.solved ? 1 : 0, static_cast<unsigned long>(prog.seq));
  char row[MAX_SIDE + 1];
  for (int r = 0; r < prog.h; r++) {
    for (int c = 0; c < prog.w; c++) {
      const char f = prog.fill[r * prog.w + c];
      row[c] = f == EMPTY ? '.' : f;
    }
    row[prog.w] = '\0';
    w.add("row %s\n", row);
  }
  for (int r = 0; r < prog.h; r++) {
    for (int c = 0; c < prog.w; c++) {
      const uint8_t f = prog.flags[r * prog.w + c];
      row[c] = (f & FLAG_REVEALED) ? 'r' : (f & FLAG_WRONG) ? 'w' : '.';
    }
    row[prog.w] = '\0';
    w.add("flags %s\n", row);
  }
  w.add("end\n");
  return w.finish();
}

bool parseProgress(const char* text, const size_t len, Progress& out) {
  out.clear();
  if (!text) return false;
  Reader in{text, text + len};
  std::string_view l;
  std::string_view rest;
  std::string_view tok;
  uint64_t v = 0;
  uint64_t v2 = 0;
  if (!in.line(l) || l != "CW1") return false;
  if (!in.field("key", rest) || !copyText(rest, MAX_SOURCE_KEY, out.sourceKey)) return false;
  if (!in.field("fnv", rest) || !parseHex32(rest, out.fnv)) return false;
  if (!in.field("size", rest) || !uintToken(rest, MAX_SIDE, v) || !uintToken(rest, MAX_SIDE, v2) || !rest.empty()) {
    return false;
  }
  if (v < MIN_SIDE || v2 < MIN_SIDE) return false;
  out.w = static_cast<uint8_t>(v);
  out.h = static_cast<uint8_t>(v2);
  if (!in.field("cursor", rest) || !uintToken(rest, out.h - 1u, v) || !uintToken(rest, out.w - 1u, v2) ||
      !token(rest, tok) || !rest.empty() || (tok != "A" && tok != "D")) {
    return false;
  }
  out.cursor = static_cast<uint8_t>(v * out.w + v2);
  out.dir = tok == "D" ? DOWN : ACROSS;
  if (!in.field("elapsed", rest) || !parseUint(rest, ELAPSED_MAX, v)) return false;
  out.elapsed = static_cast<uint32_t>(v);
  if (!in.field("counts", rest) || !uintToken(rest, 0xFFFF, v) || !uintToken(rest, 0xFFFF, v2) || !rest.empty()) {
    return false;
  }
  out.checks = static_cast<uint16_t>(v);
  out.reveals = static_cast<uint16_t>(v2);
  if (!in.field("solved", rest) || !parseUint(rest, 1, v)) return false;
  out.solved = v == 1;
  if (!in.field("seq", rest) || !parseUint(rest, 0xFFFFFFFFull, v)) return false;
  out.seq = static_cast<uint32_t>(v);
  for (int r = 0; r < out.h; r++) {
    if (!in.field("row", rest) || rest.size() != out.w) return false;
    for (int c = 0; c < out.w; c++) {
      const char ch = rest[c];
      if (ch != '.' && ch != BLOCK && (ch < 'A' || ch > 'Z')) return false;
      out.fill[r * out.w + c] = ch == '.' ? EMPTY : ch;
    }
  }
  for (int r = 0; r < out.h; r++) {
    if (!in.field("flags", rest) || rest.size() != out.w) return false;
    for (int c = 0; c < out.w; c++) {
      const char ch = rest[c];
      const char f = out.fill[r * out.w + c];
      if (ch == '.') continue;
      if ((ch != 'w' && ch != 'r') || f == EMPTY || f == BLOCK) return false;
      out.flags[r * out.w + c] = ch == 'r' ? FLAG_REVEALED : FLAG_WRONG;
    }
  }
  if (!in.line(l) || l != "end" || !in.atEnd()) return false;
  if (out.fill[out.cursor] == BLOCK) return false;
  return true;
}

size_t progressPath(const uint32_t fnv, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  const int n = std::snprintf(out, cap, "%s/%08lx.dat", PROGRESS_DIR, static_cast<unsigned long>(fnv));
  return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

bool progressFileFnv(const char* name, uint32_t& fnv) {
  if (!name || std::strlen(name) != 12 || std::strcmp(name + 8, ".dat") != 0) return false;
  return parseHex32(std::string_view(name, 8), fnv);
}

int pruneVictim(const SavedProgress* saved, const int count, const uint32_t currentFnv) {
  if (!saved || count <= MAX_PROGRESS_FILES) return -1;
  int victim = -1;
  for (int i = 0; i < count; i++) {
    if (saved[i].fnv == currentFnv) continue;
    if (victim < 0 || saved[i].seq < saved[victim].seq) victim = i;
  }
  return victim;
}

// ---- solved list --------------------------------------------------------------------------------

size_t formatSolvedLine(const uint32_t fnv, const char* sourceKey, char* out, const size_t cap) {
  if (!out || cap == 0 || !sourceKey) return 0;
  const size_t keyLen = std::strlen(sourceKey);
  if (keyLen == 0 || keyLen > MAX_SOURCE_KEY || !printable(std::string_view(sourceKey, keyLen))) return 0;
  const int n = std::snprintf(out, cap, "%08lx %s\n", static_cast<unsigned long>(fnv), sourceKey);
  return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

bool parseSolvedLine(std::string_view line, uint32_t& fnv, std::string_view& sourceKey) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  if (line.size() < 10 || line[8] != ' ' || !parseHex32(line.substr(0, 8), fnv)) return false;
  sourceKey = line.substr(9);
  return !sourceKey.empty() && sourceKey.size() <= MAX_SOURCE_KEY && printable(sourceKey);
}

bool solvedListHas(const char* text, const size_t len, const std::string_view sourceKey, const uint32_t fnv) {
  if (!text) return false;
  Reader in{text, text + len};
  std::string_view l;
  while (in.line(l)) {
    uint32_t f = 0;
    std::string_view key;
    if (!parseSolvedLine(l, f, key)) continue;
    if (sourceKey.empty() && fnv == 0) return false;
    if ((sourceKey.empty() || key == sourceKey) && (fnv == 0 || f == fnv)) return true;
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

}  // namespace cw
