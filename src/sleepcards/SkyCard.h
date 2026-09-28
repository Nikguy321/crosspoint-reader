#pragma once

#include <cstdint>

#include "SkyPlanets.h"
#include "SleepCard.h"

// TONIGHT'S SKY: the moon (phase, moonrise/moonset) and which bright planets are up tonight (rise,
// highest point and set, with directions), with an all-sky dome drawn for the evening: 21:00
// local, or an hour after sunset when that is later, or the moment of sleep when it is later
// still (or when it falls asleep before dawn: that night is the one in progress).
// Entry point: sleepcards::renderSkyCard() (declared in SleepCard.h). No location: the moon's
// phase only, with tr(STR_SET_LOCATION_IN_SETTINGS). No trustworthy time: declines.
namespace sleepcards::skycard {

// Refraction at the horizon (34'): a body rises when its upper limb is this far below the true horizon.
constexpr double REFRACTION_DEG = 0.5667;
// The moon's centre altitude at rise/set: its upper limb on the 34' horizon, with the topocentric
// semidiameter of the moment (Astro.h's rise/set definition: -0.82 .. -0.85 degrees).
double moonHorizonAlt(int64_t t, double latDeg, double lonDeg);

// The instant the dome shows. dayStart/nextDayStart bound the local day of `now`; sunrise/sunset
// are that day's (0 = none, e.g. polar day or night); ref21 is 21:00 local that day.
int64_t referenceTime(int64_t now, int64_t dayStart, int64_t nextDayStart, int64_t ref21, int64_t sunrise,
                      int64_t sunset);

// The dark hours around `ref`: [start, end). Sunset before it and sunrise after it, from the one
// day of sun events the card computes (the other day's is that day's shifted by a day: a few
// minutes off at most, and only used to sort planets into "later tonight" / "this evening").
// Without a sunset or sunrise (polar summer/winter) ref - 3 h / ref + 9 h.
void nightWindow(int64_t ref, int64_t dayStart, int64_t nextDayStart, int64_t sunrise, int64_t sunset, int64_t& start,
                 int64_t& end);

// 16-point compass: 0 = N, 4 = E, 8 = S, 12 = W.
int compassIndex16(double azDeg);
const char* compassName(int index16);  // translated, "" outside 0..15

// Azimuthal-equidistant projection of the sky onto a dome of `radius` px: the horizon on the
// circle, the zenith in the middle, north up and EAST ON THE LEFT (a map of the sky overhead, as
// on a planisphere). Altitudes below 0 land outside the circle.
void domeProject(double altDeg, double azDeg, int cx, int cy, int radius, int& x, int& y);

enum class PlanetState : uint8_t {
  Up,          // above the horizon at the reference time
  RisesLater,  // below it, and rises during the night
  BeforeDawn,  // below it, and rises in the last two hours before sunrise (a morning object)
  SetEarlier,  // below it, having set after sunset
  NotTonight,  // in the daytime sky (near the sun)
  UpAllNight,  // circumpolar tonight
  NeverUp,     // stays below the horizon
};

struct PlanetTonight {
  sky::Planet planet = sky::Planet::Venus;
  PlanetState state = PlanetState::NeverUp;
  double alt = 0;  // at the reference time
  double az = 0;
  sky::PlanetPass pass;  // the pass shown: the one in progress, else the next (SetEarlier: the last)
  double riseAz = 0;     // azimuth at pass.rise / pass.set (when Normal)
  double setAz = 0;
  double transitAz = 180;  // 180 = due south, 0 = due north (the side of the zenith it culminates on)
};

// Where a planet is at `ref` and which of its passes matters tonight.
void planetTonight(sky::Planet p, int64_t ref, int64_t nightStart, int64_t nightEnd, double latDeg, double lonDeg,
                   PlanetTonight& out);

// The moon's pass tonight from the rise/set events of two consecutive local days (0 = none):
// the pass in progress at ref (rise <= ref < set) or else the next one. 0 = not known.
void moonPassFromEvents(const int64_t rises[2], const int64_t sets[2], int64_t ref, bool upAtRef, int64_t& rise,
                        int64_t& set);

struct SkyFacts {
  LocalDate date;         // the local date of the reference time
  int64_t ref = 0;        // the dome's instant
  bool hasPlace = false;  // a location: everything below the moon's phase needs it
  double lat = 0;
  double lon = 0;
  // Moon.
  double moonIllum = 0;
  bool moonWaxing = true;
  int moonPhase = 0;              // 0..7 (Astro.h), as named (moonlabel::displayPhase)
  bool fullMoonToday = false;     // a full moon falls on the evening's date
  int64_t nextFullMoon = 0;       // only without a place (the moon-only layout shows it)
  double moonHorizonAlt = -0.83;  // the moon's centre altitude at rise/set, at ref
  double moonAlt = 0;
  double moonAz = 0;
  // The sun at the reference time: drawn on the dome when it is up (a midsummer night far north).
  double sunAlt = -90;
  double sunAz = 0;
  int64_t moonRise = 0;
  int64_t moonSet = 0;
  // Planets.
  PlanetTonight planets[sky::PLANET_COUNT];
};

// Everything the card shows. False without a trustworthy time.
bool computeSkyFacts(const CardContext& ctx, SkyFacts& out);

}  // namespace sleepcards::skycard
