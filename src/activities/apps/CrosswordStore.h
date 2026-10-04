#pragma once

// Crossword on the SD card: the puzzle files in cw::CARD_DIR (root files and one level of pack
// folders), and the game's own files under cw::SAVE_DIR (prefs, one progress file per puzzle,
// the solved list; written tmp -> remove -> rename, the solved list appended). Device-only: the
// formats are the pure codecs and parsers in lib/Crossword.

#include <Crossword.h>

#include <cstddef>
#include <memory>

namespace cw::store {

// ---- the game's files ---------------------------------------------------------------------

// False (prefs = defaults) when there is no valid prefs file.
bool loadPrefs(Prefs& prefs);
bool savePrefs(const Prefs& prefs);

// The saved progress for a puzzle (by fnv): false when there is none or it does not parse
// (matching it to the puzzle is the caller's progressMatches()).
bool loadProgress(uint32_t fnv, Progress& out);
bool saveProgress(const Progress& prog);
// Deletes the oldest progress files (lowest seq, never currentFnv's) past MAX_PROGRESS_FILES.
void pruneProgress(uint32_t currentFnv);

// The solved list, read whole (it is capped at SOLVED_CAP lines).
class SolvedList {
 public:
  bool load();  // false when unreadable or out of memory (empty: nothing is marked)
  // fnv 0: the key alone (a card file not parsed yet); otherwise the key and the fnv.
  bool has(const char* sourceKey, uint32_t fnv = 0) const;
  size_t size() const { return len; }

 private:
  std::unique_ptr<char[]> text;
  size_t len = 0;
};
// Adds a solved puzzle (no-op when the key and fnv are listed); past SOLVED_CAP lines the list is
// rewritten with its newest lines.
bool appendSolved(uint32_t fnv, const char* sourceKey);

// ---- sources ------------------------------------------------------------------------------

// Parses a puzzle from its source key ("builtin:<id>" or a card path) into out. missing
// (optional): the source is gone (no such built-in or file).
LoadStatus loadSource(const char* sourceKey, Puzzle& out, bool* missing = nullptr);

// A collection key (Prefs::collection) for a source key: "builtin", "card" (a file in the
// folder's root) or "pack:<folder>". False for a key that is neither.
bool collectionOf(const char* sourceKey, char* out, size_t cap);

// One puzzle file of a card collection: its name inside the folder.
struct FileEntry {
  char name[MAX_SOURCE_KEY + 1] = {};
};

// The card folder's packs (folder names, sorted, at most cap). Returns the count; root files
// (optional) = how many puzzle files the root holds (up to MAX_FOLDER_FILES).
int listPacks(FileEntry* packs, int cap, int* rootFiles = nullptr);
// A card collection's puzzle files (sorted by name, at most cap and MAX_FOLDER_FILES); capped
// is set when the folder held more. Names whose full path would pass MAX_SOURCE_KEY are
// skipped. Returns the count (0 for "builtin" or an unknown key).
int listFiles(const char* collection, FileEntry* out, int cap, bool* capped = nullptr);
// The source key of a file in a card collection.
bool fileKey(const char* collection, const char* name, char* out, size_t cap);

}  // namespace cw::store
