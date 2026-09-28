#pragma once

#include <cstdint>

// The bright planets for the Sky card: where Venus, Mars, Jupiter and Saturn are, and when they
// rise, cross the meridian and set. Low precision on purpose (Paul Schlyter, "How to compute
// planetary positions": mean elements of date, Kepler's equation, the largest Jupiter/Saturn
// perturbations) - an arc-minute or two, far inside what a sleep screen can draw. No light time,
// aberration or nutation (each under an arc-minute); no parallax (Mars at its closest: 0.006 deg).
//
// Proven against PyEphem by test/sleep_card_sky/SleepCardSkyTest.cpp (vectors from
// scripts/gen_sky_vectors.py). A wrong planet looks as plausible as a right one - change nothing
// here without running that test.
//
// Pure: <cmath>/<cstdint> only, no heap, no static mutable state. Conventions as lib/Almanac/Astro.h:
// instants are UNIX seconds, angles degrees, latitude north / longitude east positive, azimuth from
// true north clockwise 0..360, altitude with NO refraction (rise/set use the standard -34' horizon).
namespace sleepcards::sky {

enum class Planet : uint8_t { Venus = 0, Mars, Jupiter, Saturn };
constexpr int PLANET_COUNT = 4;

// The almanac horizon for a point-like body: the centre 34' below the geometric horizon.
constexpr double RISE_ALT_DEG = -34.0 / 60.0;

struct Equatorial {
  double ra = 0;      // right ascension of date, deg 0..360
  double dec = 0;     // declination of date, deg
  double distAu = 0;  // geocentric distance, AU
};

// Geocentric RA/Dec of date at an instant.
Equatorial planetEquatorial(Planet p, int64_t t);
// Local sidereal time (mean, deg 0..360) at an instant and east longitude.
double localSiderealDeg(int64_t t, double lonDeg);
// RA/Dec to altitude/azimuth for an observer at an instant (no refraction).
void horizontalOf(double raDeg, double decDeg, int64_t t, double latDeg, double lonDeg, double* altDeg, double* azDeg);
void planetAltAz(Planet p, int64_t t, double latDeg, double lonDeg, double* altDeg, double* azDeg);

// The planet's RA/Dec at daily nodes around `center`, quadratically interpolated between them:
// events are then a few cheap iterations each instead of full theory evaluations (a planet's
// RA/Dec curves slowly; Venus, the fastest, moves ~1.5 deg a day). Valid for t within ~2.5 days of
// center. Plain data: copy it freely.
struct PlanetTrack {
  static constexpr int NODES = 5;  // center - 2 d .. center + 2 d
  Planet planet = Planet::Venus;
  int64_t center = 0;
  double ra[NODES] = {};  // unwrapped (continuous) across the nodes
  double dec[NODES] = {};
};
void trackInit(PlanetTrack& track, Planet p, int64_t center);
void trackRaDec(const PlanetTrack& track, int64_t t, double& raDeg, double& decDeg);

// One pass across the sky: the upper culmination (transit) with the rise before it and the set
// after it. Kind tells the passes where the planet does not cross the horizon.
struct PlanetPass {
  enum class Kind : uint8_t { Normal, AlwaysUp, NeverUp };
  Kind kind = Kind::Normal;
  int64_t transit = 0;
  int64_t rise = 0;  // 0 unless Normal
  int64_t set = 0;   // 0 unless Normal
  double transitAlt = 0;
};
// The transit nearest `guess` (within half a day of it).
int64_t trackTransitNear(const PlanetTrack& track, int64_t guess, double latDeg, double lonDeg);
// The pass around a transit found by trackTransitNear().
PlanetPass trackPass(const PlanetTrack& track, int64_t transit, double latDeg, double lonDeg);

enum class EventKind : uint8_t { Rise, Transit, Set };
// The first event of a kind strictly after `from` / the last at or before it, within
// EVENT_WINDOW_S; 0 = none there (circumpolar, never up, or simply not in the window). The track
// should be centred near `from`.
constexpr int64_t EVENT_WINDOW_S = 30 * 3600;
int64_t trackNextEvent(const PlanetTrack& track, EventKind kind, int64_t from, double latDeg, double lonDeg);
int64_t trackPrevEvent(const PlanetTrack& track, EventKind kind, int64_t from, double latDeg, double lonDeg);

}  // namespace sleepcards::sky
