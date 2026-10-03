#include "WsThemes.h"

#include <cstring>

namespace ws {

namespace {

bool isTrimmable(const char c) { return c == ' ' || c == '\t' || c == '\r'; }

// [begin, end) of the next line starting at pos; pos moves past its '\n'.
void nextLine(const char* text, const size_t len, size_t& pos, size_t& begin, size_t& end) {
  begin = pos;
  while (pos < len && text[pos] != '\n') pos++;
  end = pos;
  if (pos < len) pos++;
  while (begin < end && isTrimmable(text[begin])) begin++;
  while (end > begin && isTrimmable(text[end - 1])) end--;
}

size_t skipBom(const char* text, const size_t len) {
  return (len >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
          static_cast<unsigned char>(text[2]) == 0xBF)
             ? 3
             : 0;
}

// A title: at most MAX_TITLE_LEN bytes (and cap - 1), cut before a UTF-8 continuation byte;
// control bytes become spaces.
void copyTitle(const char* src, size_t n, char* out, const size_t cap) {
  size_t limit = cap - 1 < static_cast<size_t>(MAX_TITLE_LEN) ? cap - 1 : static_cast<size_t>(MAX_TITLE_LEN);
  if (n > limit) {
    n = limit;
    while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) n--;
  }
  for (size_t i = 0; i < n; i++) {
    const auto c = static_cast<unsigned char>(src[i]);
    out[i] = c < 0x20 || c == 0x7F ? ' ' : static_cast<char>(c);
  }
  out[n] = '\0';
  while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
}

}  // namespace

size_t normalizeWordLine(const char* line, size_t len, char* out, const size_t cap) {
  if (!line || !out || cap == 0) return 0;
  size_t begin = 0;
  while (begin < len && isTrimmable(line[begin])) begin++;
  while (len > begin && isTrimmable(line[len - 1])) len--;
  size_t n = 0;
  int letters = 0;
  for (size_t i = begin; i < len; i++) {
    char c = line[i];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') {
      letters++;
    } else if (c == ' ') {
      if (n > 0 && out[n - 1] == ' ') continue;  // collapse runs
    } else if (c != '-' && c != '\'') {
      return 0;
    }
    if (n + 1 >= cap || n >= static_cast<size_t>(MAX_DISPLAY_LEN)) return 0;
    out[n++] = c;
  }
  out[n] = '\0';
  if (letters < MIN_WORD_LETTERS || letters > MAX_WORD_LETTERS) return 0;
  return n;
}

bool loadBuiltinTheme(const BuiltinTheme& theme, ThemeWords& out) {
  out.words.clear();
  if (!theme.title || !theme.words) return false;
  copyTitle(theme.title, std::strlen(theme.title), out.title, sizeof(out.title));
  size_t count = 1;
  for (const char* p = theme.words; *p; p++) count += *p == '|' ? 1 : 0;
  out.words.reserve(count);
  const char* start = theme.words;
  for (const char* p = theme.words;; p++) {
    if (*p == '|' || *p == '\0') {
      if (p > start) out.words.emplace_back(start, static_cast<size_t>(p - start));
      if (*p == '\0') break;
      start = p + 1;
    }
  }
  return out.title[0] != '\0' && out.words.size() >= MIN_FILE_WORDS;
}

bool parseThemeFile(char* text, const size_t len, ThemeWords& out) {
  out.words.clear();
  out.title[0] = '\0';
  if (!text) return false;
  // A generous first guess: about one word per 8 bytes, never more than the cap.
  const size_t guess = len / 8 + 1;
  out.words.reserve(guess < MAX_FILE_WORDS ? guess : MAX_FILE_WORDS);
  bool haveTitle = false;
  size_t pos = skipBom(text, len);
  char word[MAX_DISPLAY_LEN + 1];
  while (pos < len && out.words.size() < MAX_FILE_WORDS) {
    size_t begin = 0;
    size_t end = 0;
    nextLine(text, len, pos, begin, end);
    if (begin == end || text[begin] == '#') continue;
    if (!haveTitle) {
      copyTitle(text + begin, end - begin, out.title, sizeof(out.title));
      haveTitle = out.title[0] != '\0';
      continue;
    }
    const size_t n = normalizeWordLine(text + begin, end - begin, word, sizeof(word));
    if (n == 0) continue;
    // The normalised text is never longer than the trimmed line, so it fits where the line was.
    std::memcpy(text + begin, word, n);
    out.words.emplace_back(text + begin, n);
  }
  return haveTitle && out.words.size() >= MIN_FILE_WORDS;
}

bool readThemeTitle(const char* text, const size_t len, char* out, const size_t cap) {
  if (!text || !out || cap == 0) return false;
  out[0] = '\0';
  size_t pos = skipBom(text, len);
  while (pos < len) {
    size_t begin = 0;
    size_t end = 0;
    nextLine(text, len, pos, begin, end);
    if (begin == end || text[begin] == '#') continue;
    copyTitle(text + begin, end - begin, out, cap);
    if (out[0] != '\0') return true;
  }
  return false;
}

bool makeFileThemeKey(const char* fileName, char* out, const size_t cap) {
  if (!fileName || !out || fileName[0] == '\0') return false;
  const size_t prefix = sizeof(FILE_KEY_PREFIX) - 1;
  const size_t n = std::strlen(fileName);
  if (prefix + n > static_cast<size_t>(MAX_THEME_KEY) || prefix + n + 1 > cap) return false;
  for (size_t i = 0; i < n; i++) {
    const auto c = static_cast<unsigned char>(fileName[i]);
    if (c < 0x20 || c == 0x7F) return false;
  }
  std::memcpy(out, FILE_KEY_PREFIX, prefix);
  std::memcpy(out + prefix, fileName, n + 1);
  return true;
}

const char* fileNameFromKey(const char* key) {
  const size_t prefix = sizeof(FILE_KEY_PREFIX) - 1;
  if (!key || std::strncmp(key, FILE_KEY_PREFIX, prefix) != 0 || key[prefix] == '\0') return nullptr;
  return key + prefix;
}

}  // namespace ws
