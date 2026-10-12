#include "AiText.h"

#include <strings.h>

#include <cstring>

#include "util/CardPath.h"

namespace aitext {

namespace {

// Bytes go in while they fit, the count goes on; a result that does not fit is left empty.
struct Writer {
  char* out;
  size_t cap;
  size_t n;
};

void put(Writer& w, const char* s, const size_t len) {
  if (w.out && w.n + len < w.cap) std::memcpy(w.out + w.n, s, len);
  w.n += len;
}

void put(Writer& w, const char* s) { put(w, s, std::strlen(s)); }

size_t finish(Writer& w) {
  if (w.out && w.cap) w.out[w.n < w.cap ? w.n : 0] = '\0';
  return w.n;
}

bool isBlank(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool startsWith(const char* s, const size_t len, const char* prefix) {
  const size_t k = std::strlen(prefix);
  return len >= k && std::memcmp(s, prefix, k) == 0;
}

// [s, s + len) on one line: every run of whitespace one space, both ends trimmed.
void putOneLine(Writer& w, const char* s, size_t len) {
  while (len && isBlank(*s)) {
    s++;
    len--;
  }
  while (len && isBlank(s[len - 1])) len--;
  bool space = false;
  for (size_t i = 0; i < len; i++) {
    if (isBlank(s[i])) {
      space = true;
      continue;
    }
    if (space) put(w, " ", 1);
    space = false;
    put(w, s + i, 1);
  }
}

// The length of the question's first `max` bytes cut at the last space (or at a character
// boundary when it has none past a third of it).
size_t cutAtWord(const char* s, const size_t len, const size_t max) {
  if (len <= max) return len;
  size_t cut = max;
  while (cut > max / 3 && s[cut] != ' ') cut--;
  if (cut <= max / 3) {
    cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) cut--;
  }
  while (cut > 0 && (s[cut - 1] == ' ' || s[cut - 1] == ',' || s[cut - 1] == ';')) cut--;
  return cut;
}

// One line of the answer as a block: a bullet, a step, or a paragraph (escaped when the page
// format would read it as markup). Blank lines and items with no text are left out.
void putBlock(Writer& w, const char* s, size_t len) {
  while (len && isBlank(*s)) {
    s++;
    len--;
  }
  while (len && isBlank(s[len - 1])) len--;
  if (len == 0 || (len == 1 && s[0] == '-')) return;  // an empty bullet (its space trimmed)
  if (startsWith(s, len, "- ")) {
    size_t k = 2;
    while (k < len && s[k] == ' ') k++;
    if (k == len) return;
    put(w, "- ");
    put(w, s + k, len - k);
    put(w, "\n");
    return;
  }
  size_t digits = 0;
  int number = 0;
  while (digits < len && digits < 4 && s[digits] >= '0' && s[digits] <= '9') {
    number = number * 10 + (s[digits] - '0');
    digits++;
  }
  if (digits >= 1 && digits <= 3 && len == digits + 1 && s[digits] == '.') return;  // an empty step
  const bool stepShape = digits >= 1 && digits <= 3 && digits + 1 < len && s[digits] == '.' && s[digits + 1] == ' ';
  if (stepShape) {
    size_t k = digits + 2;
    while (k < len && s[k] == ' ') k++;
    if (k == len) return;
    // Steps 1..99: the layout right-aligns a step's number left of the text, and three digits would
    // reach into the bezel; "100." and "0." stay paragraphs.
    if (number >= 1 && digits <= 2) {
      put(w, s, digits);
      put(w, ". ");
      put(w, s + k, len - k);
      put(w, "\n");
      return;
    }
  }
  const char c = s[0];
  const bool markup = c == '=' || c == '@' || c == '!' || c == '*' || c == '\\' || stepShape ||
                      (len == 3 && std::memcmp(s, "---", 3) == 0);
  if (markup) put(w, "\\");
  put(w, s, len);
  put(w, "\n");
}

// Hinnant's days_from_civil: days since 1970-01-01.
int64_t daysFromCivil(int64_t y, const int m, const int d) {
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

}  // namespace

bool isKeyDirName(const char* name) { return name != nullptr && strcasecmp(name, KEY_DIR) == 0; }

bool isKeyFileName(const char* name) { return name != nullptr && strcasecmp(name, KEY_FILE) == 0; }

namespace {

bool isKeyDirSpan(const char* name, const size_t len) {
  return len == sizeof(KEY_DIR) - 1 && strncasecmp(name, KEY_DIR, len) == 0;
}

}  // namespace

bool isKeyFolderPath(const char* path) {
  if (path == nullptr) return false;
  const char* name = nullptr;
  size_t len = 0;
  return cardpath::nextName(path, name, len) && isKeyDirSpan(name, len);
}

bool pathHasKeyDir(const char* path) {
  if (path == nullptr) return false;
  const char* name = nullptr;
  size_t len = 0;
  while (cardpath::nextName(path, name, len)) {
    if (isKeyDirSpan(name, len)) return true;
  }
  return false;
}

size_t buildPage(const char* question, const char* note, const char* body, char* out, const size_t cap) {
  Writer w{out, cap, 0};
  const char* q = question ? question : "";
  size_t qlen = std::strlen(q);
  while (qlen && isBlank(*q)) {
    q++;
    qlen--;
  }
  while (qlen && isBlank(q[qlen - 1])) qlen--;

  const size_t cut = cutAtWord(q, qlen, TITLE_MAX - 3);
  const bool titleCut = cut > 0 && cut < qlen;
  put(w, "= ");
  if (qlen == 0) {
    put(w, "Question");
  } else if (titleCut) {
    putOneLine(w, q, cut);
    put(w, "...");
  } else {
    putOneLine(w, q, qlen);
  }
  put(w, "\n");
  if (titleCut) {
    // The title had to be cut: the whole question opens the page, as a paragraph (a box would move
    // whole to the next screen and leave this one nearly empty).
    put(w, "\\");
    putOneLine(w, q, qlen);
    put(w, "\n");
  }
  if (note && note[0]) {
    const size_t before = w.n;
    put(w, "* ");
    const size_t mark = w.n;
    putOneLine(w, note, std::strlen(note));
    if (w.n == mark) {
      w.n = before;  // a note of blanks: none
    } else {
      put(w, "\n");
    }
  }
  const char* s = body ? body : "";
  while (*s) {
    const char* e = std::strchr(s, '\n');
    const size_t len = e ? static_cast<size_t>(e - s) : std::strlen(s);
    putBlock(w, s, len);
    s += len;
    if (*s == '\n') s++;
  }
  return finish(w);
}

void zoneWord(const char* entryName, const char* mapName, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  const char* src = (mapName && mapName[0]) ? mapName : entryName;
  if (src == nullptr) return;
  size_t len = std::strlen(src);
  if (src != mapName) {
    const char* slash = std::strchr(src, '/');
    if (slash) len = static_cast<size_t>(slash - src);
  }
  while (len && src[len - 1] == ' ') len--;
  if (len >= cap) len = cap - 1;
  std::memcpy(out, src, len);
  out[len] = '\0';
}

int32_t utcOffsetSeconds(const int year, const int month, const int day, const int hour, const int minute,
                         const int second, const int64_t utc) {
  const int64_t local = daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
  int64_t diff = local - utc;
  // To the minute: the clock read the two halves a moment apart.
  diff = diff >= 0 ? (diff + 30) / 60 * 60 : -((-diff + 30) / 60 * 60);
  return static_cast<int32_t>(diff);
}

}  // namespace aitext
