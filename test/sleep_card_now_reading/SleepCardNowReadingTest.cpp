#include <HalDisplay.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>

#include "CardPreview.h"
#include "src/sleepcards/CoverDraw.h"
#include "src/sleepcards/NowReadingCard.h"
#include "src/sleepcards/NowReadingPace.h"

using namespace sleepcards;
using namespace sleepcards::pace;

namespace {

// Pages 0, 1, 2, ... of one chapter, one every `secondsPerPage`.
void readPages(PaceSession& s, uint32_t& ms, const int spine, const int firstPage, const int count,
               const float secondsPerPage, const int pageCount = 20, const float share = 0.01f) {
  for (int p = firstPage; p < firstPage + count; p++) {
    observePage(s, ms, spine, p, pageCount, share);
    ms += static_cast<uint32_t>(secondsPerPage * 1000);
  }
}

// The host CardIo with the book changed by a test (no cover, another chapter, ...).
class EditedBookIo final : public CardIo {
 public:
  std::function<void(CardBook&)> edit;
  bool coverDraws = true;
  int32_t fileSize(const char* path) const override { return preview::hostIo().fileSize(path); }
  int32_t readFileAt(const char* path, uint32_t offset, char* buf, size_t cap) const override {
    return preview::hostIo().readFileAt(path, offset, buf, cap);
  }
  bool writeFile(const char* path, const char* data, size_t len) const override {
    return preview::hostIo().writeFile(path, data, len);
  }
  bool loadBook(CardBook& out) const override {
    if (!preview::hostIo().loadBook(out)) return false;
    if (edit) edit(out);
    return true;
  }
  bool drawBookCover(GfxRenderer& r, int x, int y, int w, int h) const override {
    return coverDraws && preview::hostIo().drawBookCover(r, x, y, w, h);
  }
  int loadBookmarks(CardSnippet* out, int max) const override { return preview::hostIo().loadBookmarks(out, max); }
};

// The session as the reader would leave it after reading the sample book's chapter (spine 38,
// 14 pages, 0.95 % of the book) at ~42 s a page.
void seedSampleSession() {
  PaceSession& s = session();
  s = PaceSession{};
  uint32_t ms = 1000000;
  readPages(s, ms, 37, 0, 12, 42.0f, 12, 0.008f);
  readPages(s, ms, 38, 0, 5, 42.0f, 14, 0.0095f);
}

}  // namespace

// ---- the pace estimator ------------------------------------------------------------------------

TEST(NowReadingPace, FirstSampleIsTheEstimate) {
  PaceSession s;
  uint32_t ms = 5000;
  readPages(s, ms, 3, 0, 2, 30.0f);
  EXPECT_EQ(s.samples, 1u);
  PaceState st = merged(PaceState{}, s);
  EXPECT_EQ(st.samples, 1u);
  EXPECT_LT(secondsPerPage(st), 0) << "no estimate below MIN_SAMPLES";
  readPages(s, ms, 3, 2, MIN_SAMPLES, 30.0f);
  st = merged(PaceState{}, s);
  EXPECT_NEAR(secondsPerPage(st), 30.0f, 1e-3);
}

TEST(NowReadingPace, IgnoresRepaintsJumpsAndBack) {
  PaceSession s;
  observePage(s, 0, 5, 3, 20, 0.01f);
  EXPECT_FALSE(observePage(s, 20000, 5, 3, 20, 0.01f)) << "same page again = repaint";
  EXPECT_TRUE(observePage(s, 40000, 5, 4, 20, 0.01f)) << "40 s after the first showing, not the repaint";
  EXPECT_NEAR(s.sum / s.weight, 40.0f, 1e-3);
  EXPECT_FALSE(observePage(s, 60000, 5, 3, 20, 0.01f)) << "back";
  EXPECT_FALSE(observePage(s, 80000, 5, 9, 20, 0.01f)) << "jump";
  EXPECT_FALSE(observePage(s, 100000, 6, 0, 20, 0.01f)) << "next chapter (includes building it)";
  EXPECT_TRUE(observePage(s, 130000, 6, 1, 20, 0.01f));
  EXPECT_EQ(s.samples, 2u);
}

