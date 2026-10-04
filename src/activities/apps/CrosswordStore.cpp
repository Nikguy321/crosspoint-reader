#include "CrosswordStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cw::store {

namespace {

constexpr size_t PATH_CAP = MAX_SOURCE_KEY + 24;
constexpr size_t NAME_CAP = 128;  // a directory entry's name (longer ones are skipped)
constexpr size_t SOLVED_MAX_BYTES = static_cast<size_t>(SOLVED_CAP) * SOLVED_LINE_MAX;
constexpr size_t READ_CHUNK = 512;

// The whole file (at most cap - 1 bytes) into buf; its length, or 0 when missing or empty.
size_t readText(const char* path, char* buf, const size_t cap) {
  if (!Storage.exists(path)) return 0;
  return Storage.readFileToBuffer(path, buf, cap, cap - 1);
}

// tmp -> remove -> rename: a cut between the last two reads as "no save", never a torn file
// (SdFat's rename does not replace an existing file).
bool writeAtomic(const char* dir, const char* path, const char* text, const size_t len) {
  if (!Storage.ensureDirectoryExists(dir)) {
    LOG_ERR("CW", "Cannot create %s", dir);
    return false;
  }
  char tmp[PATH_CAP + 8];
  std::snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  {
    HalFile f;
    if (!Storage.openFileForWrite("CW", tmp, f)) return false;
    const size_t written = f.write(text, len);
    if (written != len) {
      LOG_ERR("CW", "Short write %s: %u/%u", tmp, static_cast<unsigned>(written), static_cast<unsigned>(len));
      return false;
    }
    f.flush();
  }
  Storage.remove(path);
  if (!Storage.rename(tmp, path)) {
    LOG_ERR("CW", "Rename into %s failed", path);
    return false;
  }
  return true;
}

// The .ipuz parser's JSON heap: PSRAM first (a 15x15 file peaks near 19 KB), internal RAM on
// boards without it.
void* jsonAllocate(const size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : std::malloc(size);
}
void jsonDeallocate(void* ptr) { std::free(ptr); }
void* jsonReallocate(void* ptr, const size_t size) {
  void* p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : std::realloc(ptr, size);
}
const Allocator JSON_HEAP = {jsonAllocate, jsonDeallocate, jsonReallocate};

// A ByteReader over a card file, through a small buffer (the .puz parser reads a byte at a time).
class FileReader final : public ByteReader {
 public:
  explicit FileReader(HalFile& file) : file(file) {}
  int read() override {
    if (pos == fill && !refill()) return -1;
    return static_cast<unsigned char>(buf[pos++]);
  }
  size_t readBytes(char* out, const size_t n) override {
    size_t done = 0;
    while (done < n) {
      if (pos == fill && !refill()) break;
      const size_t k = std::min(n - done, fill - pos);
      std::memcpy(out + done, buf + pos, k);
      pos += k;
      done += k;
    }
    return done;
  }

