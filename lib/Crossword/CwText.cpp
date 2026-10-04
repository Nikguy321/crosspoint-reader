#include "CwText.h"

#include <cstdio>
#include <cstring>

#include "CwNumbering.h"

namespace cw {

namespace {

struct Interval {
  uint32_t lo;
  uint32_t hi;
};
// Non-ASCII text ranges every UI font (ubuntu_10/12 regular and bold) has a glyph for: Latin-1
// and Latin Extended-A/B letters, Cyrillic, Vietnamese, and a few symbols.
constexpr Interval UI_GLYPHS[] = {
    {0xA1, 0xAC},   {0xAE, 0x17F},    {0x1A0, 0x1A1},   {0x1AF, 0x1B0},   {0x1C4, 0x21F},
    {0x400, 0x45F}, {0x1EA0, 0x1EF9}, {0x2020, 0x2022}, {0x2030, 0x2030}, {0x20AC, 0x20AC},
};

// Windows-1252 0x80-0x9F (0: undefined).
constexpr uint16_t CP1252_HIGH[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

bool isSpaceCp(const uint32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F ||
         c == 0x205F || c == 0x3000;
}

bool isDroppedCp(const uint32_t c) {
  return c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0xAD || (c >= 0x200B && c <= 0x200F) || c == 0xFEFF ||
         (c >= 0x300 && c <= 0x36F) || (c >= 0x2060 && c <= 0x2064);
}

struct NamedEntity {
  const char* name;
  uint32_t cp;
};
constexpr NamedEntity ENTITIES[] = {
    {"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},      {"apos", '\''},
    {"nbsp", 0xA0},    {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"lsquo", 0x2018},
    {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"eacute", 0xE9},   {"egrave", 0xE8},
    {"aacute", 0xE1},  {"iacute", 0xED},  {"oacute", 0xF3},  {"uacute", 0xFA},   {"ntilde", 0xF1},
    {"uuml", 0xFC},    {"ouml", 0xF6},    {"auml", 0xE4},    {"ccedil", 0xE7},   {"deg", 0xB0},
};

bool isAlnum(const uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// The value of a decoded entity body ("amp", "#39", "#x2019"); 0 = not one we know.
uint32_t entityValue(const char* body, const size_t n) {
  if (n >= 2 && body[0] == '#') {
    uint32_t v = 0;
    const bool hex = body[1] == 'x' || body[1] == 'X';
    size_t i = hex ? 2 : 1;
    if (i >= n) return 0;
    for (; i < n; i++) {
      const char c = body[i];
      uint32_t d = 0;
      if (c >= '0' && c <= '9') {
        d = static_cast<uint32_t>(c - '0');
      } else if (hex && c >= 'a' && c <= 'f') {
        d = static_cast<uint32_t>(c - 'a' + 10);
      } else if (hex && c >= 'A' && c <= 'F') {
        d = static_cast<uint32_t>(c - 'A' + 10);
      } else {
        return 0;
      }
      v = v * (hex ? 16 : 10) + d;
      if (v > 0x10FFFF) return 0;
    }
    return v;
  }
  for (const NamedEntity& e : ENTITIES) {
    if (std::strlen(e.name) == n && std::memcmp(e.name, body, n) == 0) return e.cp;
  }
  return 0;
}

size_t encodeUtf8(const uint32_t c, char* out) {
  if (c < 0x80) {
    out[0] = static_cast<char>(c);
    return 1;
  }
  if (c < 0x800) {
    out[0] = static_cast<char>(0xC0 | (c >> 6));
    out[1] = static_cast<char>(0x80 | (c & 0x3F));
    return 2;
  }
  if (c < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (c >> 12));
    out[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (c & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (c >> 18));
  out[1] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (c & 0x3F));
  return 4;
}

}  // namespace

bool uiFontHasGlyph(const uint32_t cp) {
  if (cp >= 0x20 && cp < 0x7F) return true;
  for (const Interval& r : UI_GLYPHS) {
    if (cp >= r.lo && cp <= r.hi) return true;
  }
  return false;
}

// ---- TextCleaner --------------------------------------------------------------------------------

TextCleaner::TextCleaner(char* out, const size_t cap, const uint8_t flags) : out(out), cap(cap), flags(flags) {
  if (out && cap > 0) out[0] = '\0';
}

void TextCleaner::put(const char byte) { decodeByte(static_cast<uint8_t>(byte)); }

void TextCleaner::put(const char* s, const size_t n) {
  for (size_t i = 0; i < n; i++) decodeByte(static_cast<uint8_t>(s[i]));
}

void TextCleaner::decodeByte(const uint8_t b) {
  if (flags & TEXT_LATIN1) {
    // Across Lite text is Windows-1252 in practice: its 0x80-0x9F are curly quotes, dashes and the
    // ellipsis, not C1 controls (the bytes it leaves undefined stay dropped).
    if (b >= 0x80 && b <= 0x9F) {
      const uint16_t cp = CP1252_HIGH[b - 0x80];
      htmlCodepoint(cp != 0 ? cp : b);
      return;
    }
    htmlCodepoint(b);
    return;
  }
  if (need > 0) {
    if ((b & 0xC0) == 0x80) {
      cp = (cp << 6) | (b & 0x3F);
      if (--need == 0) htmlCodepoint(cp);
      return;
    }
    need = 0;
    htmlCodepoint('?');  // a sequence cut short; this byte starts afresh
  }
  if (b < 0x80) {
    htmlCodepoint(b);
  } else if ((b & 0xE0) == 0xC0) {
    cp = b & 0x1F;
    need = 1;
  } else if ((b & 0xF0) == 0xE0) {
    cp = b & 0x0F;
    need = 2;
  } else if ((b & 0xF8) == 0xF0) {
    cp = b & 0x07;
    need = 3;
  } else {
    htmlCodepoint('?');
  }
}

void TextCleaner::flushEntity() {
  // Not an entity after all: the '&' and what followed it, as text.
  html = 0;
  emit('&');
  for (uint8_t i = 0; i < entLen; i++) emit(static_cast<uint8_t>(ent[i]));
  entLen = 0;
}

void TextCleaner::htmlCodepoint(const uint32_t c) {
  if (!(flags & TEXT_HTML)) {
    emit(c);
    return;
  }
  switch (html) {
    case 1:  // after '<': a tag only when a letter, '/' or '!' follows
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '/' || c == '!') {
        html = 2;
        tagLen = 0;
        tagNameOpen = true;
        if (c == '/' || c == '!') return;
        tag[tagLen++] = static_cast<char>(c | 0x20);
        return;
      }
      html = 0;
      emit('<');
      htmlCodepoint(c);
      return;
    case 2:  // inside a tag; <br> and <p> read as a space
      if (c == '>') {
        html = 0;
        if ((tagLen == 2 && tag[0] == 'b' && tag[1] == 'r') || (tagLen == 1 && tag[0] == 'p')) emit(' ');
        return;
      }
      if (tagNameOpen && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        if (tagLen < sizeof(tag)) tag[tagLen++] = static_cast<char>(c | 0x20);
        // A longer name than tag[] holds stays at sizeof(tag): neither br nor p.
      } else if (tagLen > 0) {
        tagNameOpen = false;
      }
      return;
    case 3:  // inside an entity
      if (c == ';') {
        const uint32_t v = entityValue(ent, entLen);
        if (v == 0) {
          flushEntity();
          emit(';');
          return;
        }
        html = 0;
        entLen = 0;
        emit(v);
        return;
      }
      if ((isAlnum(c) || (c == '#' && entLen == 0)) && entLen < sizeof(ent)) {
        ent[entLen++] = static_cast<char>(c);
        return;
      }
      flushEntity();
      htmlCodepoint(c);
      return;
    default:
      if (c == '<') {
        html = 1;
        return;
      }
      if (c == '&') {
        html = 3;
        entLen = 0;
        return;
      }
      emit(c);
  }
}

void TextCleaner::emit(const uint32_t c) {
  if (cut) return;
  if (isSpaceCp(c)) {
    if (len > 0) pendingSpace = true;
    return;
  }
  if (isDroppedCp(c)) return;
  char buf[4];
  switch (c) {
    case 0x2018:
    case 0x2019:
    case 0x201A:
    case 0x201B:
    case 0x2032:
      append("'", 1);
      return;
    case 0x201C:
    case 0x201D:
    case 0x201E:
    case 0x201F:
    case 0x2033:
      append("\"", 1);
      return;
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2015:
    case 0x2043:
    case 0x2212:
      append("-", 1);
      return;
    case 0x2026:
      append("...", 3);
      return;
    default:
      break;
  }
  if (!uiFontHasGlyph(c)) {
    append("?", 1);
    return;
  }
  append(buf, encodeUtf8(c, buf));
}

void TextCleaner::append(const char* utf8, const size_t n) {
  if (cut || !out || cap == 0) return;
  const size_t space = pendingSpace ? 1 : 0;
  if (len + space + n > cap - 1) {
    cut = true;
    return;
  }
  if (space) out[len++] = ' ';
  pendingSpace = false;
  std::memcpy(out + len, utf8, n);
  len += n;
}

size_t TextCleaner::finish() {
  if (html == 3) flushEntity();
  if (html == 1) {
    html = 0;
    emit('<');
  }
  if (need > 0) {
    need = 0;
    emit('?');
  }
  html = 0;
  pendingSpace = false;
  if (out && cap > 0) out[len] = '\0';
  return len;
}

size_t cleanText(const char* in, const size_t len, char* out, const size_t cap, const uint8_t flags, bool* truncated) {
  TextCleaner tc(out, cap, flags);
  if (in) tc.put(in, len);
  const size_t n = tc.finish();
  if (truncated) *truncated = tc.truncated();
  return n;
}

// ---- the clue pool ------------------------------------------------------------------------------

bool beginClue(Puzzle& p, TextCleaner& out, const uint8_t flags) {
  if (static_cast<size_t>(p.clueUsed) + MAX_CLUE_BYTES + 1 > CLUE_POOL) return false;
  out = TextCleaner(p.clues + p.clueUsed, MAX_CLUE_BYTES + 1, flags);
  return true;
}

void commitClue(Puzzle& p, const int entry, TextCleaner& cleaner) {
  const size_t n = cleaner.finish();
  if (entry < 0 || entry >= p.entryCount) return;
  p.entries[entry].clueOff = p.clueUsed;
  p.entries[entry].clueLen = static_cast<uint16_t>(n);
  p.clueUsed = static_cast<uint16_t>(p.clueUsed + n + 1);
}

Error setClue(Puzzle& p, const int entry, const char* text, const size_t len, const uint8_t flags, bool* truncated) {
  TextCleaner tc(nullptr, 0, flags);
  if (!beginClue(p, tc, flags)) return Error::TooManyClues;
  if (text) tc.put(text, len);
  if (truncated) *truncated = tc.truncated();
  commitClue(p, entry, tc);
  return Error::None;
}

// ---- the built-in text format -------------------------------------------------------------------

bool validBuiltinId(const char* id, const size_t len) {
  if (!id || len == 0 || len > MAX_BUILTIN_ID) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

namespace {

struct Line {
  const char* s;
  size_t n;
};

bool nextLine(const char*& p, const char* end, Line& out) {
  if (p >= end) return false;
  const char* start = p;
  while (p < end && *p != '\n') p++;
  const char* stop = p;
  if (p < end) p++;
  if (stop > start && stop[-1] == '\r') stop--;
  out = Line{start, static_cast<size_t>(stop - start)};
  return true;
}

bool isSkippable(const Line& l) {
  bool blank = true;
  for (size_t i = 0; i < l.n; i++) blank = blank && (l.s[i] == ' ' || l.s[i] == '\t');
  if (blank) return true;
  return l.s[0] == '#' && (l.n == 1 || l.s[1] == ' ');
}

bool isGridRow(const Line& l) {
  if (l.n == 0) return false;
  for (size_t i = 0; i < l.n; i++) {
    if (!((l.s[i] >= 'A' && l.s[i] <= 'Z') || l.s[i] == BLOCK)) return false;
  }
  return true;
}

void trimView(const char*& s, size_t& n) {
  while (n > 0 && (*s == ' ' || *s == '\t')) {
    s++;
    n--;
  }
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
}

}  // namespace

TextStatus parseTextPuzzle(const char* text, const size_t len, Puzzle& p) {
  p.reset();
  TextStatus st;
  const auto fail = [&st](const Error e, const int line) {
    st.error = e;
    st.line = line;
    return st;
  };
  if (!text) return fail(Error::Damaged, 0);
  const char* cur = text;
  const char* end = text + len;
  Line l{};
  int lineNo = 0;
  // The header.
  bool header = false;
  while (nextLine(cur, end, l)) {
    lineNo++;
    if (isSkippable(l)) continue;
    if (l.n < 5 || std::memcmp(l.s, "=== ", 4) != 0) return fail(Error::Damaged, lineNo);
    const char* rest = l.s + 4;
    const char* bar = static_cast<const char*>(std::memchr(rest, '|', l.n - 4));
    if (!bar) return fail(Error::Damaged, lineNo);
    const char* id = rest;
    size_t idLen = static_cast<size_t>(bar - rest);
    trimView(id, idLen);
    const char* title = bar + 1;
    size_t titleLen = static_cast<size_t>((l.s + l.n) - title);
    trimView(title, titleLen);
    if (!validBuiltinId(id, idLen) || titleLen == 0 || titleLen > MAX_TITLE) return fail(Error::Damaged, lineNo);
    std::snprintf(p.sourceKey, sizeof(p.sourceKey), "builtin:%.*s", static_cast<int>(idLen), id);
    cleanText(title, titleLen, p.title, sizeof(p.title), 0);
    header = true;
    break;
  }
  if (!header) return fail(Error::Damaged, lineNo);
  // The grid.
  int rows = 0;
  bool haveLine = false;
  while (nextLine(cur, end, l)) {
    lineNo++;
    if (isSkippable(l)) continue;
    if (!isGridRow(l)) {
      if (l.n < 2 || (l.s[0] != 'A' && l.s[0] != 'D') || l.s[1] != ' ') return fail(Error::Damaged, lineNo);
      haveLine = true;
      break;
    }
    if (rows == 0) {
      if (l.n > MAX_SIDE) return fail(Error::TooBig, lineNo);
      p.w = static_cast<uint8_t>(l.n);
    } else if (l.n != p.w) {
      return fail(Error::Damaged, lineNo);
    }
    if (rows >= MAX_SIDE) return fail(Error::TooBig, lineNo);
    std::memcpy(p.solution + rows * p.w, l.s, l.n);
    rows++;
  }
  p.h = static_cast<uint8_t>(rows);
  const Error numbered = numberGrid(p);
  if (numbered != Error::None) return fail(numbered, lineNo);
  // The clues.
  bool seen[MAX_ENTRIES] = {};
  while (haveLine || nextLine(cur, end, l)) {
    if (!haveLine) lineNo++;
    haveLine = false;
    if (isSkippable(l)) continue;
    if (l.n < 5 || (l.s[0] != 'A' && l.s[0] != 'D') || l.s[1] != ' ') return fail(Error::Damaged, lineNo);
    size_t i = 2;
    int number = 0;
    while (i < l.n && l.s[i] >= '0' && l.s[i] <= '9' && number < 1000) number = number * 10 + (l.s[i++] - '0');
    if (i == 2 || i >= l.n || l.s[i] != ' ') return fail(Error::Damaged, lineNo);
    const char* clue = l.s + i + 1;
    size_t clueLen = l.n - i - 1;
    trimView(clue, clueLen);
    if (clueLen == 0) return fail(Error::Damaged, lineNo);
    const int e = findEntry(p, l.s[0] == 'A' ? ACROSS : DOWN, number);
    if (e < 0 || seen[e]) return fail(Error::ClueMismatch, lineNo);
    seen[e] = true;
    const Error set = setClue(p, e, clue, clueLen, 0);
    if (set != Error::None) return fail(set, lineNo);
  }
  for (int e = 0; e < p.entryCount; e++) {
    if (!seen[e]) return fail(Error::ClueMismatch, 0);
  }
  return st;
}

}  // namespace cw
