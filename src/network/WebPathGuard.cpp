#include "WebPathGuard.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <string>

#include "activities/apps/AiText.h"
#include "util/CardPath.h"

namespace {

// The longest name the card stores, as UTF-8: 255 UTF-16 units of up to 3 bytes each, and the NUL.
// SdFat's getName gives nothing at all for a name that does not fit, so the buffer takes any name.
constexpr size_t NAME_BYTES = 255 * 3 + 1;

bool isProtectedStoredName(const char* name) {
  return cardpath::isHiddenName(name, std::strlen(name)) || aitext::isKeyDirName(name);
}

}  // namespace

bool isProtectedWebPath(const String& path) {
  if (cardpath::pathHasHiddenName(path.c_str()) || aitext::pathHasKeyDir(path.c_str())) return true;

  // Each leading part of the path, by the name the card stored for it: "/CROSSP~1/koreader.json"
  // reads as no hidden name above but opens /.crosspoint. The parts are the names as the card reads
  // them (so the folder each one is looked up in is the folder SdFat looks in). One name buffer a
  // request, on the heap rather than the web task's stack; without it the path is refused.
  auto name = makeUniqueNoThrow<char[]>(NAME_BYTES);
  if (!name) {
    LOG_ERR("WEB", "Path guard: no memory for a name, refused %s", path.c_str());
    return true;
  }
  std::string part;
  part.reserve(path.length());
  const char* p = path.c_str();
  const char* typed = nullptr;
  size_t typedLen = 0;
  while (cardpath::nextName(p, typed, typedLen)) {
    part += '/';
    part.append(typed, typedLen);
    const int stored = Storage.storedName(part.c_str(), name.get(), NAME_BYTES);
    if (stored == 0) break;  // nothing there (yet): the rest was read above as typed
    // A name the card cannot give back (a damaged entry) could be anything: refused too
    if (stored < 0 || isProtectedStoredName(name.get())) {
      LOG_DBG("WEB", "Path guard: refused %s", path.c_str());
      return true;
    }
  }
  return false;
}
