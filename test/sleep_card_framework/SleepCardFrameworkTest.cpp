// The pure pieces the sleep cards share: dates and DST-aware day boundaries, clock strings,
// location parsing, the hunting-season calendar (across New Year too), the moon's lit shape,
// and the registry.
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "CardPreview.h"
#include "src/fontIds.h"
#include "src/sleepcards/CardDraw.h"
#include "src/sleepcards/CardTime.h"
#include "src/sleepcards/FooterText.h"
#include "src/sleepcards/SleepCard.h"
#include "src/sleepcards/SleepCardSettings.h"

using namespace sleepcards;

namespace {
void usePacific() {
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
}
}  // namespace

// ---- CardTime ----------------------------------------------------------------------------------

TEST(CardTime, CivilDayNumbersRoundTrip) {
  EXPECT_EQ(daysFromCivil(1970, 1, 1), 0);
  EXPECT_EQ(daysFromCivil(2026, 11, 2), 20759);  // 1793577600 / 86400
  int y = 0, m = 0, d = 0;
  civilFromDays(daysFromCivil(2024, 2, 29), y, m, d);
  EXPECT_EQ(y * 10000 + m * 100 + d, 20240229);
  civilFromDays(-1, y, m, d);
  EXPECT_EQ(y * 10000 + m * 100 + d, 19691231);
  EXPECT_EQ(weekdayOf(1970, 1, 1), 4);   // Thursday
  EXPECT_EQ(weekdayOf(2026, 11, 2), 1);  // Monday
  EXPECT_EQ(weekdayOf(2027, 1, 1), 5);   // Friday
}

TEST(CardTime, PacificOffsetsAndDstDayBoundaries) {
  usePacific();
  // 2026: DST starts Sunday March 8 at 02:00 PST, ends Sunday November 1 at 02:00 PDT.
  const int64_t marchStart = localDayStart(2026, 3, 8, &libcUtcOffset);
  const int64_t marchNext = localNextDayStart(2026, 3, 8, &libcUtcOffset);
  EXPECT_EQ(marchStart, daysFromCivil(2026, 3, 8) * 86400 + 8 * 3600);  // midnight PST
  EXPECT_EQ(marchNext - marchStart, 23 * 3600);                         // the short day
  const int64_t novStart = localDayStart(2026, 11, 1, &libcUtcOffset);
  const int64_t novNext = localNextDayStart(2026, 11, 1, &libcUtcOffset);
  EXPECT_EQ(novStart, daysFromCivil(2026, 11, 1) * 86400 + 7 * 3600);  // midnight PDT
  EXPECT_EQ(novNext - novStart, 25 * 3600);                            // the long day
  EXPECT_EQ(libcUtcOffset(novNext), -8 * 3600);
  // Every day of a year: [start, next) holds its own date at both ends.
  for (int64_t day = daysFromCivil(2026, 1, 1); day < daysFromCivil(2027, 1, 1); day++) {
    int y = 0, m = 0, d = 0;
    civilFromDays(day, y, m, d);
    const int64_t start = localDayStart(y, m, d, &libcUtcOffset);
    const int64_t next = localNextDayStart(y, m, d, &libcUtcOffset);
    const LocalDate first = localDateOf(start, &libcUtcOffset);
    const LocalDate last = localDateOf(next - 1, &libcUtcOffset);
    ASSERT_EQ(first.day, d) << y << "-" << m << "-" << d;
    ASSERT_EQ(first.hour, 0);
    ASSERT_EQ(first.minute, 0);
    ASSERT_EQ(last.day, d);
    ASSERT_EQ(last.hour * 60 + last.minute, 23 * 60 + 59);
  }
}

