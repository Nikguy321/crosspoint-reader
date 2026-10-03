#pragma once

// Word Search themes: the built-in lists (flash) and the theme files on the card.
//
// Theme titles and words are English CONTENT, like the Quote card's quotes, not UI strings:
// they are not translated and do not go through tr().
//
// Theme file format (*.words in /Puzzles/WordSearch/, UTF-8 text):
//   - the first line that is neither blank nor a '#' comment is the title (trimmed, at most
//     MAX_TITLE_LEN bytes, cut on a code point);
//   - then one word or phrase per line; '#' comments and blank lines are ignored;
//   - ASCII letters are upper-cased; spaces, hyphens and apostrophes are kept in the display
//     text and dropped from the grid letters; any other character (a digit, an accent, an
//     emoji, a tab inside the line) skips the line; the grid letters must number 3..15 and the
//     display text at most MAX_DISPLAY_LEN bytes;
//   - at most MAX_FILE_WORDS words are read; a file with fewer than MIN_FILE_WORDS usable
//     words is not a theme.
// A downloader may fill the folder too, so nothing here assumes the files are hand-made.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "WsModel.h"

namespace ws {

constexpr size_t MAX_FILE_BYTES = 16 * 1024;  // the caller reads at most this much of a file
constexpr size_t MAX_FILE_WORDS = 400;
constexpr size_t MIN_FILE_WORDS = 8;
constexpr size_t MAX_THEME_FILES = 64;
inline constexpr char THEME_DIR[] = "/Puzzles/WordSearch";
inline constexpr char THEME_EXT[] = ".words";
inline constexpr char FILE_KEY_PREFIX[] = "file:";  // a card theme's key = "file:" + file name

struct BuiltinTheme {
  const char* key;    // stable id, saved in prefs/puzzle files ("animals")
  const char* title;  // shown in the header and the picker
  const char* words;  // "WORD|WORD|..." display texts, already valid (the tests check)
};
size_t builtinThemeCount();
const BuiltinTheme& builtinTheme(size_t index);
// Index of the built-in theme with this key, or -1.
int findBuiltinTheme(const char* key);

// A theme's words as views: into flash for a built-in theme, into the caller's (modified)
// buffer for a file. The views live as long as that storage.
struct ThemeWords {
  char title[MAX_TITLE_LEN + 1] = {};
  std::vector<std::string_view> words;
};

// Splits a built-in theme's list (no copy). False when the theme is malformed.
bool loadBuiltinTheme(const BuiltinTheme& theme, ThemeWords& out);

// Parses a theme file IN PLACE (each kept line is trimmed, upper-cased and its runs of spaces
// collapsed where it lies) and points out.words at the kept lines. False when there is no
// title or fewer than MIN_FILE_WORDS usable words.
bool parseThemeFile(char* text, size_t len, ThemeWords& out);

// Just the title (for the picker, which reads only a file's first bytes). False when none.
bool readThemeTitle(const char* text, size_t len, char* out, size_t cap);

// One word line, normalised into out (upper-case, trimmed, single spaces). Returns the
// display length, or 0 when the line is not a usable word (see the format above).
size_t normalizeWordLine(const char* line, size_t len, char* out, size_t cap);

// "file:" + name into out; false when it would not fit MAX_THEME_KEY (such a file is skipped).
bool makeFileThemeKey(const char* fileName, char* out, size_t cap);
// The file name inside a "file:" key, or nullptr for a built-in key.
const char* fileNameFromKey(const char* key);

}  // namespace ws