TEST(NowReadingPace, GapLimits) {
  PaceSession s;
  observePage(s, 0, 1, 0, 50, 0);
  EXPECT_FALSE(observePage(s, MIN_GAP_MS - 1, 1, 1, 50, 0)) << "under 1 s = flipping";
  EXPECT_TRUE(observePage(s, MIN_GAP_MS - 1 + MIN_GAP_MS, 1, 2, 50, 0)) << "exactly 1 s";
  const uint32_t t = s.previousMs;
  EXPECT_FALSE(observePage(s, t + MAX_GAP_MS + 1, 1, 3, 50, 0)) << "over 5 min = a pause";
  EXPECT_TRUE(observePage(s, t + MAX_GAP_MS + 1 + MAX_GAP_MS, 1, 4, 50, 0)) << "exactly 5 min";
  EXPECT_EQ(s.samples, 2u);
}

TEST(NowReadingPace, MillisWrapIsOneGap) {
  PaceSession s;
  observePage(s, UINT32_MAX - 9999, 1, 0, 50, 0);
  EXPECT_TRUE(observePage(s, 20000, 1, 1, 50, 0));
  EXPECT_NEAR(s.sum / s.weight, 30.0f, 1e-3);
}

TEST(NowReadingPace, OutliersAreClampedToTheReference) {
  PaceSession s;
  uint32_t ms = 0;
  readPages(s, ms, 2, 0, MIN_SAMPLES + 1, 40.0f, 100);
  EXPECT_NEAR(s.sum / s.weight, 40.0f, 1e-3);
  const float sum = s.sum;
  const float weight = s.weight;
  // A 4.5 minute phone call on one page counts as at most 3x the pace.
  observePage(s, ms + 270000 - 40000, 2, MIN_SAMPLES + 1, 100, 0);
  EXPECT_NEAR(s.sum / s.weight, ((1 - ALPHA) * sum + ALPHA * 120.0f) / ((1 - ALPHA) * weight + ALPHA), 1e-3);

  // A stored pace is the reference before the session has its own.
  PaceSession fresh;
  PaceState stored;
  stored.weight = 1;
  stored.weightedSum = 40;
  stored.samples = 50;
  seedReference(fresh, stored);
  observePage(fresh, 0, 1, 0, 50, 0);
  observePage(fresh, 2000, 1, 1, 50, 0);  // 2 s: skimming, clamped up to 40 / 3
  EXPECT_NEAR(fresh.sum / fresh.weight, 40.0f / 3, 1e-3);
}

TEST(NowReadingPace, MergeEqualsApplyingTheSamplesInOrder) {
  // Session A then session B, merged step by step, equals one session over all samples.
  const float a[] = {30, 45, 38, 52, 41, 36, 60};
  const float b[] = {25, 33, 47, 29};
  PaceSession sa, sb, all;
  uint32_t ms = 0;
  int page = 0;
  observePage(sa, ms, 1, page, 100, 0);
  observePage(all, ms, 1, page, 100, 0);
  for (const float x : a) {
    ms += static_cast<uint32_t>(x * 1000);
    observePage(sa, ms, 1, ++page, 100, 0);
    observePage(all, ms, 1, page, 100, 0);
  }
  observePage(sb, ms, 1, page, 100, 0);
  for (const float x : b) {
    ms += static_cast<uint32_t>(x * 1000);
    observePage(sb, ms, 1, ++page, 100, 0);
    observePage(all, ms, 1, page, 100, 0);
  }
  const PaceState step = merged(merged(PaceState{}, sa), sb);
  const PaceState once = merged(PaceState{}, all);
  EXPECT_EQ(step.samples, once.samples);
  EXPECT_NEAR(step.weight, once.weight, 1e-5);
  EXPECT_NEAR(secondsPerPage(step), secondsPerPage(once), 1e-3);
}

TEST(NowReadingPace, ClearSamplesKeepsTheChainAndLayout) {
  PaceSession s;
  uint32_t ms = 0;
  readPages(s, ms, 4, 0, 8, 20.0f, 30, 0.03f);
  clearSamples(s);
  EXPECT_EQ(s.samples, 0u);
  EXPECT_EQ(s.decay, 1.0f);
  EXPECT_NEAR(s.referenceSpp, 20.0f, 1e-3);
  EXPECT_EQ(s.spineIndex, 4);
  EXPECT_NEAR(s.fractionPerPage, 0.001f, 1e-7);
  EXPECT_TRUE(observePage(s, ms, 4, 8, 30, 0.03f)) << "the next page still continues the chain";
}

