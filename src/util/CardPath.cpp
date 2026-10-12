#include "CardPath.h"

namespace cardpath {

bool nextName(const char*& p, const char*& name, size_t& len) {
  while (*p) {
    while (*p == '/') p++;
    const char* s = p;
    while (*p && *p != '/') p++;
    const char* e = p;
    while (s < e && *s == ' ') s++;
    while (e > s && (e[-1] == '.' || e[-1] == ' ')) e--;
    if (e > s) {
      name = s;
      len = static_cast<size_t>(e - s);
      return true;
    }
  }
  return false;
}

bool isHiddenName(const char* name, const size_t len) { return name != nullptr && len > 0 && name[0] == '.'; }

bool pathHasHiddenName(const char* path) {
  if (path == nullptr) return false;
  const char* name = nullptr;
  size_t len = 0;
  while (nextName(path, name, len)) {
    if (isHiddenName(name, len)) return true;
  }
  return false;
}

}  // namespace cardpath
