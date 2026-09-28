// Month calendar card: the grid, the moon's quarters by local date, the hunting-season countdown,
// and the card itself rendered to build/cards/calendar*.png (look at them).
#include <Astro.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "CardPreview.h"
#include "src/sleepcards/CalendarCard.h"

using namespace sleepcards;
using namespace sleepcards::calendar;

namespace {

// US Pacific with its DST rule (the same zone the preview's sample context uses).
int32_t pacific(const int64_t utc) {
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
  return libcUtcOffset(utc);
}
int32_t utcZone(int64_t) { return 0; }

// PyEphem 4.2.1 (ephem.next_*_moon), UTC:
constexpr int64_t LQ_2026_11_01 = 1793564901;    // 2026-11-01 20:28:21 (12:28 PST)
constexpr int64_t NEW_2026_11_09 = 1794207723;   // 2026-11-09 07:02:03 (Nov 8 23:02 PST)
constexpr int64_t FQ_2026_11_17 = 1794916065;    // 2026-11-17 11:47:45
constexpr int64_t FULL_2026_11_24 = 1795532009;  // 2026-11-24 14:53:29
constexpr int64_t LQ_2026_12_01 = 1796105315;    // 2026-12-01 06:08:35 (Nov 30 22:08 PST)
constexpr int64_t FULL_2026_05_01 = 1777656186;  // 2026-05-01 17:23:06
constexpr int64_t FULL_2026_05_31 = 1780217107;  // 2026-05-31 08:45:07 (01:45 PDT) - the month's second

// Counts searches so the bound can be checked.
int gSearches = 0;
int64_t countingFinder(const int64_t from, const int quarter) {
  gSearches++;
  return astroNextMoonPhase(from, quarter);
}

SleepCardSettings between(const int sm, const int sd, const int em, const int ed) {
  SleepCardSettings s;
  s.huntMode = HuntMode::Between;
  s.huntStart = {static_cast<uint8_t>(sm), static_cast<uint8_t>(sd)};
  s.huntEnd = {static_cast<uint8_t>(em), static_cast<uint8_t>(ed)};
  return s;
}

}  // namespace

// ---- grid ---------------------------------------------------------------------------------------

TEST(SleepCardCalendar, GridSundayFirst) {
  MonthGrid g;
  ASSERT_TRUE(monthGrid(2026, 11, 0, g));  // Nov 1 2026 is a Sunday, 30 days
  EXPECT_EQ(g.lead, 0);
  EXPECT_EQ(g.days, 30);
  EXPECT_EQ(g.rows, 5);
  ASSERT_TRUE(monthGrid(2026, 2, 0, g));  // Feb 2026: Sunday the 1st, 28 days - the only 4-row shape
  EXPECT_EQ(g.lead, 0);
  EXPECT_EQ(g.rows, 4);
  ASSERT_TRUE(monthGrid(2026, 8, 0, g));  // Aug 1 2026 is a Saturday: 6 + 31 cells = 6 rows
  EXPECT_EQ(g.lead, 6);
  EXPECT_EQ(g.rows, 6);
  ASSERT_TRUE(monthGrid(2024, 2, 0, g));  // leap February: Thursday the 1st
  EXPECT_EQ(g.lead, 4);
  EXPECT_EQ(g.days, 29);
  EXPECT_EQ(g.rows, 5);
}

TEST(SleepCardCalendar, GridMondayFirst) {
  MonthGrid g;
  ASSERT_TRUE(monthGrid(2026, 11, 1, g));  // Sunday the 1st goes in the last column
  EXPECT_EQ(g.lead, 6);
  EXPECT_EQ(g.rows, 6);
  EXPECT_EQ(weekdayOfColumn(g, 0), 1);
  EXPECT_EQ(weekdayOfColumn(g, 6), 0);
  ASSERT_TRUE(monthGrid(2026, 6, 1, g));  // Jun 1 2026 is a Monday
  EXPECT_EQ(g.lead, 0);
}

