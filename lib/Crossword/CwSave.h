#pragma once

// Crossword persistence: versioned text codecs for the prefs, each puzzle's progress and the
// solved list (the Word Search pattern, one field a line). Pure: the activity reads and writes
// the files (tmp -> remove -> rename) and hands the text here. Anything that does not parse and
// validate completely is "no save": the caller starts fresh.
//
// prefs.dat                        progress/<fnv8hex>.dat
//   CP1                              CW1
//   current <sourceKey> | -          key <sourceKey>
//   skip 0|1                         fnv <8 hex digits>
//   collection <key> | -             size <w> <h>
//   seq <u32>                        cursor <row> <col> <A|D>
//   end                              elapsed <seconds>
//                                    counts <checks> <reveals>
// solved.dat (append-only)           solved 0|1
//   <fnv8hex> <sourceKey>            seq <u32>
//   ...                              row <w chars: '.' empty, '#' block, A-Z>    (h lines)
//                                    flags <w chars: '.' none, 'w' wrong, 'r' revealed> (h)
//                                    end
// The progress file is checked against the puzzle (progressMatches) after the puzzle is parsed
// again from its source; an fnv or size mismatch means the source changed: start fresh.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "CwModel.h"

namespace cw {

inline constexpr char SAVE_DIR[] = "/.crosspoint/crossword";
inline constexpr char PREFS_PATH[] = "/.crosspoint/crossword/prefs.dat";
inline constexpr char PROGRESS_DIR[] = "/.crosspoint/crossword/progress";
inline constexpr char SOLVED_PATH[] = "/.crosspoint/crossword/solved.dat";
inline constexpr char BUILTIN_KEY_PREFIX[] = "builtin:";
// Collection keys (Prefs::collection): the built-ins, the card's root files, or a pack folder.
inline constexpr char COLLECTION_BUILTIN[] = "builtin";
inline constexpr char COLLECTION_CARD[] = "card";
inline constexpr char COLLECTION_PACK_PREFIX[] = "pack:";
constexpr size_t PREFS_TEXT_MAX = 384;
constexpr size_t PROGRESS_TEXT_MAX = 1024;  // a 15x15 with a 96-byte key is ~850 bytes
constexpr size_t SOLVED_LINE_MAX = 8 + 1 + MAX_SOURCE_KEY + 1;
constexpr int MAX_PROGRESS_FILES = 40;
constexpr int SOLVED_CAP = 2000;
constexpr uint32_t ELAPSED_MAX = 99u * 3600u;
constexpr size_t MAX_COLLECTION_KEY = MAX_SOURCE_KEY;

// The id inside a "builtin:<id>" key, or nullptr for any other key.
const char* builtinIdFromKey(const char* sourceKey);

struct Prefs {
  char current[MAX_SOURCE_KEY + 1] = {};  // the puzzle being played; empty = none yet
  bool skipFilled = true;
  char collection[MAX_COLLECTION_KEY + 1] = {};  // the picker's last collection; empty = none
  uint32_t seq = 0;                              // the last progress seq handed out
};

void resetPrefs(Prefs& prefs);
// Writes the text (with its trailing newline); returns its length, or 0 when cap is too small.
size_t formatPrefs(const Prefs& prefs, char* out, size_t cap);
// False (and out = defaults) unless the text is a complete, valid prefs file.
bool parsePrefs(const char* text, size_t len, Prefs& out);

size_t formatProgress(const Progress& prog, char* out, size_t cap);
// False unless the text parses completely and is self-consistent (sizes, letters, flags only
// on letters, cursor on a white square). Matching it to the puzzle is progressMatches().
bool parseProgress(const char* text, size_t len, Progress& out);

// "/.crosspoint/crossword/progress/0badf00d.dat". Returns the length (0 when cap is too small).
size_t progressPath(uint32_t fnv, char* out, size_t cap);
// The fnv in a progress file name ("0badf00d.dat"); false for any other name.
bool progressFileFnv(const char* name, uint32_t& fnv);

// Pruning: which saved progress to delete once there are more than MAX_PROGRESS_FILES: the
// lowest seq that is not the current puzzle's. -1 when nothing needs to go.
struct SavedProgress {
  uint32_t fnv = 0;
  uint32_t seq = 0;
};
int pruneVictim(const SavedProgress* saved, int count, uint32_t currentFnv);

// One solved.dat line ("0badf00d builtin:mini-001\n"). Returns the length, 0 when cap is too
// small or the key is empty / too long / holds a control character.
size_t formatSolvedLine(uint32_t fnv, const char* sourceKey, char* out, size_t cap);
// One line without its '\n' (a trailing '\r' is ignored). False when malformed.
bool parseSolvedLine(std::string_view line, uint32_t& fnv, std::string_view& sourceKey);
// Whether the solved list holds this key and this fnv. An empty key matches on the fnv alone and
// an fnv of 0 on the key alone (a card file the picker has not parsed): a key reused for another
// puzzle (a built-in id given new content) is not solved while its fnv is known.
bool solvedListHas(const char* text, size_t len, std::string_view sourceKey, uint32_t fnv = 0);
// Where to start keeping the list when it is rewritten with keepLines of its newest lines (the
// byte offset of the first kept line; 0 = keep everything).
size_t solvedKeepOffset(const char* text, size_t len, int keepLines);

}  // namespace cw
