#pragma once

// Shared by the guide tests: file reading, a fake font (every byte 10 px wide, fixed line heights),
// a figure sizer over a map or the pack's fig/L, and the real pack loaded the way the store will
// (each file into a buffer one byte larger than the file, parsed in place).

#include <Guide.h>

#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace guidetest {

inline std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::string();
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

inline bool fileExists(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return static_cast<bool>(f);
}

// A mutable copy with one spare byte (the in-place parsers write text[len]).
struct Buf {
  std::vector<char> bytes;
  size_t len = 0;
  explicit Buf(const std::string& s = std::string()) { set(s); }
  void set(const std::string& s) {
    bytes.assign(s.begin(), s.end());
    bytes.push_back('\0');
    len = s.size();
  }
  char* data() { return bytes.data(); }
};

// Every byte 10 px (so widths are easy to predict); spaces 10; Body/Bold lines 20, Title 30, Lead 16.
class FakeMetrics final : public gd::FontMetrics {
 public:
  int charW = 10;
  int textWidth(gd::Font, const char*, const size_t len) const override { return static_cast<int>(len) * charW; }
  int spaceWidth(gd::Font) const override { return charW; }
  int lineHeight(const gd::Font font) const override {
    switch (font) {
      case gd::Font::Title:
        return 30;
      case gd::Font::Lead:
        return 16;
      default:
        return 20;
    }
  }
};

class MapSizer final : public gd::FigureSizer {
 public:
  std::map<std::string, std::pair<int, int>> sizes;
  bool size(const char* name, int& w, int& h) const override {
    const auto it = sizes.find(name);
    if (it == sizes.end()) return false;
    w = it->second.first;
    h = it->second.second;
    return true;
  }
};

// The pack's fig/L/<name>.png headers (or another figure folder's: "fig/XL").
class PackSizer final : public gd::FigureSizer {
 public:
  explicit PackSizer(std::string root, std::string dir = "fig/L") : root(std::move(root)), dir(std::move(dir)) {}
  bool size(const char* name, int& w, int& h) const override {
    const std::string png = readFile(root + "/" + dir + "/" + name + ".png");
    return gd::pngSize(reinterpret_cast<const uint8_t*>(png.data()), png.size(), w, h);
  }
  std::string root;
  std::string dir;
};

// The real device pack, as the store will hold it.
struct Pack {
  std::string root;
  gd::PackInfo info;
  gd::PackError infoError = gd::PackError::Missing;
  Buf categories, topics;
  std::unique_ptr<gd::Catalog> catalog = std::make_unique<gd::Catalog>();
  gd::PackError catalogError = gd::PackError::Missing;

  explicit Pack(std::string r) : root(std::move(r)) {
    const std::string packTxt = readFile(root + "/pack.txt");
    if (packTxt.empty()) return;
    infoError = gd::parsePackInfo(packTxt.data(), packTxt.size(), info);
    categories.set(readFile(root + "/categories.tsv"));
    topics.set(readFile(root + "/topics.tsv"));
    catalogError = catalog->parse(categories.data(), categories.len, topics.data(), topics.len);
  }

  // A topic's .gp parsed into text (buf keeps the bytes).
  gd::GpError loadTopic(const int index, Buf& buf, gd::TopicText& text) const {
    buf.set(readFile(root + "/" + catalog->topic(index).file));
    return text.parse(buf.data(), buf.len);
  }
};

}  // namespace guidetest