 private:
  bool refill() {
    const int got = file.read(buf, sizeof(buf));
    pos = 0;
    fill = got > 0 ? static_cast<size_t>(got) : 0;
    return fill > 0;
  }
  HalFile& file;
  char buf[READ_CHUNK];
  size_t pos = 0;
  size_t fill = 0;
};

bool validFolderName(const char* name) {
  return isPackFolder(name) && std::strchr(name, '/') == nullptr && std::strcmp(name, "..") != 0;
}

// The folder of a card collection ("card" or "pack:<folder>").
bool folderOf(const char* collection, char* out, const size_t cap) {
  int n = -1;
  if (std::strcmp(collection, COLLECTION_CARD) == 0) {
    n = std::snprintf(out, cap, "%s", CARD_DIR);
  } else if (std::strncmp(collection, COLLECTION_PACK_PREFIX, sizeof(COLLECTION_PACK_PREFIX) - 1) == 0) {
    const char* folder = collection + sizeof(COLLECTION_PACK_PREFIX) - 1;
    if (!validFolderName(folder)) return false;
    n = std::snprintf(out, cap, "%s/%s", CARD_DIR, folder);
  }
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool byName(const FileEntry& a, const FileEntry& b) { return strcasecmp(a.name, b.name) < 0; }

}  // namespace

bool loadPrefs(Prefs& prefs) {
  resetPrefs(prefs);
  auto text = makeUniqueNoThrow<char[]>(PREFS_TEXT_MAX + 1);
  if (!text) {
    LOG_ERR("CW", "OOM: prefs buffer");
    return false;
  }
  const size_t len = readText(PREFS_PATH, text.get(), PREFS_TEXT_MAX + 1);
  return len > 0 && parsePrefs(text.get(), len, prefs);
}

bool savePrefs(const Prefs& prefs) {
  auto text = makeUniqueNoThrow<char[]>(PREFS_TEXT_MAX);
  if (!text) {
    LOG_ERR("CW", "OOM: prefs buffer");
    return false;
  }
  const size_t len = formatPrefs(prefs, text.get(), PREFS_TEXT_MAX);
  return len > 0 && writeAtomic(SAVE_DIR, PREFS_PATH, text.get(), len);
}

bool loadProgress(const uint32_t fnv, Progress& out) {
  char path[PATH_CAP];
  if (progressPath(fnv, path, sizeof(path)) == 0) return false;
  auto text = makeUniqueNoThrow<char[]>(PROGRESS_TEXT_MAX + 1);
  if (!text) {
    LOG_ERR("CW", "OOM: progress buffer");
    return false;
  }
  const size_t len = readText(path, text.get(), PROGRESS_TEXT_MAX + 1);
  if (len == 0) return false;
  if (!parseProgress(text.get(), len, out)) {
    LOG_INF("CW", "Progress %s did not parse: fresh", path);
    return false;
  }
  return true;
}

bool saveProgress(const Progress& prog) {
  char path[PATH_CAP];
  if (progressPath(prog.fnv, path, sizeof(path)) == 0) return false;
  auto text = makeUniqueNoThrow<char[]>(PROGRESS_TEXT_MAX);
  if (!text) {
    LOG_ERR("CW", "OOM: progress buffer");
    return false;
  }
  const size_t len = formatProgress(prog, text.get(), PROGRESS_TEXT_MAX);
  if (len == 0) {
    LOG_ERR("CW", "Progress did not format");
    return false;
  }
  if (!Storage.ensureDirectoryExists(SAVE_DIR)) return false;
  return writeAtomic(PROGRESS_DIR, path, text.get(), len);
}

void pruneProgress(const uint32_t currentFnv) {
  if (!Storage.exists(PROGRESS_DIR)) return;
  // Room for the cap and a few strays (a cut write's .tmp files).
  constexpr int LIST_CAP = MAX_PROGRESS_FILES + 16;
  auto saved = makeUniqueNoThrow<SavedProgress[]>(LIST_CAP);
  if (!saved) {
    LOG_ERR("CW", "OOM: prune");
    return;
  }
  int count = 0;
  {
    HalFile dir = Storage.open(PROGRESS_DIR);
    if (!dir || !dir.isDirectory()) return;
    char name[NAME_CAP];
    for (HalFile entry = dir.openNextFile(); entry && count < LIST_CAP; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      uint32_t fnv = 0;
      if (entry.isDirectory() || !progressFileFnv(name, fnv)) continue;
      saved[count].fnv = fnv;
      saved[count].seq = 0;
      count++;
    }
  }
  if (count <= MAX_PROGRESS_FILES) return;
  // The seq is inside each file; one that does not parse counts as the oldest.
  auto prog = makeUniqueNoThrow<Progress>();
  if (!prog) return;
  for (int i = 0; i < count; i++) {
    if (loadProgress(saved[i].fnv, *prog)) saved[i].seq = prog->seq;
  }
  for (int victim = pruneVictim(saved.get(), count, currentFnv); victim >= 0;
       victim = pruneVictim(saved.get(), count, currentFnv)) {
    char path[PATH_CAP];
    if (progressPath(saved[victim].fnv, path, sizeof(path)) > 0) {
      LOG_INF("CW", "Pruning %s", path);
      Storage.remove(path);
    }
    saved[victim] = saved[count - 1];
    count--;
  }
}

namespace {

// The solved list's text (NUL-terminated) with `extra` spare bytes after it; true with len 0
// when there is no list yet.
bool readSolved(std::unique_ptr<char[]>& text, size_t& len, const size_t extra) {
  text.reset();
  len = 0;
  if (!Storage.exists(SOLVED_PATH)) return true;
  HalFile f;
  if (!Storage.openFileForRead("CW", SOLVED_PATH, f)) return false;
  const size_t size = std::min(f.fileSize(), SOLVED_MAX_BYTES);
  text = makeUniqueNoThrow<char[]>(size + extra + 1);
  if (!text) {
    LOG_ERR("CW", "OOM: solved list (%u bytes)", static_cast<unsigned>(size));
    return false;
  }
  const int got = size > 0 ? f.read(text.get(), size) : 0;
  len = got > 0 ? static_cast<size_t>(got) : 0;
  text[len] = '\0';
  return true;
}

}  // namespace

bool SolvedList::load() { return readSolved(text, len, 0); }

bool SolvedList::has(const char* sourceKey, const uint32_t fnv) const {
  return text && len > 0 && sourceKey && solvedListHas(text.get(), len, sourceKey, fnv);
}

bool appendSolved(const uint32_t fnv, const char* sourceKey) {
  char line[SOLVED_LINE_MAX + 2];
  const size_t lineLen = formatSolvedLine(fnv, sourceKey, line, sizeof(line));
  if (lineLen == 0) return false;
  std::unique_ptr<char[]> text;
  size_t len = 0;
  if (!readSolved(text, len, lineLen)) return false;
  if (len > 0 && solvedListHas(text.get(), len, sourceKey, fnv)) return true;
  int lines = 0;
  for (size_t i = 0; i < len; i++) lines += text[i] == '\n';
  if (lines >= SOLVED_CAP) {
    // Past the cap: rewritten with its newest lines and this one.
    const size_t keep = solvedKeepOffset(text.get(), len, SOLVED_CAP - 1);
    std::memmove(text.get(), text.get() + keep, len - keep);
    std::memcpy(text.get() + (len - keep), line, lineLen);
    return writeAtomic(SAVE_DIR, SOLVED_PATH, text.get(), len - keep + lineLen);
  }
  text.reset();
  if (!Storage.ensureDirectoryExists(SAVE_DIR)) return false;
  HalFile f = Storage.open(SOLVED_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) {
    LOG_ERR("CW", "Cannot append to %s", SOLVED_PATH);
    return false;
  }
  return f.write(line, lineLen) == lineLen;
}

LoadStatus loadSource(const char* sourceKey, Puzzle& out, bool* missing) {
  LoadStatus st;
  if (missing) *missing = false;
  out.reset();
  if (!sourceKey || sourceKey[0] == '\0') {
    st.error = Error::NotCrossword;
    if (missing) *missing = true;
    return st;
  }
  if (const char* id = builtinIdFromKey(sourceKey)) {
    const int index = findBuiltin(id);
    if (index < 0) {
      st.error = Error::NotCrossword;
      if (missing) *missing = true;
      return st;
    }
    const BuiltinPuzzle& b = builtinPuzzle(static_cast<size_t>(index));
    const TextStatus ts = parseTextPuzzle(b.text, std::strlen(b.text), out);
    st.error = ts.error;
    st.width = b.w;
    st.height = b.h;
    if (ts.error != Error::None)
      LOG_ERR("CW", "Built-in %s: error %d line %d", id, static_cast<int>(ts.error), ts.line);
    return st;
  }
  const Format format = formatOf(std::strrchr(sourceKey, '/') ? std::strrchr(sourceKey, '/') + 1 : sourceKey);
  if (format == Format::None || std::strncmp(sourceKey, CARD_DIR, sizeof(CARD_DIR) - 1) != 0) {
    st.error = Error::NotCrossword;
    return st;
  }
  if (!Storage.exists(sourceKey)) {
    st.error = Error::NotCrossword;
    if (missing) *missing = true;
    return st;
  }
  HalFile file;
  if (!Storage.openFileForRead("CW", sourceKey, file)) {
    st.error = Error::Damaged;
    return st;
  }
  const size_t size = file.fileSize();
  if (size > (format == Format::Ipuz ? MAX_IPUZ_BYTES : MAX_PUZ_BYTES)) {
    st.error = Error::FileTooLarge;
    return st;
  }
  auto reader = makeUniqueNoThrow<FileReader>(file);  // its buffer stays off the stack
  if (!reader) {
    st.error = Error::OutOfMemory;
    return st;
  }
  st = format == Format::Ipuz ? parseIpuz(*reader, sourceKey, out, &JSON_HEAP) : parsePuz(*reader, sourceKey, out);
  if (!st.ok()) {
    LOG_INF("CW", "%s refused: error %d (%ux%u)", sourceKey, static_cast<int>(st.error), st.width, st.height);
  } else if (st.checksumWarnings > 0 || st.truncatedClues > 0) {
    LOG_INF("CW", "%s: %u checksum warnings, %u clues cut", sourceKey, st.checksumWarnings, st.truncatedClues);
  }
  return st;
}

bool collectionOf(const char* sourceKey, char* out, const size_t cap) {
  if (!sourceKey || !out || cap == 0) return false;
  if (builtinIdFromKey(sourceKey)) return std::snprintf(out, cap, "%s", COLLECTION_BUILTIN) > 0;
  const size_t root = sizeof(CARD_DIR) - 1;
  if (std::strncmp(sourceKey, CARD_DIR, root) != 0 || sourceKey[root] != '/') return false;
  const char* rest = sourceKey + root + 1;
  const char* slash = std::strchr(rest, '/');
  if (!slash) return std::snprintf(out, cap, "%s", COLLECTION_CARD) > 0;
  if (std::strchr(slash + 1, '/')) return false;  // one level only
  const int n = std::snprintf(out, cap, "%s%.*s", COLLECTION_PACK_PREFIX, static_cast<int>(slash - rest), rest);
  return n > 0 && static_cast<size_t>(n) < cap;
}

int listPacks(FileEntry* packs, const int cap, int* rootFiles) {
  if (rootFiles) *rootFiles = 0;
  if (!Storage.exists(CARD_DIR)) return 0;
  HalFile dir = Storage.open(CARD_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  const int limit = std::min(cap, MAX_PACKS);
  int count = 0;
  int files = 0;
  char name[NAME_CAP];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.getName(name, sizeof(name)) == 0) continue;
    if (entry.isDirectory()) {
      // The folder's key and its files' paths must fit a source key.
      if (!validFolderName(name) || count >= limit || !packs) continue;
      if (sizeof(CARD_DIR) + std::strlen(name) + 2 > MAX_SOURCE_KEY) continue;
      std::snprintf(packs[count].name, sizeof(packs[count].name), "%s", name);
      count++;
    } else if (formatOf(name) != Format::None && files < MAX_FOLDER_FILES) {
      files++;
    }
  }
  std::sort(packs, packs + count, byName);
  if (rootFiles) *rootFiles = files;
  return count;
}

