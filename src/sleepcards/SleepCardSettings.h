#pragma once

#include <cstddef>
#include <cstdint>

#include "CardId.h"

// The sleep-card preferences as plain data: snapshotSettings() (DeviceCards.cpp) copies them out
// of CrossPointSettings once per sleep, and host tests build them by hand. Pure
// helpers (location parsing, the hunting-season calendar, the shuffle mask)
// live here so they can be tested without the device.
namespace sleepcards {

enum class HuntMode : uint8_t { Off = 0, On = 1, Between = 2, Count };
enum class LegalLightRule : uint8_t { ThirtyMinutes = 0, CivilTwilight = 1, Count };
enum class QuoteSource : uint8_t { File = 0, Bookmarks = 1, Both = 2, Count };

struct MonthDay {
  uint8_t month = 1;  // 1..12
  uint8_t day = 1;    // 1..31 (clamped to the month's length where used)
};

constexpr size_t LOCATION_CAP = 32;
constexpr size_t OWNER_LINE_CAP = 48;

struct SleepCardSettings {
  char location[LOCATION_CAP] = "";  // "47.6100,-122.3300"; empty = not set
  HuntMode huntMode = HuntMode::Off;
  MonthDay huntStart{10, 1};
  MonthDay huntEnd{10, 31};
  LegalLightRule legalRule = LegalLightRule::ThirtyMinutes;
  char ownerName[OWNER_LINE_CAP] = "";
  char ownerContact1[OWNER_LINE_CAP] = "";
  char ownerContact2[OWNER_LINE_CAP] = "";
  QuoteSource quoteSource = QuoteSource::Both;
  uint16_t shuffleMask = 0;  // cardBit() of every card Shuffle may pick
};

// Default Shuffle picks: Now reading, Day, Calendar, Quote and Sky; not the
// Owner card or the picture frame.
constexpr uint16_t defaultShuffleMask() {
  return cardBit(CardId::NowReading) | cardBit(CardId::Day) | cardBit(CardId::Calendar) | cardBit(CardId::Quote) |
         cardBit(CardId::Sky);
}
constexpr bool shuffleIncludes(const uint16_t mask, const CardId id) { return (mask & cardBit(id)) != 0; }

// Decimal degrees, latitude first: "47.61, -122.33", "47.61 -122.33", "47.61N 122.33W",
// "47.61 N, 122.33 W". A hemisphere letter replaces the sign (a sign plus a letter is refused).
// Latitude within +-90, longitude within +-180; anything else in the text is refused.
bool parseLocation(const char* text, double& lat, double& lon);
// "47.6100,-122.3300" (4 decimals, ~11 m). Returns false when it does not fit.
bool formatLocation(double lat, double lon, char* out, size_t cap);
// What the keyboard gave back, made canonical: true with out = the stored form (or "" for an
// empty entry, which unsets the location); false when the text is not a location.
bool normalizeLocation(const char* text, char* out, size_t cap);

int daysInMonth(int year, int month);
bool isLeapYear(int year);
// "11-01", "11/1", "11 1": month then day; the day must exist in a leap year (so 02-29 is kept).
bool parseMonthDay(const char* text, MonthDay& out);

// Is the date inside the hunting season? Off = never, On = every day, Between = start..end
// inclusive, wrapping past New Year when end is before start (Dec 20 .. Jan 10). A day past the
// month's end (Feb 29 in a common year) is taken as the month's last day.
bool huntingSeasonOn(const SleepCardSettings& settings, int year, int month, int day);

}  // namespace sleepcards
