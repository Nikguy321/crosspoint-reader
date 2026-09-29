#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "CardPreview.h"
#include "src/sleepcards/BuiltInQuotes.h"
#include "src/sleepcards/QuoteCard.h"

using namespace sleepcards;
using namespace sleepcards::quote;

namespace {

// ---- an in-memory SD card that remembers what the card writes -------------------------------------
class FakeIo final : public CardIo {
 public:
  std::map<std::string, std::string> files;
  std::vector<CardSnippet> bookmarks;
  bool hasBook = true;
  mutable int reads = 0;
  mutable int writes = 0;
  mutable uint32_t maxReadEnd = 0;
  // Fail the n-th read of /quotes.txt (1-based; 0 = never): the file changed or the card faltered.
  int failQuotesReadAt = 0;
  mutable int quotesReads = 0;

  int32_t fileSize(const char* path) const override {
    const auto it = files.find(path);
    return it == files.end() ? -1 : static_cast<int32_t>(it->second.size());
  }
  int32_t readFileAt(const char* path, const uint32_t offset, char* buf, const size_t cap) const override {
    reads++;
    if (std::strcmp(path, QUOTES_PATH) == 0 && ++quotesReads == failQuotesReadAt) return -1;
    const auto it = files.find(path);
    if (it == files.end()) return -1;
    if (offset > it->second.size()) return 0;
    const size_t n = std::min(cap, it->second.size() - offset);
    std::memcpy(buf, it->second.data() + offset, n);
    maxReadEnd = std::max<uint32_t>(maxReadEnd, offset + static_cast<uint32_t>(n));
    return static_cast<int32_t>(n);
  }
  bool writeFile(const char* path, const char* data, const size_t len) const override {
    writes++;
    const_cast<FakeIo*>(this)->files[path] = std::string(data, std::min<size_t>(len, 4096));
    return true;
  }
  bool loadBook(CardBook& out) const override {
    out = CardBook{};
    if (!hasBook) return false;
    // The bookmarks below are from Moby-Dick (1851, public domain): the open book is that one.
    std::snprintf(out.title, sizeof(out.title), "%s", "Moby-Dick; or, The Whale");
    std::snprintf(out.author, sizeof(out.author), "%s", "Herman Melville");
    return true;
  }
  bool drawBookCover(GfxRenderer&, int, int, int, int) const override { return false; }
  int loadBookmarks(CardSnippet* out, const int max) const override {
    if (!hasBook) return 0;
    const int n = std::min<int>(max, static_cast<int>(bookmarks.size()));
    for (int i = 0; i < n; i++) out[i] = bookmarks[i];
    return n;
  }
};

CardSnippet snippet(const char* text, const char* label = "", const float fraction = -1.0f) {
  CardSnippet s;
  std::snprintf(s.text, sizeof(s.text), "%s", text);
  std::snprintf(s.label, sizeof(s.label), "%s", label);
  s.fraction = fraction;
  return s;
}

std::vector<EntrySpan> scanAll(const std::string& text, const size_t chunk, const bool reachedEof = true) {
  std::vector<EntrySpan> out;
  EntryScanner scanner([](void* user, const EntrySpan& e) { static_cast<std::vector<EntrySpan>*>(user)->push_back(e); },
                       &out);
  for (size_t i = 0; i < text.size(); i += chunk) scanner.feed(text.data() + i, std::min(chunk, text.size() - i));
  scanner.finish(reachedEof);
  return out;
}

std::string sliceOf(const std::string& text, const EntrySpan& e) { return text.substr(e.offset, e.length); }

// 10 px per byte, "..." (3 bytes) counts 10 px.
int fakeMeasure(void*, const int fontIndex, const char*, const size_t n, const bool ellipsis) {
  (void)fontIndex;
  return static_cast<int>(n) * 10 + (ellipsis ? 10 : 0);
}
// Font i is (i + 1) times narrower: font 0 = 30 px/byte, 1 = 15, 2 = 10.
int sizedMeasure(void*, const int fontIndex, const char*, const size_t n, const bool ellipsis) {
  const int per = 30 / (fontIndex + 1);
  return static_cast<int>(n) * per + (ellipsis ? per : 0);
}

std::string lineOf(const char* text, const LineSpan& l) { return std::string(text + l.start, l.len); }

CardContext fakeContext(FakeIo& io, const QuoteSource source, const uint32_t seed = 1) {
  CardContext ctx = preview::sampleContext();
  ctx.io = &io;
  ctx.settings.quoteSource = source;
  ctx.seed = seed;
  return ctx;
}

const char* const THREE_QUOTES =
    "First quote, short.\n -- Author One\n\nSecond quote here.\n-- Author Two\n\nThird one, no attribution.\n";

// The hash the card saved as "shown last" (0 when none).
uint32_t shownHash(const FakeIo& io) {
  const auto it = io.files.find(STATE_PATH);
  if (it == io.files.end()) return 0;
  return static_cast<uint32_t>(std::strtoul(it->second.c_str(), nullptr, 16));
}

bool isBuiltInHash(const uint32_t hash) {
  for (size_t i = 0; i < builtInCount(); i++) {
    const BuiltInQuote& q = *builtInQuote(i);
    if (hashInk(q.attribution, std::strlen(q.attribution), hashInk(q.text, std::strlen(q.text))) == hash) return true;
  }
  return false;
}

bool isFileEntry(const uint32_t hash, const std::string& file) {
  for (const EntrySpan& e : scanAll(file, file.size())) {
    if (e.hash == hash) return true;
  }
  return false;
}

}  // namespace

// ---- randomness and hashing ----------------------------------------------------------------------

TEST(SleepCardQuote, RandomIsDeterministicAndInRange) {
  uint32_t a = 42, b = 42;
  for (int i = 0; i < 100; i++) EXPECT_EQ(nextRandom(a), nextRandom(b));
  uint32_t s = 0;
  int hits[7] = {};
  for (int i = 0; i < 7000; i++) {
    const uint32_t v = randomBelow(s, 7);
    ASSERT_LT(v, 7u);
    hits[v]++;
  }
  for (const int h : hits) EXPECT_NEAR(h, 1000, 150);
  EXPECT_EQ(randomBelow(s, 0), 0u);
}