TEST(CardTime, LocalDateAndClockStrings) {
  usePacific();
  const int64_t t = localDayStart(2026, 11, 2, &libcUtcOffset) + 20 * 3600 + 40 * 60;
  const LocalDate d = localDateOf(t, &libcUtcOffset);
  EXPECT_EQ(d.year, 2026);
  EXPECT_EQ(d.month, 11);
  EXPECT_EQ(d.day, 2);
  EXPECT_EQ(d.weekday, 1);
  EXPECT_EQ(d.yearDay, 306);
  EXPECT_EQ(d.hour, 20);
  EXPECT_EQ(d.minute, 40);
  char buf[16];
  formatClock(t, &libcUtcOffset, false, buf, sizeof(buf));
  EXPECT_STREQ(buf, "20:40");
  formatClock(t, &libcUtcOffset, true, buf, sizeof(buf));
  EXPECT_STREQ(buf, "8:40 PM");
  formatHourMinute(0, 5, true, buf, sizeof(buf));
  EXPECT_STREQ(buf, "12:05 AM");
  formatHourMinute(12, 0, true, buf, sizeof(buf));
  EXPECT_STREQ(buf, "12:00 PM");
  formatClock(0, &libcUtcOffset, false, buf, sizeof(buf));
  EXPECT_STREQ(buf, "--:--");
}

TEST(CardTime, PlausibleTime) {
  EXPECT_FALSE(plausibleTime(0));
  EXPECT_FALSE(plausibleTime(946684800));  // 2000-01-01: an RTC that was never set
  EXPECT_TRUE(plausibleTime(1790000000));  // 2026
  EXPECT_FALSE(plausibleTime(4102444800));
}

// ---- SleepCardSettings -----------------------------------------------------------------------------

TEST(SleepCardSettings, ParsesLocations) {
  double lat = 0, lon = 0;
  ASSERT_TRUE(parseLocation("47.61, -122.33", lat, lon));
  EXPECT_DOUBLE_EQ(lat, 47.61);
  EXPECT_DOUBLE_EQ(lon, -122.33);
  ASSERT_TRUE(parseLocation("  47.61 -122.33 ", lat, lon));
  EXPECT_DOUBLE_EQ(lon, -122.33);
  ASSERT_TRUE(parseLocation("47.61N 122.33W", lat, lon));
  EXPECT_DOUBLE_EQ(lon, -122.33);
  ASSERT_TRUE(parseLocation("33.87 S, 151.21 E", lat, lon));
  EXPECT_DOUBLE_EQ(lat, -33.87);
  EXPECT_DOUBLE_EQ(lon, 151.21);
  ASSERT_TRUE(parseLocation("51.4779,-0.0015", lat, lon));
  ASSERT_TRUE(parseLocation("0,0", lat, lon));
  ASSERT_TRUE(parseLocation("90, 180", lat, lon));

  for (const char* bad : {"", "   ", "47.61", "91, 0", "0, 181", "-47.61S, 0", "47.61-122.33", "47.61, -122.33x",
                          "nan, 0", "1e2, 0", "0x10, 0", "47.61, , -122.33", "N 47, W 122", "47.61 E, 122.33"}) {
    EXPECT_FALSE(parseLocation(bad, lat, lon)) << "'" << bad << "'";
  }
}

TEST(SleepCardSettings, NormalizesLocations) {
  char out[LOCATION_CAP];
  ASSERT_TRUE(normalizeLocation("47.61 N, 122.33 W", out, sizeof(out)));
  EXPECT_STREQ(out, "47.6100,-122.3300");
  ASSERT_TRUE(normalizeLocation("   ", out, sizeof(out)));
  EXPECT_STREQ(out, "");  // unset
  EXPECT_FALSE(normalizeLocation("somewhere", out, sizeof(out)));
}

