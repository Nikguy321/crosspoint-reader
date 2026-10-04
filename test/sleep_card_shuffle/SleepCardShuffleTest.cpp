#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "CardPreview.h"
#include "src/sleepcards/QuoteCard.h"
#include "src/sleepcards/ShuffleCard.h"

using namespace sleepcards;
using namespace sleepcards::shuffle;

namespace {

constexpr uint16_t bits(std::initializer_list<CardId> ids) {
  uint16_t m = 0;
  for (const CardId id : ids) m |= cardBit(id);
  return m;
}

const uint16_t ALL = bits(
    {CardId::NowReading, CardId::Day, CardId::Calendar, CardId::Quote, CardId::Owner, CardId::Sky, CardId::Pictures});

// An SD card in memory that round-trips the state file (the preview's fake writes state apart
// from what it reads). Book details come from the preview's sample book.
class MemIo final : public CardIo {
 public:
  std::map<std::string, std::string> files;
  int bookmarks = 0;
  int writes = 0;
  bool failWrites = false;
  bool bookLoads = true;  // false: the book is on the card but cannot be read (Now Reading declines)

  int32_t fileSize(const char* path) const override {
    const auto it = files.find(path);
    return it == files.end() ? -1 : static_cast<int32_t>(it->second.size());
  }
  int32_t readFileAt(const char* path, uint32_t offset, char* buf, size_t cap) const override {
    const auto it = files.find(path);
    if (it == files.end()) return -1;
    if (offset > it->second.size()) return -1;
    const size_t n = std::min(cap, it->second.size() - offset);
    std::memcpy(buf, it->second.data() + offset, n);
    return static_cast<int32_t>(n);
  }
  bool writeFile(const char* path, const char* data, size_t len) const override {
    auto* self = const_cast<MemIo*>(this);
    self->writes++;
    if (failWrites) return false;
    self->files[path] = std::string(data, len);
    return true;
  }
  bool loadBook(CardBook& out) const override { return bookLoads && preview::hostIo().loadBook(out); }
  bool drawBookCover(GfxRenderer& r, int x, int y, int w, int h) const override {
    return preview::hostIo().drawBookCover(r, x, y, w, h);
  }
  int loadBookmarks(CardSnippet* out, int max) const override {
    const int n = std::min(max, bookmarks);
    for (int i = 0; i < n; i++) {
      out[i] = CardSnippet{};
      std::snprintf(out[i].text, sizeof(out[i].text), "Call me Ishmael.");
    }
    return n;
  }
};

// The sample context on an SD card that holds the book and /quotes.txt, with an owner name.
CardContext memContext(MemIo& io) {
  CardContext ctx = preview::sampleContext();
  io.files[ctx.bookPath] = "PK";
  io.files[quote::QUOTES_PATH] = "Call me Ishmael.\n -- Herman Melville\n";
  std::snprintf(ctx.settings.ownerName, sizeof(ctx.settings.ownerName), "A. Reader");
  ctx.io = &io;
  return ctx;
}

uint32_t lcg(uint32_t& s) { return s = s * 1664525u + 1013904223u; }

}  // namespace

// ---- the pure chooser ---------------------------------------------------------------------------------

TEST(SleepCardShuffle, NothingUsableIsNoneAndLeavesState) {
  ShuffleState st{CardId::Day, bits({CardId::Day})};
  EXPECT_EQ(chooseCard(0, st, 1), CardId::None);
  EXPECT_EQ(st.last, CardId::Day);
  EXPECT_EQ(st.dealt, bits({CardId::Day}));
  // None and Shuffle itself are never usable.
  EXPECT_EQ(chooseCard(cardBit(CardId::None) | cardBit(CardId::Shuffle), st, 1), CardId::None);
}

TEST(SleepCardShuffle, OneUsableIsAlwaysThatOne) {
  ShuffleState st;
  for (uint32_t seed = 0; seed < 50; seed++) EXPECT_EQ(chooseCard(bits({CardId::Sky}), st, seed), CardId::Sky);
  EXPECT_EQ(st.last, CardId::Sky);
}

