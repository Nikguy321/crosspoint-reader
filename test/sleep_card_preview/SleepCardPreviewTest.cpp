// Renders every sleep card on the host into <repo>/build/cards/<name>.png so a reviewer can LOOK
// at them, and holds each card to the sleep path's compute budget. A card that declines is
// written as the logo screen the reader would fall back to.
//
//   cmake --build build/test --target SleepCardPreviewTest && ./build/test/sleep_card_preview/SleepCardPreviewTest
//   open build/cards/*.png
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "CardPreview.h"
#include "src/fontIds.h"
#include "src/sleepcards/BrandScreen.h"
#include "src/sleepcards/CardDraw.h"
#include "src/sleepcards/CardText.h"

using namespace sleepcards;

namespace {
constexpr double BUDGET_MS = preview::HOST_RUNAWAY_MS;

bool anyInk() {
  const uint8_t* fb = display.getFrameBuffer();
  for (uint32_t i = 0; i < display.getBufferSize(); i++) {
    if (fb[i] != 0xFF) return true;
  }
  return false;
}
}  // namespace

TEST(SleepCardPreview, LogoScreen) {
  GfxRenderer& r = preview::renderer();
  drawX4ProLogoScreen(r, tr(STR_SLEEPING));
  EXPECT_TRUE(anyInk());
  ASSERT_TRUE(preview::writeFramePng(preview::outputDir() + "/logo.png"));
  r.invertScreen();
  ASSERT_TRUE(preview::writeFramePng(preview::outputDir() + "/logo_dark.png"));
}

TEST(SleepCardPreview, EveryCardRendersWithinBudget) {
  const CardId cards[] = {CardId::NowReading, CardId::Day, CardId::Calendar, CardId::Quote, CardId::Owner, CardId::Sky};
  for (const CardId id : cards) {
    bool declined = false;
    CardContext ctx = preview::sampleContext();
    if (id == CardId::Owner) {
      // Fictional (555-01xx and example.com are reserved for examples); nothing is preset on the device.
      std::snprintf(ctx.settings.ownerName, sizeof(ctx.settings.ownerName), "Sam Example");
      std::snprintf(ctx.settings.ownerContact1, sizeof(ctx.settings.ownerContact1), "555-0100");
      std::snprintf(ctx.settings.ownerContact2, sizeof(ctx.settings.ownerContact2), "sam@example.com");
    }
    const double ms = preview::renderCardPng(id, ctx, cardName(id), &declined);
    std::printf("  %-12s %s in %.2f ms -> build/cards/%s.png\n", cardName(id), declined ? "declined" : "drawn", ms,
                cardName(id));
    EXPECT_LT(ms, BUDGET_MS) << cardName(id);
    EXPECT_TRUE(anyInk()) << cardName(id);
    EXPECT_FALSE(declined) << cardName(id);
  }
}

// Dark Cards: every card white on black -> build/cards/dark_<name>.png. The page turns
// black; the moon and the book cover keep their tones.
TEST(SleepCardPreview, DarkCards) {
  const CardId cards[] = {CardId::NowReading, CardId::Day, CardId::Calendar, CardId::Quote, CardId::Owner, CardId::Sky};
  const uint32_t size = display.getBufferSize();
  for (const CardId id : cards) {
    CardContext ctx = preview::sampleContext();
    if (id == CardId::Owner) {
      std::snprintf(ctx.settings.ownerName, sizeof(ctx.settings.ownerName), "Sam Example");
      std::snprintf(ctx.settings.ownerContact1, sizeof(ctx.settings.ownerContact1), "555-0100");
    }
    GfxRenderer& r = preview::renderer();
    ASSERT_TRUE(renderCard(id, ctx, r)) << cardName(id);
    const std::vector<uint8_t> light(display.getFrameBuffer(), display.getFrameBuffer() + size);
    ctx.dark = true;
    bool declined = false;
    preview::renderCardPng(id, ctx, std::string("dark_") + cardName(id), &declined);
    ASSERT_FALSE(declined) << cardName(id);
    // Every pixel flipped but the kept pictures: the same card, the other way round.
    const uint8_t* fb = display.getFrameBuffer();
    uint32_t same = 0;
    uint32_t inkBits = 0;
    for (uint32_t i = 0; i < size; i++) {
      same += __builtin_popcount(static_cast<uint8_t>(~(fb[i] ^ light[i])));
      inkBits += __builtin_popcount(static_cast<uint8_t>(~fb[i]));
    }
    EXPECT_GT(inkBits, size * 8 / 2) << cardName(id) << ": the page should be black";
    if (id == CardId::Quote || id == CardId::Owner) EXPECT_EQ(same, 0u) << cardName(id) << " has no pictures";
    if (id == CardId::NowReading || id == CardId::Day || id == CardId::Calendar || id == CardId::Sky) {
      EXPECT_GT(same, 0u) << cardName(id) << ": its cover or moon should keep its tones";
    }
  }
}

