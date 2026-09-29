#pragma once

#include <cstddef>
#include <cstdint>

#include "SleepCard.h"

// QUOTE: a random quote from the categories Settings > Quote Source enables - the built-in set
// (BuiltInQuotes.h), "My quotes" (/quotes.txt) and the open book's bookmarks - wrapped in the
// largest serif size that fits, attribution below. When none of the enabled categories has an
// entry, My quotes fill in, then the built-in set, so the card always has a quote to show.
// Entry point: sleepcards::renderQuoteCard() (declared in SleepCard.h).
//
// /quotes.txt: plain UTF-8 text (LF, CRLF or CR line ends). Entries are separated by blank lines
// (a line holding only "%", the fortune(6) separator, counts as blank too). A line starting with
// "#" is a heading for the reader of the file ("# BattleTech"): the card skips it, and it ends the
// entry before it. An optional attribution goes on its own line starting with "--" (or an em
// dash); leading spaces are fine:
//
//     Adopt the pace of nature: her secret is patience.
//      -- Ralph Waldo Emerson
//
// or at the end of the entry's last line after " -- " ("Be kind. -- Plato"). An entry whose FIRST
// line opens with an em dash is dialogue, kept as text; only "--" can open an attribution there.
//
// Lines of one entry are joined with spaces (so hard-wrapped prose reflows). Only the first
// FILE_READ_CAP bytes are read, and an entry longer than ENTRY_CAP bytes is skipped.
//
// An entry of My quotes whose text matches a built-in quote (letters and digits compared, see
// hashQuoteText) is left out while the built-in set is enabled too, so a file that repeats them
// does not show them twice as often.
//
// The card remembers the last quote it showed (a hash in STATE_PATH) and does not show it twice
// in a row while there is another one to choose from.
namespace sleepcards::quote {

constexpr const char* QUOTES_PATH = "/quotes.txt";
constexpr const char* STATE_PATH = "/.crosspoint/sleepcards/quote.dat";
constexpr uint32_t FILE_READ_CAP = 64 * 1024;  // bytes of /quotes.txt scanned at most
constexpr size_t READ_CHUNK = 4096;            // one CardIo read (one file open) per chunk
constexpr size_t ENTRY_CAP = 1024;             // raw bytes of one entry, blank lines excluded
constexpr size_t TEXT_CAP = ENTRY_CAP + 8;     // the joined quote (+ room for ellipses)
constexpr size_t ATTRIBUTION_CAP = 160;
constexpr int MAX_BOOKMARKS = 24;
constexpr int MAX_LINES = 24;
// BookmarkUtil::sanitizeBookmarkSummary() keeps the first 72 bytes of the page: a summary this
// long was probably cut mid-sentence.
constexpr size_t SUMMARY_CUT_LEN = 72;

// ---- randomness (injectable: the device seeds from esp_random, tests from a constant) -----------
uint32_t nextRandom(uint32_t& state);
// Uniform in [0, n); 0 when n == 0.
uint32_t randomBelow(uint32_t& state, uint32_t n);

// FNV-1a over the non-whitespace bytes only, so re-wrapping or CRLF does not change a quote's
// identity. Chain calls by passing the previous result as h.
constexpr uint32_t HASH_SEED = 2166136261u;
uint32_t hashInk(const char* s, size_t n, uint32_t h = HASH_SEED);

// A quote's identity for deduplication: its letters and digits only (ASCII folded to lower case),
// so spacing, line breaks, straight or curly quotes and dashes do not matter. General Punctuation
// and the blocks after it (UTF-8 lead byte 0xE2) are skipped; other non-ASCII characters count.
struct TextHasher {
  uint32_t h = HASH_SEED;
  uint8_t skip = 0;  // continuation bytes of a skipped character still to come
  void feed(unsigned char c);
};
uint32_t hashQuoteText(const char* s, size_t n);

// ---- the quotes file, streamed ------------------------------------------------------------------
struct EntrySpan {
  uint32_t offset = 0;    // file offset of the entry's first non-blank byte
  uint32_t length = 0;    // up to the end of its last non-blank line
  uint32_t hash = 0;      // hashInk() of the entry
  uint32_t textHash = 0;  // hashQuoteText() of its text lines (the attribution, own-line or inline, left out)
  bool hasText = false;   // has a line that is not attribution
};

// Feed the file's bytes in order, in chunks of any size; each complete entry is reported once.
class EntryScanner {
 public:
  using Callback = void (*)(void* user, const EntrySpan& entry);
  EntryScanner(Callback callback, void* user) : callback_(callback), user_(user) {}
  void feed(const char* data, size_t n);
  // Skip n bytes that belong to no entry (a UTF-8 byte order mark at offset 0).
  void skip(size_t n) { pos_ += static_cast<uint32_t>(n); }
  // End of input. reachedEof = the whole file was fed; otherwise the entry still open at the cap
  // is incomplete and dropped.
  void finish(bool reachedEof);