TEST(SleepCardShuffle, TwoUsableAlternate) {
  ShuffleState st;
  uint32_t rng = 7;
  CardId prev = CardId::None;
  for (int i = 0; i < 200; i++) {
    const CardId pick = chooseCard(bits({CardId::Day, CardId::Quote}), st, lcg(rng));
    ASSERT_TRUE(pick == CardId::Day || pick == CardId::Quote);
    ASSERT_NE(pick, prev) << "sleep " << i;
    prev = pick;
  }
}

TEST(SleepCardShuffle, EveryCardOncePerRoundNeverTwiceInARow) {
  for (const uint32_t start : {1u, 99u, 0xDEADBEEFu}) {
    const uint16_t usable = bits({CardId::NowReading, CardId::Day, CardId::Calendar, CardId::Quote, CardId::Sky});
    ShuffleState st;
    uint32_t rng = start;
    CardId prev = CardId::None;
    uint16_t round = 0;
    for (int i = 0; i < 500; i++) {
      const CardId pick = chooseCard(usable, st, lcg(rng));
      ASSERT_NE(pick, prev) << "sleep " << i;
      ASSERT_TRUE(usable & cardBit(pick));
      ASSERT_FALSE(round & cardBit(pick)) << "card dealt twice in one round, sleep " << i;
      round |= cardBit(pick);
      if (round == usable) round = 0;  // five sleeps = one round
      prev = pick;
    }
  }
}

TEST(SleepCardShuffle, ConstantSeedStillRotates) {
  // Even a seed that never changes (a broken RNG) deals every card before repeating.
  const uint16_t usable = bits({CardId::Day, CardId::Calendar, CardId::Quote, CardId::Sky});
  ShuffleState st;
  CardId prev = CardId::None;
  int seen[static_cast<int>(CardId::Count)] = {};
  for (int i = 0; i < 40; i++) {
    const CardId pick = chooseCard(usable, st, 0);
    ASSERT_NE(pick, prev);
    seen[static_cast<int>(pick)]++;
    prev = pick;
  }
  for (const CardId id : {CardId::Day, CardId::Calendar, CardId::Quote, CardId::Sky}) {
    EXPECT_EQ(seen[static_cast<int>(id)], 10);
  }
}

TEST(SleepCardShuffle, PicksAreSpreadOverSeeds) {
  // A fresh round's first card: roughly uniform over 7 cards.
  int count[static_cast<int>(CardId::Count)] = {};
  for (uint32_t seed = 0; seed < 7000; seed++) {
    ShuffleState st;
    count[static_cast<int>(chooseCard(ALL, st, seed))]++;
  }
  for (uint8_t i = static_cast<uint8_t>(CardId::NowReading); i <= static_cast<uint8_t>(CardId::Pictures); i++) {
    EXPECT_GT(count[i], 800) << cardName(static_cast<CardId>(i));
    EXPECT_LT(count[i], 1200) << cardName(static_cast<CardId>(i));
  }
}

TEST(SleepCardShuffle, UsableSetChangesMidRound) {
  ShuffleState st{CardId::Day, bits({CardId::Day, CardId::Owner})};
  // Owner was unticked: it drops out of the round; Day was last so Quote comes next.
  EXPECT_EQ(chooseCard(bits({CardId::Day, CardId::Quote}), st, 3), CardId::Quote);
  EXPECT_EQ(st.dealt, bits({CardId::Day, CardId::Quote}));
  // The last card is no longer usable at all: any usable card may follow.
  ShuffleState gone{CardId::Owner, 0};
  EXPECT_EQ(chooseCard(bits({CardId::Sky}), gone, 5), CardId::Sky);
}

TEST(SleepCardShuffle, NewRoundNeverOpensWithTheLastCard) {
  for (uint32_t seed = 0; seed < 200; seed++) {
    ShuffleState st{CardId::Quote, bits({CardId::Day, CardId::Quote, CardId::Sky})};
    const CardId pick = chooseCard(bits({CardId::Day, CardId::Quote, CardId::Sky}), st, seed);
    ASSERT_NE(pick, CardId::Quote);
    EXPECT_EQ(st.dealt, cardBit(pick));  // a fresh round holds only the new card
  }
}

// ---- the state line -------------------------------------------------------------------------------------