TEST(NowReadingPace, StateTextRoundTrip) {
  PaceState st;
  st.weight = 0.8765f;
  st.weightedSum = 0.8765f * 41.25f;
  st.samples = 1234;
  st.spineIndex = 38;
  st.chapterPageCount = 14;
  st.fractionPerPage = 0.000678f;
  char text[STATE_TEXT_CAP];
  const size_t len = formatState(st, text, sizeof(text));
  EXPECT_STREQ(text, "NRP1 41250 876500 1234 39 14 678000\n");
  PaceState back;
  ASSERT_TRUE(parseState(text, len, back));
  EXPECT_NEAR(secondsPerPage(back), 41.25f, 1e-3);
  EXPECT_NEAR(back.weight, st.weight, 1e-6);
  EXPECT_EQ(back.samples, 1234u);
  EXPECT_EQ(back.spineIndex, 38);
  EXPECT_EQ(back.chapterPageCount, 14);
  EXPECT_NEAR(back.fractionPerPage, st.fractionPerPage, 1e-9);

  PaceState none;
  const size_t noneLen = formatState(none, text, sizeof(text));
  ASSERT_TRUE(parseState(text, noneLen, back));
  EXPECT_EQ(back.spineIndex, -1);
  EXPECT_LT(secondsPerPage(back), 0);
}

TEST(NowReadingPace, BadStateTextIsRejected) {
  PaceState out;
  out.samples = 77;
  const char* bad[] = {"",
                       "NRP1",
                       "NRP2 1 2 3 4 5 6\n",
                       "NRP1 1 2 3 4 5\n",
                       "NRP1 1 2 3 4 5 x\n",
                       "NRP1 1 2 3 4 5 6 7\n",
                       "NRP1 -1 2 3 4 5 6\n",
                       "NRP1 1 2000000 3 4 5 6\n",
                       "NRP1 99999999999 2 3 4 5 6\n"};
  for (const char* text : bad) EXPECT_FALSE(parseState(text, strlen(text), out)) << text;
  EXPECT_EQ(out.samples, 77u) << "untouched on failure";
  // A read cut short keeps the prefix only: rejected.
  const char* full = "NRP1 41250 876500 1234 39 14 678000\n";
  EXPECT_FALSE(parseState(full, 20, out));
  EXPECT_TRUE(parseState(full, strlen(full) - 1, out)) << "no trailing newline is fine";
}

TEST(NowReadingPace, TimeLeft) {
  PaceState pace;
  pace.weight = 1;
  pace.weightedSum = 42;
  pace.samples = 30;
  pace.spineIndex = 38;
  pace.chapterPageCount = 14;
  pace.fractionPerPage = 0.0095f / 14;
  CardBook book;
  book.spineIndex = 38;
  book.chapterPage = 4;
  book.chapterPageCount = 14;
  book.bookFraction = 0.284f;

  TimeLeft t = estimateTimeLeft(pace, book);
  EXPECT_EQ(t.chapterMinutes, 7);  // 10 pages x 42 s
  const double pages = (1 - 0.284) / (0.0095 / 14);
  EXPECT_EQ(t.bookMinutes, static_cast<int>(std::lround(pages * 42 / 60)));

  // Another chapter or another layout: the book estimate is unknown, the chapter one still fine.
  book.spineIndex = 39;
  t = estimateTimeLeft(pace, book);
  EXPECT_EQ(t.chapterMinutes, 7);
  EXPECT_EQ(t.bookMinutes, -1);
  book.spineIndex = 38;
  book.chapterPageCount = 15;
  EXPECT_EQ(estimateTimeLeft(pace, book).bookMinutes, -1);

  // No pace yet: nothing.
  pace.samples = MIN_SAMPLES - 1;
  t = estimateTimeLeft(pace, book);
  EXPECT_EQ(t.chapterMinutes, -1);
  EXPECT_EQ(t.bookMinutes, -1);

  // Last page of a chapter: never "0 min".
  pace.samples = 30;
  book.chapterPageCount = 14;
  book.chapterPage = 13;
  pace.weightedSum = 10;
  EXPECT_EQ(estimateTimeLeft(pace, book).chapterMinutes, 1);
}

TEST(NowReadingPace, FormatMinutes) {
  char out[24];
  const struct {
    int minutes;
    const char* text;
  } cases[] = {{0, "~1 min"},  {1, "~1 min"},      {12, "~12 min"},      {59, "~59 min"}, {60, "~1 h"},
               {62, "~1 h"},   {63, "~1 h 5 min"}, {342, "~5 h 40 min"}, {598, "~10 h"},  {599, "~10 h"},
               {629, "~10 h"}, {631, "~11 h"},     {60 * 999, "~999 h"}};
  for (const auto& c : cases) {
    ASSERT_TRUE(formatMinutes(c.minutes, out, sizeof(out)));
    EXPECT_STREQ(out, c.text) << c.minutes;
  }
  EXPECT_FALSE(formatMinutes(-1, out, sizeof(out)));
}