 private:
  void endLine();
  void endEntry();

  Callback callback_;
  void* user_;
  uint32_t pos_ = 0;
  // the line being read
  uint32_t lineFirstInk_ = 0;
  uint32_t lineInkEnd_ = 0;  // one past its last non-whitespace byte
  uint32_t lineHash_ = HASH_SEED;
  TextHasher lineText_;
  uint16_t lineInk_ = 0;
  uint8_t prefix_[3] = {0, 0, 0};  // its first ink bytes
  // the entry being read
  bool inEntry_ = false;
  EntrySpan entry_;
  bool sawAttribution_ = false;
  bool lastWasCr_ = false;
  // " -- Name" on the line being read (the inline attribution parseEntry splits off)
  bool spaceBefore_ = false;  // whitespace since the line's last ink byte
  uint8_t dashes_ = 0;        // dashes in the run that followed a space (0 = not in one)
  uint32_t dashHash_ = 0;     // the text hash before that run
  bool lineCut_ = false;      // the line has " -- " before more ink ...
  uint32_t cutHash_ = 0;      // ... the text hash before the last one ...
  uint32_t cutPos_ = 0;       // ... and where the name after it starts
  uint32_t cutTextHash_ = 0;  // the entry's text hash with its last line's inline name left out
};

// One raw entry -> the quote (lines joined, whitespace collapsed) and its attribution (leading
// dashes stripped; "" when none). False when there is no quote text. Both outputs are always
// terminated and never end in a partial UTF-8 sequence.
bool parseEntry(const char* raw, size_t n, char* text, size_t textCap, char* attribution, size_t attributionCap);

// A bookmark summary (the first ~72 bytes of the page) made presentable: trimmed, a partial
// UTF-8 tail dropped, a summary that was cut mid-sentence ends at a word with "..." (U+2026), one
// that starts mid-sentence begins with it. False when it has fewer than 3 visible characters.
bool snippetText(const char* summary, char* out, size_t cap);

// ---- choosing ------------------------------------------------------------------------------------
enum class Pick : uint8_t { None, BuiltIn, Mine, Bookmarks };
struct PoolStats {
  uint32_t count = 0;  // usable candidates
  uint32_t fresh = 0;  // of those, not the one shown last time
};
struct Pools {
  PoolStats builtIn;
  PoolStats mine;
  PoolStats bookmarks;
};
// The enabled categories (QUOTES_* bits) that have an entry.
uint8_t categoriesWithEntries(uint8_t enabled, const Pools& pools);
// Which category to show this time: a coin among the enabled ones that have something not shown
// last time (else among those with anything); when none has an entry, My quotes, then the
// built-in set. None only when every pool is empty.
Pick chooseCategory(uint8_t enabled, const Pools& pools, uint32_t& rng);

// Uniform choice in one pass (reservoir sampling) that avoids lastHash while any other candidate
// exists. value is the caller's payload (an offset, an index ...).
class FreshPicker {
 public:
  FreshPicker(uint32_t lastHash, uint32_t& rng) : lastHash_(lastHash), rng_(rng) {}
  void offer(uint32_t hash, uint32_t value, uint32_t value2 = 0);
  PoolStats stats() const { return {count_, fresh_}; }
  bool has() const { return count_ > 0; }
  uint32_t value() const { return fresh_ > 0 ? freshValue_ : staleValue_; }
  uint32_t value2() const { return fresh_ > 0 ? freshValue2_ : staleValue2_; }
  uint32_t hash() const { return fresh_ > 0 ? freshHash_ : lastHash_; }