TEST(SleepCardQuote, HashIgnoresWhitespace) {
  const char* a = "To be,\r\n  or not   to be.";
  const char* b = "To be, or not to be.";
  EXPECT_EQ(hashInk(a, std::strlen(a)), hashInk(b, std::strlen(b)));
  EXPECT_NE(hashInk(b, std::strlen(b)), hashInk("To be.", 6));
  // Chaining equals hashing the concatenation.
  EXPECT_EQ(hashInk("cd", 2, hashInk("ab", 2)), hashInk("abcd", 4));
}

// ---- the scanner ------------------------------------------------------------------------------------

TEST(SleepCardQuote, ScannerFindsEntriesWhateverTheChunking) {
  const std::string text =
      "\n\n  First line\nsecond line\n -- Someone\n\n\n%\nAnother quote.\r\n\r\n%\n-- only an attribution\n\n"
      "Last, with no newline at the end";
  const auto ref = scanAll(text, text.size());
  ASSERT_EQ(ref.size(), 4u);
  EXPECT_EQ(sliceOf(text, ref[0]), "First line\nsecond line\n -- Someone");
  EXPECT_TRUE(ref[0].hasText);
  EXPECT_EQ(sliceOf(text, ref[1]), "Another quote.");
  EXPECT_TRUE(ref[1].hasText);
  EXPECT_EQ(sliceOf(text, ref[2]), "-- only an attribution");
  EXPECT_FALSE(ref[2].hasText);
  EXPECT_EQ(sliceOf(text, ref[3]), "Last, with no newline at the end");
  const std::string first = "First line second line -- Someone";
  EXPECT_EQ(ref[0].hash, hashInk(first.data(), first.size()));
  for (size_t chunk = 1; chunk < 40; chunk++) {
    const auto got = scanAll(text, chunk);
    ASSERT_EQ(got.size(), ref.size()) << "chunk " << chunk;
    for (size_t i = 0; i < ref.size(); i++) {
      EXPECT_EQ(got[i].offset, ref[i].offset);
      EXPECT_EQ(got[i].length, ref[i].length);
      EXPECT_EQ(got[i].hash, ref[i].hash);
      EXPECT_EQ(got[i].hasText, ref[i].hasText);
    }
  }
}

TEST(SleepCardQuote, ScannerDropsTheEntryCutByTheCap) {
  const std::string text = "One.\n\nTwo is cut";
  EXPECT_EQ(scanAll(text, 4, false).size(), 1u);
  EXPECT_EQ(scanAll(text, 4, true).size(), 2u);
  // "--" alone is an attribution with no text; an em dash opening an entry is dialogue (text), and
  // after a text line it opens the attribution.
  const auto dashes = scanAll("-- Nobody\n", 3);
  ASSERT_EQ(dashes.size(), 1u);
  EXPECT_FALSE(dashes[0].hasText);
  const auto dialogue = scanAll("\xE2\x80\x94 Where to?\n", 3);
  ASSERT_EQ(dialogue.size(), 1u);
  EXPECT_TRUE(dialogue[0].hasText);
  EXPECT_TRUE(scanAll("", 1).empty());
  EXPECT_TRUE(scanAll("\n \n\t\n%\n", 2).empty());
}

// ---- parsing ----------------------------------------------------------------------------------------