TEST(NowReadingCard, CurrentPaceMergesStoredAndSession) {
  const char* stored = "NRP1 40000 1000000 100 39 14 678571\n";
  PaceSession s;
  EXPECT_NEAR(secondsPerPage(nowreading::currentPace(stored, strlen(stored), s)), 40.0f, 1e-3);
  EXPECT_LT(secondsPerPage(nowreading::currentPace(nullptr, 0, s)), 0);
  EXPECT_LT(secondsPerPage(nowreading::currentPace("junk", 4, s)), 0);
  uint32_t ms = 0;
  readPages(s, ms, 7, 0, 3, 60.0f);  // two new samples at 60 s
  const PaceState p = nowreading::currentPace(stored, strlen(stored), s);
  EXPECT_EQ(p.samples, 102u);
  EXPECT_NEAR(secondsPerPage(p), 0.81f * 40 + 0.19f * 60, 1e-2);
  EXPECT_EQ(p.spineIndex, 7) << "the session's layout is the newer one";
}

TEST(NowReadingCard, Finished) {
  CardBook book;
  book.spineCount = 10;
  book.spineIndex = 10;
  EXPECT_TRUE(nowreading::bookFinished(book));
  book.spineIndex = 9;
  book.bookFraction = 0.98f;
  EXPECT_FALSE(nowreading::bookFinished(book));
  book.bookFraction = 1.0f;
  EXPECT_TRUE(nowreading::bookFinished(book));
}

// ---- the cover -------------------------------------------------------------------------------------

TEST(NowReadingCover, FitSize) {
  int w = 0, h = 0;
  ASSERT_TRUE(cover::fitSize(180, 300, 250, 360, w, h));  // fits: as it is
  EXPECT_EQ(w, 180);
  EXPECT_EQ(h, 300);
  ASSERT_TRUE(cover::fitSize(240, 400, 250, 360, w, h));  // too tall: height-limited, aspect kept
  EXPECT_EQ(h, 360);
  EXPECT_EQ(w, 216);
  ASSERT_TRUE(cover::fitSize(400, 300, 250, 360, w, h));  // too wide: width-limited
  EXPECT_EQ(w, 250);
  EXPECT_EQ(h, 187);
  EXPECT_FALSE(cover::fitSize(0, 300, 250, 360, w, h));
  EXPECT_FALSE(cover::fitSize(180, 300, 250, 0, w, h));
}

TEST(NowReadingCover, ScaledCoverKeepsItsTone) {
  // The fixture thumbnail at 1:1 and at 0.9: the share of ink stays (an area average, re-dithered),
  // and nothing lands outside the box.
  GfxRenderer& r = preview::renderer();
  const auto inkIn = [&](const int x0, const int y0, const int w, const int h) {
    // Logical (portrait) pixels of the framebuffer, as writeFramePng reads them.
    const uint8_t* fb = display.getFrameBuffer();
    int n = 0;
    for (int y = y0; y < y0 + h; y++) {
      for (int x = x0; x < x0 + w; x++) {
        const int px = y;
        const int py = HalDisplay::DISPLAY_HEIGHT - 1 - x;
        n += ((fb[py * HalDisplay::DISPLAY_WIDTH_BYTES + px / 8] >> (7 - (px & 7))) & 1) == 0 ? 1 : 0;
      }
    }
    return n / static_cast<double>(w * h);
  };
  r.clearScreen();
  ASSERT_TRUE(preview::hostIo().drawBookCover(r, 20, 20, 180, 300));
  const double full = inkIn(20, 20, 180, 300);
  r.clearScreen();
  ASSERT_TRUE(preview::hostIo().drawBookCover(r, 20, 20, 162, 270));
  const double scaled = inkIn(20, 20, 162, 270);
  EXPECT_GT(full, 0.2);
  EXPECT_NEAR(scaled, full, 0.04);
  EXPECT_EQ(inkIn(20, 290, 162, 30), 0.0);  // below the scaled box
  EXPECT_EQ(inkIn(182, 20, 30, 300), 0.0);  // right of it
}

// ---- rendering -----------------------------------------------------------------------------------