TEST(SleepCardShuffle, StateRoundTrips) {
  char line[STATE_TEXT_CAP];
  const ShuffleState st{CardId::Pictures, bits({CardId::Day, CardId::Pictures})};
  const size_t n = formatState(st, line, sizeof(line));
  EXPECT_STREQ(line, "S1 7 0084\n");
  EXPECT_EQ(n, std::strlen(line));
  ShuffleState back;
  ASSERT_TRUE(parseState(line, n, back));
  EXPECT_EQ(back.last, CardId::Pictures);
  EXPECT_EQ(back.dealt, st.dealt);
  EXPECT_TRUE(parseState("S1 0 0000", 9, back));
  EXPECT_EQ(back.last, CardId::None);
  EXPECT_TRUE(parseState("S1 2 4\r\n", 8, back));
  EXPECT_EQ(back.last, CardId::Day);
  EXPECT_EQ(back.dealt, cardBit(CardId::Day));
  char tiny[4];
  EXPECT_EQ(formatState(st, tiny, sizeof(tiny)), 0u);
  EXPECT_STREQ(tiny, "");
}

TEST(SleepCardShuffle, StateRejectsGarbage) {
  // Ids past the last card (Weather = 9) and Shuffle itself (8) are never dealt.
  const char* bad[] = {"",          "S2 1 0002", "S1",          "S1 x 0",  "S1 8 0000",  "S1 10 0000", "S1 1 0400",
                       "S1 1 0100", "S1 1 0001", "S1 1 0002 x", "S1 -1 0", "S1 1 10000", "garbage\n",  "S1 256 0002"};
  for (const char* text : bad) {
    ShuffleState st{CardId::Day, 4};
    EXPECT_FALSE(parseState(text, std::strlen(text), st)) << text;
    EXPECT_EQ(st.last, CardId::None) << text;
    EXPECT_EQ(st.dealt, 0) << text;
  }
  const char nul[] = {'S', '1', ' ', '1', '\0', '2'};
  ShuffleState st;
  EXPECT_FALSE(parseState(nul, sizeof(nul), st));
  EXPECT_FALSE(parseState(nullptr, 3, st));
  const std::string longLine(STATE_TEXT_CAP, 'S');
  EXPECT_FALSE(parseState(longLine.data(), longLine.size(), st));
}

// ---- what can show right now ----------------------------------------------------------------------------

TEST(SleepCardShuffle, UsableMaskFollowsTheDevice) {
  MemIo io;
  CardContext ctx = memContext(io);
  EXPECT_EQ(shuffleUsableMask(ctx, ALL), ALL);
  EXPECT_EQ(shuffleUsableMask(ctx, bits({CardId::Day})), bits({CardId::Day}));  // only ticked cards

  CardContext noTime = ctx;
  noTime.timeValid = false;
  EXPECT_EQ(shuffleUsableMask(noTime, ALL), ALL & ~bits({CardId::Day, CardId::Calendar, CardId::Sky}));
  noTime.timeValid = true;
  noTime.utcNow = 0;  // an RTC that was never set
  EXPECT_EQ(shuffleUsableMask(noTime, ALL), ALL & ~bits({CardId::Day, CardId::Calendar, CardId::Sky}));

  CardContext noOwner = ctx;
  std::snprintf(noOwner.settings.ownerName, OWNER_LINE_CAP, "  \t ");
  EXPECT_FALSE(shuffleUsableMask(noOwner, ALL) & cardBit(CardId::Owner));
  std::snprintf(noOwner.settings.ownerContact2, OWNER_LINE_CAP, "reader@example.com");
  EXPECT_TRUE(shuffleUsableMask(noOwner, ALL) & cardBit(CardId::Owner));

  CardContext noBook = ctx;
  noBook.bookPath = "";
  io.files.erase(quote::QUOTES_PATH);
  io.bookmarks = 3;
  // No book: no Now Reading; Quote still has its built-in set.
  EXPECT_EQ(shuffleUsableMask(noBook, ALL), ALL & ~bits({CardId::NowReading}));
  // A book path that is no longer on the card.
  CardContext stale = ctx;
  stale.bookPath = "/Books/Gone.epub";
  EXPECT_FALSE(shuffleUsableMask(stale, ALL) & cardBit(CardId::NowReading));
  // Quote: always, whatever the card holds (the built-in set is its fallback).
  EXPECT_TRUE(shuffleUsableMask(ctx, ALL) & cardBit(CardId::Quote));
  io.bookmarks = 0;
  EXPECT_TRUE(shuffleUsableMask(ctx, ALL) & cardBit(CardId::Quote));
  io.files[quote::QUOTES_PATH] = "";
  EXPECT_TRUE(shuffleUsableMask(ctx, ALL) & cardBit(CardId::Quote));

  EXPECT_TRUE(hasInk("x", 1));
  EXPECT_FALSE(hasInk(" \r\n", 8));
  EXPECT_FALSE(hasInk(nullptr, 8));
  EXPECT_FALSE(hasInk("   x", 3));  // cap respected
}

