#pragma once

#include <cstddef>
#include <cstdint>

// Calendar and clock arithmetic for the sleep cards: local dates, DST-aware day
// boundaries, clock strings. Pure apart from libcUtcOffset(), which asks the C
// library under the process TZ (HalClock::setTimezone() sets it on the device;
// host tests setenv("TZ", ...)).
namespace sleepcards {

// Local time minus UTC, in seconds, at a UTC instant.
using UtcOffsetFn = int32_t (*)(int64_t utc);

// localtime_r under the current TZ rule (newlib has no tm_gmtoff: the offset is
// the local fields read back as if they were UTC, minus the instant).
int32_t libcUtcOffset(int64_t utc);

struct LocalDate {
  int year = 1970;
  int month = 1;    // 1..12
  int day = 1;      // 1..31
  int weekday = 4;  // 0 = Sunday
  int yearDay = 1;  // 1..366
  int hour = 0;
  int minute = 0;
  int second = 0;
};

// The RTC's time is believable: 2025-01-01 .. 2099-12-31. A clock that was never
// set reads 1970 or 2000 and must not become a date on a card.
bool plausibleTime(int64_t utc);

// The local calendar date and wall time of a UTC instant.
LocalDate localDateOf(int64_t utc, UtcOffsetFn offsetAt);

// The UTC instant of local midnight starting year-month-day. DST-aware: on the day the
// clocks change the day is 23 or 25 hours long, and this is still its first second.
int64_t localDayStart(int year, int month, int day, UtcOffsetFn offsetAt);
// The first second of the next local day (so a day spans [start, next)).
int64_t localNextDayStart(int year, int month, int day, UtcOffsetFn offsetAt);

// Days since 1970-01-01 of a civil date, and back (proleptic Gregorian).
int64_t daysFromCivil(int year, int month, int day);
void civilFromDays(int64_t days, int& year, int& month, int& day);
// 0 = Sunday.
int weekdayOf(int year, int month, int day);

// "21:04" or "9:04 PM" for a UTC instant, truncated to its minute (round first with
// almanac::roundUpToMinute & co. when the rounding matters). "--:--" for 0.
void formatClock(int64_t utc, UtcOffsetFn offsetAt, bool twelveHour, char* out, size_t cap);
// The same for a wall time already split into hours and minutes.
void formatHourMinute(int hour, int minute, bool twelveHour, char* out, size_t cap);
// The same in two parts: time = "21:04" / "9:04", suffix = tr(STR_TIME_PM) with the 12-hour
// clock, "" otherwise (for layouts that set the suffix apart).
void formatHourMinuteParts(int hour, int minute, bool twelveHour, char* time, size_t timeCap, char* suffix,
                           size_t suffixCap);

}  // namespace sleepcards
