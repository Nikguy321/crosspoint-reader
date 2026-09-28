#include "CardTime.h"

#include <I18n.h>

#include <cstdio>
#include <ctime>

namespace sleepcards {
namespace {

int64_t floorDiv(const int64_t a, const int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

constexpr int64_t PLAUSIBLE_FROM = 1735689600;  // 2025-01-01T00:00:00Z
constexpr int64_t PLAUSIBLE_TO = 4102444800;    // 2100-01-01T00:00:00Z

}  // namespace

int64_t daysFromCivil(int year, const int month, const int day) {
  // Howard Hinnant's days_from_civil.
  year -= month <= 2;
  const int64_t era = floorDiv(year, 400);
  const int64_t yoe = year - era * 400;
  const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t days, int& year, int& month, int& day) {
  days += 719468;
  const int64_t era = floorDiv(days, 146097);
  const int64_t doe = days - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

int weekdayOf(const int year, const int month, const int day) {
  const int64_t d = daysFromCivil(year, month, day);
  return static_cast<int>(((d % 7) + 11) % 7);  // 1970-01-01 was a Thursday (4)
}

int32_t libcUtcOffset(const int64_t utc) {
  const time_t t = static_cast<time_t>(utc);
  struct tm wall{};
  if (localtime_r(&t, &wall) == nullptr) return 0;
  const int64_t asUtc = daysFromCivil(wall.tm_year + 1900, wall.tm_mon + 1, wall.tm_mday) * 86400 +
                        wall.tm_hour * 3600 + wall.tm_min * 60 + wall.tm_sec;
  return static_cast<int32_t>(asUtc - utc);
}

bool plausibleTime(const int64_t utc) { return utc >= PLAUSIBLE_FROM && utc < PLAUSIBLE_TO; }

LocalDate localDateOf(const int64_t utc, const UtcOffsetFn offsetAt) {
  const int64_t wallSecs = utc + (offsetAt ? offsetAt(utc) : 0);
  const int64_t days = floorDiv(wallSecs, 86400);
  const int64_t secs = wallSecs - days * 86400;
  LocalDate d;
  civilFromDays(days, d.year, d.month, d.day);
  d.weekday = static_cast<int>(((days % 7) + 11) % 7);
  d.yearDay = static_cast<int>(days - daysFromCivil(d.year, 1, 1) + 1);
  d.hour = static_cast<int>(secs / 3600);
  d.minute = static_cast<int>((secs / 60) % 60);
  d.second = static_cast<int>(secs % 60);
  return d;
}

int64_t localDayStart(const int year, const int month, const int day, const UtcOffsetFn offsetAt) {
  const int64_t wall = daysFromCivil(year, month, day) * 86400;
  if (!offsetAt) return wall;
  // Midnight takes the offset in force at midnight: start from the noon offset (never inside a
  // change, which happens in the small hours) and settle it at the candidate instant.
  int64_t t = wall - offsetAt(wall + 43200 - offsetAt(wall + 43200));
  for (int i = 0; i < 3; i++) {
    const int64_t next = wall - offsetAt(t);
    if (next == t) break;
    t = next;
  }
  return t;
}

int64_t localNextDayStart(const int year, const int month, const int day, const UtcOffsetFn offsetAt) {
  int y = 0;
  int m = 0;
  int d = 0;
  civilFromDays(daysFromCivil(year, month, day) + 1, y, m, d);
  return localDayStart(y, m, d, offsetAt);
}

void formatHourMinuteParts(const int hour, const int minute, const bool twelveHour, char* time, const size_t timeCap,
                           char* suffix, const size_t suffixCap) {
  if (time == nullptr || timeCap == 0) return;
  if (suffix != nullptr && suffixCap > 0) suffix[0] = '\0';
  if (twelveHour) {
    const int h12 = hour % 12 == 0 ? 12 : hour % 12;
    std::snprintf(time, timeCap, "%d:%02d", h12, minute);
    if (suffix != nullptr && suffixCap > 0) {
      std::snprintf(suffix, suffixCap, "%s", hour < 12 ? tr(STR_TIME_AM) : tr(STR_TIME_PM));
    }
  } else {
    std::snprintf(time, timeCap, "%02d:%02d", hour, minute);
  }
}

void formatHourMinute(const int hour, const int minute, const bool twelveHour, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return;
  char time[16];
  char suffix[16];
  formatHourMinuteParts(hour, minute, twelveHour, time, sizeof(time), suffix, sizeof(suffix));
  if (suffix[0] != '\0') {
    std::snprintf(out, cap, "%s %s", time, suffix);
  } else {
    std::snprintf(out, cap, "%s", time);
  }
}

void formatClock(const int64_t utc, const UtcOffsetFn offsetAt, const bool twelveHour, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return;
  if (utc == 0) {
    std::snprintf(out, cap, "--:--");
    return;
  }
  const LocalDate d = localDateOf(utc, offsetAt);
  formatHourMinute(d.hour, d.minute, twelveHour, out, cap);
}

}  // namespace sleepcards
