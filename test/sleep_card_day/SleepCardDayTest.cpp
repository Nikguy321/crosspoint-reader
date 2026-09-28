// Day card: the facts it shows (computeDayFacts, pure) against PyEphem 4.2.1 reference times, the
// legal-light window and when it appears, the moon block, and the rendering of every variant into
// build/cards/day*.png within the sleep path's budget.
//
// Reference places are public city coordinates only. PyEphem times quoted below were computed with
// the Almanac's conventions (upper limb on a 34' horizon, no pressure) - the same as
// scripts/gen_almanac_vectors.py.
#include <Astro.h>
#include <HalDisplay.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>

#include "CardPreview.h"
#include "src/sleepcards/CardTime.h"
#include "src/sleepcards/DayCard.h"

using namespace sleepcards;
using daycard::DayFacts;
using daycard::LegalDay;
using daycard::SunKind;
using Kind = almanac::LegalWindow::Kind;

namespace {

constexpr const char* TZ_PACIFIC = "PST8PDT,M3.2.0,M11.1.0";
constexpr const char* TZ_ALASKA = "AKST9AKDT,M3.2.0,M11.1.0";
constexpr const char* TZ_HOBART = "AEST-10AEDT,M10.1.0,M4.1.0/3";

// Seattle's public city-centre coordinates (as in the preview).
constexpr double SEA_LAT = 47.61, SEA_LON = -122.33;

// A context at a local time in a TZ, at a public place (located = false: no location set).
CardContext contextAt(const char* tz, const int y, const int mo, const int d, const int h, const int mi,
                      const bool located = true, const double lat = SEA_LAT, const double lon = SEA_LON) {
  CardContext ctx = preview::sampleContext();  // sets TZ to Pacific; override below
  setenv("TZ", tz, 1);
  tzset();
  ctx.utcOffsetAt = &libcUtcOffset;
  ctx.utcNow = localDayStart(y, mo, d, ctx.utcOffsetAt) + h * 3600 + mi * 60;
  // localDayStart + hours counts real seconds: on a DST-change day a time after 02:00 is an hour
  // off the wall clock. The tests only use that for "some time that day".
  ctx.utcOffsetS = libcUtcOffset(ctx.utcNow);
  ctx.localNow = localDateOf(ctx.utcNow, ctx.utcOffsetAt);
  ctx.location = CardLocation{};
  ctx.settings.location[0] = '\0';
  if (located) {
    formatLocation(lat, lon, ctx.settings.location, sizeof(ctx.settings.location));
    ctx.location.valid = parseLocation(ctx.settings.location, ctx.location.lat, ctx.location.lon);
  }
  return ctx;
}

// Local wall minutes since midnight of an instant (DST-aware).
int wallMinutes(const CardContext& ctx, const int64_t utc) {
  const LocalDate d = localDateOf(utc, ctx.utcOffsetAt);
  return d.hour * 60 + d.minute;
}
int64_t wallSeconds(const CardContext& ctx, const int64_t utc) {
  const LocalDate d = localDateOf(utc, ctx.utcOffsetAt);
  return d.hour * 3600 + d.minute * 60 + d.second;
}
constexpr int64_t hms(const int h, const int m, const int s) { return h * 3600 + m * 60 + s; }

DayFacts factsOf(const CardContext& ctx) {
  DayFacts f;
  EXPECT_TRUE(daycard::computeDayFacts(ctx, f));
  return f;
}

}  // namespace

// ---- the sun --------------------------------------------------------------------------------------

