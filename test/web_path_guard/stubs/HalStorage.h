#pragma once

// A fake FAT card for network/WebPathGuard: folders and files with a stored (long) name and an
// 8.3 alias, opened by path the way SdFat opens them (FatFile::open, parsePathName, makeSFN):
// - the path's separators are skipped, and after each name its spaces and separators;
// - a name's leading spaces are skipped and its trailing dots and spaces dropped; a name with
//   nothing left fails the open;
// - a name matches an entry's long name in any case, or - when it is itself a valid 8.3 name -
//   the entry's 8.3 alias ("CROSSP~1" opens ".crosspoint");
// - an entry opened by its alias gives back the alias as its name (SdFat drops the long name:
//   lfnOrd is 0), which is why the guard asks storedName() for the long name instead.
#include <strings.h>

#include <cctype>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace fakecard {

struct Entry {
  std::string longName;
  std::string shortName;  // upper case, "CROSSP~1" or "KOREAD~1.JSO"
  bool directory = false;
  bool damagedName = false;  // getName fails, as on a damaged long-name entry
  std::vector<std::shared_ptr<Entry>> children;
};

inline std::shared_ptr<Entry> root = std::make_shared<Entry>();
inline unsigned opens = 0;

inline void reset() {
  root = std::make_shared<Entry>();
  root->directory = true;
  opens = 0;
}

inline std::shared_ptr<Entry> add(const std::shared_ptr<Entry>& parent, const std::string& longName,
                                  const std::string& shortName, const bool directory) {
  auto e = std::make_shared<Entry>();
  e->longName = longName;
  e->shortName = shortName;
  e->directory = directory;
  parent->children.push_back(e);
  return e;
}

// makeSFN's "is83": no leading dot, a base of 1..8 and an extension of 0..3 characters around at
// most one dot, nothing FAT reserves in a short name.
inline bool is83(const std::string& n) {
  if (n.empty() || n[0] == '.') return false;
  const size_t dot = n.find('.');
  if (dot != std::string::npos && n.find('.', dot + 1) != std::string::npos) return false;
  const size_t base = dot == std::string::npos ? n.size() : dot;
  const size_t ext = dot == std::string::npos ? 0 : n.size() - dot - 1;
  if (base < 1 || base > 8 || ext > 3) return false;
  for (const char c : n) {
    if (c == '.') continue;
    if (c <= ' ' || std::strchr("\"*+,/:;<=>?[\\]|", c)) return false;
  }
  return true;
}

// 1: the long name, 2: the 8.3 alias, 0: no match
inline int matches(const Entry& e, const std::string& typed) {
  if (strcasecmp(e.longName.c_str(), typed.c_str()) == 0) return 1;
  if (!is83(typed)) return 0;
  std::string upper = typed;
  for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return upper == e.shortName ? 2 : 0;
}

inline std::shared_ptr<Entry> open(const char* path, bool* byAlias = nullptr) {
  opens++;
  std::shared_ptr<Entry> at = root;
  const char* p = path;
  while (*p == '/') p++;
  while (*p) {
    if (!at->directory) return nullptr;
    while (*p == ' ') p++;
    const char* s = p;
    while (*p && *p != '/') p++;
    const char* e = p;
    while (e > s && (e[-1] == '.' || e[-1] == ' ')) e--;
    if (e == s) return nullptr;
    const std::string typed(s, e);
    std::shared_ptr<Entry> next;
    for (const auto& child : at->children) {
      const int how = matches(*child, typed);
      if (how) {
        next = child;
        if (byAlias) *byAlias = how == 2;
        break;
      }
    }
    if (!next) return nullptr;
    at = next;
    while (*p == ' ' || *p == '/') p++;
  }
  return at;
}

}  // namespace fakecard

class HalFile {
 public:
  HalFile() = default;
  HalFile(std::shared_ptr<fakecard::Entry> e, const bool byAlias) : e_(std::move(e)), byAlias_(byAlias) {}
  explicit operator bool() const { return e_ != nullptr; }
  bool isDirectory() const { return e_ && e_->directory; }
  // As SdFat: the long name - the alias when opened by the alias - or nothing when it does not fit
  size_t getName(char* name, const size_t len) {
    if (len) name[0] = '\0';
    if (!e_ || e_->damagedName) return 0;
    const std::string& n = byAlias_ ? e_->shortName : e_->longName;
    if (n.size() + 1 > len) return 0;
    std::memcpy(name, n.c_str(), n.size() + 1);
    return n.size();
  }
  void close() { e_.reset(); }

 private:
  std::shared_ptr<fakecard::Entry> e_;
  bool byAlias_ = false;
};

class HalStorage {
 public:
  HalFile open(const char* path) {
    bool byAlias = false;
    auto e = fakecard::open(path, &byAlias);
    return HalFile(e, byAlias);
  }
  // As the real one (lib/hal): the long name of what `path` opens, however it was reached; 0 when
  // nothing opens there, -1 when its name cannot be read or does not fit.
  int storedName(const char* path, char* name, const size_t len) {
    if (len) name[0] = '\0';
    auto e = fakecard::open(path);
    if (!e) return 0;
    if (e->damagedName || e->longName.size() + 1 > len) return -1;
    std::memcpy(name, e->longName.c_str(), e->longName.size() + 1);
    return static_cast<int>(e->longName.size());
  }
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
};

#define Storage HalStorage::getInstance()
