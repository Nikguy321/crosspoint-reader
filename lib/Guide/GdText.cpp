#include "GdText.h"

#include <cstring>

namespace gd {

bool LineSplitter::next(char*& line, size_t& lineLen) {
  if (p >= end) return false;
  char* start = p;
  while (p < end && *p != '\n') p++;
  char* stop = p;
  if (p < end) p++;  // past the '\n'
  if (stop > start && stop[-1] == '\r') stop--;
  *stop = '\0';  // stop <= end: text[len] is writable by contract
  line = start;
  lineLen = std::strlen(start);  // a NUL inside the line ends it early
  number++;
  return true;
}

int splitTabs(char* line, char** fields, const int cap) {
  int n = 0;
  char* f = line;
  while (true) {
    if (n < cap) fields[n] = f;
    n++;
    char* tab = std::strchr(f, '\t');
    if (!tab) break;
    *tab = '\0';
    f = tab + 1;
  }
  return n;
}

bool parseUint(const std::string_view s, const uint32_t hi, uint32_t& out) {
  if (s.empty() || s.size() > 10) return false;
  uint64_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  if (v > hi) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

size_t copyCut(const std::string_view src, char* out, const size_t cap) {
  if (cap == 0) return 0;
  size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
  if (n < src.size()) {
    // Do not split a UTF-8 sequence: back up over continuation bytes to a lead byte.
    while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80) n--;
  }
  std::memcpy(out, src.data(), n);
  out[n] = '\0';
  return n;
}

bool containsNoCase(const std::string_view hay, const std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > hay.size()) return false;
  for (size_t i = 0; i + needle.size() <= hay.size(); i++) {
    size_t k = 0;
    while (k < needle.size() && lowerAscii(hay[i + k]) == lowerAscii(needle[k])) k++;
    if (k == needle.size()) return true;
  }
  return false;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

}  // namespace gd
