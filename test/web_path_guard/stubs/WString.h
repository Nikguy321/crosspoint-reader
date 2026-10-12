#pragma once

// Minimal native stand-in for the Arduino String class: what network/WebPathGuard uses.
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : value_(s ? s : "") {}
  String(const std::string& s) : value_(s) {}

  const char* c_str() const { return value_.c_str(); }
  unsigned int length() const { return static_cast<unsigned int>(value_.length()); }
  char charAt(unsigned int i) const { return i < value_.length() ? value_[i] : '\0'; }
  int indexOf(char c, unsigned int from) const {
    const size_t at = value_.find(c, from);
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  String substring(unsigned int from, unsigned int to) const {
    if (from > value_.length()) from = static_cast<unsigned int>(value_.length());
    if (to > value_.length()) to = static_cast<unsigned int>(value_.length());
    return to > from ? String(value_.substr(from, to - from)) : String();
  }

 private:
  std::string value_;
};
