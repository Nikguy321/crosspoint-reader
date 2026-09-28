#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "CardPreview.h"
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

  int32_t fileSize(const char* path) const override {
    const auto it = files.find(path);
    return it == files.end() ? -1 : static_cast<int32_t>(it->second.size());
  }
  int32_t readFileAt(const char* path, const uint32_t offset, char* buf, const size_t cap) const override {
    reads++;
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

TEST(SleepCardQuote, ChooseSourcePrefersTheSettingAndFallsBack) {
  uint32_t rng = 7;
  const PoolStats none{0, 0}, some{3, 3}, stale{1, 0};
  EXPECT_EQ(chooseSource(QuoteSource::File, none, none, rng), Pick::None);
  EXPECT_EQ(chooseSource(QuoteSource::Both, none, none, rng), Pick::None);
  EXPECT_EQ(chooseSource(QuoteSource::File, some, some, rng), Pick::File);
  EXPECT_EQ(chooseSource(QuoteSource::File, none, some, rng), Pick::Bookmarks);
  EXPECT_EQ(chooseSource(QuoteSource::Bookmarks, some, some, rng), Pick::Bookmarks);
  EXPECT_EQ(chooseSource(QuoteSource::Bookmarks, some, none, rng), Pick::File);
  EXPECT_EQ(chooseSource(QuoteSource::File, stale, some, rng), Pick::File);  // its own source, even if stale
  EXPECT_EQ(chooseSource(QuoteSource::Both, stale, some, rng), Pick::Bookmarks);
  EXPECT_EQ(chooseSource(QuoteSource::Both, some, stale, rng), Pick::File);
  EXPECT_EQ(chooseSource(QuoteSource::Both, stale, none, rng), Pick::File);
  int file = 0;
  for (int i = 0; i < 1000; i++) file += chooseSource(QuoteSource::Both, some, some, rng) == Pick::File;
  EXPECT_NEAR(file, 500, 80);
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
    const double ms = preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::File), f.name, &declined);
    EXPECT_FALSE(declined) << f.name;
    EXPECT_LT(ms, preview::HOST_RUNAWAY_MS) << f.name;
    std::printf("%s: %.2f ms\n", f.name, ms);
  }
  FakeIo io;
  io.bookmarks = {snippet("It is not down in any map; true places never are.", "Kokovoko", 0.071f)};
  preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::Bookmarks), "quote_bookmark", &declined);
  EXPECT_FALSE(declined);
  io.bookmarks = {snippet("Call me Ishmael. Some years ago - never mind how long precisely - having", "", 0.002f)};
  preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::Bookmarks), "quote_bookmark_cut", &declined);
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
  const double ms = preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::File), "quote_max", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, preview::HOST_RUNAWAY_MS);
  std::printf("quote_max (%zu bytes): %.2f ms\n", entry.size(), ms);
}

TEST(SleepCardQuote, NeverTheSameTwiceInARow) {
  FakeIo io;
  io.files[QUOTES_PATH] = THREE_QUOTES;
  std::string last;
  for (uint32_t seed = 0; seed < 30; seed++) {
    ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::File, seed), preview::renderer()));
    const std::string now = io.files[STATE_PATH];
    EXPECT_EQ(now.size(), 9u);
    EXPECT_NE(now, last) << "seed " << seed;
    last = now;
  }
}

TEST(SleepCardQuote, OneQuoteIsShownAgain) {
  FakeIo io;
  io.files[QUOTES_PATH] = "Only me.\n";
  EXPECT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::File), preview::renderer()));
  EXPECT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::File), preview::renderer()));
}

TEST(SleepCardQuote, FallsBackBetweenSourcesThenDeclines) {
  FakeIo io;
  io.bookmarks = {snippet("It is not down in any map; true places never are.", "", 0.07f)};
  // File wanted, no file: the bookmark.
  EXPECT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::File), preview::renderer()));
  // Bookmarks wanted, no book: the file.
  io.hasBook = false;
  io.files[QUOTES_PATH] = THREE_QUOTES;
  EXPECT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::Bookmarks), preview::renderer()));
  // Nothing anywhere: decline (the logo screen), and nothing written.
  io.files.clear();
  io.writes = 0;
  bool declined = false;
  preview::renderCardPng(CardId::Quote, fakeContext(io, QuoteSource::Both), "quote_none", &declined);
  EXPECT_TRUE(declined);
  EXPECT_EQ(io.writes, 0);
  // Bookmarks whose summaries are empty do not count.
  io.hasBook = true;
  io.bookmarks = {snippet(""), snippet("  ")};
  EXPECT_FALSE(renderQuoteCard(fakeContext(io, QuoteSource::Both), preview::renderer()));
}

TEST(SleepCardQuote, JunkFilesAreHarmless) {
  FakeIo io;
  // Binary noise: no newline ever, NULs, high bytes.
  std::string junk(20000, '\0');
  uint32_t s = 9;
  for (auto& c : junk) c = static_cast<char>(nextRandom(s));
  io.files[QUOTES_PATH] = junk;
  renderQuoteCard(fakeContext(io, QuoteSource::File), preview::renderer());  // whatever it decides, no crash
  // Only attributions and an entry over the size cap: nothing usable.
  io.files[QUOTES_PATH] = "-- a\n\n-- b\n\n" + std::string(ENTRY_CAP + 1, 'x') + "\n";
  EXPECT_FALSE(renderQuoteCard(fakeContext(io, QuoteSource::File), preview::renderer()));
}

TEST(SleepCardQuote, BigFilesAreReadOnlyUpToTheCap) {
  FakeIo io;
  std::string big;
  for (int i = 0; big.size() < 3 * FILE_READ_CAP; i++) big += "Quote number " + std::to_string(i) + ".\n\n";
  io.files[QUOTES_PATH] = big;
  io.reads = 0;
  ASSERT_TRUE(renderQuoteCard(fakeContext(io, QuoteSource::File), preview::renderer()));
  EXPECT_LE(io.maxReadEnd, FILE_READ_CAP);
  // 16 chunks of the file + the chosen entry + the state file.
  EXPECT_LE(io.reads, static_cast<int>(FILE_READ_CAP / READ_CHUNK) + 2);
}