TEST(SleepCardQuote, ParseEntryJoinsLinesAndSplitsTheAttribution) {
  char text[128], attribution[64];
  const std::string raw = "\xEF\xBB\xBFI went   to the woods\nbecause I wished\r\n  -- Henry David Thoreau,\n   Walden";
  ASSERT_TRUE(parseEntry(raw.data(), raw.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(text, "I went to the woods because I wished");
  EXPECT_STREQ(attribution, "Henry David Thoreau, Walden");

  const std::string em = "Brevity.\n\xE2\x80\x94\xE2\x80\x94 Anon";
  ASSERT_TRUE(parseEntry(em.data(), em.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(text, "Brevity.");
  EXPECT_STREQ(attribution, "Anon");

  const std::string none = "Just words.";
  ASSERT_TRUE(parseEntry(none.data(), none.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(attribution, "");

  const std::string onlyAttribution = "-- Somebody";
  EXPECT_FALSE(
      parseEntry(onlyAttribution.data(), onlyAttribution.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_FALSE(parseEntry(nullptr, 0, text, sizeof(text), attribution, sizeof(attribution)));
}

TEST(SleepCardQuote, ParseEntryInlineAttribution) {
  char text[128], attribution[64];
  const std::string same = "Be kind. -- Plato";
  ASSERT_TRUE(parseEntry(same.data(), same.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(text, "Be kind.");
  EXPECT_STREQ(attribution, "Plato");
  // Only on the last line, and the last " -- " of it.
  const std::string twoLines = "One -- two\nthree. -- Anon";
  ASSERT_TRUE(parseEntry(twoLines.data(), twoLines.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(text, "One -- two three.");
  EXPECT_STREQ(attribution, "Anon");
  const std::string earlier = "One -- two\nthree.";
  ASSERT_TRUE(parseEntry(earlier.data(), earlier.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(text, "One -- two three.");
  EXPECT_STREQ(attribution, "");
  // An own attribution line wins; nothing after the dashes or nothing before them: no split.
  const std::string own = "A -- B\n-- C";
  ASSERT_TRUE(parseEntry(own.data(), own.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(text, "A -- B");
  EXPECT_STREQ(attribution, "C");
  const std::string trailing = "Wait -- ";
  ASSERT_TRUE(parseEntry(trailing.data(), trailing.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(attribution, "");
}

TEST(SleepCardQuote, CarriageReturnsAndDialogue) {
  // Classic Mac line ends: three lines, two entries.
  const auto entries = scanAll("First.\r -- A\r\rSecond.\r", 5);
  ASSERT_EQ(entries.size(), 2u);
  char text[128], attribution[64];
  const std::string cr = "First\rline.\r -- A";
  ASSERT_TRUE(parseEntry(cr.data(), cr.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(text, "First line.");
  EXPECT_STREQ(attribution, "A");
  // CRLF is one line end, not a blank line between two.
  EXPECT_EQ(scanAll("One\r\ntwo\r\n\r\nThree\r\n", 4).size(), 2u);
  // Dialogue: an entry opening with an em dash is text.
  const std::string talk = "\xE2\x80\x94 Where are you going?\n\xE2\x80\x94 Out.";
  ASSERT_TRUE(parseEntry(talk.data(), talk.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_STREQ(text, "\xE2\x80\x94 Where are you going?");
  EXPECT_STREQ(attribution, "Out.");
}

TEST(SleepCardQuote, ParseEntryNeverSplitsACharacter) {
  // "ééééé" (2 bytes each) into 8 bytes: 3 whole characters (6 bytes) + terminator.
  const std::string raw = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";
  char text[8], attribution[8];
  ASSERT_TRUE(parseEntry(raw.data(), raw.size(), text, sizeof(text), attribution, sizeof(attribution)));
  EXPECT_EQ(std::strlen(text), 6u);
}

TEST(SleepCardQuote, SnippetTextTidiesBookmarkSummaries) {
  char out[CardSnippet::TEXT_CAP + 8];
  // A whole sentence stays as it is.
  ASSERT_TRUE(snippetText("It is not down in any map; true places never are.", out, sizeof(out)));
  EXPECT_STREQ(out, "It is not down in any map; true places never are.");
  // Cut at 72 bytes mid-word: back to the last whole word, then an ellipsis.
  const std::string cut = "Call me Ishmael. Some years ago - never mind how long precisely - having";
  ASSERT_EQ(cut.size(), SUMMARY_CUT_LEN);
  ASSERT_TRUE(snippetText(cut.c_str(), out, sizeof(out)));
  EXPECT_STREQ(out, "Call me Ishmael. Some years ago - never mind how long precisely\xE2\x80\xA6");
  // Starts mid-sentence: an ellipsis in front.
  ASSERT_TRUE(snippetText("and so he went home.", out, sizeof(out)));
  EXPECT_STREQ(out,
               "\xE2\x80\xA6"
               "and so he went home.");
  // A partial UTF-8 tail (the 72-byte cut can split one) is dropped.
  std::string partial(70, 'a');
  partial[0] = 'A';
  partial += "\xC3";
  ASSERT_TRUE(snippetText(partial.c_str(), out, sizeof(out)));
  EXPECT_EQ(std::strstr(out, "\xC3\xE2"), nullptr);
  EXPECT_FALSE(snippetText("  a ", out, sizeof(out)));
  EXPECT_FALSE(snippetText("", out, sizeof(out)));
  EXPECT_FALSE(snippetText(nullptr, out, sizeof(out)));
}

// ---- choosing ---------------------------------------------------------------------------------------

TEST(SleepCardQuote, QuoteSourceCategoriesAndMigration) {
  EXPECT_EQ(quoteCategories(QuoteSource::All), QUOTES_BUILT_IN | QUOTES_MINE | QUOTES_BOOKMARKS);
  EXPECT_EQ(quoteCategories(QuoteSource::BuiltInAndMine), QUOTES_BUILT_IN | QUOTES_MINE);
  EXPECT_EQ(quoteCategories(QuoteSource::MineAndBookmarks), QUOTES_MINE | QUOTES_BOOKMARKS);
  EXPECT_EQ(quoteCategories(QuoteSource::BuiltInAndBookmarks), QUOTES_BUILT_IN | QUOTES_BOOKMARKS);
  EXPECT_EQ(quoteCategories(QuoteSource::BuiltInOnly), QUOTES_BUILT_IN);
  EXPECT_EQ(quoteCategories(QuoteSource::MineOnly), QUOTES_MINE);
  EXPECT_EQ(quoteCategories(QuoteSource::BookmarksOnly), QUOTES_BOOKMARKS);
  EXPECT_EQ(quoteCategories(static_cast<QuoteSource>(200)), QUOTES_BUILT_IN | QUOTES_MINE | QUOTES_BOOKMARKS);
  // Every choice enables something, and no two choices are the same.
  uint8_t seen = 0;
  for (uint8_t i = 0; i < static_cast<uint8_t>(QuoteSource::Count); i++) {
    const uint8_t mask = quoteCategories(static_cast<QuoteSource>(i));
    EXPECT_NE(mask, 0) << int(i);
    EXPECT_EQ(seen & (1u << mask), 0) << int(i);
    seen |= static_cast<uint8_t>(1u << mask);
  }
  // The old setting: 0 quotes file, 1 bookmarks, 2 both.
  EXPECT_EQ(migrateQuoteSource(0), static_cast<uint8_t>(QuoteSource::MineOnly));
  EXPECT_EQ(migrateQuoteSource(1), static_cast<uint8_t>(QuoteSource::BookmarksOnly));
  EXPECT_EQ(migrateQuoteSource(2), static_cast<uint8_t>(QuoteSource::All));
  EXPECT_EQ(migrateQuoteSource(9), static_cast<uint8_t>(QuoteSource::All));
  // A new install: all three.
  EXPECT_EQ(SleepCardSettings{}.quoteSource, QuoteSource::All);
}

TEST(SleepCardQuote, ChooseCategoryPrefersFreshEnabledAndFallsBack) {
  uint32_t rng = 7;
  const PoolStats none{0, 0}, some{3, 3}, stale{1, 0};
  const uint8_t all = QUOTES_BUILT_IN | QUOTES_MINE | QUOTES_BOOKMARKS;
  EXPECT_EQ(chooseCategory(all, Pools{none, none, none}, rng), Pick::None);
  EXPECT_EQ(chooseCategory(QUOTES_MINE, Pools{some, some, some}, rng), Pick::Mine);
  EXPECT_EQ(chooseCategory(QUOTES_BOOKMARKS, Pools{some, some, some}, rng), Pick::Bookmarks);
  EXPECT_EQ(chooseCategory(QUOTES_BUILT_IN, Pools{some, some, some}, rng), Pick::BuiltIn);
  // Its own category even when stale.
  EXPECT_EQ(chooseCategory(QUOTES_MINE, Pools{some, stale, some}, rng), Pick::Mine);
  // Fresh wins among the enabled ones.
  EXPECT_EQ(chooseCategory(QUOTES_MINE | QUOTES_BOOKMARKS, Pools{some, stale, some}, rng), Pick::Bookmarks);
  EXPECT_EQ(chooseCategory(all, Pools{stale, stale, some}, rng), Pick::Bookmarks);
  // Nothing in the enabled categories: My quotes, then the built-in set.
  EXPECT_EQ(chooseCategory(QUOTES_BOOKMARKS, Pools{some, some, none}, rng), Pick::Mine);
  EXPECT_EQ(chooseCategory(QUOTES_BOOKMARKS, Pools{some, none, none}, rng), Pick::BuiltIn);
  EXPECT_EQ(chooseCategory(QUOTES_MINE, Pools{some, none, some}, rng), Pick::BuiltIn);
  EXPECT_EQ(chooseCategory(QUOTES_MINE | QUOTES_BOOKMARKS, Pools{stale, none, none}, rng), Pick::BuiltIn);
  EXPECT_EQ(categoriesWithEntries(all, Pools{some, none, stale}), QUOTES_BUILT_IN | QUOTES_BOOKMARKS);
  EXPECT_EQ(categoriesWithEntries(QUOTES_MINE, Pools{some, none, stale}), 0);
  // A coin between the enabled categories, whatever their sizes.
  int hits[4] = {};
  for (int i = 0; i < 3000; i++) hits[static_cast<int>(chooseCategory(all, Pools{some, some, some}, rng))]++;
  for (const Pick p : {Pick::BuiltIn, Pick::Mine, Pick::Bookmarks}) EXPECT_NEAR(hits[static_cast<int>(p)], 1000, 120);
}

TEST(SleepCardQuote, FreshPickerIsUniformAndAvoidsTheLastOne) {
  uint32_t rng = 3;
  int hits[5] = {};
  for (int round = 0; round < 5000; round++) {
    FreshPicker p(/*lastHash=*/102, rng);
    for (uint32_t i = 0; i < 5; i++) p.offer(100 + i, i);
    ASSERT_EQ(p.stats().count, 5u);
    ASSERT_EQ(p.stats().fresh, 4u);
    hits[p.value()]++;
    EXPECT_NE(p.hash(), 102u);
  }
  EXPECT_EQ(hits[2], 0);
  for (const int i : {0, 1, 3, 4}) EXPECT_NEAR(hits[i], 1250, 150);

  // Only the last one left: show it again rather than nothing.
  FreshPicker only(55, rng);
  only.offer(55, 9, 77);
  EXPECT_TRUE(only.has());
  EXPECT_EQ(only.value(), 9u);
  EXPECT_EQ(only.value2(), 77u);
  EXPECT_EQ(only.hash(), 55u);
  FreshPicker empty(0, rng);
  EXPECT_FALSE(empty.has());
}

// ---- layout -----------------------------------------------------------------------------------------

TEST(SleepCardQuote, WrapLinesGreedyAndBounded) {
  const char* text = "aaa bb cccc d eeeee";
  LineSpan lines[8];
  bool overflow = false;
  // 60 px = 6 bytes a line.
  const int n = wrapLines(text, 60, 0, &fakeMeasure, nullptr, lines, 8, overflow);
  ASSERT_EQ(n, 3);
  EXPECT_FALSE(overflow);
  EXPECT_EQ(lineOf(text, lines[0]), "aaa bb");
  EXPECT_EQ(lineOf(text, lines[1]), "cccc d");
  EXPECT_EQ(lineOf(text, lines[2]), "eeeee");
  EXPECT_EQ(wrapLines(text, 60, 0, &fakeMeasure, nullptr, lines, 2, overflow), 2);
  EXPECT_TRUE(overflow);
}

TEST(SleepCardQuote, WrapLinesSplitsAWordTooLongButNotACharacter) {
  const char* text = "xy \xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9 z";  // "éééé" is 8 bytes
  LineSpan lines[8];
  bool overflow = false;
  const int n = wrapLines(text, 50, 0, &fakeMeasure, nullptr, lines, 8, overflow);  // 5 bytes a line
  ASSERT_GE(n, 3);
  EXPECT_FALSE(overflow);
  EXPECT_EQ(lineOf(text, lines[0]), "xy");
  for (int i = 0; i < n; i++) {
    EXPECT_LE(lines[i].len, 5);
    EXPECT_NE(static_cast<unsigned char>(text[lines[i].start]) & 0xC0, 0x80) << "line " << i << " starts mid-character";
  }
  // Even a line narrower than one character still takes one (no endless loop).
  EXPECT_GT(wrapLines("abc", 1, 0, &fakeMeasure, nullptr, lines, 8, overflow), 0);
}

TEST(SleepCardQuote, EllipsizeKeepsWholeWords) {
  const char* text = "alpha beta, gamma";
  const LineSpan all{0, static_cast<uint16_t>(std::strlen(text))};
  EXPECT_EQ(ellipsizeLine(text, all, 1000, 0, &fakeMeasure, nullptr), 17);
  // 120 px: 11 bytes + ellipsis. "alpha beta," is 11 but the comma is trimmed: "alpha beta".
  EXPECT_EQ(ellipsizeLine(text, all, 120, 0, &fakeMeasure, nullptr), 10);
  EXPECT_EQ(ellipsizeLine(text, all, 40, 0, &fakeMeasure, nullptr), 3);  // characters when no word fits
}

TEST(SleepCardQuote, FitTextPicksTheLargestFontThatFits) {
  const std::string text = "one two three four five six seven eight nine ten";  // 48 bytes
  const int lh[] = {30, 20, 10};
  const int h200[] = {200, 200, 200}, h600[] = {600, 600, 600}, h25[] = {25, 25, 25}, h5[] = {5, 5, 5};
  FitResult fit;
  // Font 0 (30 px/byte) needs ~12 lines of 120 px; with 200 px height font 1 fits in 6 lines x 20.
  ASSERT_TRUE(fitText(text.c_str(), 120, lh, h200, 3, &sizedMeasure, nullptr, fit));
  EXPECT_EQ(fit.fontIndex, 1);
  EXPECT_FALSE(fit.truncated);
  // Generous height: the largest.
  ASSERT_TRUE(fitText(text.c_str(), 120, lh, h600, 3, &sizedMeasure, nullptr, fit));
  EXPECT_EQ(fit.fontIndex, 0);
  // Too little room for anything: the smallest, truncated.
  ASSERT_TRUE(fitText(text.c_str(), 120, lh, h25, 3, &sizedMeasure, nullptr, fit));
  EXPECT_EQ(fit.fontIndex, 2);
  EXPECT_TRUE(fit.truncated);
  EXPECT_EQ(fit.lineCount, 2);
  EXPECT_FALSE(fitText("", 120, lh, h600, 3, &sizedMeasure, nullptr, fit));
  EXPECT_FALSE(fitText(text.c_str(), 120, lh, h5, 3, &sizedMeasure, nullptr, fit));
}

TEST(SleepCardQuote, FitTextCandidatesMayTradeDecorationForRoom) {
  // Same font twice: the first in a short box, the second in a taller one.
  const std::string text = "one two three four five six";
  const int lh[] = {10, 10};
  const int maxH[] = {20, 40};
  FitResult fit;
  ASSERT_TRUE(fitText(text.c_str(), 100, lh, maxH, 2, &fakeMeasure, nullptr, fit));
  EXPECT_EQ(fit.fontIndex, 1);
  EXPECT_EQ(fit.lineCount, 3);
}

TEST(SleepCardQuote, SplitAttribution) {
  char name[64], work[64];
  splitAttribution("Henry David Thoreau, Walden", name, sizeof(name), work, sizeof(work));
  EXPECT_STREQ(name, "Henry David Thoreau");
  EXPECT_STREQ(work, "Walden");
  splitAttribution("Jane Austen, Pride and Prejudice", name, sizeof(name), work, sizeof(work));
  EXPECT_STREQ(name, "Jane Austen");
  EXPECT_STREQ(work, "Pride and Prejudice");
  splitAttribution("Martin Luther King, Jr.", name, sizeof(name), work, sizeof(work));
  EXPECT_STREQ(name, "Martin Luther King, Jr.");
  EXPECT_STREQ(work, "");
  splitAttribution("Ralph Waldo Emerson", name, sizeof(name), work, sizeof(work));
  EXPECT_STREQ(name, "Ralph Waldo Emerson");
  EXPECT_STREQ(work, "");
  splitAttribution("Anon, 1850", name, sizeof(name), work, sizeof(work));
  EXPECT_STREQ(work, "1850");
  splitAttribution("Anon, x", name, sizeof(name), work, sizeof(work));  // too short to be a work
  EXPECT_STREQ(name, "Anon, x");
  splitAttribution(nullptr, name, sizeof(name), work, sizeof(work));
  EXPECT_STREQ(name, "");
}

// ---- the card ---------------------------------------------------------------------------------------

TEST(SleepCardQuote, SampleRendersWithinBudget) {
  bool declined = true;
  const double ms = preview::renderCardPng(CardId::Quote, preview::sampleContext(), "quote", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, preview::HOST_RUNAWAY_MS);
  std::printf("quote card: %.2f ms\n", ms);
}

TEST(SleepCardQuote, PreviewVariants) {
  // Public-domain text only (Walden, 1854; Moby-Dick, 1851).
  const struct {
    const char* name;
    const char* file;
  } FILES[] = {
      {"quote_short", "Heaven is under our feet as well as over our heads.\n -- Henry David Thoreau, Walden\n"},
      {"quote_long",
       "I went to the woods because I wished to live deliberately, to front only the essential facts of life, and "
       "see if I could not learn what it had to teach, and not, when I came to die, discover that I had not lived. "
       "I did not wish to live what was not life, living is so dear; nor did I wish to practise resignation, unless "
       "it was quite necessary. I wanted to live deep and suck out all the marrow of life, to live so sturdily and "
       "Spartan-like as to put to rout all that was not life, to cut a broad swath and shave close, to drive life "
       "into a corner, and reduce it to its lowest terms.\n -- Henry David Thoreau, Walden\n"},
      {"quote_plain", "Simplify, simplify.\n"},
  };
  bool declined = true;
  for (const auto& f : FILES) {
    FakeIo io;
    io.files[QUOTES_PATH] = f.file;
    const double ms = preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::MineOnly), f.name, &declined);
    EXPECT_FALSE(declined) << f.name;
    EXPECT_LT(ms, preview::HOST_RUNAWAY_MS) << f.name;
    std::printf("%s: %.2f ms\n", f.name, ms);
  }
  FakeIo io;
  io.bookmarks = {snippet("It is not down in any map; true places never are.", "Kokovoko", 0.071f)};
  preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::BookmarksOnly), "quote_bookmark", &declined);
  EXPECT_FALSE(declined);
  io.bookmarks = {snippet("Call me Ishmael. Some years ago - never mind how long precisely - having", "", 0.002f)};
  preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::BookmarksOnly), "quote_bookmark_cut", &declined);
  EXPECT_FALSE(declined);
}

TEST(SleepCardQuote, LongestEntryStillFitsInBudget) {
  // An entry at ENTRY_CAP: the worst case for the fit (every candidate size is tried).
  const std::string sentence =
      "Our life is frittered away by detail. An honest man has hardly need to count more than his ten fingers, or "
      "in extreme cases he may add his ten toes, and lump the rest. Simplicity, simplicity, simplicity! ";
  std::string entry;
  while (entry.size() + sentence.size() < ENTRY_CAP - 40) entry += sentence;
  entry += "\n -- Henry David Thoreau, Walden\n";
  ASSERT_LE(entry.size(), ENTRY_CAP);
  FakeIo io;
  io.files[QUOTES_PATH] = entry;
  bool declined = true;
  const double ms =
      preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::MineOnly), "quote_max", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, preview::HOST_RUNAWAY_MS);
  std::printf("quote_max (%zu bytes): %.2f ms\n", entry.size(), ms);
}

TEST(SleepCardQuote, NeverTheSameTwiceInARow) {
  FakeIo io;
  io.files[QUOTES_PATH] = THREE_QUOTES;
  std::string last;
  for (uint32_t seed = 0; seed < 30; seed++) {
    ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::MineOnly, seed), preview::renderer()));
    const std::string now = io.files[STATE_PATH];
    EXPECT_EQ(now.size(), 9u);
    EXPECT_NE(now, last) << "seed " << seed;
    last = now;
  }
}

TEST(SleepCardQuote, OneQuoteIsShownAgain) {
  FakeIo io;
  io.files[QUOTES_PATH] = "Only me.\n";
  EXPECT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::MineOnly), preview::renderer()));
  EXPECT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::MineOnly), preview::renderer()));
}

TEST(SleepCardQuote, FallsBackToMyQuotesThenTheBuiltInSet) {
  FakeIo io;
  io.hasBook = false;
  io.files[QUOTES_PATH] = THREE_QUOTES;
  // Bookmarks wanted, no book: My quotes.
  ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::BookmarksOnly), preview::renderer()));
  EXPECT_TRUE(isFileEntry(shownHash(io), THREE_QUOTES));
  // Nothing anywhere: the built-in set, in every mode - never the logo screen.
  io.files.clear();
  for (uint8_t mode = 0; mode < static_cast<uint8_t>(QuoteSource::Count); mode++) {
    io.files.erase(STATE_PATH);
    bool declined = true;
    preview::renderCardPng(CardId::Quote, fakeContext(io, static_cast<QuoteSource>(mode), mode), "quote_none",
                           &declined);
    EXPECT_FALSE(declined) << int(mode);
    EXPECT_TRUE(isBuiltInHash(shownHash(io))) << int(mode);
  }
  // My quotes only, the file empty or absent: the built-in set.
  io.files[QUOTES_PATH] = "\n\n";
  ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::MineOnly), preview::renderer()));
  EXPECT_TRUE(isBuiltInHash(shownHash(io)));
  // Bookmarks whose summaries are empty do not count either.
  io.hasBook = true;
  io.bookmarks = {snippet(""), snippet("  ")};
  ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::BookmarksOnly), preview::renderer()));
  EXPECT_TRUE(isBuiltInHash(shownHash(io)));
}

