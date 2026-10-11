#pragma once

// A small JSON reader for vectors_tzgrid.json - the WiPhone repository's tests/vectors_tzgrid.json,
// copied here byte for byte - shared by TzGridTest and ClockZoneTest. Objects, arrays, strings
// (with escapes), integers, reals, true, false and null: what that file holds. Test-only.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace tzv {

struct Value {
  enum class Kind : uint8_t { Null, Bool, Int, Real, String, Array, Object };
  Kind kind = Kind::Null;
  bool flag = false;
  int64_t integer = 0;
  double real = 0;
  std::string text;
  std::vector<Value> items;
  std::vector<std::pair<std::string, Value>> members;

  bool isNull() const { return kind == Kind::Null; }
  int64_t asInt() const { return kind == Kind::Real ? static_cast<int64_t>(real) : integer; }
  const char* str() const { return text.c_str(); }
  size_t size() const { return kind == Kind::Array ? items.size() : members.size(); }

  // A null Value when absent.
  const Value& operator[](const char* key) const {
    for (const auto& member : members) {
      if (member.first == key) return member.second;
    }
    return none();
  }
  const Value& operator[](const size_t index) const { return index < items.size() ? items[index] : none(); }
  // An int index (p[0]) would otherwise be ambiguous with the key overload.
  const Value& operator[](const int index) const { return index < 0 ? none() : (*this)[static_cast<size_t>(index)]; }

  static const Value& none() {
    static const Value NONE;
    return NONE;
  }
};

class Parser {
 public:
  explicit Parser(const std::string& text) : s(text) {}

  bool parse(Value& out, std::string& error) {
    if (!value(out) || (skip(), pos != s.size())) {
      char where[64];
      snprintf(where, sizeof(where), "JSON error near byte %zu", pos);
      error = where;
      return false;
    }
    return true;
  }

 private:
  const std::string& s;
  size_t pos = 0;

  void skip() {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) pos++;
  }

  bool literal(const char* word) {
    const size_t n = strlen(word);
    if (s.compare(pos, n, word) != 0) return false;
    pos += n;
    return true;
  }

  static void utf8(std::string& out, const uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool hex4(uint32_t& out) {
    if (pos + 4 > s.size()) return false;
    out = static_cast<uint32_t>(strtoul(s.substr(pos, 4).c_str(), nullptr, 16));
    pos += 4;
    return true;
  }

  bool string(std::string& out) {
    if (s[pos] != '"') return false;
    pos++;
    while (pos < s.size() && s[pos] != '"') {
      char c = s[pos++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos >= s.size()) return false;
      c = s[pos++];
      switch (c) {
        case 'n':
          out += '\n';
          break;
        case 't':
          out += '\t';
          break;
        case 'r':
          out += '\r';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'u': {
          uint32_t cp = 0;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && s.compare(pos, 2, "\\u") == 0) {
            pos += 2;
            uint32_t low = 0;
            if (!hex4(low)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
          utf8(out, cp);
          break;
        }
        default:  // " \ /
          out += c;
          break;
      }
    }
    if (pos >= s.size()) return false;
    pos++;
    return true;
  }

  bool number(Value& out) {
    const size_t start = pos;
    bool real = false;
    if (s[pos] == '-') pos++;
    while (pos < s.size() && (isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.' || s[pos] == 'e' ||
                              s[pos] == 'E' || s[pos] == '+' || s[pos] == '-')) {
      if (s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E') real = true;
      pos++;
    }
    const std::string digits = s.substr(start, pos - start);
    if (digits.empty() || digits == "-") return false;
    if (real) {
      out.kind = Value::Kind::Real;
      out.real = strtod(digits.c_str(), nullptr);
    } else {
      out.kind = Value::Kind::Int;
      out.integer = strtoll(digits.c_str(), nullptr, 10);
      out.real = static_cast<double>(out.integer);
    }
    return true;
  }

  bool value(Value& out) {
    skip();
    if (pos >= s.size()) return false;
    const char c = s[pos];
    if (c == '{') {
      pos++;
      out.kind = Value::Kind::Object;
      skip();
      if (pos < s.size() && s[pos] == '}') {
        pos++;
        return true;
      }
      while (true) {
        skip();
        std::string key;
        if (pos >= s.size() || !string(key)) return false;
        skip();
        if (pos >= s.size() || s[pos] != ':') return false;
        pos++;
        out.members.emplace_back(std::move(key), Value{});
        if (!value(out.members.back().second)) return false;
        skip();
        if (pos < s.size() && s[pos] == ',') {
          pos++;
          continue;
        }
        if (pos < s.size() && s[pos] == '}') {
          pos++;
          return true;
        }
        return false;
      }
    }
    if (c == '[') {
      pos++;
      out.kind = Value::Kind::Array;
      skip();
      if (pos < s.size() && s[pos] == ']') {
        pos++;
        return true;
      }
      while (true) {
        out.items.emplace_back();
        if (!value(out.items.back())) return false;
        skip();
        if (pos < s.size() && s[pos] == ',') {
          pos++;
          continue;
        }
        if (pos < s.size() && s[pos] == ']') {
          pos++;
          return true;
        }
        return false;
      }
    }
    if (c == '"') {
      out.kind = Value::Kind::String;
      return string(out.text);
    }
    if (literal("null")) return true;
    if (literal("true")) {
      out.kind = Value::Kind::Bool;
      out.flag = true;
      return true;
    }
    if (literal("false")) {
      out.kind = Value::Kind::Bool;
      return true;
    }
    return number(out);
  }
};

inline bool load(const char* path, Value& out, std::string& error) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) {
    error = std::string("cannot open ") + path;
    return false;
  }
  std::string text;
  char buf[65536];
  size_t n = 0;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
  fclose(f);
  return Parser(text).parse(out, error);
}

// The vectors, read once per test program (TZ_VECTORS_JSON is the copy next to these tests).
inline const Value& vectors() {
  static Value root;
  static std::string error;
  static const bool loaded = load(TZ_VECTORS_JSON, root, error);
  if (!loaded) fprintf(stderr, "vectors_tzgrid.json: %s\n", error.c_str());
  return root;
}

// A named place of the vectors by its exact name (a null Value when absent).
inline const Value& named(const char* name) {
  const Value& places = vectors()["named"];
  for (const auto& place : places.items) {
    if (place["name"].text == name) return place;
  }
  return Value::none();
}

}  // namespace tzv
