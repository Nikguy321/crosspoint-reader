#include "WordSearchStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ws::store {

namespace {

constexpr size_t PATH_CAP = 96;
constexpr size_t TITLE_READ_BYTES = 512;  // the title is the first non-comment line

bool hasThemeExtension(const char* name) {
  const size_t n = std::strlen(name);
  const size_t ext = sizeof(THEME_EXT) - 1;
  return n > ext && name[0] != '.' && strcasecmp(name + n - ext, THEME_EXT) == 0;
}

bool themePath(const char* key, char* out, const size_t cap) {
  const char* name = fileNameFromKey(key);
  if (!name) return false;
  const int n = std::snprintf(out, cap, "%s/%s", THEME_DIR, name);
  return n > 0 && static_cast<size_t>(n) < cap;
}

// The whole file (at most cap - 1 bytes) into buf; its length, or 0 when missing or empty.
size_t readText(const char* path, char* buf, const size_t cap) {
  if (!Storage.exists(path)) return 0;
  return Storage.readFileToBuffer(path, buf, cap, cap - 1);
}

// tmp -> remove -> rename: a cut between the last two reads as "no save", never a torn file
// (SdFat's rename does not replace an existing file).
bool writeAtomic(const char* path, const char* text, const size_t len) {
  if (!Storage.ensureDirectoryExists(SAVE_DIR)) {
    LOG_ERR("WS", "Cannot create %s", SAVE_DIR);
    return false;
  }
  char tmp[PATH_CAP];
  std::snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  {
    HalFile f;
    if (!Storage.openFileForWrite("WS", tmp, f)) return false;
    const size_t written = f.write(text, len);
    if (written != len) {
      LOG_ERR("WS", "Short write %s: %u/%u", tmp, static_cast<unsigned>(written), static_cast<unsigned>(len));
      return false;
    }
    f.flush();
  }
  Storage.remove(path);
  if (!Storage.rename(tmp, path)) {
    LOG_ERR("WS", "Rename into %s failed", path);
    return false;
  }
  return true;
}

}  // namespace

int listThemeFiles(ThemeKey* keys, const int cap) {
  const int limit = std::min(cap, static_cast<int>(MAX_THEME_FILES));
  if (!keys || limit <= 0 || !Storage.exists(THEME_DIR)) return 0;
  // Room for a few other files in the folder besides the themes.
  const auto names = Storage.listFiles(THEME_DIR, static_cast<int>(MAX_THEME_FILES) * 2);
  int count = 0;
  for (const auto& name : names) {
    if (count >= limit) break;
    if (!hasThemeExtension(name.c_str())) continue;
    if (makeFileThemeKey(name.c_str(), keys[count].key, sizeof(keys[count].key))) count++;
  }
  std::sort(keys, keys + count, [](const ThemeKey& a, const ThemeKey& b) { return strcasecmp(a.key, b.key) < 0; });
  return count;
}

bool readFileTitle(const char* key, char* out, const size_t cap) {
  char path[PATH_CAP];
  if (!themePath(key, path, sizeof(path))) return false;
  auto text = makeUniqueNoThrow<char[]>(TITLE_READ_BYTES + 1);
  if (!text) {
    LOG_ERR("WS", "OOM: title buffer");
    return false;
  }
  const size_t len = readText(path, text.get(), TITLE_READ_BYTES + 1);
  return len > 0 && readThemeTitle(text.get(), len, out, cap);
}

bool loadThemeFile(const char* key, std::unique_ptr<char[]>& buffer, ThemeWords& out) {
  char path[PATH_CAP];
  if (!themePath(key, path, sizeof(path))) return false;
  buffer = makeUniqueNoThrow<char[]>(MAX_FILE_BYTES + 1);
  if (!buffer) {
    LOG_ERR("WS", "OOM: theme file buffer");
    return false;
  }
  size_t len = readText(path, buffer.get(), MAX_FILE_BYTES + 1);
  if (len == MAX_FILE_BYTES) {
    // Over the cap: drop the line the cut went through.
    while (len > 0 && buffer[len - 1] != '\n') len--;
  }
  if (len == 0 || !parseThemeFile(buffer.get(), len, out)) {
    LOG_DBG("WS", "Not a theme: %s", path);
    buffer.reset();
    return false;
  }
  return true;
}

bool loadTheme(const char* key, std::unique_ptr<char[]>& buffer, ThemeWords& out) {
  if (fileNameFromKey(key)) return loadThemeFile(key, buffer, out);
  const int index = findBuiltinTheme(key);
  return index >= 0 && loadBuiltinTheme(builtinTheme(static_cast<size_t>(index)), out);
}

bool loadPrefs(Prefs& prefs) {
  resetPrefs(prefs);
  auto text = makeUniqueNoThrow<char[]>(PREFS_TEXT_MAX + 1);
  if (!text) {
    LOG_ERR("WS", "OOM: prefs buffer");
    return false;
  }
  const size_t len = readText(PREFS_PATH, text.get(), PREFS_TEXT_MAX + 1);
  return len > 0 && parsePrefs(text.get(), len, prefs);
}

bool savePrefs(const Prefs& prefs) {
  auto text = makeUniqueNoThrow<char[]>(PREFS_TEXT_MAX);
  if (!text) {
    LOG_ERR("WS", "OOM: prefs buffer");
    return false;
  }
  const size_t len = formatPrefs(prefs, text.get(), PREFS_TEXT_MAX);
  return len > 0 && writeAtomic(PREFS_PATH, text.get(), len);
}

bool loadPuzzle(Puzzle& puzzle, char* text, const size_t cap) {
  if (!text || cap < 2) return false;
  const size_t len = readText(PUZZLE_PATH, text, cap);
  if (len == 0) return false;
  if (!parsePuzzle(text, len, puzzle)) {
    LOG_INF("WS", "Saved puzzle did not validate: a new one");
    return false;
  }
  return true;
}

bool savePuzzleText(const char* text, const size_t len) { return len > 0 && writeAtomic(PUZZLE_PATH, text, len); }

}  // namespace ws::store