TEST(SleepCardDay, SeattleSampleDay) {
  const CardContext ctx = preview::sampleContext();  // 2026-11-02 20:40 PST
  const DayFacts f = factsOf(ctx);
  EXPECT_EQ(f.date.year, 2026);
  EXPECT_EQ(f.date.month, 11);
  EXPECT_EQ(f.date.day, 2);
  EXPECT_EQ(f.date.weekday, 1);  // Monday
  ASSERT_EQ(f.sunKind, SunKind::Normal);
  // PyEphem: sunrise 06:55:13, sunset 16:49:53 PST -> nearest minute 06:55 / 16:50.
  EXPECT_EQ(wallMinutes(ctx, f.sunrise), 6 * 60 + 55);
  EXPECT_EQ(wallMinutes(ctx, f.sunset), 16 * 60 + 50);
  EXPECT_EQ(f.sunrise % 60, 0);
  EXPECT_EQ(f.sunset % 60, 0);
  // 9:54:40 of daylight.
  EXPECT_NEAR(static_cast<double>(f.dayLengthS), static_cast<double>(hms(9, 54, 40)), 60.0);
  char span[24];
  daycard::formatDayLength(f.dayLengthS, span, sizeof(span));
  EXPECT_STREQ(span, "9h 55m");
  EXPECT_FALSE(f.moonSouthern);
  EXPECT_EQ(f.legalDay, LegalDay::None);  // Hunting Season Off by default
}

// 2026-11-01 is the day US clocks fall back (a 25-hour day): the times must be PST ones.
TEST(SleepCardDay, FallBackDayUsesTheNewOffset) {
  const CardContext ctx = contextAt(TZ_PACIFIC, 2026, 11, 1, 20, 0);
  const DayFacts f = factsOf(ctx);
  ASSERT_EQ(f.sunKind, SunKind::Normal);
  // PyEphem: 06:53:42 / 16:51:25 PST.
  EXPECT_NEAR(wallSeconds(ctx, f.sunrise), hms(6, 54, 0), 60);
  EXPECT_NEAR(wallSeconds(ctx, f.sunset), hms(16, 51, 0), 60);
  EXPECT_EQ(f.dayStart, localDayStart(2026, 11, 1, ctx.utcOffsetAt));
}

// 2026-03-08, spring forward (a 23-hour day): PDT times.
TEST(SleepCardDay, SpringForwardDayUsesTheNewOffset) {
  const CardContext ctx = contextAt(TZ_PACIFIC, 2026, 3, 8, 12, 0);
  const DayFacts f = factsOf(ctx);
  ASSERT_EQ(f.sunKind, SunKind::Normal);
  // PyEphem: 07:35:51 / 19:04:53 PDT.
  EXPECT_NEAR(wallSeconds(ctx, f.sunrise), hms(7, 36, 0), 60);
  EXPECT_NEAR(wallSeconds(ctx, f.sunset), hms(19, 5, 0), 60);
  // 11:29:02 of daylight in real seconds (not wall-clock difference, which would be an hour more).
  EXPECT_NEAR(static_cast<double>(f.dayLengthS), static_cast<double>(hms(11, 29, 2)), 60.0);
}

TEST(SleepCardDay, SouthernHemisphere) {
  const CardContext ctx = contextAt(TZ_HOBART, 2026, 11, 2, 21, 0, true, -42.88, 147.33);
  const DayFacts f = factsOf(ctx);
  ASSERT_EQ(f.sunKind, SunKind::Normal);
  // PyEphem: 05:53:21 / 19:55:54 AEDT.
  EXPECT_NEAR(wallSeconds(ctx, f.sunrise), hms(5, 53, 0), 60);
  EXPECT_NEAR(wallSeconds(ctx, f.sunset), hms(19, 56, 0), 60);
  EXPECT_TRUE(f.moonSouthern);
}

TEST(SleepCardDay, PolarNightAndMidnightSun) {
  // Utqiagvik, Alaska (public town coordinates).
  const CardContext winter = contextAt(TZ_ALASKA, 2026, 12, 21, 12, 0, true, 71.29, -156.79);
  const DayFacts w = factsOf(winter);
  EXPECT_EQ(w.sunKind, SunKind::DownAllDay);
  EXPECT_EQ(w.sunrise, 0);
  EXPECT_EQ(w.sunset, 0);
  EXPECT_EQ(w.dayLengthS, -1);

  const CardContext summer = contextAt(TZ_ALASKA, 2026, 6, 21, 12, 0, true, 71.29, -156.79);
  EXPECT_EQ(factsOf(summer).sunKind, SunKind::UpAllDay);
}