TEST(SleepCardQuote, JunkFilesAreHarmless) {
  FakeIo io;
  // Binary noise: no newline ever, NULs, high bytes.
  std::string junk(20000, '\0');
  uint32_t s = 9;
  for (auto& c : junk) c = static_cast<char>(nextRandom(s));
  io.files[QUOTES_PATH] = junk;
  renderQuoteCard(fakeContext(io, QuoteSource::MineOnly), preview::renderer());  // whatever it decides, no crash
  // Only attributions and an entry over the size cap: nothing usable, the built-in set instead.
  io.files[QUOTES_PATH] = "-- a\n\n-- b\n\n" + std::string(ENTRY_CAP + 1, 'x') + "\n";
  EXPECT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::MineOnly), preview::renderer()));
  EXPECT_TRUE(isBuiltInHash(shownHash(io)));
}

TEST(SleepCardQuote, BigFilesAreReadOnlyUpToTheCap) {
  FakeIo io;
  std::string big;
  for (int i = 0; big.size() < 3 * FILE_READ_CAP; i++) big += "Quote number " + std::to_string(i) + ".\n\n";
  io.files[QUOTES_PATH] = big;
  io.reads = 0;
  ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::MineOnly), preview::renderer()));
  EXPECT_LE(io.maxReadEnd, FILE_READ_CAP);
  // 16 chunks of the file + the chosen entry + the state file.
  EXPECT_LE(io.reads, static_cast<int>(FILE_READ_CAP / READ_CHUNK) + 2);
}