// On a dark card the moon still reads as the moon: its lit part white, a new moon dark inside a
// white ring (never the white disc of a full moon), and the page around it black.
TEST(SleepCardPreview, DarkMoonKeepsItsTones) {
  GfxRenderer& r = preview::renderer();
  const int radius = 40;
  const int cx = 120;
  const int cy = 200;
  const int nx = 360;  // a new moon beside it
  r.clearScreen();
  draw::clearKeptTones();
  draw::drawMoon(r, cx, cy, radius, 0.5, true, false, draw::DITHER_LEVELS);  // first quarter: right half lit
  draw::drawMoon(r, nx, cy, radius, 0.0, true, false, draw::DITHER_LEVELS);
  draw::keepTonesRect(200, 400, 40, 20);
  r.fillRect(200, 400, 20, 20, true);  // a picture: left half ink
  draw::invertKeepingTones(r);
  ASSERT_TRUE(preview::writeFramePng(preview::outputDir() + "/_dark_moon.png"));

  EXPECT_TRUE(r.readPixel(10, 10)) << "page";
  EXPECT_TRUE(r.readPixel(cx, cy - radius - 3)) << "page just outside the ring";
  EXPECT_FALSE(r.readPixel(cx + radius / 2, cy)) << "lit side";
  EXPECT_TRUE(r.readPixel(cx - radius / 2, cy)) << "shadow side";
  EXPECT_FALSE(r.readPixel(cx + radius, cy)) << "ring";
  EXPECT_FALSE(r.readPixel(cx - radius, cy)) << "ring";
  EXPECT_TRUE(r.readPixel(nx, cy)) << "new moon: dark";
  EXPECT_TRUE(r.readPixel(nx + radius / 2, cy)) << "new moon: dark";
  EXPECT_FALSE(r.readPixel(nx - radius, cy)) << "new moon: white ring";
  EXPECT_TRUE(r.readPixel(205, 405)) << "picture ink kept";
  EXPECT_FALSE(r.readPixel(230, 405)) << "picture paper kept";
  EXPECT_TRUE(r.readPixel(245, 405)) << "page beside the picture";
}

// Without a location, sun and moon times cannot be computed; cards must still render (or decline)
// quickly and never crash.
TEST(SleepCardPreview, CardsWithoutLocation) {
  for (const CardId id : {CardId::Day, CardId::Calendar, CardId::Sky}) {
    const std::string name = std::string(cardName(id)) + "_nolocation";
    const double ms = preview::renderCardPng(id, preview::sampleContextNoLocation(), name);
    EXPECT_LT(ms, BUDGET_MS) << name;
  }
}

// Without a trustworthy clock every card that shows a date must decline.
TEST(SleepCardPreview, CardsWithoutTime) {
  for (const CardId id : {CardId::Day, CardId::Calendar, CardId::Sky}) {
    CardContext ctx = preview::sampleContext();
    ctx.timeValid = false;
    ctx.utcNow = 0;
    bool declined = false;
    preview::renderCardPng(id, ctx, std::string(cardName(id)) + "_notime", &declined);
    EXPECT_TRUE(declined) << cardName(id);
  }
}

