#include "GuideStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace gd::store {

namespace {

constexpr size_t PATH_CAP = 96;

}  // namespace

// ---- Text --------------------------------------------------------------------------------------------

void Text::reset() {
  raw.reset();
  heap.reset();
  ptr = nullptr;
  len = 0;
}

bool Text::load(const char* path, const size_t maxBytes) {
  reset();
  HalFile f;
  if (!Storage.exists(path) || !Storage.openFileForRead("GD", path, f)) return false;
  const size_t size = f.fileSize();
  if (size == 0 || size > maxBytes) {
    LOG_ERR("GD", "%s: %u bytes (cap %u)", path, static_cast<unsigned>(size), static_cast<unsigned>(maxBytes));
    return false;
  }
  raw = HalMemory::allocatePsram(size + 1);
  if (raw) {
    ptr = reinterpret_cast<char*>(raw.get());
  } else {
    heap.reset(new (std::nothrow) char[size + 1]);
    ptr = heap.get();
  }
  if (!ptr) {
    LOG_ERR("GD", "OOM: %s (%u bytes)", path, static_cast<unsigned>(size));
    return false;
  }
  size_t got = 0;
  while (got < size) {
    const int n = f.read(ptr + got, size - got);
    if (n <= 0) break;
    got += static_cast<size_t>(n);
  }
  if (got != size) {
    LOG_ERR("GD", "Short read %s: %u/%u", path, static_cast<unsigned>(got), static_cast<unsigned>(size));
    reset();
    return false;
  }
  ptr[size] = '\0';
  len = size;
  return true;
}

// ---- CardSizer ---------------------------------------------------------------------------------------

void CardSizer::setPack(const char* packId, const char* figureDir) {
  std::snprintf(pack, sizeof(pack), "%s", packId ? packId : "");
  dir = figureDir ? figureDir : FIGURE_DIR;
  for (auto& e : cache) e = Entry{};
  next = 0;
}

bool CardSizer::size(const char* name, int& w, int& h) const {
  if (!name || !validId(name, MAX_ID)) return false;
  for (const auto& e : cache) {
    if (e.name[0] && std::strcmp(e.name, name) == 0) {
      w = e.w;
      h = e.h;
      return e.ok;
    }
  }
  char path[PATH_CAP];
  bool ok = false;
  int fw = 0;
  int fh = 0;
  bool present = false;
  if (figurePath(pack, name, path, sizeof(path), dir) > 0) {
    HalFile f;
    present = Storage.exists(path);
    if (present && Storage.openFileForRead("GD", path, f)) {
      uint8_t head[PNG_HEADER_BYTES];
      const int n = f.read(head, sizeof(head));
      ok = n == static_cast<int>(sizeof(head)) && pngSize(head, sizeof(head), fw, fh);
    }
  }
  // A page figure must be there; a full-screen one is optional (the view falls back to the page's).
  if (!ok && (present || dir == FIGURE_DIR)) LOG_ERR("GD", "Figure %s/%s unreadable", dir, name);
  Entry& e = cache[next];
  next = (next + 1) % CACHE;
  std::snprintf(e.name, sizeof(e.name), "%s", name);
  e.w = static_cast<int16_t>(fw);
  e.h = static_cast<int16_t>(fh);
  e.ok = ok;
  w = fw;
  h = fh;
  return ok;
}

// ---- Store -------------------------------------------------------------------------------------------

Store& Store::get() {
  static Store store;
  return store;
}

void Store::release() {
  if (data_) LOG_DBG("GD", "Store released");
  data_.reset();
}

bool Store::readSmall(const char* file, char* buf, const size_t cap, size_t& len) const {
  len = 0;
  char path[PATH_CAP];
  char tmp[PATH_CAP];
  if (statePath(packId(), file, path, sizeof(path)) == 0 || tmpPathOf(path, tmp, sizeof(tmp)) == 0) return false;
  const char* from = nullptr;
  switch (loadFrom(Storage.exists(path), Storage.exists(tmp))) {
    case LoadFrom::Main:
      from = path;
      break;
    case LoadFrom::Tmp:
      LOG_INF("GD", "%s missing: reading %s", path, tmp);
      from = tmp;
      break;
    case LoadFrom::None:
      return false;
  }
  len = Storage.readFileToBuffer(from, buf, cap, cap - 1);
  if (len >= cap) len = cap - 1;
  buf[len] = '\0';
  return len > 0;
}

bool Store::writeAtomic(const char* file, const char* text, const size_t len) const {
  char dir[PATH_CAP];
  char path[PATH_CAP];
  char tmp[PATH_CAP];
  if (stateDir(packId(), dir, sizeof(dir)) == 0 || statePath(packId(), file, path, sizeof(path)) == 0 ||
      tmpPathOf(path, tmp, sizeof(tmp)) == 0) {
    return false;
  }
  if (!Storage.ensureDirectoryExists(dir)) {
    LOG_ERR("GD", "Cannot create %s", dir);
    return false;
  }
  {
    HalFile f;
    if (!Storage.openFileForWrite("GD", tmp, f)) return false;
    const size_t written = f.write(text, len);
    if (written != len) {
      LOG_ERR("GD", "Short write %s: %u/%u", tmp, static_cast<unsigned>(written), static_cast<unsigned>(len));
      return false;
    }
    f.flush();
  }
  Storage.remove(path);
  if (!Storage.rename(tmp, path)) {
    LOG_ERR("GD", "Rename into %s failed", path);
    return false;
  }
  return true;
}