TEST(SleepCardDay, NoLocationKeepsTheMoon) {
  CardContext ctx = preview::sampleContextNoLocation();
  ctx.settings.huntMode = HuntMode::On;  // no place, no legal light either
  const DayFacts f = factsOf(ctx);
  EXPECT_EQ(f.sunKind, SunKind::NoLocation);
  EXPECT_EQ(f.sunrise, 0);
  EXPECT_EQ(f.legalDay, LegalDay::None);
  EXPECT_GT(f.nextFullMoon, 0);
  EXPECT_GT(f.moonIllum, 0.3);
}

TEST(SleepCardDay, NoTrustworthyTimeDeclines) {
  CardContext ctx = preview::sampleContext();
  ctx.timeValid = false;
  DayFacts f;
  EXPECT_FALSE(daycard::computeDayFacts(ctx, f));
  ctx.timeValid = true;
  ctx.utcNow = 946684800;  // 2000-01-01: an RTC that was never set
  EXPECT_FALSE(daycard::computeDayFacts(ctx, f));
  ctx.utcNow = 0;
  EXPECT_FALSE(renderCard(CardId::Day, ctx, preview::renderer()));
}

TEST(SleepCardDay, FormatDayLength) {
  char out[24];
  daycard::formatDayLength(0, out, sizeof(out));
  EXPECT_STREQ(out, "0h 00m");
  daycard::formatDayLength(-50, out, sizeof(out));
  EXPECT_STREQ(out, "0h 00m");
  daycard::formatDayLength(hms(9, 54, 29), out, sizeof(out));
  EXPECT_STREQ(out, "9h 54m");
  daycard::formatDayLength(hms(9, 54, 30), out, sizeof(out));  // half a minute rounds up
  EXPECT_STREQ(out, "9h 55m");
  daycard::formatDayLength(hms(23, 59, 45), out, sizeof(out));
  EXPECT_STREQ(out, "24h 00m");
}

// ---- the moon ---------------------------------------------------------------------------------------

TEST(SleepCardDay, MoonOnTheSampleEvening) {
  const CardContext ctx = preview::sampleContext();
  const DayFacts f = factsOf(ctx);
  // PyEphem at 2026-11-03 04:40 UTC: 35.4 % lit, waning (full moon was 2026-10-26).
  EXPECT_NEAR(f.moonIllum, 0.354, 0.005);
  EXPECT_FALSE(f.moonWaxing);
  EXPECT_EQ(f.moonPhase, 7);  // waning crescent (elongation 282..348 degrees)
  // PyEphem: next full moon 2026-11-24 14:53:29 UTC = 06:53 PST, a Tuesday, 22 days on.
  EXPECT_NEAR(static_cast<double>(f.nextFullMoon), static_cast<double>(ctx.utcNow + 0), 23.0 * 86400);
  EXPECT_EQ(f.nextFullDate.year, 2026);
  EXPECT_EQ(f.nextFullDate.month, 11);
  EXPECT_EQ(f.nextFullDate.day, 24);
  EXPECT_EQ(f.nextFullDate.weekday, 2);
  EXPECT_EQ(f.daysToFullMoon, 22);
  EXPECT_NEAR(wallSeconds(ctx, f.nextFullMoon), hms(6, 53, 29), 300);
}

TEST(SleepCardDay, FullMoonTodayAndTomorrow) {
  // The full moon is 2026-11-24 06:53 PST.
  const DayFacts sameDay = factsOf(contextAt(TZ_PACIFIC, 2026, 11, 24, 1, 0));
  EXPECT_EQ(sameDay.daysToFullMoon, 0);
  EXPECT_EQ(sameDay.moonPhase, 4);
  const DayFacts eve = factsOf(contextAt(TZ_PACIFIC, 2026, 11, 23, 22, 0));
  EXPECT_EQ(eve.daysToFullMoon, 1);
  EXPECT_TRUE(eve.moonWaxing);
  // Just after it, the next one is a lunation away.
  const DayFacts after = factsOf(contextAt(TZ_PACIFIC, 2026, 11, 24, 9, 0));
  EXPECT_GE(after.daysToFullMoon, 28);
  EXPECT_LE(after.daysToFullMoon, 31);
}