int listFiles(const char* collection, FileEntry* out, const int cap, bool* capped) {
  if (capped) *capped = false;
  char folder[PATH_CAP];
  if (!collection || !out || !folderOf(collection, folder, sizeof(folder)) || !Storage.exists(folder)) return 0;
  HalFile dir = Storage.open(folder);
  if (!dir || !dir.isDirectory()) return 0;
  const int limit = std::min(cap, MAX_FOLDER_FILES);
  const size_t folderLen = std::strlen(folder);
  int count = 0;
  char name[NAME_CAP];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory() || entry.getName(name, sizeof(name)) == 0) continue;
    if (formatOf(name) == Format::None) continue;
    if (folderLen + 1 + std::strlen(name) > MAX_SOURCE_KEY) continue;
    if (count >= limit) {
      if (capped) *capped = true;
      break;
    }
    std::snprintf(out[count].name, sizeof(out[count].name), "%s", name);
    count++;
  }
  std::sort(out, out + count, byName);
  return count;
}

bool fileKey(const char* collection, const char* name, char* out, const size_t cap) {
  char folder[PATH_CAP];
  if (!collection || !name || !folderOf(collection, folder, sizeof(folder))) return false;
  const int n = std::snprintf(out, cap, "%s/%s", folder, name);
  return n > 0 && static_cast<size_t>(n) < cap && static_cast<size_t>(n) <= MAX_SOURCE_KEY;
}

}  // namespace cw::store