// No location and no open book: every card (and Shuffle) draws what it can or declines to the
// logo screen -> build/cards/fallback_<card>.png.
TEST(SleepCardPreview, FallbacksWithoutLocationOrBook) {
  CardContext ctx = preview::sampleContextNoLocation();
  ctx.bookPath = "";
  ctx.fromReader = false;
  preview::hostIo().hasBook = false;
  const CardId cards[] = {CardId::NowReading, CardId::Day, CardId::Calendar, CardId::Quote,
                          CardId::Owner,      CardId::Sky, CardId::Shuffle};
  for (const CardId id : cards) {
    GfxRenderer& r = preview::renderer();
    const auto start = std::chrono::steady_clock::now();
    CardId shown = CardId::None;
    const bool drawn = renderCardOrShuffle(id, ctx, r, shown);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!drawn) drawX4ProLogoScreen(r, tr(STR_SLEEPING));
    const std::string name = std::string("fallback_") + cardName(id);
    ASSERT_TRUE(preview::writeFramePng(preview::outputDir() + "/" + name + ".png"));
    std::printf("  %-22s %s %s in %.2f ms\n", name.c_str(), cardName(shown), drawn ? "drawn" : "declined -> logo", ms);
    EXPECT_LT(ms, BUDGET_MS) << name;
    EXPECT_TRUE(anyInk()) << name;
  }
  // Now Reading has nothing true to show without a book.
  CardId shown = CardId::None;
  EXPECT_FALSE(renderCardOrShuffle(CardId::NowReading, ctx, preview::renderer(), shown));
  preview::hostIo().hasBook = true;
}

// A sheet of the shared drawing helpers (src/sleepcards/CardDraw.h), for card authors.
TEST(SleepCardPreview, HelpersSheet) {
  GfxRenderer& r = preview::renderer();
  const CardContext ctx = preview::sampleContext();
  r.clearScreen();
  r.drawText(UI_12_FONT_ID, SCREEN_MARGIN, 16, "CardDraw.h helpers", true, EpdFontFamily::BOLD);

  // Big date from the largest built-in font, magnified.
  char day[4];
  std::snprintf(day, sizeof(day), "%d", ctx.localNow.day);
  draw::drawCenteredTextScaled(r, NOTOSANS_18_FONT_ID, 120, 48, day, 3.0f, true, EpdFontFamily::BOLD);
  draw::drawTextCenteredAt(r, UI_12_FONT_ID, 120, 190, monthName(ctx.localNow.month), true, EpdFontFamily::BOLD);
  draw::drawTextCenteredAt(r, UI_10_FONT_ID, 120, 214, weekdayName(ctx.localNow.weekday));

  // Circles, rings, ellipses.
  draw::fillCircle(r, 300, 90, 30);
  draw::drawCircle(r, 390, 90, 30, 3);
  draw::drawEllipse(r, 345, 175, 70, 26, 2);
  draw::fillEllipse(r, 345, 175, 30, 12);

  // Dither ramp 0..16.
  for (int level = 0; level <= 16; level++) {
    draw::fillRectDithered(r, SCREEN_MARGIN + level * 25, 250, 24, 36, static_cast<uint8_t>(level));
  }
  r.drawRect(SCREEN_MARGIN, 250, 17 * 25, 36, true);

  // The moon through a lunation (northern hemisphere): new, crescent, quarter, gibbous, full, waning.
  const double illum[8] = {0.0, 0.2, 0.5, 0.8, 1.0, 0.8, 0.5, 0.2};
  for (int i = 0; i < 8; i++) {
    draw::drawMoon(r, SCREEN_MARGIN + 26 + i * 54, 340, 24, illum[i], i < 4);
  }
  draw::drawMoon(r, 120, 450, 60, 0.35, true);
  draw::fillCircleDithered(r, 330, 450, 50, 6);
  draw::drawDashedHLine(r, SCREEN_MARGIN, r.getScreenWidth() - SCREEN_MARGIN, 530);
  draw::drawDashedVLine(r, 240, 540, 600);

  draw::drawProgressBar(r, SCREEN_MARGIN, 620, r.getScreenWidth() - 2 * SCREEN_MARGIN, 18, 0.28f);
  draw::drawWrapped(r, NOTOSERIF_14_FONT_ID, SCREEN_MARGIN, 650, r.getScreenWidth() - 2 * SCREEN_MARGIN,
                    "Call me Ishmael. Some years ago - never mind how long precisely - having little or no money in "
                    "my purse, and nothing particular to interest me on shore.",
                    2, draw::Align::Left, EpdFontFamily::ITALIC);
  draw::drawSleepFooter(ctx, r);
  ASSERT_TRUE(preview::writeFramePng(preview::outputDir() + "/_helpers.png"));
}
