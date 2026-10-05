#include "SudokuStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace sd::store {

namespace {

constexpr size_t PATH_CAP = sizeof(SOLVED_PATH) + sizeof(TMP_SUFFIX);
constexpr size_t SOLVED_MAX_BYTES = static_cast<size_t>(SOLVED_CAP) * SOLVED_LINE_MAX;

// The save at path (or, when it is missing, its .tmp) into buf, at most cap - 1 bytes; its
// length, or 0 when neither exists or it is empty.
size_t readSave(const char* path, char* buf, const size_t cap) {
  char tmp[PATH_CAP];
  if (tmpPathOf(path, tmp, sizeof(tmp)) == 0) return 0;
  switch (loadFrom(Storage.exists(path), Storage.exists(tmp))) {
    case LoadFrom::Main:
      return Storage.readFileToBuffer(path, buf, cap, cap - 1);
    case LoadFrom::Tmp:
      LOG_INF("SU", "%s missing: reading %s", path, tmp);
      return Storage.readFileToBuffer(tmp, buf, cap, cap - 1);
    case LoadFrom::None:
      break;
  }
  return 0;
}

// tmp -> remove -> rename (SdFat's rename does not replace an existing file); a cut between the
// last two leaves the .tmp, which readSave takes.
bool writeAtomic(const char* path, const char* text, const size_t len) {
  if (!Storage.ensureDirectoryExists(SAVE_DIR)) {
    LOG_ERR("SU", "Cannot create %s", SAVE_DIR);
    return false;
  }
  char tmp[PATH_CAP];
  if (tmpPathOf(path, tmp, sizeof(tmp)) == 0) return false;
  {
    HalFile f;
    if (!Storage.openFileForWrite("SU", tmp, f)) return false;
    const size_t written = f.write(text, len);
    if (written != len) {
      LOG_ERR("SU", "Short write %s: %u/%u", tmp, static_cast<unsigned>(written), static_cast<unsigned>(len));
      return false;
    }
    f.flush();
  }
  Storage.remove(path);
  if (!Storage.rename(tmp, path)) {
    LOG_ERR("SU", "Rename into %s failed", path);
    return false;
  }
  return true;
}

// The solved list's text (NUL-terminated) with `extra` spare bytes after it; true with len 0
// when there is no list yet.
bool readSolved(std::unique_ptr<char[]>& text, size_t& len, const size_t extra) {
  text.reset();
  len = 0;
  if (!Storage.exists(SOLVED_PATH)) return true;
  HalFile f;
  if (!Storage.openFileForRead("SU", SOLVED_PATH, f)) return false;
  const size_t size = std::min(f.fileSize(), SOLVED_MAX_BYTES);
  text = makeUniqueNoThrow<char[]>(size + extra + 1);
  if (!text) {
    LOG_ERR("SU", "OOM: solved list (%u bytes)", static_cast<unsigned>(size));
    return false;
  }
  const int got = size > 0 ? f.read(text.get(), size) : 0;
  len = got > 0 ? static_cast<size_t>(got) : 0;
  text[len] = '\0';
  return true;
}

}  // namespace

bool loadPrefs(Prefs& prefs) {
  resetPrefs(prefs);
  char text[PREFS_TEXT_MAX + 1];
  const size_t len = readSave(PREFS_PATH, text, sizeof(text));
  return len > 0 && parsePrefs(text, len, prefs);
}

bool savePrefs(const Prefs& prefs) {
  char text[PREFS_TEXT_MAX];
  const size_t len = formatPrefs(prefs, text, sizeof(text));
  return len > 0 && writeAtomic(PREFS_PATH, text, len);
}

bool loadGame(Game& out) {
  auto text = makeUniqueNoThrow<char[]>(PUZZLE_TEXT_MAX + 1);
  if (!text) {
    LOG_ERR("SU", "OOM: puzzle buffer");
    return false;
  }
  const size_t len = readSave(PUZZLE_PATH, text.get(), PUZZLE_TEXT_MAX + 1);
  if (len == 0) return false;
  if (!parseGame(text.get(), len, out)) {
    LOG_INF("SU", "Saved puzzle did not validate: a new one");
    return false;
  }
  return true;
}

bool saveGame(const Game& game) {
  auto text = makeUniqueNoThrow<char[]>(PUZZLE_TEXT_MAX);
  if (!text) {
    LOG_ERR("SU", "OOM: puzzle buffer");
    return false;
  }
  const size_t len = formatGame(game, text.get(), PUZZLE_TEXT_MAX);
  return len > 0 && writeAtomic(PUZZLE_PATH, text.get(), len);
}

bool appendSolved(const Game& game) {
  char line[SOLVED_LINE_MAX + 2];
  const size_t lineLen = formatSolvedLine(solvedEntryOf(game), line, sizeof(line));
  if (lineLen == 0) return false;
  std::unique_ptr<char[]> text;
  size_t len = 0;
  if (!readSolved(text, len, lineLen)) return false;
  if (len > 0 && solvedListHas(text.get(), len, game.fnv)) return true;
  int lines = 0;
  for (size_t i = 0; i < len; i++) lines += text[i] == '\n';
  if (lines >= SOLVED_CAP) {
    // Past the cap: rewritten with its newest lines and this one.
    const size_t keep = solvedKeepOffset(text.get(), len, SOLVED_CAP - 1);
    std::memmove(text.get(), text.get() + keep, len - keep);
    std::memcpy(text.get() + (len - keep), line, lineLen);
    return writeAtomic(SOLVED_PATH, text.get(), len - keep + lineLen);
  }
  text.reset();
  if (!Storage.ensureDirectoryExists(SAVE_DIR)) return false;
  HalFile f = Storage.open(SOLVED_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) {
    LOG_ERR("SU", "Cannot append to %s", SOLVED_PATH);
    return false;
  }
  return f.write(line, lineLen) == lineLen;
}

}  // namespace sd::store