// ---- pickShuffleCard end to end ---------------------------------------------------------------------------

TEST(SleepCardShuffle, PickRemembersAcrossSleeps) {
  MemIo io;
  CardContext ctx = memContext(io);
  ctx.settings.shuffleMask = ALL;
  CardId prev = CardId::None;
  uint16_t round = 0;
  for (int i = 0; i < 70; i++) {
    ctx.seed = 0x5EED1234u + i * 7919u;
    const CardId pick = pickShuffleCard(ctx);
    ASSERT_NE(pick, CardId::None);
    ASSERT_NE(pick, CardId::Shuffle);
    ASSERT_NE(pick, prev) << "sleep " << i;
    ASSERT_FALSE(round & cardBit(pick)) << "sleep " << i;
    round |= cardBit(pick);
    if (round == ALL) round = 0;
    prev = pick;
    ShuffleState saved;
    const std::string& line = io.files.at(STATE_PATH);
    ASSERT_TRUE(parseState(line.data(), line.size(), saved)) << line;
    EXPECT_EQ(saved.last, pick);
  }
  EXPECT_EQ(io.writes, 70);
}

TEST(SleepCardShuffle, BrokenStateStartsFresh) {
  MemIo io;
  CardContext ctx = memContext(io);
  io.files[STATE_PATH] = std::string("\xFF\xFE garbage", 10);
  const CardId pick = pickShuffleCard(ctx);
  EXPECT_NE(pick, CardId::None);
  const std::string& line = io.files.at(STATE_PATH);
  ShuffleState saved;
  ASSERT_TRUE(parseState(line.data(), line.size(), saved));
  EXPECT_EQ(saved.last, pick);
  EXPECT_EQ(saved.dealt, cardBit(pick));
}

TEST(SleepCardShuffle, NoWriteWhenNothingChanges) {
  MemIo io;
  CardContext ctx = memContext(io);
  ctx.settings.shuffleMask = bits({CardId::Day});
  EXPECT_EQ(pickShuffleCard(ctx), CardId::Day);
  EXPECT_EQ(io.writes, 1);
  EXPECT_EQ(pickShuffleCard(ctx), CardId::Day);
  EXPECT_EQ(io.writes, 1);
}

TEST(SleepCardShuffle, AWriteFailureStillPicks) {
  MemIo io;
  CardContext ctx = memContext(io);
  io.failWrites = true;
  EXPECT_NE(pickShuffleCard(ctx), CardId::None);
  ctx.io = nullptr;  // no storage at all
  EXPECT_NE(pickShuffleCard(ctx), CardId::None);
}

TEST(SleepCardShuffle, NothingUsableFallsBackToTheLogo) {
  MemIo io;
  CardContext ctx = memContext(io);
  ctx.settings.shuffleMask = 0;
  EXPECT_EQ(pickShuffleCard(ctx), CardId::None);
  // Only the Owner card ticked, and no name typed in.
  ctx.settings.shuffleMask = bits({CardId::Owner});
  ctx.settings.ownerName[0] = '\0';
  EXPECT_EQ(pickShuffleCard(ctx), CardId::None);
  // Only Shuffle "ticked" (impossible from Settings): never picks itself.
  ctx.settings.shuffleMask = cardBit(CardId::Shuffle) | cardBit(CardId::None);
  EXPECT_EQ(pickShuffleCard(ctx), CardId::None);
  EXPECT_EQ(io.writes, 0);
  // The two time cards without a clock, plus a usable Owner: Owner.
  ctx.timeValid = false;
  std::snprintf(ctx.settings.ownerName, OWNER_LINE_CAP, "A. Reader");
  ctx.settings.shuffleMask = bits({CardId::Day, CardId::Calendar, CardId::Owner});
  EXPECT_EQ(pickShuffleCard(ctx), CardId::Owner);
}

