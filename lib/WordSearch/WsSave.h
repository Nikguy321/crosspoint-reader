#pragma once

// Word Search persistence: a versioned text codec for the player's choices and the puzzle in
// progress (the sleep cards' "S1 ..." precedent, one field a line). Pure: the activity reads
// and writes the files (tmp -> remove -> rename) and hands the text here. Anything that does
// not parse and validate completely is "no save": the caller starts fresh.
//
// prefs.dat                       puzzle.dat
//   WP1                             WS1
//   difficulty <0..2>               difficulty <0..2>
//   choice <key> | choice random    seed <u32>
//   recent <key>        (0..3)      elapsed <seconds>
//   end                             cursor <row> <col>
//                                   hint <hintsUsed> <word | -1>
//                                   theme <key>
//                                   title <text>
//                                   row <letters>          (one per grid row)
//                                   word <row> <col> <dir> <len> <found> <r0> <c0> <r1> <c1> <display>
//                                   end
// The puzzle keeps its whole grid and placements, not just the seed, so an edited theme file
// cannot change a puzzle in progress.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "WsModel.h"

namespace ws {

inline constexpr char SAVE_DIR[] = "/.crosspoint/wordsearch";
inline constexpr char PREFS_PATH[] = "/.crosspoint/wordsearch/prefs.dat";
inline constexpr char PUZZLE_PATH[] = "/.crosspoint/wordsearch/puzzle.dat";
inline constexpr char RANDOM_CHOICE[] = "random";  // never a theme key
constexpr int RECENT_THEMES = 3;
constexpr size_t PREFS_TEXT_MAX = 256;
constexpr size_t PUZZLE_TEXT_MAX = 2048;  // a full Hard puzzle is ~1.5 KB
constexpr uint32_t ELAPSED_MAX = 99u * 3600u;

struct Prefs {
  Difficulty difficulty = Difficulty::Medium;
  char choice[MAX_THEME_KEY + 1] = {};                 // a theme key; empty = Random
  char recent[RECENT_THEMES][MAX_THEME_KEY + 1] = {};  // most recent first; empty = none
  bool randomChoice() const { return choice[0] == '\0'; }
};

// Back to the defaults (Medium, Random, no recent themes).
void resetPrefs(Prefs& prefs);
// Writes the text (with its trailing newline); returns its length, or 0 when cap is too small.
size_t formatPrefs(const Prefs& prefs, char* out, size_t cap);
// False (and out = defaults) unless the text is a complete, valid prefs file.
bool parsePrefs(const char* text, size_t len, Prefs& out);

// A theme was played: it moves to the front of the recent list (no duplicates, 3 kept).
void rememberTheme(Prefs& prefs, const char* key);
bool isRecentTheme(const Prefs& prefs, std::string_view key);
// Random's pick: an index into keys, never one of the recent themes unless every key is
// recent; -1 when count is 0.
int pickRandomTheme(uint32_t& rng, const std::string_view* keys, size_t count, const Prefs& prefs);

size_t formatPuzzle(const Puzzle& p, char* out, size_t cap);
// False (out unusable) unless the text parses completely and validatePuzzle() passes.
bool parsePuzzle(const char* text, size_t len, Puzzle& out);
// Dimensions match the difficulty, letters are A-Z, every word's letters come from its display
// text and lie on the grid where its placement says, found lines spell their word, the hint
// names an unfound word, the cursor is on the grid.
bool validatePuzzle(const Puzzle& p);

}  // namespace ws