TEST(SleepCardCalendar, GridCellsRoundTrip) {
  for (int first = 0; first < 7; first++) {
    for (int month = 1; month <= 12; month++) {
      MonthGrid g;
      ASSERT_TRUE(monthGrid(2027, month, first, g));
      int seen = 0;
      for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
          const int day = dayAtCell(g, row, col);
          if (day == 0) continue;
          seen++;
          int rr = -1, cc = -1;
          ASSERT_TRUE(cellOfDay(g, day, rr, cc));
          EXPECT_EQ(rr, row);
          EXPECT_EQ(cc, col);
          EXPECT_LT(row, g.rows);
          // The column's weekday is the day's weekday.
          EXPECT_EQ(weekdayOfColumn(g, col), weekdayOf(2027, month, day));
        }
      }
      EXPECT_EQ(seen, g.days);
    }
  }
  MonthGrid g;
  ASSERT_TRUE(monthGrid(2026, 11, 0, g));
  int r = 0, c = 0;
  EXPECT_FALSE(cellOfDay(g, 0, r, c));
  EXPECT_FALSE(cellOfDay(g, 31, r, c));
  EXPECT_EQ(dayAtCell(g, -1, 0), 0);
  EXPECT_EQ(dayAtCell(g, 0, 7), 0);
  EXPECT_FALSE(monthGrid(2026, 13, 0, g));
  EXPECT_FALSE(monthGrid(2026, 1, 7, g));
}

// ---- the moon's quarters ------------------------------------------------------------------------

TEST(SleepCardCalendar, QuartersNovember2026Pacific) {
  QuarterMark q[MAX_QUARTERS];
  gSearches = 0;
  const int n = monthMoonQuarters(2026, 11, &pacific, &countingFinder, q, MAX_QUARTERS);
  ASSERT_EQ(n, 5);
  EXPECT_LE(gSearches, 5);
  // Time order. The new moon is Nov 9 in UTC but still Nov 8 on a Pacific calendar, and December's
  // first last quarter (Dec 1 06:08 UTC) is Nov 30 here: a second last quarter in November.
  EXPECT_EQ(q[0].quarter, 3);
  EXPECT_EQ(q[0].day, 1);
  EXPECT_NEAR(static_cast<double>(q[0].utc), LQ_2026_11_01, 300);
  EXPECT_EQ(q[1].quarter, 0);
  EXPECT_EQ(q[1].day, 8);
  EXPECT_NEAR(static_cast<double>(q[1].utc), NEW_2026_11_09, 300);
  EXPECT_EQ(q[2].quarter, 1);
  EXPECT_EQ(q[2].day, 17);
  EXPECT_NEAR(static_cast<double>(q[2].utc), FQ_2026_11_17, 300);
  EXPECT_EQ(q[3].quarter, 2);
  EXPECT_EQ(q[3].day, 24);
  EXPECT_NEAR(static_cast<double>(q[3].utc), FULL_2026_11_24, 300);
  EXPECT_EQ(q[4].quarter, 3);
  EXPECT_EQ(q[4].day, 30);
  EXPECT_NEAR(static_cast<double>(q[4].utc), LQ_2026_12_01, 300);
}

TEST(SleepCardCalendar, QuartersFollowTheLocalDate) {
  QuarterMark q[MAX_QUARTERS];
  const int n = monthMoonQuarters(2026, 11, &utcZone, &astroNextMoonPhase, q, MAX_QUARTERS);
  ASSERT_EQ(n, 4);  // in UTC December's last quarter stays in December
  EXPECT_EQ(q[1].quarter, 0);
  EXPECT_EQ(q[1].day, 9);  // in UTC the new moon is on the 9th
}

TEST(SleepCardCalendar, TwoFullMoonsInOneMonth) {
  // May 2026 has a full moon on the 1st and again on the 31st (Pacific).
  QuarterMark q[MAX_QUARTERS];
  gSearches = 0;
  const int n = monthMoonQuarters(2026, 5, &pacific, &countingFinder, q, MAX_QUARTERS);
  ASSERT_EQ(n, 5);
  EXPECT_LE(gSearches, 5);
  EXPECT_EQ(q[0].quarter, 2);
  EXPECT_EQ(q[0].day, 1);
  EXPECT_NEAR(static_cast<double>(q[0].utc), FULL_2026_05_01, 300);
  EXPECT_EQ(q[4].quarter, 2);
  EXPECT_EQ(q[4].day, 31);
  EXPECT_NEAR(static_cast<double>(q[4].utc), FULL_2026_05_31, 300);
  for (int i = 1; i < n; i++) EXPECT_LT(q[i - 1].utc, q[i].utc);
}

