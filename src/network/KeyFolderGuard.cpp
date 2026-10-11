#include "KeyFolderGuard.h"

#include <HalStorage.h>

#include "activities/apps/AiText.h"

bool isKeyFolderWebPath(const String& path) {
  if (aitext::pathHasKeyDir(path.c_str())) return true;

  // The first name, as typed: is it the key folder under another spelling (its 8.3 alias)?
  int start = 0;
  while (start < static_cast<int>(path.length()) && path.charAt(start) == '/') start++;
  int end = path.indexOf('/', start);
  if (end < 0) end = path.length();
  if (end <= start) return false;
  const String first = "/" + path.substring(start, end);
  HalFile dir = Storage.open(first.c_str());
  if (!dir) return false;
  char name[sizeof(aitext::KEY_DIR) + 8] = {0};
  const bool key = dir.isDirectory() && dir.getName(name, sizeof(name)) > 0 && aitext::isKeyDirName(name);
  dir.close();
  return key;
}