// ---- the built-in set ---------------------------------------------------------------------------------

namespace {
bool validUtf8(const char* s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s);
  while (*p) {
    int extra = 0;
    if (*p < 0x80) {
      extra = 0;
    } else if ((*p & 0xE0) == 0xC0) {
      extra = 1;
    } else if ((*p & 0xF0) == 0xE0) {
      extra = 2;
    } else if ((*p & 0xF8) == 0xF0) {
      extra = 3;
    } else {
      return false;
    }
    p++;
    for (int i = 0; i < extra; i++, p++) {
      if ((*p & 0xC0) != 0x80) return false;
    }
  }
  return true;
}
}  // namespace

TEST(SleepCardQuote, BuiltInSetIsSound) {
  ASSERT_GE(builtInCount(), 30u);
  ASSERT_LE(builtInCount(), 64u);  // QuoteCard.cpp's MAX_BUILT_IN
  EXPECT_EQ(builtInQuote(builtInCount()), nullptr);
  std::vector<uint32_t> ids;
  for (size_t i = 0; i < builtInCount(); i++) {
    const BuiltInQuote& q = *builtInQuote(i);
    SCOPED_TRACE(q.text);
    ASSERT_NE(q.text, nullptr);
    ASSERT_NE(q.attribution, nullptr);
    EXPECT_TRUE(validUtf8(q.text));
    EXPECT_TRUE(validUtf8(q.attribution));
    const size_t len = std::strlen(q.text);
    EXPECT_GT(len, 10u);
    EXPECT_LT(len, TEXT_CAP);
    // Tidy text: no leading/trailing space, single spaces, no blank verse lines, no file syntax.
    EXPECT_NE(q.text[0], ' ');
    EXPECT_NE(q.text[len - 1], ' ');
    EXPECT_EQ(std::strstr(q.text, "  "), nullptr);
    EXPECT_EQ(std::strstr(q.text, "\n\n"), nullptr);
    EXPECT_EQ(std::strstr(q.text, " \n"), nullptr);
    EXPECT_EQ(std::strstr(q.text, "\n "), nullptr);
    EXPECT_EQ(std::strstr(q.text, "--"), nullptr);
    // Every entry has an attribution that splits into the author and the work.
    char name[160], work[160];
    splitAttribution(q.attribution, name, sizeof(name), work, sizeof(work));
    EXPECT_GT(std::strlen(name), 3u);
    EXPECT_GT(std::strlen(work), 3u);
    EXPECT_EQ(std::strstr(q.attribution, "--"), nullptr);
    // It survives the quotes-file format too (a line each, then the attribution line).
    const std::string asEntry = std::string(q.text) + "\n-- " + q.attribution;
    char text[TEXT_CAP], attribution[ATTRIBUTION_CAP];
    ASSERT_TRUE(parseEntry(asEntry.data(), asEntry.size(), text, sizeof(text), attribution, sizeof(attribution)));
    EXPECT_STREQ(attribution, q.attribution);
    ids.push_back(hashQuoteText(q.text, len));
  }
  // No two alike.
  for (size_t i = 0; i < ids.size(); i++) {
    for (size_t j = i + 1; j < ids.size(); j++) EXPECT_NE(ids[i], ids[j]) << i << " " << j;
  }
}