TEST(SleepCardSettings, MonthDays) {
  MonthDay md;
  ASSERT_TRUE(parseMonthDay("10-01", md));
  EXPECT_EQ(md.month, 10);
  EXPECT_EQ(md.day, 1);
  ASSERT_TRUE(parseMonthDay("2/29", md));
  EXPECT_EQ(md.day, 29);
  ASSERT_TRUE(parseMonthDay(" 12 31 ", md));
  for (const char* bad : {"", "13-01", "0-10", "4-31", "2-30", "10", "10-1-1", "001-01", "a-b"}) {
    EXPECT_FALSE(parseMonthDay(bad, md)) << bad;
  }
  EXPECT_EQ(daysInMonth(2027, 2), 28);
  EXPECT_EQ(daysInMonth(2028, 2), 29);
  EXPECT_EQ(daysInMonth(2100, 2), 28);
}

TEST(SleepCardSettings, HuntingSeasonCalendar) {
  SleepCardSettings s;
  EXPECT_FALSE(huntingSeasonOn(s, 2026, 10, 15));  // Off by default
  s.huntMode = HuntMode::On;
  EXPECT_TRUE(huntingSeasonOn(s, 2026, 3, 1));
  s.huntMode = HuntMode::Between;
  s.huntStart = {10, 14};
  s.huntEnd = {10, 20};
  EXPECT_FALSE(huntingSeasonOn(s, 2026, 10, 13));
  EXPECT_TRUE(huntingSeasonOn(s, 2026, 10, 14));
  EXPECT_TRUE(huntingSeasonOn(s, 2026, 10, 20));
  EXPECT_FALSE(huntingSeasonOn(s, 2026, 10, 21));
  // Across New Year.
  s.huntStart = {12, 20};
  s.huntEnd = {1, 10};
  EXPECT_TRUE(huntingSeasonOn(s, 2026, 12, 31));
  EXPECT_TRUE(huntingSeasonOn(s, 2027, 1, 1));
  EXPECT_TRUE(huntingSeasonOn(s, 2027, 1, 10));
  EXPECT_FALSE(huntingSeasonOn(s, 2027, 1, 11));
  EXPECT_FALSE(huntingSeasonOn(s, 2026, 12, 19));
  // A Feb 29 end in a common year ends on Feb 28.
  s.huntStart = {2, 1};
  s.huntEnd = {2, 29};
  EXPECT_TRUE(huntingSeasonOn(s, 2027, 2, 28));
  EXPECT_FALSE(huntingSeasonOn(s, 2027, 3, 1));
}

TEST(SleepCardSettings, ShuffleDefaults) {
  const uint16_t m = defaultShuffleMask();
  EXPECT_TRUE(shuffleIncludes(m, CardId::NowReading));
  EXPECT_TRUE(shuffleIncludes(m, CardId::Day));
  EXPECT_TRUE(shuffleIncludes(m, CardId::Calendar));
  EXPECT_TRUE(shuffleIncludes(m, CardId::Quote));
  EXPECT_TRUE(shuffleIncludes(m, CardId::Sky));
  EXPECT_FALSE(shuffleIncludes(m, CardId::Owner));
  EXPECT_FALSE(shuffleIncludes(m, CardId::Pictures));
  EXPECT_FALSE(shuffleIncludes(m, CardId::Shuffle));
}

// Nothing personal ships as a default.
TEST(SleepCardSettings, DefaultsCarryNothingPersonal) {
  const SleepCardSettings s;
  EXPECT_STREQ(s.location, "");
  EXPECT_STREQ(s.ownerName, "");
  EXPECT_STREQ(s.ownerContact1, "");
  EXPECT_STREQ(s.ownerContact2, "");
  EXPECT_EQ(s.huntMode, HuntMode::Off);
}

// ---- the moon's shape ----------------------------------------------------------------------------