TEST(SleepCardShuffle, ExcludingSkipsADeclinedCard) {
  MemIo io;
  CardContext ctx = memContext(io);
  ctx.settings.shuffleMask = bits({CardId::Day, CardId::Sky});
  const CardId first = pickShuffleCard(ctx);
  const CardId other = first == CardId::Day ? CardId::Sky : CardId::Day;
  // The caller drew `first`, it declined: the next pick is the other card and is what is saved.
  EXPECT_EQ(pickShuffleCardExcluding(ctx, cardBit(first)), other);
  ShuffleState saved;
  const std::string& line = io.files.at(STATE_PATH);
  ASSERT_TRUE(parseState(line.data(), line.size(), saved));
  EXPECT_EQ(saved.last, other);
  // Everything excluded: None, state kept.
  EXPECT_EQ(pickShuffleCardExcluding(ctx, bits({CardId::Day, CardId::Sky})), CardId::None);
}

// A pick that declines when drawn (here a book that is on the card but cannot be read, which
// looks usable to Now Reading) hands over to the next card instead of the logo screen.
TEST(SleepCardShuffle, RenderMovesOnWhenThePickDeclines) {
  MemIo io;
  CardContext ctx = memContext(io);
  io.bookLoads = false;
  ctx.settings.shuffleMask = bits({CardId::NowReading, CardId::Day});
  for (int i = 0; i < 6; i++) {
    ctx.seed = 0x5EED1234u + static_cast<uint32_t>(i);
    CardId shown = CardId::None;
    EXPECT_TRUE(renderCardOrShuffle(CardId::Shuffle, ctx, preview::renderer(), shown)) << i;
    EXPECT_EQ(shown, CardId::Day) << i;
  }
  // Only the declining card ticked: nothing drawn, the caller falls back to the logo.
  ctx.settings.shuffleMask = bits({CardId::NowReading});
  CardId shown = CardId::None;
  EXPECT_FALSE(renderCardOrShuffle(CardId::Shuffle, ctx, preview::renderer(), shown));
  // A card asked for by name is drawn or declines on its own; no shuffling.
  EXPECT_FALSE(renderCardOrShuffle(CardId::NowReading, ctx, preview::renderer(), shown));
  EXPECT_EQ(shown, CardId::NowReading);
  // The Quote card never declines: an attribution-only file still leaves the built-in set.
  io.files[quote::QUOTES_PATH] = " -- Nobody\n";
  EXPECT_TRUE(renderCardOrShuffle(CardId::Quote, ctx, preview::renderer(), shown));
  // Pictures is handed back undrawn.
  ctx.settings.shuffleMask = bits({CardId::Pictures});
  EXPECT_FALSE(renderCardOrShuffle(CardId::Shuffle, ctx, preview::renderer(), shown));
  EXPECT_EQ(shown, CardId::Pictures);
}

// Live sleep's cycle: the picture frame found no picture, so the deal moves on past Pictures
// (excluded) to the next card instead of showing the logo; Pictures alone has nothing else.
TEST(SleepCardShuffle, CycleSkipsPicturesWithNoPictures) {
  MemIo io;
  CardContext ctx = memContext(io);
  ctx.settings.shuffleMask = bits({CardId::Pictures, CardId::Day});
  for (int i = 0; i < 6; i++) {
    ctx.seed = 0xC0FFEEu + static_cast<uint32_t>(i);
    CardId shown = CardId::None;
    EXPECT_TRUE(renderCardOrShuffle(CardId::Shuffle, ctx, preview::renderer(), shown, cardBit(CardId::Pictures))) << i;
    EXPECT_EQ(shown, CardId::Day) << i;
  }
  // Without the exclusion the deal does come round to Pictures (handed back undrawn).
  bool sawPictures = false;
  for (int i = 0; i < 6 && !sawPictures; i++) {
    ctx.seed = 0xC0FFEEu + static_cast<uint32_t>(i);
    CardId shown = CardId::None;
    if (!renderCardOrShuffle(CardId::Shuffle, ctx, preview::renderer(), shown)) sawPictures = shown == CardId::Pictures;
  }
  EXPECT_TRUE(sawPictures);
  // Pictures the only pick and no picture: nothing drawn (the logo screen).
  ctx.settings.shuffleMask = bits({CardId::Pictures});
  CardId shown = CardId::Day;
  EXPECT_FALSE(renderCardOrShuffle(CardId::Shuffle, ctx, preview::renderer(), shown, cardBit(CardId::Pictures)));
  EXPECT_EQ(shown, CardId::None);
}