TEST(NowReadingCard, RendersTheSampleBookWithPace) {
  seedSampleSession();
  bool declined = true;
  const double ms = preview::renderCardPng(CardId::NowReading, preview::sampleContext(), "now_reading_pace", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, preview::HOST_RUNAWAY_MS);
  std::printf("now_reading_pace: %.2f ms\n", ms);
  EXPECT_EQ(session().samples, 0u) << "the card saved the session's samples";

  // The saved state (the host writes it under build/cards/state).
  std::ifstream in(preview::outputDir() + "/state" + STATE_PATH);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  PaceState saved;
  ASSERT_TRUE(parseState(text.c_str(), text.size(), saved)) << text;
  EXPECT_EQ(saved.samples, 15u);
  EXPECT_NEAR(secondsPerPage(saved), 42.0f, 0.01f);
  EXPECT_EQ(saved.spineIndex, 38);
}

TEST(NowReadingCard, RendersWithoutPace) {
  session() = PaceSession{};
  bool declined = true;
  preview::renderCardPng(CardId::NowReading, preview::sampleContext(), "now_reading_nopace", &declined);
  EXPECT_FALSE(declined);
}

TEST(NowReadingCard, RendersWithoutTime) {
  seedSampleSession();
  CardContext ctx = preview::sampleContext();
  ctx.timeValid = false;
  bool declined = true;
  preview::renderCardPng(CardId::NowReading, ctx, "now_reading_notime", &declined);
  EXPECT_FALSE(declined) << "the book needs no clock";
}

TEST(NowReadingCard, Variants) {
  EditedBookIo io;
  CardContext ctx = preview::sampleContext();
  ctx.io = &io;
  bool declined = true;

  // No cover; a long title and chapter; no author.
  seedSampleSession();
  io.edit = [](CardBook& b) {
    b.hasCover = false;
    std::snprintf(b.title, sizeof(b.title), "%s",
                  "The Narrative of Arthur Gordon Pym of Nantucket, Comprising the Details of a Mutiny");
    b.author[0] = '\0';
    std::snprintf(b.chapterTitle, sizeof(b.chapterTitle), "%s",
                  "Chapter XXIV. Our Escape from the Island and What Befell Us on the Southern Sea");
  };
  const double ms = preview::renderCardPng(CardId::NowReading, ctx, "now_reading_nocover", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, preview::HOST_RUNAWAY_MS);

  // A cover that fails to draw: the card still shows, text moves up.
  seedSampleSession();
  io.edit = nullptr;
  io.coverDraws = false;
  preview::renderCardPng(CardId::NowReading, ctx, "now_reading_coverfail", &declined);
  EXPECT_FALSE(declined);
  io.coverDraws = true;

  // A TXT/XTC book: title and percent only.
  session() = PaceSession{};
  io.edit = [](CardBook& b) {
    b = CardBook{};
    std::snprintf(b.title, sizeof(b.title), "%s", "field-notes-2026");
    b.percent = 63;
    b.bookFraction = 0.63f;
  };
  preview::renderCardPng(CardId::NowReading, ctx, "now_reading_txt", &declined);
  EXPECT_FALSE(declined);

  // Finished.
  io.edit = [](CardBook& b) {
    b.spineIndex = b.spineCount;
    b.chapterPage = -1;
    b.chapterPageCount = 0;
    b.bookFraction = 1.0f;
  };
  preview::renderCardPng(CardId::NowReading, ctx, "now_reading_finished", &declined);
  EXPECT_FALSE(declined);

  // Only the chapter estimate (the saved layout is another book's).
  seedSampleSession();
  io.edit = [](CardBook& b) { b.spineIndex = 12; };
  preview::renderCardPng(CardId::NowReading, ctx, "now_reading_chapteronly", &declined);
  EXPECT_FALSE(declined);
}

TEST(NowReadingCard, DeclinesWithoutABook) {
  CardContext ctx = preview::sampleContext();
  preview::hostIo().hasBook = false;
  bool declined = false;
  preview::renderCardPng(CardId::NowReading, ctx, "now_reading_nobook", &declined);
  preview::hostIo().hasBook = true;
  EXPECT_TRUE(declined);

  ctx.bookPath = "";
  preview::renderCardPng(CardId::NowReading, ctx, "now_reading_nobook", &declined);
  EXPECT_TRUE(declined);

  EditedBookIo io;
  io.edit = [](CardBook& b) { b.title[0] = '\0'; };
  ctx = preview::sampleContext();
  ctx.io = &io;
  preview::renderCardPng(CardId::NowReading, ctx, "now_reading_nobook", &declined);
  EXPECT_TRUE(declined) << "a book with no title and no file name is not a book";
}
