#pragma once

#include <cstddef>
#include <cstdint>

#include "SleepCardSettings.h"

// Where the sleep-card location came from (Display > Sleep Screen Cards): typed in, or found by
// "Locate me" from nearby Wi-Fi or the internet address, with its accuracy and the day it was
// saved. Stored as one setting beside sleepCardLocation, and it names the location it describes,
// so a location changed anywhere else (the web settings page, a restored settings file) reads as
// typed in rather than inheriting an old fix:
//
//   "wifi 80 2026-09-29 47.6100,-122.3300"     source accuracy-m date location
//   "typed 0 2026-09-29 47.6100,-122.3300"     accuracy 0 = none; date "-" = unknown
//   "wifi-auto 80 2026-09-29 ..."              refreshed from Wi-Fi during a sync (AutoLocate)
namespace sleepcards {

enum class LocationSource : uint8_t { Typed = 0, Wifi = 1, Internet = 2, WifiAuto = 3 };

constexpr size_t LOCATION_FIX_CAP = 48;
constexpr uint32_t MAX_FIX_ACCURACY_M = 999999;

struct LocationFix {
  LocationSource source = LocationSource::Typed;
  uint32_t accuracyM = 0;  // 0 = not known (typed in)
  uint16_t year = 0;       // the day it was saved; 0 = not known
  uint8_t month = 0;
  uint8_t day = 0;
  char location[LOCATION_CAP] = "";  // the stored location it describes ("47.6100,-122.3300")
};

// The stored record. False (out = "") when the fix does not describe a valid location.
bool formatLocationFix(const LocationFix& fix, char* out, size_t cap);
// Reads a record written by formatLocationFix; anything else is refused.
bool parseLocationFix(const char* text, LocationFix& out);
// The settings-file loader's check: "" stays "", a valid record is rewritten canonically.
bool normalizeLocationFix(const char* text, char* out, size_t cap);

// The record after the Location row is typed (before/after = the stored location either side
// of the entry, record = the record before it): an unchanged location keeps its record, a
// cleared one has none, and a new one is typed in on the given day (year 0 = not known). False
// (out = "") when no record fits.
bool recordForTypedLocation(const char* before, const char* after, const char* record, uint16_t year, uint8_t month,
                            uint8_t day, char* out, size_t cap);

// The fix to show for `location` (the stored sleepCardLocation): the record when it describes
// that location, otherwise "typed in" with nothing else known. location "" = not set.
LocationFix describeLocation(const char* record, const char* location);

// "80 m", "2.5 km", "25 km" (rounded to what the number can honestly claim).
void formatAccuracy(uint32_t meters, char* out, size_t cap);

// The line under the Location row: "From Wi-Fi, ±80 m, Sep 29", "From Wi-Fi (auto), ±80 m, Sep 29",
// "From internet address, city level, Sep 29" (an address lookup has no measured accuracy),
// "Typed in, Sep 29", "Typed in".
// "" when the location is not set.
void formatFixLine(const LocationFix& fix, char* out, size_t cap);

}  // namespace sleepcards