TEST(SleepCardCalendar, QuartersEveryMonthAreSaneAndBounded) {
  for (int year = 2025; year <= 2030; year++) {
    for (int month = 1; month <= 12; month++) {
      QuarterMark q[MAX_QUARTERS];
      gSearches = 0;
      const int n = monthMoonQuarters(year, month, &pacific, &countingFinder, q, MAX_QUARTERS);
      EXPECT_LE(gSearches, 5) << year << "-" << month;
      EXPECT_GE(n, month == 2 ? 3 : 4) << year << "-" << month;
      EXPECT_LE(n, 5) << year << "-" << month;
      const int dim = daysInMonth(year, month);
      for (int i = 0; i < n; i++) {
        EXPECT_GE(q[i].day, 1);
        EXPECT_LE(q[i].day, dim);
        if (i > 0) {
          EXPECT_LT(q[i - 1].utc, q[i].utc);
          EXPECT_NE(q[i - 1].quarter, q[i].quarter);
        }
      }
    }
  }
}

TEST(SleepCardCalendar, QuartersRejectBadInput) {
  QuarterMark q[MAX_QUARTERS];
  EXPECT_EQ(monthMoonQuarters(2026, 0, &pacific, &astroNextMoonPhase, q, MAX_QUARTERS), 0);
  EXPECT_EQ(monthMoonQuarters(2026, 11, nullptr, &astroNextMoonPhase, q, MAX_QUARTERS), 0);
  EXPECT_EQ(monthMoonQuarters(2026, 11, &pacific, nullptr, q, MAX_QUARTERS), 0);
  EXPECT_EQ(monthMoonQuarters(2026, 11, &pacific, &astroNextMoonPhase, q, 2), 2);  // capped
  // A finder that fails (returns 0) yields nothing and does not loop.
  EXPECT_EQ(monthMoonQuarters(2026, 11, &pacific, [](int64_t, int) -> int64_t { return 0; }, q, MAX_QUARTERS), 0);
}

// ---- the hunting season -------------------------------------------------------------------------

TEST(SleepCardCalendar, HuntMarksOnlyForBetween) {
  SleepCardSettings s = between(10, 1, 10, 31);
  EXPECT_TRUE(huntDayMarked(s, 2026, 10, 1));
  EXPECT_TRUE(huntDayMarked(s, 2026, 10, 31));
  EXPECT_FALSE(huntDayMarked(s, 2026, 11, 1));
  s.huntMode = HuntMode::On;
  EXPECT_FALSE(huntDayMarked(s, 2026, 10, 15));
  s.huntMode = HuntMode::Off;
  EXPECT_FALSE(huntDayMarked(s, 2026, 10, 15));
}

TEST(SleepCardCalendar, CountdownBeforeAndInsideTheSeason) {
  const SleepCardSettings s = between(10, 1, 10, 31);
  SeasonCountdown c = seasonCountdown(s, 2026, 9, 28);
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Opens);
  EXPECT_EQ(c.days, 3);
  EXPECT_EQ(c.month, 10);
  EXPECT_EQ(c.day, 1);
  c = seasonCountdown(s, 2026, 9, 30);
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Opens);
  EXPECT_EQ(c.days, 1);
  c = seasonCountdown(s, 2026, 10, 1);
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Ends);
  EXPECT_EQ(c.days, 30);
  EXPECT_EQ(c.day, 31);
  c = seasonCountdown(s, 2026, 10, 31);  // the last day itself
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Ends);
  EXPECT_EQ(c.days, 0);
  c = seasonCountdown(s, 2026, 11, 1);  // the day after: next year's opening
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Opens);
  EXPECT_EQ(c.year, 2027);
  EXPECT_EQ(c.days, 334);
}

TEST(SleepCardCalendar, CountdownAcrossNewYear) {
  const SleepCardSettings s = between(12, 20, 1, 10);
  SeasonCountdown c = seasonCountdown(s, 2026, 12, 30);
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Ends);
  EXPECT_EQ(c.year, 2027);
  EXPECT_EQ(c.month, 1);
  EXPECT_EQ(c.day, 10);
  EXPECT_EQ(c.days, 11);
  c = seasonCountdown(s, 2027, 1, 5);
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Ends);
  EXPECT_EQ(c.days, 5);
  c = seasonCountdown(s, 2027, 1, 11);
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Opens);
  EXPECT_EQ(c.year, 2027);
  EXPECT_EQ(c.month, 12);
  EXPECT_EQ(c.day, 20);
  EXPECT_EQ(c.days, 343);
}