// ---- legal light ------------------------------------------------------------------------------------

namespace {

// The raw sun day for a context's local date (or the day after).
AstroSunDay rawSun(const CardContext& ctx, const int dayOffset) {
  const LocalDate d = localDateOf(ctx.utcNow, ctx.utcOffsetAt);
  int64_t t0 = localDayStart(d.year, d.month, d.day, ctx.utcOffsetAt);
  if (dayOffset == 1) t0 = localNextDayStart(d.year, d.month, d.day, ctx.utcOffsetAt);
  AstroSunDay s{};
  astroSunDay(t0, ctx.location.lat, ctx.location.lon, &s);
  return s;
}

}  // namespace

TEST(SleepCardDay, LegalLightTodayRoundsInward) {
  CardContext ctx = contextAt(TZ_PACIFIC, 2026, 11, 2, 5, 0);  // before dawn
  ctx.settings.huntMode = HuntMode::On;
  const DayFacts f = factsOf(ctx);
  ASSERT_EQ(f.legalDay, LegalDay::Today);
  ASSERT_EQ(f.legal.kind, Kind::Window);
  const AstroSunDay s = rawSun(ctx, 0);
  // First legal minute rounded UP, last rounded DOWN: never wider than the true window.
  EXPECT_EQ(f.legal.first, almanac::roundUpToMinute(s.rise - 1800 + almanac::MODEL_MARGIN_S));
  EXPECT_EQ(f.legal.last, almanac::roundDownToMinute(s.set + 1800 - almanac::MODEL_MARGIN_S));
  EXPECT_GE(f.legal.first, s.rise - 1800);
  EXPECT_LE(f.legal.last, s.set + 1800);
  EXPECT_LT(f.legal.first - (s.rise - 1800), 60);
  // PyEphem: 06:25:13 .. 17:19:53 PST.
  EXPECT_NEAR(wallSeconds(ctx, f.legal.first), hms(6, 26, 0), 60);
  EXPECT_NEAR(wallSeconds(ctx, f.legal.last), hms(17, 19, 0), 60);
}

TEST(SleepCardDay, LegalLightCivilTwilight) {
  CardContext ctx = contextAt(TZ_PACIFIC, 2026, 11, 2, 12, 0);
  ctx.settings.huntMode = HuntMode::On;
  ctx.settings.legalRule = LegalLightRule::CivilTwilight;
  const DayFacts f = factsOf(ctx);
  ASSERT_EQ(f.legalDay, LegalDay::Today);
  EXPECT_EQ(f.legalRule, almanac::LegalRule::CivilTwilight);
  const AstroSunDay s = rawSun(ctx, 0);
  EXPECT_EQ(f.legal.first, almanac::roundUpToMinute(s.dawn + almanac::MODEL_MARGIN_S));
  EXPECT_EQ(f.legal.last, almanac::roundDownToMinute(s.dusk - almanac::MODEL_MARGIN_S));
  // PyEphem civil dawn/dusk: 06:22:52 / 17:22:13 PST.
  EXPECT_NEAR(wallSeconds(ctx, f.legal.first), hms(6, 23, 0), 60);
  EXPECT_NEAR(wallSeconds(ctx, f.legal.last), hms(17, 22, 0), 60);
}

TEST(SleepCardDay, LegalLightTomorrowOnceTodaysHasEnded) {
  CardContext ctx = preview::sampleContext();  // 20:40, after today's window
  ctx.settings.huntMode = HuntMode::On;
  const DayFacts f = factsOf(ctx);
  ASSERT_EQ(f.legalDay, LegalDay::Tomorrow);
  const AstroSunDay s = rawSun(ctx, 1);
  EXPECT_EQ(f.legal.first, almanac::roundUpToMinute(s.rise - 1800 + almanac::MODEL_MARGIN_S));
  EXPECT_EQ(f.legal.last, almanac::roundDownToMinute(s.set + 1800 - almanac::MODEL_MARGIN_S));
  // PyEphem 2026-11-03: sunrise 06:56:44, sunset 16:48:23 -> 06:27 .. 17:18.
  EXPECT_EQ(localDateOf(f.legal.first, ctx.utcOffsetAt).day, 3);
  EXPECT_NEAR(wallSeconds(ctx, f.legal.first), hms(6, 27, 0), 60);
  EXPECT_NEAR(wallSeconds(ctx, f.legal.last), hms(17, 18, 0), 60);
  // Still today's during the afternoon.
  CardContext afternoon = contextAt(TZ_PACIFIC, 2026, 11, 2, 16, 0);
  afternoon.settings.huntMode = HuntMode::On;
  EXPECT_EQ(factsOf(afternoon).legalDay, LegalDay::Today);
}