TEST(MoonShape, PhasesLightTheRightSide) {
  const int r = 40;
  int l = 0, rr = 0;
  // New: nothing lit on any row.
  for (int dy = -r; dy <= r; dy++) {
    draw::moonLitSpan(0.0, true, false, r, dy, l, rr);
    EXPECT_GT(l, rr);
  }
  // Full: the whole row.
  draw::moonLitSpan(1.0, true, false, r, 0, l, rr);
  EXPECT_EQ(l, -r);
  EXPECT_EQ(rr, r);
  // First quarter (northern): the right half.
  draw::moonLitSpan(0.5, true, false, r, 0, l, rr);
  EXPECT_EQ(l, 0);
  EXPECT_EQ(rr, r);
  // Last quarter: the left half; the southern hemisphere mirrors it.
  draw::moonLitSpan(0.5, false, false, r, 0, l, rr);
  EXPECT_EQ(l, -r);
  EXPECT_EQ(rr, 0);
  draw::moonLitSpan(0.5, false, true, r, 0, l, rr);
  EXPECT_EQ(l, 0);
  EXPECT_EQ(rr, r);
  // A waxing crescent is a sliver on the right, widest at the equator.
  draw::moonLitSpan(0.1, true, false, r, 0, l, rr);
  EXPECT_EQ(rr, r);
  EXPECT_GT(l, r / 2);
  // The lit area grows with the illuminated fraction.
  int prevArea = -1;
  for (double f = 0.0; f <= 1.0001; f += 0.05) {
    int area = 0;
    for (int dy = -r; dy <= r; dy++) {
      draw::moonLitSpan(f, true, false, r, dy, l, rr);
      if (rr >= l) area += rr - l + 1;
    }
    EXPECT_GE(area, prevArea) << f;
    prevArea = area;
  }
}

// ---- registry ------------------------------------------------------------------------------------

TEST(SleepCardRegistry, ModesNamesAndIds) {
  EXPECT_EQ(cardForSleepMode(0), CardId::None);  // Dark
  EXPECT_EQ(cardForSleepMode(7), CardId::None);  // Transparent
  EXPECT_EQ(cardForSleepMode(SLEEP_MODE_NOW_READING), CardId::NowReading);
  EXPECT_EQ(cardForSleepMode(SLEEP_MODE_SKY), CardId::Sky);
  EXPECT_EQ(cardForSleepMode(SLEEP_MODE_SHUFFLE), CardId::Shuffle);
  EXPECT_EQ(cardForSleepMode(15), CardId::None);
  for (const char* name : {"now_reading", "day", "calendar", "quote", "owner", "sky", "pictures", "shuffle"}) {
    const CardId id = cardByName(name);
    ASSERT_NE(id, CardId::None) << name;
    EXPECT_STREQ(cardName(id), name);
  }
  EXPECT_EQ(cardByName("bogus"), CardId::None);
  EXPECT_EQ(cardByName(nullptr), CardId::None);
  // Only real cards render; Pictures and Shuffle are resolved by the caller.
  EXPECT_EQ(cardInfo(CardId::Pictures)->render, nullptr);
  EXPECT_EQ(cardInfo(CardId::Shuffle)->render, nullptr);
  EXPECT_NE(cardInfo(CardId::Day)->render, nullptr);
}

// ---- footer ----------------------------------------------------------------------------------------

namespace {
LocalDate wallTime(const int month, const int day, const int weekday, const int hour, const int minute) {
  LocalDate d;
  d.year = 2026;
  d.month = month;
  d.day = day;
  d.weekday = weekday;
  d.hour = hour;
  d.minute = minute;
  return d;
}

std::string updated(const LocalDate& d, const bool clock12h, const footer::Form form) {
  char buf[96];
  footer::formatUpdated(d, clock12h, form, buf, sizeof(buf));
  return buf;
}

int uiWidth(void* user, const char* text) { return static_cast<GfxRenderer*>(user)->getTextWidth(UI_10_FONT_ID, text); }

// Logical (portrait) pixel of the framebuffer, as writeFramePng reads it.
bool inkAt(const int x, const int y) {
  const uint8_t* fb = display.getFrameBuffer();
  const int px = y;
  const int py = HalDisplay::DISPLAY_HEIGHT - 1 - x;
  return ((fb[py * HalDisplay::DISPLAY_WIDTH_BYTES + px / 8] >> (7 - (px & 7))) & 1) == 0;
}
}  // namespace

