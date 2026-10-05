#pragma once

// Sudoku persistence: versioned text codecs, one field a line (the Word Search / Crossword
// pattern). Pure: SudokuStore reads and writes the files (tmp -> remove -> rename) and hands the
// text here. Anything that does not parse and validate completely is "no save": start fresh.
//
// puzzle.dat (~720 B)                          prefs.txt
//   SD1                                          SP1
//   tier <easy|medium|hard|expert>               tier <easy|medium|hard|expert>   (New puzzle's)
//   number <u32>                                 next <easy> <medium> <hard> <expert>  (>= 1)
//   seed <8 hex>                                 removenotes 0|1
//   fnv <8 hex>        (givensFnv)               end
//   givens <81: '.' or 1-9>
//   solution <81: 1-9>                         solved.txt (appended, capped at SOLVED_CAP lines)
//   entries <81: '.' or 1-9; givens as '.'>      <fnv 8 hex> <tier> <number> <seconds> <checks>
//   notes <243 hex: 3 a square, 9 bits>             <hints> <reveals>
//   flags <81: '.', 'w' wrong, 'r' revealed>
//   cursor <0..80 | ->
//   mode <notes 0|1> <lock 0..9 | e>
//   elapsed <seconds>
//   counts <checks> <hints> <reveals>
//   solved 0|1
//   end
// The puzzle keeps its givens and solution, never just the seed, so a firmware whose generator
// changed never alters a puzzle in progress. Loading checks the whole game (validGame: the
// solution is a valid grid, the givens are unique and part of it, the fnv matches, ...).

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "SdModel.h"

namespace sd {

inline constexpr char SAVE_DIR[] = "/.crosspoint/sudoku";
inline constexpr char PUZZLE_PATH[] = "/.crosspoint/sudoku/puzzle.dat";
inline constexpr char PREFS_PATH[] = "/.crosspoint/sudoku/prefs.txt";
inline constexpr char SOLVED_PATH[] = "/.crosspoint/sudoku/solved.txt";
inline constexpr char TMP_SUFFIX[] = ".tmp";
constexpr size_t PUZZLE_TEXT_MAX = 1024;
constexpr size_t PREFS_TEXT_MAX = 128;
constexpr size_t SOLVED_LINE_MAX = 64;
constexpr int SOLVED_CAP = 1000;

// ---- prefs ----------------------------------------------------------------------------------------

struct Prefs {
  uint8_t tier = Easy;                       // the tier New puzzle makes
  uint32_t next[TIER_COUNT] = {1, 1, 1, 1};  // the next number of each tier
  bool removeNotes = true;
};
void resetPrefs(Prefs& prefs);
// Writes the text (with its trailing newline); its length, or 0 when cap is too small.
size_t formatPrefs(const Prefs& prefs, char* out, size_t cap);
// False (and out = defaults) unless the text is a complete, valid prefs file.
bool parsePrefs(const char* text, size_t len, Prefs& out);
// The number New puzzle gives the next puzzle of a tier, advancing the counter.
uint32_t takeNumber(Prefs& prefs, int tier);

// ---- the puzzle in progress -------------------------------------------------------------------------

size_t formatGame(const Game& game, char* out, size_t cap);
// False unless the text parses completely and validGame() passes (out is then unusable).
bool parseGame(const char* text, size_t len, Game& out);

// Which file to load: the main file, or, when it is missing, the .tmp a power cut left between
// "remove" and "rename" (writes go tmp -> remove -> rename). A main file that is present always
// wins (a .tmp beside it is an unfinished newer write or a leftover).
enum class LoadFrom : uint8_t { None = 0, Main, Tmp };
LoadFrom loadFrom(bool mainExists, bool tmpExists);
// "<path>.tmp". Its length, or 0 when cap is too small.
size_t tmpPathOf(const char* path, char* out, size_t cap);

// ---- the solved list -------------------------------------------------------------------------------

struct SolvedEntry {
  uint32_t fnv = 0;
  uint8_t tier = Easy;
  uint32_t number = 0;
  uint32_t elapsed = 0;
  uint16_t checks = 0;
  uint16_t hints = 0;
  uint16_t reveals = 0;
};
SolvedEntry solvedEntryOf(const Game& game);
// One line with its '\n'. Its length, or 0 when cap is too small.
size_t formatSolvedLine(const SolvedEntry& e, char* out, size_t cap);
// One line without its '\n' (a trailing '\r' is ignored). False when malformed.
bool parseSolvedLine(std::string_view line, SolvedEntry& out);
// Whether the list holds a puzzle (by fnv): appending twice is avoided.
bool solvedListHas(const char* text, size_t len, uint32_t fnv);
// Where to start keeping the list when it is rewritten with keepLines of its newest lines (the
// byte offset of the first kept line; 0 = keep everything).
size_t solvedKeepOffset(const char* text, size_t len, int keepLines);

}  // namespace sd
