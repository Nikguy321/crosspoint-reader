#pragma once

// Survival guide on the SD card (device only): the pack the app reads (/Guides/<id>/, made by
// scripts/guide/make_pack.py) and the app's own files under /.crosspoint/guide/<id>/ (state.txt,
// marks.txt, recent.txt; the codecs are lib/Guide's GdState). Big things live in PSRAM when it is
// there (the catalog ~23 KB, a topic's text ~13 KB + its file, a page layout ~16 KB, the search index
// ~190 KB while a search runs), else on the heap.
//
// The store is shared by the guide's screens, which replace each other (no stack survives sleep):
// it opens once (pack.txt, categories.tsv, topics.tsv, the bookmarks and the recent list, ids the
// pack no longer has dropped) and stays open while the guide is on screen; a screen that leaves the
// guide releases it (GuideApp.h). Writes are atomic: tmp -> remove -> rename (SdFat's rename does
// not replace a file); a load whose main file is missing takes the .tmp a power cut left.

#include <Guide.h>
#include <HalMemory.h>

#include <cstddef>
#include <memory>
#include <new>

namespace gd::store {

// One object in PSRAM (placement new), or on the heap when PSRAM is absent or full.
template <typename T>
class Box {
 public:
  Box() = default;
  Box(const Box&) = delete;
  Box& operator=(const Box&) = delete;
  ~Box() { reset(); }

  bool make() {
    reset();
    raw = HalMemory::allocatePsram(sizeof(T));
    if (raw) {
      ptr = new (raw.get()) T();
    } else {
      heap.reset(new (std::nothrow) T());
      ptr = heap.get();
    }
    return ptr != nullptr;
  }
  void reset() {
    if (raw) {
      ptr->~T();
      raw.reset();
    }
    heap.reset();
    ptr = nullptr;
  }
  explicit operator bool() const { return ptr != nullptr; }
  T* get() const { return ptr; }
  T* operator->() const { return ptr; }
  T& operator*() const { return *ptr; }

 private:
  HalMemory::PsramBuffer raw;
  std::unique_ptr<T> heap;
  T* ptr = nullptr;
};

// A file's bytes with one writable byte past them (lib/Guide's in-place parsers write text[len]).
class Text {
 public:
  char* data() const { return ptr; }
  size_t size() const { return len; }
  void reset();
  // The file into PSRAM (or the heap); false when it is missing, empty, unreadable or over maxBytes.
  bool load(const char* path, size_t maxBytes);

 private:
  HalMemory::PsramBuffer raw;
  std::unique_ptr<char[]> heap;
  char* ptr = nullptr;
  size_t len = 0;
};

// A figure's size from its PNG header on the card, remembered (a page lays out more than once).
// dir: FIGURE_DIR (the page's figures) or FIGURE_XL_DIR (the full-screen view's, which may be absent).
class CardSizer final : public FigureSizer {
 public:
  void setPack(const char* packId, const char* dir = FIGURE_DIR);
  bool size(const char* name, int& w, int& h) const override;

 private:
  static constexpr int CACHE = 8;
  struct Entry {
    char name[MAX_ID + 1] = {};
    int16_t w = 0;
    int16_t h = 0;
    bool ok = false;
  };
  char pack[MAX_ID + 1] = {};
  const char* dir = FIGURE_DIR;
  mutable Entry cache[CACHE];
  mutable int next = 0;
};

class Store {
 public:
  static Store& get();

  // Opens the default pack once (later calls return the first result until release()).
  PackError open();
  bool isOpen() const { return data_.get() != nullptr && data_->error == PackError::None; }
  PackError error() const { return data_ ? data_->error : PackError::Missing; }
  void release();

  // Valid while isOpen().
  const PackInfo& info() const { return data_->info; }
  const Catalog& catalog() const { return *data_->catalog; }
  const char* packId() const { return data_ && data_->info.id[0] ? data_->info.id : DEFAULT_PACK_ID; }
  Marks& marks() { return data_->marks; }
  Recent& recent() { return data_->recent; }

  bool saveMarks();
  bool saveRecent();
  // A topic was opened: the recent list changes now and is written by flushRecent (no card write
  // on the page turn itself).
  void touchRecent(const char* topicId);
  // Writes the recent list when touchRecent changed it; true when nothing was owed.
  bool flushRecent();
  // False (out = a fresh State) when there is no complete state file.
  bool loadState(State& out);
  bool saveState(const State& state);

  // A file of the open pack ("t/<id>.gp", "about.txt", "search.idx").
  bool loadPackFile(const char* file, Text& out, size_t maxBytes) const;

 private:
  struct Data {
    PackError error = PackError::Missing;
    PackInfo info;
    Box<Catalog> catalog;
    Text categories;
    Text topics;
    Marks marks;
    Recent recent;
    bool recentDirty = false;
  };
  Store() = default;
  bool readSmall(const char* file, char* buf, size_t cap, size_t& len) const;
  bool writeAtomic(const char* file, const char* text, size_t len) const;

  Box<Data> data_;
};

}  // namespace gd::store