TEST(SleepCardFooter, EveryFormIn12And24Hours) {
  const LocalDate oct1 = wallTime(10, 1, 3, 14, 17);  // Wed Oct 1, 14:17
  EXPECT_EQ(updated(oct1, true, footer::Form::Full), "Screen updated 2:17 PM, Wed Oct 1");
  EXPECT_EQ(updated(oct1, true, footer::Form::NoWeekday), "Screen updated 2:17 PM, Oct 1");
  EXPECT_EQ(updated(oct1, true, footer::Form::Short), "Updated 2:17 PM, Oct 1");
  EXPECT_EQ(updated(oct1, true, footer::Form::TimeOnly), "Updated 2:17 PM");
  EXPECT_EQ(updated(oct1, false, footer::Form::Full), "Screen updated 14:17, Wed Oct 1");
  EXPECT_EQ(updated(oct1, false, footer::Form::TimeOnly), "Updated 14:17");
  const LocalDate late = wallTime(5, 30, 3, 0, 59);
  EXPECT_EQ(updated(late, true, footer::Form::Full), "Screen updated 12:59 AM, Wed May 30");
  EXPECT_EQ(updated(late, false, footer::Form::Full), "Screen updated 00:59, Wed May 30");
}

TEST(SleepCardFooter, PlaceholdersMayBeReordered) {
  const footer::Field fields[] = {{"time", "14:17"}, {"date", "1 Oct"}};
  char buf[64];
  EXPECT_EQ(footer::expand("{date}, {time} aktualisiert", fields, 2, buf, sizeof(buf)), 25u);
  EXPECT_STREQ(buf, "1 Oct, 14:17 aktualisiert");
  // An unknown or unclosed placeholder stays as written; a short buffer is cut, terminated.
  footer::expand("{when} {time} {", fields, 2, buf, sizeof(buf));
  EXPECT_STREQ(buf, "{when} 14:17 {");
  char small[6];
  EXPECT_EQ(footer::expand("at {time}", fields, 2, small, sizeof(small)), 5u);
  EXPECT_STREQ(small, "at 14");
}

TEST(SleepCardFooter, FallsBackLongestFirst) {
  const LocalDate oct1 = wallTime(10, 1, 3, 14, 17);
  // A pretend font: 10 px a byte.
  const auto width = [](void*, const char* text) { return static_cast<int>(std::strlen(text)) * 10; };
  char buf[96];
  EXPECT_EQ(footer::fitUpdated(oct1, true, 1000, width, nullptr, buf, sizeof(buf)), footer::Form::Full);
  EXPECT_EQ(footer::fitUpdated(oct1, true, 330, width, nullptr, buf, sizeof(buf)), footer::Form::Full);
  EXPECT_EQ(footer::fitUpdated(oct1, true, 329, width, nullptr, buf, sizeof(buf)), footer::Form::NoWeekday);
  EXPECT_STREQ(buf, "Screen updated 2:17 PM, Oct 1");
  EXPECT_EQ(footer::fitUpdated(oct1, true, 250, width, nullptr, buf, sizeof(buf)), footer::Form::Short);
  EXPECT_EQ(footer::fitUpdated(oct1, true, 200, width, nullptr, buf, sizeof(buf)), footer::Form::TimeOnly);
  EXPECT_EQ(footer::fitUpdated(oct1, true, 10, width, nullptr, buf, sizeof(buf)), footer::Form::TimeOnly);
  EXPECT_STREQ(buf, "Updated 2:17 PM");
}

