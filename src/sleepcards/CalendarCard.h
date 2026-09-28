#pragma once

#include <cstdint>

#include "SleepCard.h"

// MONTH CALENDAR: the month's grid, today circled, a moon glyph on the days of the four quarters,
// hunting-season days marked, and a line or two under the grid (the next full moon; the days until
// the hunting season opens or ends).
// Entry point: sleepcards::renderCalendarCard() (declared in SleepCard.h). The pure pieces below are
// host-tested by test/sleep_card_calendar.
namespace sleepcards::calendar {

// ---- the grid -----------------------------------------------------------------------------------
// A month laid out in week rows of 7 columns; column 0 is `firstWeekday` (0 = Sunday, 1 = Monday).
struct MonthGrid {
  int year = 1970;
  int month = 1;
  int firstWeekday = 0;
  int lead = 0;  // blank cells before day 1 in the first row (0..6)
  int days = 31;
  int rows = 5;  // week rows the month needs (4..6)
};

// The layout of year-month. False for a month outside 1..12 or a weekday outside 0..6.
bool monthGrid(int year, int month, int firstWeekday, MonthGrid& out);
// Where day 1..days sits; false outside the month.
bool cellOfDay(const MonthGrid& g, int day, int& row, int& col);
// The day in a cell, 0 for a blank cell (before day 1, after the last day, or outside the grid).
int dayAtCell(const MonthGrid& g, int row, int col);
// The weekday (0 = Sunday) shown in column col.
int weekdayOfColumn(const MonthGrid& g, int col);

// ---- the moon's quarters ------------------------------------------------------------------------
// quarter: 0 new, 1 first quarter, 2 full, 3 last quarter (astroNextMoonPhase's numbering).
struct QuarterMark {
  int64_t utc = 0;  // the instant, UNIX s
  int day = 0;      // its local day of the month
  uint8_t quarter = 0;
};
constexpr int MAX_QUARTERS = 6;  // a month holds 4 or 5 (two of one kind at most)

// First instant of `quarter` strictly after `from` (astroNextMoonPhase on the device; tests may fake it).
using PhaseFinder = int64_t (*)(int64_t from, int quarter);

// The quarters whose LOCAL date falls in year-month, in time order. Returns how many (<= max).
// Bounded: one search per kind, a second one only for a kind whose first instant leaves room
// for another before the month ends (at most 5 searches in all).
int monthMoonQuarters(int year, int month, UtcOffsetFn offsetAt, PhaseFinder find, QuarterMark* out, int max);

// ---- the hunting season -------------------------------------------------------------------------
// Days of the season get marked only for HuntMode::Between (On has no dates to show).
bool huntDayMarked(const SleepCardSettings& settings, int year, int month, int day);

struct SeasonCountdown {
  enum class Kind : uint8_t { None, Opens, Ends };
  Kind kind = Kind::None;
  int days = 0;  // from the given date: Opens = to the opening day (>= 1); Ends = to the LAST day (>= 0)
  int year = 0;  // the opening day / the last day
  int month = 0;
  int day = 0;
};
// For HuntMode::Between: inside the season, when its last day is; outside it, when it opens.
// None for Off / On, a season covering every day, or a date outside the calendar. Handles a
// season running over New Year and a Feb 29 bound in a common year (SleepCardSettings.h).
SeasonCountdown seasonCountdown(const SleepCardSettings& settings, int year, int month, int day);

}  // namespace sleepcards::calendar