PackError Store::open() {
  if (data_) return data_->error;
  if (!data_.make()) {
    LOG_ERR("GD", "OOM: guide store");
    return PackError::Damaged;
  }
  Data& d = *data_;
  d.error = PackError::Damaged;
  char path[PATH_CAP];
  if (packPath(DEFAULT_PACK_ID, PACK_FILE, path, sizeof(path)) == 0) return d.error;
  if (!Storage.exists(path)) {
    LOG_INF("GD", "No guide pack at %s", path);
    d.error = PackError::Missing;
    return d.error;
  }
  {
    // Off the loop task's stack: 4 KB.
    std::unique_ptr<char[]> text(new (std::nothrow) char[MAX_PACKTXT_BYTES + 1]);
    if (!text) return d.error;
    const size_t len = Storage.readFileToBuffer(path, text.get(), MAX_PACKTXT_BYTES + 1, MAX_PACKTXT_BYTES);
    text[len <= MAX_PACKTXT_BYTES ? len : MAX_PACKTXT_BYTES] = '\0';
    d.error = len > 0 ? parsePackInfo(text.get(), len, d.info) : PackError::Damaged;
  }
  if (d.error != PackError::None) {
    LOG_ERR("GD", "pack.txt refused (%d)", static_cast<int>(d.error));
    return d.error;
  }
  d.error = PackError::Damaged;
  // The pack folder is the default id's (the app reads /Guides/survival/); a pack.txt naming
  // another id would key the bookmarks under a folder the pack is not in.
  if (std::strcmp(d.info.id, DEFAULT_PACK_ID) != 0) {
    LOG_ERR("GD", "pack.txt id %s is not %s", d.info.id, DEFAULT_PACK_ID);
    return d.error;
  }
  char catPath[PATH_CAP];
  char topPath[PATH_CAP];
  if (packPath(d.info.id, CATEGORIES_FILE, catPath, sizeof(catPath)) == 0 ||
      packPath(d.info.id, TOPICS_FILE, topPath, sizeof(topPath)) == 0 ||
      !d.categories.load(catPath, MAX_CATEGORIES_BYTES) || !d.topics.load(topPath, MAX_TOPICS_BYTES) ||
      !d.catalog.make()) {
    LOG_ERR("GD", "Catalog files unreadable");
    return d.error;
  }
  d.error = d.catalog->parse(d.categories.data(), d.categories.size(), d.topics.data(), d.topics.size());
  if (d.error != PackError::None) {
    LOG_ERR("GD", "Catalog refused (%d)", static_cast<int>(d.error));
    return d.error;
  }
  // The bookmarks and the recent list, ids the pack no longer has dropped.
  {
    std::unique_ptr<char[]> text(new (std::nothrow) char[MARKS_TEXT_MAX + 1]);
    size_t len = 0;
    if (text && readSmall(MARKS_FILE, text.get(), MARKS_TEXT_MAX + 1, len) && parseMarks(text.get(), len, d.marks) &&
        d.marks.prune(*d.catalog)) {
      saveMarks();
    }
  }
  {
    char text[RECENT_TEXT_MAX + 1];
    size_t len = 0;
    if (readSmall(RECENT_FILE, text, sizeof(text), len) && parseRecent(text, len, d.recent) &&
        d.recent.prune(*d.catalog)) {
      saveRecent();
    }
  }
  LOG_INF("GD", "Guide %s %s: %d categories, %d topics, %d marks", d.info.id, d.info.version,
          d.catalog->categoryCount(), d.catalog->topicCount(), d.marks.count);
  return d.error;
}

bool Store::saveMarks() {
  if (!isOpen()) return false;
  std::unique_ptr<char[]> text(new (std::nothrow) char[MARKS_TEXT_MAX]);
  if (!text) return false;
  const size_t len = formatMarks(data_->marks, text.get(), MARKS_TEXT_MAX);
  return len > 0 && writeAtomic(MARKS_FILE, text.get(), len);
}

bool Store::saveRecent() {
  if (!isOpen()) return false;
  char text[RECENT_TEXT_MAX];
  const size_t len = formatRecent(data_->recent, text, sizeof(text));
  return len > 0 && writeAtomic(RECENT_FILE, text, len);
}

void Store::touchRecent(const char* topicId) {
  if (!isOpen() || !topicId) return;
  data_->recent.touch(topicId);
  data_->recentDirty = true;
}

bool Store::flushRecent() {
  if (!isOpen() || !data_->recentDirty) return true;
  if (!saveRecent()) return false;
  data_->recentDirty = false;
  return true;
}

bool Store::loadState(State& out) {
  out = State{};
  if (!isOpen()) return false;
  char text[STATE_TEXT_MAX + 1];
  size_t len = 0;
  if (!readSmall(STATE_FILE, text, sizeof(text), len) || !parseState(text, len, out)) return false;
  sanitizeState(out, catalog());
  return true;
}

bool Store::saveState(const State& state) {
  if (!isOpen()) return false;
  char text[STATE_TEXT_MAX];
  const size_t len = formatState(state, text, sizeof(text));
  return len > 0 && writeAtomic(STATE_FILE, text, len);
}

bool Store::loadPackFile(const char* file, Text& out, const size_t maxBytes) const {
  char path[PATH_CAP];
  if (!data_ || packPath(packId(), file, path, sizeof(path)) == 0) return false;
  return out.load(path, maxBytes);
}

}  // namespace gd::store