TEST(SleepCardDay, LegalLightFollowsTheSeasonDates) {
  CardContext ctx = contextAt(TZ_PACIFIC, 2026, 11, 2, 12, 0);
  ctx.settings.huntMode = HuntMode::Off;
  EXPECT_EQ(factsOf(ctx).legalDay, LegalDay::None);

  ctx.settings.huntMode = HuntMode::Between;
  ctx.settings.huntStart = {10, 1};
  ctx.settings.huntEnd = {10, 31};
  EXPECT_EQ(factsOf(ctx).legalDay, LegalDay::None);  // the season is over

  ctx.settings.huntStart = {11, 1};
  ctx.settings.huntEnd = {11, 2};
  EXPECT_EQ(factsOf(ctx).legalDay, LegalDay::Today);  // last day, window not over

  // The evening of the last day: tomorrow is out of season, so nothing.
  CardContext evening = contextAt(TZ_PACIFIC, 2026, 11, 2, 21, 0);
  evening.settings = ctx.settings;
  EXPECT_EQ(factsOf(evening).legalDay, LegalDay::None);

  // The evening before opening day: tomorrow's window.
  evening.settings.huntStart = {11, 3};
  evening.settings.huntEnd = {11, 9};
  EXPECT_EQ(factsOf(evening).legalDay, LegalDay::Tomorrow);
  // ... but not that morning.
  ctx.settings = evening.settings;
  EXPECT_EQ(factsOf(ctx).legalDay, LegalDay::None);
}

TEST(SleepCardDay, LegalLightAcrossNewYear) {
  CardContext ctx = contextAt(TZ_PACIFIC, 2026, 12, 31, 21, 0);
  ctx.settings.huntMode = HuntMode::Between;
  ctx.settings.huntStart = {12, 20};
  ctx.settings.huntEnd = {1, 10};
  const DayFacts f = factsOf(ctx);
  ASSERT_EQ(f.legalDay, LegalDay::Tomorrow);
  const LocalDate first = localDateOf(f.legal.first, ctx.utcOffsetAt);
  EXPECT_EQ(first.year, 2027);
  EXPECT_EQ(first.month, 1);
  EXPECT_EQ(first.day, 1);
  // PyEphem 2027-01-01: sunrise 07:57:32 -> first legal 07:28.
  EXPECT_NEAR(wallSeconds(ctx, f.legal.first), hms(7, 28, 0), 60);

  // Jan 10 evening: the season ends, nothing for Jan 11.
  CardContext last = contextAt(TZ_PACIFIC, 2027, 1, 10, 21, 0);
  last.settings = ctx.settings;
  EXPECT_EQ(factsOf(last).legalDay, LegalDay::None);
}

