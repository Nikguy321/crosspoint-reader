#pragma once

// Word Search on the SD card: the theme files in ws::THEME_DIR and the game's own files under
// /.crosspoint/wordsearch (written tmp -> remove -> rename). Device-only: the text formats are
// the pure codecs in lib/WordSearch.

#include <WordSearch.h>

#include <cstddef>
#include <memory>

namespace ws::store {

struct ThemeKey {
  char key[MAX_THEME_KEY + 1] = {};
};

// The theme files' keys ("file:<name>"), sorted by name, at most `cap` (and MAX_THEME_FILES).
// Files whose key would not fit are skipped. Returns the count.
int listThemeFiles(ThemeKey* keys, int cap);

// A theme file's title from its first bytes (the picker). False when the file is missing or
// has no title.
bool readFileTitle(const char* key, char* out, size_t cap);

// Reads and parses a theme file; `buffer` holds the text the word views point into and must
// outlive their use. False when the file is missing or is not a theme.
bool loadThemeFile(const char* key, std::unique_ptr<char[]>& buffer, ThemeWords& out);

// The built-in theme (a key without "file:") or the theme file. False when neither works.
bool loadTheme(const char* key, std::unique_ptr<char[]>& buffer, ThemeWords& out);

// False (prefs = defaults) when there is no valid prefs file.
bool loadPrefs(Prefs& prefs);
bool savePrefs(const Prefs& prefs);

// text: a PUZZLE_TEXT_MAX scratch buffer. False when there is no valid saved puzzle.
bool loadPuzzle(Puzzle& puzzle, char* text, size_t cap);
// Writes formatted puzzle text (formatPuzzle's output).
bool savePuzzleText(const char* text, size_t len);

}  // namespace ws::store
