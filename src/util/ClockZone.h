#pragma once

#include <cstddef>
#include <cstdint>

#include "network/GeolocateProtocol.h"

// Time Zone From Location (Settings > Clock, X4 Pro, on by default): the clock zone that a location
// saved by Locate Me or Update Location When Syncing sets. Pure (host-tested in test/tz_grid); the
// settings and the clock are timezones::zoneForFix / applyZoneForFix's.
//
// A wrong clock is worse than an unchanged one, so the zone changes only when all of these hold:
//   1. the fix was measured: the phone's GPS over its hotspot, or two agreeing beaconDB halves
//      (the only kinds network/LocateRun produces), with an accuracy within MARGIN_M;
//   2. the time zone map (lib/TzGrid, North America at 0.05 deg) answers a zone - not near a change
//      of clock, not at sea, not outside the map, not a zone whose rules it does not model - AND
//      every map cell within MARGIN_M plus the fix's accuracy keeps that zone's clock;
//   3. a Timezones entry stands for the zone (entryForZone);
//   4. Daylight Saving is Auto (a forced one is the owner's own override of the zone's rule);
//   5. that entry keeps a different clock from the zone in use: Detroit and Windsor, or Phoenix
//      and Vancouver since 2026, are one clock and change nothing.
// Otherwise the setting stays as it was: the owner's own pick is what this falls back to.
namespace clockzone {

// The map refuses within one cell (1.7-5.6 km) of a change of clock, which is sized for a receiver
// metres off. A phone's relayed fix can be kilometres off with nothing in its sentence to say so
// (an app's stand-in figures, gpsdRelay's generated coordinates near a whole degree), and a Wi-Fi
// fix rests on where access points were last seen: every fix also needs this much room around it.
// It is the WiPhone's margin for the same phone apps; the coastal capitals keep an answer with it.
constexpr int32_t MARGIN_M = 10000;

enum class Verdict : uint8_t {
  Set,          // a different clock: Decision::entry is the entry to set
  Same,         // the zone in use keeps this clock already
  Off,          // Time Zone From Location is off
  ManualDst,    // Daylight Saving is forced on or off in Clock settings
  NotMeasured,  // not a phone or two-half Wi-Fi fix, or rougher than MARGIN_M
  NearLine,     // within one map cell, or the margin, of a change of clock
  AtSea,        // beyond any zone's territorial waters
  Outside,      // outside the map (or no valid position)
  Unmodelled,   // a zone whose rules the map does not model (Cuba, Greenland)
  NoEntry,      // no Timezones entry stands for the zone (outside North America)
};

// What decide() reads from the settings.
struct Settings {
  bool enabled = true;  // Time Zone From Location
  bool dstAuto = true;  // Daylight Saving: Auto
  uint8_t current = 0;  // the zone in use (timezones::activeIndex())
};

struct Decision {
  Verdict verdict = Verdict::NotMeasured;
  int16_t gridZone = -1;  // the map's zone (lib/TzGrid index) whenever the map answered one
  int16_t entry = -1;     // ... and the Timezones entry that stands for it (-1 = none)
};

// The verdict for one fix. gridZone / entry are filled whenever the map answered (Off, ManualDst
// and Same included, so the bench can say what it would have set).
Decision decide(const geolocate::Fix& fix, const Settings& settings);

// The map's zone at a position (1e-7 degrees) when every cell within marginM metres keeps its
// clock; otherwise TZ_BORDER (why TZ_WHY_BORDER), or the position's own refusal (TZ_NOT_COVERED
// with its why). marginM <= 0: the map's plain lookup.
int lookupClear(int32_t latE7, int32_t lonE7, int32_t marginM, uint8_t* why);

// The Timezones entry that stands for a map zone: the one with the same IANA name, else the first
// whose POSIX rule is the same text as zonePosix(); -1 when none (or not a modelled zone).
int entryForZone(int gridZone);

// A map zone's clock as a POSIX TZ rule: "EST5EDT,M3.2.0,M11.1.0", "<-05>5" (a numeric
// abbreviation in angle brackets), "NST3:30NDT,M3.2.0,M11.1.0". False for a zone whose rule is
// not modelled, a bad index, or a short buffer.
bool zonePosix(int gridZone, char* out, size_t cap);

// Do two POSIX TZ rules keep the same clock? They must match once each zone abbreviation is set
// aside ("COT5" = "<-05>5" = "UTC5"; "EST5" != "EST5EDT,M3.2.0,M11.1.0"). False for nullptr.
bool sameClock(const char* posixA, const char* posixB);

// The map zone's own name ("Eastern", "Mountain, no DST"), for the bench; "" for a bad index.
const char* zoneName(int gridZone);

// The region word of a map zone's own name, for the screen to say in the reader's language:
// Eastern for "Eastern" and for "Eastern, no DST" (*noDst tells them apart). None for a bad index
// and any other name (the UTC-offset zones, the unmodelled ones named by a city).
enum class Region : uint8_t {
  None,
  Hawaii,
  Aleutian,
  Alaska,
  Pacific,
  Mountain,
  Central,
  Eastern,
  Atlantic,
  Newfoundland,
  StPierre,
};
Region zoneRegion(int gridZone, bool* noDst);

// "set", "same", "off", ... for log lines and the bench.
const char* verdictName(Verdict verdict);

}  // namespace clockzone
