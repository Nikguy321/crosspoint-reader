#pragma once

// Crossword text: the clue/title cleaner every parser shares, the clue pool, and the built-in
// text format.
//
// Cleaned text is UTF-8 with single spaces, trimmed; control characters are dropped (tabs and
// line breaks become spaces); typographic quotes, dashes and the ellipsis become ASCII
// (' " - ...); any other non-ASCII character is kept only when the UI fonts (Ubuntu 10/12, see
// uiFontHasGlyph) can draw it, else it becomes '?'. Combining marks, soft hyphens and zero-width
// characters are dropped.
//
// Built-in text format (scripts/crossword/builtin.txt; each built-in's text is one block):
//   # a comment: '#' then a space (or '#' alone); blank lines are ignored too
//   === mini-001 | Mini 1
//   ##HUT
//   #CASE
//   ...                      h grid rows of equal width 3..15, A-Z and '#'
//   A 1 Clue text            one per Across entry
//   D 1 Clue text            one per Down entry, numbers per CwNumbering
// The id is [a-z0-9-]{1,24}; the title is 1..48 bytes. A comment is exactly '#' alone or '#'
// then a space; any other line is read as it stands, so a line made only of A-Z and '#' is a
// grid row wherever it is ("#####" or "#NOTE" in a block is a row, never a comment) and
// "#note" is Damaged. A grid row never holds a space, so the two never meet.
// scripts/crossword/gen_builtin.py reads the same rule.

#include <cstddef>
#include <cstdint>

#include "CwModel.h"

namespace cw {

constexpr uint8_t TEXT_LATIN1 = 1;  // the bytes are ISO-8859-1 (else UTF-8; a bad sequence -> '?')
constexpr uint8_t TEXT_HTML = 2;    // strip tags, decode entities

// True when every UI font draws this code point (a table that mirrors the built-in Ubuntu 10/12
// fonts; the host tests check it against their glyph intervals).
bool uiFontHasGlyph(uint32_t cp);

// A streaming cleaner: feed bytes, then finish(). Output is cut on a code point when it would
// pass cap - 1 bytes (truncated() is then true); finish() always NUL-terminates.
class TextCleaner {
 public:
  TextCleaner(char* out, size_t cap, uint8_t flags);
  void put(char byte);
  void put(const char* s, size_t n);
  size_t finish();
  bool truncated() const { return cut; }

 private:
  void decodeByte(uint8_t b);
  void htmlCodepoint(uint32_t c);
  void flushEntity();
  void emit(uint32_t c);
  void append(const char* utf8, size_t n);

  char* out;
  size_t cap;
  size_t len = 0;
  uint8_t flags;
  bool cut = false;
  bool pendingSpace = false;
  uint32_t cp = 0;   // UTF-8 decoder: the code point so far
  uint8_t need = 0;  // continuation bytes still expected
  uint8_t html = 0;  // 0 text, 1 after '<', 2 in a tag, 3 in an entity
  char ent[12] = {};
  uint8_t entLen = 0;
  char tag[3] = {};  // the tag name's first letters (br / p read as a space)
  uint8_t tagLen = 0;
  bool tagNameOpen = false;
};

// Cleans text into a NUL-terminated buffer; returns its length. truncated (optional) says
// whether it was cut.
size_t cleanText(const char* in, size_t len, char* out, size_t cap, uint8_t flags, bool* truncated = nullptr);

// Cleans a clue into the pool for an entry (cut at MAX_CLUE_BYTES). TooManyClues when the pool
// has no room for a whole clue. truncated (optional) is set when the clue was cut.
Error setClue(Puzzle& p, int entry, const char* text, size_t len, uint8_t flags, bool* truncated = nullptr);
// A cleaner writing straight into the pool's free tail (for streamed sources); false when the
// pool has no room for a whole clue. Finish it, then commitClue().
bool beginClue(Puzzle& p, TextCleaner& out, uint8_t flags);
void commitClue(Puzzle& p, int entry, TextCleaner& cleaner);

// Where a text-format parse failed (1-based line; 0 = none / end of text).
struct TextStatus {
  Error error = Error::None;
  int line = 0;
};

// Parses one built-in text block. On success p is complete (numbered, clued, fnv set) with title
// from the header and sourceKey "builtin:<id>". Every Across and Down entry must have exactly
// one clue (ClueMismatch otherwise); syntax problems are Damaged.
TextStatus parseTextPuzzle(const char* text, size_t len, Puzzle& p);

// An id is [a-z0-9-]{1,24}.
constexpr size_t MAX_BUILTIN_ID = 24;
bool validBuiltinId(const char* id, size_t len);

}  // namespace cw