// Every built-in quote fits the card whole in one of the serif sizes with the opening mark; each is
// written to build/cards/quote_builtin_NN.png.
TEST(SleepCardQuote, EveryBuiltInQuoteFitsTheCard) {
  GfxRenderer& r = preview::renderer();
  int largestStep = 0;
  for (size_t i = 0; i < builtInCount(); i++) {
    const BuiltInQuote& q = *builtInQuote(i);
    SCOPED_TRACE(q.text);
    r.clearScreen();
    QuoteLayout layout;
    ASSERT_TRUE(renderQuoteText(r, q.text, q.attribution, &layout));
    EXPECT_FALSE(layout.truncated);
    EXPECT_TRUE(layout.mark);
    EXPECT_LT(layout.fontIndex, SERIF_WITH_MARK_COUNT);
    largestStep = std::max(largestStep, layout.fontIndex);
    char name[64];
    std::snprintf(name, sizeof(name), "/quote_builtin_%02zu.png", i);
    ASSERT_TRUE(preview::writeFramePng(preview::outputDir() + name));
  }
  std::printf("built-in quotes: %zu, smallest size used: step %d of %d\n", builtInCount(), largestStep,
              SERIF_WITH_MARK_COUNT - 1);
}

TEST(SleepCardQuote, BuiltInOnlyCardRenders) {
  FakeIo io;
  io.files[QUOTES_PATH] = THREE_QUOTES;
  bool declined = true;
  const double ms =
      preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::BuiltInOnly), "quote_builtin", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, preview::HOST_RUNAWAY_MS);
  EXPECT_TRUE(isBuiltInHash(shownHash(io)));
  // Never the same twice in a row, over the whole set.
  uint32_t last = shownHash(io);
  for (uint32_t seed = 1; seed < 40; seed++) {
    ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::BuiltInOnly, seed), preview::renderer()));
    EXPECT_NE(shownHash(io), last);
    last = shownHash(io);
  }
}