TEST(SleepCardCalendar, CountdownLeapDayBound) {
  // Feb 29 as the last day: in a common year the season ends on Feb 28.
  const SleepCardSettings s = between(2, 1, 2, 29);
  SeasonCountdown c = seasonCountdown(s, 2027, 2, 10);
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Ends);
  EXPECT_EQ(c.day, 28);
  c = seasonCountdown(s, 2028, 2, 10);
  EXPECT_EQ(c.kind, SeasonCountdown::Kind::Ends);
  EXPECT_EQ(c.day, 29);
}

TEST(SleepCardCalendar, CountdownNoneWhenNothingToCount) {
  SleepCardSettings s = between(10, 1, 10, 31);
  s.huntMode = HuntMode::Off;
  EXPECT_EQ(seasonCountdown(s, 2026, 9, 1).kind, SeasonCountdown::Kind::None);
  s.huntMode = HuntMode::On;
  EXPECT_EQ(seasonCountdown(s, 2026, 9, 1).kind, SeasonCountdown::Kind::None);
  // A season covering the whole year never opens or ends.
  const SleepCardSettings all = between(1, 1, 12, 31);
  EXPECT_EQ(seasonCountdown(all, 2026, 6, 1).kind, SeasonCountdown::Kind::None);
  EXPECT_EQ(seasonCountdown(between(10, 1, 10, 31), 2026, 2, 30).kind, SeasonCountdown::Kind::None);
}

// ---- the card -----------------------------------------------------------------------------------

namespace {
constexpr double BUDGET_MS = preview::HOST_RUNAWAY_MS;

CardContext contextAt(const int64_t utc) {
  CardContext ctx = preview::sampleContext();
  ctx.utcNow = utc;
  ctx.localNow = localDateOf(utc, ctx.utcOffsetAt);
  ctx.utcOffsetS = ctx.utcOffsetAt(utc);
  return ctx;
}
}  // namespace

TEST(SleepCardCalendar, RendersSample) {
  bool declined = true;
  const double ms = preview::renderCardPng(CardId::Calendar, preview::sampleContext(), "calendar", &declined);
  std::printf("  calendar drawn in %.2f ms\n", ms);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, BUDGET_MS);
}

TEST(SleepCardCalendar, RendersHuntingSeason) {
  // Season about to open (Nov 14-22 seen from Nov 2) and inside it (seen from Nov 18).
  CardContext ctx = preview::sampleContext();
  ctx.settings = between(11, 14, 11, 22);
  bool declined = true;
  double ms = preview::renderCardPng(CardId::Calendar, ctx, "calendar_hunt_soon", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, BUDGET_MS);

  ctx = contextAt(1795500000);  // 2026-11-23 22:00 PST, the eve of the full moon
  ctx.settings = between(11, 14, 11, 30);
  ms = preview::renderCardPng(CardId::Calendar, ctx, "calendar_hunt_in", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, BUDGET_MS);

  // A season running over New Year, seen in December.
  ctx = contextAt(1798000000);  // 2026-12-23 ~04:26 PST
  ctx.settings = between(12, 20, 1, 10);
  ms = preview::renderCardPng(CardId::Calendar, ctx, "calendar_hunt_newyear", &declined);
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, BUDGET_MS);
}

TEST(SleepCardCalendar, RendersSixAndFourRowMonths) {
  bool declined = true;
  double ms = preview::renderCardPng(CardId::Calendar, contextAt(1786500000), "calendar_6rows", &declined);  // Aug
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, BUDGET_MS);
  ms = preview::renderCardPng(CardId::Calendar, contextAt(1771000000), "calendar_4rows", &declined);  // Feb 2026
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, BUDGET_MS);
  ms = preview::renderCardPng(CardId::Calendar, contextAt(1779000000), "calendar_5quarters", &declined);  // May 2026
  EXPECT_FALSE(declined);
  EXPECT_LT(ms, BUDGET_MS);
}

TEST(SleepCardCalendar, RendersWithoutLocationAndDeclinesWithoutTime) {
  bool declined = true;
  preview::renderCardPng(CardId::Calendar, preview::sampleContextNoLocation(), "calendar_nolocation", &declined);
  EXPECT_FALSE(declined);  // the calendar and the moon's phases need no place

  CardContext ctx = preview::sampleContext();
  ctx.timeValid = false;
  preview::renderCardPng(CardId::Calendar, ctx, "calendar_notime", &declined);
  EXPECT_TRUE(declined);

  ctx = preview::sampleContext();
  ctx.utcNow = 946684800;  // 2000-01-01: an RTC that was never set
  preview::renderCardPng(CardId::Calendar, ctx, "calendar_badclock", &declined);
  EXPECT_TRUE(declined);
}