// The real footer with the real font: the widest times and dates, at 100 % (the widest battery),
// stay between the margins, and still name the day whenever the full line fits.
TEST(SleepCardFooter, WidestCaseStaysBetweenTheMargins) {
  GfxRenderer& r = preview::renderer();
  const int w = r.getScreenWidth();
  const int h = r.getScreenHeight();
  for (const bool clock12h : {true, false}) {
    for (const bool charging : {false, true}) {
      CardContext ctx = preview::sampleContext();
      ctx.localNow = wallTime(5, 30, 3, 12, 59);  // "12:59 PM, Wed May 30"
      ctx.clock12h = clock12h;
      ctx.batteryPercent = 100;
      ctx.charging = charging;
      r.clearScreen();
      draw::drawSleepFooter(ctx, r);
      for (int y = h - FOOTER_HEIGHT + 2; y < h; y++) {
        for (int x = 0; x < SCREEN_MARGIN; x++) {
          ASSERT_FALSE(inkAt(x, y)) << "left margin at " << x << "," << y << " 12h=" << clock12h;
          ASSERT_FALSE(inkAt(w - 1 - x, y)) << "right margin at " << w - 1 - x << "," << y << " 12h=" << clock12h;
        }
      }
      char buf[96];
      const int room = w - 2 * SCREEN_MARGIN - (20 + 5 + r.getTextWidth(UI_10_FONT_ID, "100%")) - 16;
      const footer::Form form = footer::fitUpdated(ctx.localNow, clock12h, room, &uiWidth, &r, buf, sizeof(buf));
      EXPECT_LT(static_cast<int>(form), static_cast<int>(footer::Form::TimeOnly)) << buf;
      std::printf("footer %s: \"%s\" (form %d)\n", clock12h ? "12h" : "24h", buf, static_cast<int>(form));
    }
  }
  // The usual case shows everything.
  CardContext ctx = preview::sampleContext();
  ctx.localNow = wallTime(10, 1, 3, 14, 17);
  ctx.clock12h = true;
  ctx.batteryPercent = 73;
  char buf[96];
  const int room = w - 2 * SCREEN_MARGIN - (20 + 5 + r.getTextWidth(UI_10_FONT_ID, "73%")) - 16;
  EXPECT_EQ(footer::fitUpdated(ctx.localNow, true, room, &uiWidth, &r, buf, sizeof(buf)), footer::Form::Full)
      << buf << " in " << room << " px: " << uiWidth(&r, "Screen updated 2:17 PM, Wed Oct 1");
  ctx.charging = true;
  r.clearScreen();
  draw::drawSleepFooter(ctx, r);
  ASSERT_TRUE(preview::writeFramePng(preview::outputDir() + "/_footer.png"));
}

TEST(SleepCardFooter, ChargingBoltIsWhiteInsideTheFill) {
  GfxRenderer& r = preview::renderer();
  for (const int percent : {0, 5, 50, 100}) {
    r.clearScreen();
    draw::drawBatteryIcon(r, 100, 100, 20, 10, percent, true);
    // The fill reaches past the bolt, and the bolt's middle rows are white inside it.
    EXPECT_TRUE(inkAt(102, 105)) << percent;
    int white = 0;
    for (int x = 103; x < 110; x++) {
      for (int y = 102; y < 108; y++) white += inkAt(x, y) ? 0 : 1;
    }
    EXPECT_GT(white, 6) << percent;
    // Both tips sit inside the fill's rows (y + 2 .. y + 7), never on the white gap: the top
    // tip's right pixel and the bottom tip's left pixel are white with ink beside them.
    EXPECT_FALSE(inkAt(108, 102)) << percent;
    EXPECT_TRUE(inkAt(109, 102)) << percent;
    EXPECT_FALSE(inkAt(104, 107)) << percent;
    EXPECT_TRUE(inkAt(103, 107)) << percent;
    // Not charging: an empty battery has no fill at all.
    r.clearScreen();
    draw::drawBatteryIcon(r, 100, 100, 20, 10, percent, false);
    if (percent == 0) EXPECT_FALSE(inkAt(104, 105));
  }
}