TEST(SleepCardQuote, VerseLineBreaksAreKept) {
  const char* text = "aa bb\ncc\ndd ee ff";
  LineSpan lines[8];
  bool overflow = false;
  // Wide enough for everything on one line: the breaks still hold.
  const int n = wrapLines(text, 1000, 0, &fakeMeasure, nullptr, lines, 8, overflow);
  ASSERT_EQ(n, 3);
  EXPECT_FALSE(overflow);
  EXPECT_EQ(lineOf(text, lines[0]), "aa bb");
  EXPECT_EQ(lineOf(text, lines[1]), "cc");
  EXPECT_EQ(lineOf(text, lines[2]), "dd ee ff");
  // Narrow: a verse line wraps inside itself, and the next one still starts a line.
  EXPECT_EQ(wrapLines(text, 50, 0, &fakeMeasure, nullptr, lines, 8, overflow), 4);
  EXPECT_EQ(lineOf(text, lines[2]), "dd ee");
  EXPECT_EQ(lineOf(text, lines[3]), "ff");
  EXPECT_EQ(wrapLines("\n\nx\n", 50, 0, &fakeMeasure, nullptr, lines, 8, overflow), 1);
}

// ---- My quotes: repeats of the built-in set, and headings --------------------------------------------

TEST(SleepCardQuote, TextHashIgnoresSpacingPunctuationAndCase) {
  const std::string a = "Nature\xE2\x80\x99s peace will flow into you\nas sunshine flows into trees.";
  const std::string b = "nature's  peace will flow into you as sunshine flows into trees";
  EXPECT_EQ(hashQuoteText(a.data(), a.size()), hashQuoteText(b.data(), b.size()));
  const std::string c = "Nature's peace will flow into me as sunshine flows into trees.";
  EXPECT_NE(hashQuoteText(a.data(), a.size()), hashQuoteText(c.data(), c.size()));
  // Letters outside ASCII count.
  EXPECT_NE(hashQuoteText("caf\xC3\xA9", 5), hashQuoteText("caf", 3));
  // The scanner's text hash leaves the attribution out.
  const auto entries =
      scanAll("Early to bed and early to rise,\nmakes a man healthy, wealthy, and wise.\n-- Someone\n", 7);
  ASSERT_EQ(entries.size(), 1u);
  const BuiltInQuote* franklin = nullptr;
  for (size_t i = 0; i < builtInCount(); i++) {
    if (std::strstr(builtInQuote(i)->text, "Early to bed")) franklin = builtInQuote(i);
  }
  ASSERT_NE(franklin, nullptr);
  EXPECT_EQ(entries[0].textHash, hashQuoteText(franklin->text, std::strlen(franklin->text)));
}

TEST(SleepCardQuote, MyQuotesThatRepeatABuiltInAreLeftOut) {
  // Built-in wording, straight apostrophe, re-wrapped, own attribution style.
  const std::string repeat =
      "Climb the mountains and get their good tidings.\nNature's peace will flow into you as "
      "sunshine flows into trees.\n -- John Muir\n";
  FakeIo io;
  io.files[QUOTES_PATH] = repeat;
  const uint32_t repeatHash = scanAll(repeat, repeat.size())[0].hash;
  // With the built-in set on, the file's copy never shows (the built-in one may).
  for (uint32_t seed = 0; seed < 30; seed++) {
    ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::BuiltInAndMine, seed), preview::renderer()));
    EXPECT_NE(shownHash(io), repeatHash) << seed;
  }
  // Without it, the file is all there is: its copy shows.
  ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::MineOnly), preview::renderer()));
  EXPECT_EQ(shownHash(io), repeatHash);
  // Its own quotes still show beside the built-in set.
  const std::string mine = repeat + "\nA quote of my own.\n-- Me\n";
  io.files[QUOTES_PATH] = mine;
  const uint32_t ownHash = scanAll(mine, mine.size())[1].hash;
  int own = 0;
  for (uint32_t seed = 0; seed < 60; seed++) {
    ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::BuiltInAndMine, seed), preview::renderer()));
    EXPECT_NE(shownHash(io), repeatHash) << seed;
    own += shownHash(io) == ownHash;
  }
  EXPECT_GT(own, 10);
}

