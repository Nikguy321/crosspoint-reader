#pragma once

#include <cstdint>

#include "Astro.h"

// Legal shooting light for one local day, rounded INWARD to the minute: the
// first legal minute is rounded UP and the last DOWN, so the window shown is
// never wider than the true one (a true first light at 06:33:20 reads 06:34).
// Sun and moon times elsewhere round to the NEAREST minute; this is the one
// place where rounding is a legal question rather than a cosmetic one.
// Before rounding, both ends move MODEL_MARGIN_S inward: the sun model is good
// to ~10 s against PyEphem, so a true first light a few seconds past a minute
// could otherwise compute just before it and show a minute early.
// Ported from the WiPhone firmware's almanac_lines.cpp (legalRow/almFmtClock),
// which rounds without the margin.
namespace almanac {

constexpr int64_t MODEL_MARGIN_S = 15;

enum class LegalRule : uint8_t {
  ThirtyMinutes = ASTRO_LEGAL_30MIN,  // sunrise - 30 min .. sunset + 30 min (e.g. WA big game)
  CivilTwilight = ASTRO_LEGAL_CIVIL,  // civil dawn .. civil dusk (sun centre 6 degrees down)
};

struct LegalWindow {
  enum class Kind : uint8_t {
    Window,     // first..last are both set
    FromOnly,   // legal from `first` to the end of the day (no bound in the day after it)
    UntilOnly,  // legal from the start of the day until `last`
    AllDay,     // the sun never goes below the rule's threshold
    None,       // the sun never comes above it (polar night)
  };
  Kind kind = Kind::None;
  int64_t first = 0;  // UNIX seconds on a whole minute, rounded up; 0 when absent
  int64_t last = 0;   // UNIX seconds on a whole minute, rounded down; 0 when absent
};

// Floor division that is correct for negative numerators.
int64_t floorDiv(int64_t a, int64_t b);

// Whole minutes: UP (ceil), DOWN (floor) or to the NEAREST minute (half up), as UNIX seconds.
int64_t roundUpToMinute(int64_t t);
int64_t roundDownToMinute(int64_t t);
int64_t roundToNearestMinute(int64_t t);

// The day's legal window from an astroSunDay() result for the day starting at
// t0 (local midnight, UNIX seconds) at lat/lon. Asks for one sun position only
// when a bound is missing (to tell AllDay from None).
LegalWindow legalWindow(const AstroSunDay& sun, int64_t t0, double lat, double lon, LegalRule rule);

}  // namespace almanac