// SleepActivity::drawLive's cycle with Pictures ticked and no picture on the card, redraw after
// redraw: the deal that found Pictures empty retries without Pictures and without the card on
// screen, and every later deal leaves Pictures out. No card shows twice in a row, and the deck
// writes its state about once a redraw.
TEST(SleepCardShuffle, LiveCycleWithNoPicturesNeverRepeatsACard) {
  for (const uint16_t picks : {bits({CardId::Day, CardId::Calendar, CardId::Pictures}), ALL}) {
    MemIo io;
    CardContext ctx = memContext(io);
    ctx.settings.shuffleMask = picks;
    bool picturesEmpty = false;
    bool lastWasCard = false;
    CardId lastShown = CardId::None;
    constexpr int REDRAWS = 120;
    for (int redraw = 0; redraw < REDRAWS; redraw++) {
      ctx.seed = 0x5EED0000u + static_cast<uint32_t>(redraw) * 7919u;
      uint16_t base = picturesEmpty ? cardBit(CardId::Pictures) : 0;
      uint16_t excluded = base;
      CardId shown = CardId::None;
      bool drawn = false;
      for (int attempt = 0; attempt < 3; attempt++) {
        drawn = renderCardOrShuffle(CardId::Shuffle, ctx, preview::renderer(), shown, excluded);
        if (drawn) break;
        if (shown != CardId::Pictures) {
          if (excluded == base) break;
          excluded = base;
          continue;
        }
        picturesEmpty = true;
        excluded |= cardBit(CardId::Pictures);
        base |= cardBit(CardId::Pictures);
        if (lastWasCard) excluded |= cardBit(lastShown);
      }
      ASSERT_TRUE(drawn) << redraw;
      if (lastWasCard) EXPECT_NE(shown, lastShown) << "redraw " << redraw << " repeated " << cardName(shown);
      lastShown = shown;
      lastWasCard = true;
    }
    EXPECT_TRUE(picturesEmpty);
    // The Quote card writes its own file; without it every write is the deck's.
    if ((picks & cardBit(CardId::Quote)) == 0) EXPECT_LE(io.writes, REDRAWS + 2);
  }
}

// ---- preview + budget --------------------------------------------------------------------------------------

TEST(SleepCardShuffle, PreviewAndBudget) {
  MemIo io;
  CardContext ctx = memContext(io);
  const auto start = std::chrono::steady_clock::now();
  CardId picks[8];
  for (int i = 0; i < 8; i++) {
    ctx.seed = 0x5EED1234u + static_cast<uint32_t>(i);
    picks[i] = pickShuffleCard(ctx);
  }
  const double pickMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 8.0;
  EXPECT_LT(pickMs, 5.0);
  std::string sequence;
  for (const CardId id : picks) sequence += std::string(cardName(id)) + " ";
  std::printf("shuffle: 8 sleeps (default picks) -> %s(%.3f ms a pick)\n", sequence.c_str(), pickMs);

  // What the device shows for the first of them (a card that declines -> the logo screen).
  ctx.seed = 0x5EED1234u;
  io.files.erase(STATE_PATH);
  const CardId pick = pickShuffleCard(ctx);
  ASSERT_NE(pick, CardId::None);
  bool declined = false;
  const double ms = preview::renderCardPng(pick, ctx, "shuffle", &declined);
  std::printf("shuffle.png = %s (%s) in %.1f ms\n", cardName(pick), declined ? "declined -> logo" : "drawn", ms);
  EXPECT_LT(ms + pickMs, preview::HOST_RUNAWAY_MS);
}