TEST(SleepCardDay, LegalLightInPolarNight) {
  CardContext ctx = contextAt(TZ_ALASKA, 2026, 12, 21, 12, 0, true, 71.29, -156.79);
  ctx.settings.huntMode = HuntMode::On;
  DayFacts f = factsOf(ctx);
  ASSERT_EQ(f.legalDay, LegalDay::Today);
  EXPECT_EQ(f.legal.kind, Kind::None);  // no sunrise: the 30-minute rule gives no light
  // The sun's centre climbs to about -4.7 degrees at noon: a short civil-twilight window around it.
  ctx.settings.legalRule = LegalLightRule::CivilTwilight;
  f = factsOf(ctx);
  ASSERT_EQ(f.legalDay, LegalDay::Today);
  ASSERT_EQ(f.legal.kind, Kind::Window);
  EXPECT_LT(f.legal.first, f.legal.last);
  EXPECT_LT(f.legal.last - f.legal.first, 8 * 3600);
  EXPECT_GT(f.legal.last - f.legal.first, 2 * 3600);
  // Midsummer there: the sun never sets, so civil light never ends.
  CardContext june = contextAt(TZ_ALASKA, 2026, 6, 21, 12, 0, true, 71.29, -156.79);
  june.settings.huntMode = HuntMode::On;
  june.settings.legalRule = LegalLightRule::CivilTwilight;
  f = factsOf(june);
  ASSERT_EQ(f.legalDay, LegalDay::Today);
  EXPECT_EQ(f.legal.kind, Kind::AllDay);
}

// ---- rendering ----------------------------------------------------------------------------------

namespace {

bool anyInk() {
  const uint8_t* fb = display.getFrameBuffer();
  for (uint32_t i = 0; i < display.getBufferSize(); i++) {
    if (fb[i] != 0xFF) return true;
  }
  return false;
}

void renderVariant(const CardContext& ctx, const char* name) {
  bool declined = true;
  const double ms = preview::renderCardPng(CardId::Day, ctx, name, &declined);
  std::printf("  %-26s %.2f ms -> build/cards/%s.png\n", name, ms, name);
  EXPECT_FALSE(declined) << name;
  EXPECT_TRUE(anyInk()) << name;
  EXPECT_LT(ms, preview::HOST_RUNAWAY_MS) << name;
}

}  // namespace

TEST(SleepCardDay, RendersEveryVariant) {
  renderVariant(preview::sampleContext(), "day");
  renderVariant(preview::sampleContextNoLocation(), "day_nolocation");

  CardContext hunting = preview::sampleContext();
  hunting.settings.huntMode = HuntMode::On;
  renderVariant(hunting, "day_hunting");  // evening: tomorrow's window

  CardContext morning = contextAt(TZ_PACIFIC, 2026, 11, 14, 5, 10);
  morning.settings.huntMode = HuntMode::On;
  morning.clock12h = true;
  renderVariant(morning, "day_hunting_12h");

  CardContext civil = contextAt(TZ_PACIFIC, 2026, 10, 28, 12, 0);
  civil.settings.huntMode = HuntMode::On;
  civil.settings.legalRule = LegalLightRule::CivilTwilight;
  renderVariant(civil, "day_hunting_civil");

  renderVariant(contextAt(TZ_PACIFIC, 2026, 11, 23, 22, 0), "day_fullmoon_tomorrow");
  renderVariant(contextAt(TZ_HOBART, 2026, 11, 18, 21, 0, true, -42.88, 147.33), "day_southern");

  CardContext polar = contextAt(TZ_ALASKA, 2026, 12, 21, 12, 0, true, 71.29, -156.79);
  polar.settings.huntMode = HuntMode::On;
  renderVariant(polar, "day_polar_night");
  renderVariant(contextAt(TZ_ALASKA, 2026, 6, 21, 12, 0, true, 71.29, -156.79), "day_midnight_sun");
}

// The whole card - both sun days (a hunting evening), the phase and the full-moon search - on the
// host. The device is ~3 orders slower (Astro.h: ~26.5 us a libm call at 160 MHz; the worst case
// here is ~2 x 1,850 + 215 + ~800 calls, ~120 ms): a runaway guard, not the budget.
TEST(SleepCardDay, ComputeCostOnTheHost) {
  CardContext ctx = preview::sampleContext();
  ctx.settings.huntMode = HuntMode::On;
  constexpr int RUNS = 50;
  const auto start = std::chrono::steady_clock::now();
  DayFacts f;
  for (int i = 0; i < RUNS; i++) daycard::computeDayFacts(ctx, f);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / RUNS;
  std::printf("  computeDayFacts (hunting evening): %.3f ms on the host\n", ms);
  EXPECT_LT(ms, 2.0);
}