TEST(SleepCardQuote, AStarterFileOfBuiltInsCountsOnce) {
  // Public-domain entries as a starter /quotes.txt spells them (plain apostrophes, no works, a
  // comma less): each is recognised as its built-in quote.
  const std::string starter =
      "We need the tonic of wildness.\n-- Henry David Thoreau, Walden\n\n"
      "Heaven is under our feet as well as over our heads.\n-- Henry David Thoreau, Walden\n\n"
      "In wildness is the preservation of the world.\n-- Henry David Thoreau, Walking\n\n"
      "Climb the mountains and get their good tidings. Nature's peace will flow into you as sunshine flows into "
      "trees.\n-- John Muir, Our National Parks\n\n"
      "The woods are lovely, dark and deep,\nBut I have promises to keep,\nAnd miles to go before I sleep.\n"
      "-- Robert Frost\n\n"
      "I travel not to go anywhere, but to go. I travel for travel's sake. The great affair is to move.\n"
      "-- Robert Louis Stevenson, Travels with a Donkey in the Cevennes\n\n"
      "Nature always wears the colors of the spirit.\n-- Ralph Waldo Emerson, Nature\n\n"
      "The poetry of earth is never dead.\n-- John Keats\n\n"
      "Early to bed and early to rise, makes a man healthy, wealthy, and wise.\n-- Benjamin Franklin\n\n"
      "One touch of nature makes the whole world kin.\n-- William Shakespeare, Troilus and Cressida\n\n"
      "# Mine\n"
      "A line nobody else wrote.\n-- Me\n";
  std::vector<uint32_t> builtIn;
  for (size_t i = 0; i < builtInCount(); i++) {
    builtIn.push_back(hashQuoteText(builtInQuote(i)->text, std::strlen(builtInQuote(i)->text)));
  }
  const auto entries = scanAll(starter, 97);
  ASSERT_EQ(entries.size(), 11u);
  int repeats = 0;
  for (const EntrySpan& e : entries) {
    for (const uint32_t id : builtIn) repeats += e.textHash == id;
  }
  EXPECT_EQ(repeats, 10);
}

TEST(SleepCardQuote, HeadingLinesGroupWithoutShowing) {
  const std::string text = "# BattleTech\nFirst of mine.\n-- Someone\n# Another group\nSecond of mine.\n\n#\nThird.\n";
  const auto entries = scanAll(text, 5);
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(sliceOf(text, entries[0]), "First of mine.\n-- Someone");
  EXPECT_EQ(sliceOf(text, entries[1]), "Second of mine.");
  EXPECT_EQ(sliceOf(text, entries[2]), "Third.");
  char out[64], attribution[64];
  const std::string raw = "# Group\nSome text.\n-- A";
  ASSERT_TRUE(parseEntry(raw.data(), raw.size(), out, sizeof(out), attribution, sizeof(attribution)));
  EXPECT_STREQ(out, "Some text.");
  EXPECT_STREQ(attribution, "A");
  const std::string onlyHeading = "# Nothing here";
  EXPECT_FALSE(parseEntry(onlyHeading.data(), onlyHeading.size(), out, sizeof(out), attribution, sizeof(attribution)));
  EXPECT_TRUE(scanAll(onlyHeading, 3).empty());
}

TEST(SleepCardQuote, WrappedVerseLinesAreMarkedForTheHangingIndent) {
  const char* verse = "aaaa bbb ccc\ndddd eee ff";
  bool overflow = false;
  FitResult fit;
  fit.lineCount = wrapLines(verse, 80, 0, &fakeMeasure, nullptr, fit.lines, MAX_LINES, overflow);  // 8 bytes a line
  ASSERT_EQ(fit.lineCount, 4);
  EXPECT_EQ(lineOf(verse, fit.lines[1]), "ccc");
  EXPECT_FALSE(continuesVerseLine(verse, fit, 0));
  EXPECT_TRUE(continuesVerseLine(verse, fit, 1));   // "ccc" goes on from "aaaa bbb"
  EXPECT_FALSE(continuesVerseLine(verse, fit, 2));  // "dddd" starts a verse line
  EXPECT_TRUE(continuesVerseLine(verse, fit, 3));
  // Prose never hangs.
  const char* prose = "aaaa bbb ccc dddd";
  fit.lineCount = wrapLines(prose, 80, 0, &fakeMeasure, nullptr, fit.lines, MAX_LINES, overflow);
  EXPECT_FALSE(continuesVerseLine(prose, fit, 1));
}

TEST(SleepCardQuote, AnInlineAttributionIsNotPartOfTheIdentity) {
  // The scanner's identity of an entry is the hash of the text parseEntry shows, whichever way the
  // attribution is written (own line, inline after " -- ", none) and however the bytes arrive.
  const std::string name80(80, 'n');
  const std::string name81(81, 'n');
  const std::vector<std::string> entries = {
      "We need the tonic of wildness. -- Henry David Thoreau",
      "Be kind. -- Plato",
      "a -- b -- Name",
      "text -- -- Name",
      "text --- Name",
      "text --Name",
      "text -- -Name",
      "Line one\nline two -- Name",
      "Line one -- X\nline two",
      "Tabbed\t--\tName",
      "Own line.\n-- Someone -- Else",
      "Own line.\n  -- Someone",
      "x -- " + name80,
      "x -- " + name81,
      "-- only a name",
      "  Spaced   out  --   Name  ",
  };
  for (const std::string& entry : entries) {
    char text[TEXT_CAP];
    char attribution[ATTRIBUTION_CAP];
    const bool parsed = parseEntry(entry.data(), entry.size(), text, sizeof(text), attribution, sizeof(attribution));
    for (const size_t chunk : {size_t{1}, size_t{3}, size_t{100}}) {
      const auto spans = scanAll(entry, chunk);
      ASSERT_EQ(spans.size(), 1u) << entry;
      EXPECT_EQ(spans[0].hasText, parsed) << entry;
      if (parsed) EXPECT_EQ(spans[0].textHash, hashQuoteText(text, std::strlen(text))) << entry << " / " << chunk;
    }
  }
}

TEST(SleepCardQuote, ARepeatWithAnInlineAttributionIsLeftOut) {
  const std::string repeat = "We need the tonic of wildness. -- Henry David Thoreau\n";
  FakeIo io;
  io.files[QUOTES_PATH] = repeat;
  const uint32_t repeatHash = scanAll(repeat, repeat.size())[0].hash;
  for (uint32_t seed = 0; seed < 20; seed++) {
    ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::BuiltInAndMine, seed), preview::renderer()));
    EXPECT_NE(shownHash(io), repeatHash) << seed;
  }
}

TEST(SleepCardQuote, AnUnreadableEntryFallsBackToTheBuiltInSet) {
  // The file is scanned, then its chosen entry cannot be read back (rewritten over USB meanwhile,
  // or the card read failed): the built-in set stands in, never the logo screen.
  FakeIo io;
  io.files[QUOTES_PATH] = THREE_QUOTES;
  io.failQuotesReadAt = 2;  // read 1 = the scan, read 2 = the chosen entry
  bool declined = true;
  preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::MineOnly), "quote_unreadable", &declined);
  EXPECT_FALSE(declined);
  EXPECT_EQ(io.quotesReads, 2);
  EXPECT_TRUE(isBuiltInHash(shownHash(io)));
}