 private:
  uint32_t lastHash_;
  uint32_t& rng_;
  uint32_t count_ = 0;
  uint32_t fresh_ = 0;
  uint32_t freshValue_ = 0, freshValue2_ = 0, freshHash_ = 0;
  uint32_t staleValue_ = 0, staleValue2_ = 0;
};

// ---- layout (pure: the width of a run of text comes from a callback) ------------------------------
struct LineSpan {
  uint16_t start = 0;
  uint16_t len = 0;
};
// Width in px of text[0..n) in candidate font fontIndex, with "..." appended when ellipsis.
using MeasureFn = int (*)(void* user, int fontIndex, const char* s, size_t n, bool ellipsis);

// Greedy word wrap of single-spaced text into lines no wider than width. A '\n' ends a line (verse
// in the built-in set). A word wider than a line is split between characters. Fills at most
// maxLines; overflow = the text did not fit.
int wrapLines(const char* text, int width, int fontIndex, MeasureFn measure, void* user, LineSpan* lines, int maxLines,
              bool& overflow);

// How many bytes of a line to keep so that it plus "..." fits width (at a word end when it can).
int ellipsizeLine(const char* text, const LineSpan& line, int width, int fontIndex, MeasureFn measure, void* user);

struct FitResult {
  int fontIndex = -1;
  int lineCount = 0;
  bool truncated = false;  // even the smallest font overflowed: the last line needs "..."
  LineSpan lines[MAX_LINES];
};
// Candidate i is font i with line height lineHeights[i] in a box maxHeights[i] tall (a candidate
// may give up decoration for room). Picks the first candidate whose wrapped text fits; the last
// one, truncated, when none does. False when not even one line fits or the text is empty.
bool fitText(const char* text, int width, const int* lineHeights, const int* maxHeights, int count, MeasureFn measure,
             void* user, FitResult& out);

// Does line i of a fit continue a verse line wrapped on the line before it? (Set with a hanging
// indent.) Never for the first line or for text without '\n' line breaks.
bool continuesVerseLine(const char* text, const FitResult& fit, int i);

// "Henry David Thoreau, Walden" -> name "Henry David Thoreau", work "Walden": split at the first
// ", " when what follows has at least 4 visible characters and is not "Jr."/"Sr." ("" work when
// not split). Outputs are terminated and whole-character.
void splitAttribution(const char* attribution, char* name, size_t nameCap, char* work, size_t workCap);

// ---- drawing -------------------------------------------------------------------------------------
// How a quote was set: the candidate size (0 = the largest serif with the opening mark), its line
// count, and whether even the smallest size ran out of room.
struct QuoteLayout {
  int fontIndex = -1;
  int lineCount = 0;
  bool truncated = false;
  bool mark = false;  // the size carries the opening mark
};
// The candidate sizes; fontIndex < SERIF_WITH_MARK_COUNT = one of the serif sizes with the mark.
constexpr int SERIF_WITH_MARK_COUNT = 4;
// One quote drawn exactly as the card sets it (text, "Name, Work" attribution), into the cleared
// frame; layout (when not nullptr) says how it fitted. False when nothing fits or out of memory.
bool renderQuoteText(GfxRenderer& renderer, const char* text, const char* attribution, QuoteLayout* layout);

}  // namespace sleepcards::quote
