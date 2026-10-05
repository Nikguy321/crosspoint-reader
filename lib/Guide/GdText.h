#pragma once

// Survival guide: small text helpers the pack, page, search and state code share. Pure.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gd {

// Walks a mutable buffer line by line, NUL-terminating each line in place (a trailing '\r' is
// dropped too). EVERY in-place parser in lib/Guide needs text[len] writable (the buffer is one
// byte larger than the text: the store reads a file into len + 1 bytes), because a last line
// without '\n' is terminated there. NUL bytes inside the text end their line early (harmless).
class LineSplitter {
 public:
  LineSplitter(char* text, size_t len) : p(text), end(text + len) {}
  // The next line (NUL-terminated in place) and its length; false at the end.
  bool next(char*& line, size_t& lineLen);
  int lineNumber() const { return number; }  // 1-based number of the last line returned

 private:
  char* p;
  char* end;
  int number = 0;
};

// Splits a NUL-terminated line on TABs in place. Returns the number of fields found; only the
// first cap are stored (the rest are still split off and counted).
int splitTabs(char* line, char** fields, int cap);

// Decimal 0..hi, the whole view. False otherwise.
bool parseUint(std::string_view s, uint32_t hi, uint32_t& out);

// Copies src into out (cap includes the NUL), cut on a UTF-8 character boundary. Returns the
// length written.
size_t copyCut(std::string_view src, char* out, size_t cap);

// ASCII helpers.
inline char lowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
inline char upperAscii(const char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }
inline bool isAlnumLower(const char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
// Case-insensitive (ASCII) substring search. An empty needle is found at 0.
bool containsNoCase(std::string_view hay, std::string_view needle);
std::string_view trim(std::string_view s);

}  // namespace gd
