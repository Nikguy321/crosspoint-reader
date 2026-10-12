#pragma once

// Apps > AI: the app's own pure helpers (no Arduino, no card, no screen), host-tested in test/ai_text.
// The Gemini and Wikipedia rules themselves are lib/Gemini and lib/WikiLookup, shared with the
// WiPhone firmware.

#include <cstddef>
#include <cstdint>

namespace aitext {

// The key file: gemini.txt in a folder named "API Keys" at the card's root, both names in any case
// (a computer may write "Gemini.txt" or "api keys").
inline constexpr char KEY_DIR[] = "API Keys";
inline constexpr char KEY_FILE[] = "gemini.txt";
bool isKeyDirName(const char* name);
bool isKeyFileName(const char* name);
// A card path inside that folder ("/API Keys", "/api keys/gemini.txt", "/API Keys/sub/x"): the key
// file is never opened as a book or printed by the bench console. Each name is read as the card
// reads it (util/CardPath): SdFat ignores a name's leading spaces and trailing dots and spaces, so
// "/API Keys./x" and "/ API Keys /x" are inside it too.
bool isKeyFolderPath(const char* path);
// A path with a folder or file named "API Keys" ANYWHERE in it, each name read as above: the web
// File Transfer server and WebDAV never list, serve, write, move, rename or delete one
// (network/WebPathGuard, which also catches the folder's FAT 8.3 alias, "API_KE~1" or "APIKEY~1").
bool pathHasKeyDir(const char* path);

// The answer page in the survival guide's page format (lib/Guide/GdPage.h), so the guide's layout
// and drawing show it: "= <title>", then one block a line of `body` - "- " a bullet, "N. " a step
// (1..99: a longer number would reach into the bezel), anything else a paragraph (a line the format
// would read as markup - "= ", "@fig", "! ", "* ", "---", a leading backslash, a "0." or "100." step - is
// escaped as one), blank lines and empty items left out. The title is the question on one line, cut
// at a word to TITLE_MAX with "..." when longer - and then the whole question opens the page as its
// first paragraph. `note`, when not empty, is a NOTE box first (a failed question's reason). The result
// always parses (TopicText::parse) unless it does not fit `cap`. Returns the length (NUL not
// counted) whatever `cap` is; `out` holds it only when it fits (else "").
constexpr size_t TITLE_MAX = 100;
size_t buildPage(const char* question, const char* note, const char* body, char* out, size_t cap);

// The zone's word for the date sentence (geminiSystemText's `zone`): the time zone map's name of
// the clock's zone when the map has it ("Pacific", "Mountain, no DST"), else the clock entry's
// first city ("Berlin" of "Berlin / Paris / Madrid / Rome"), else "".
void zoneWord(const char* entryName, const char* mapName, char* out, size_t cap);

// The local clock's offset from UTC in seconds, from the local wall time the clock shows and the
// UTC instant it shows it at (rounded to the minute).
int32_t utcOffsetSeconds(int year, int month, int day, int hour, int minute, int second, int64_t utc);

}  // namespace aitext
