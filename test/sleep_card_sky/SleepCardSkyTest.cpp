// The Sky card: its planet ephemeris against PyEphem (SkyVectors.h, scripts/gen_sky_vectors.py),
// its pure helpers, and its rendering through the host preview (build/cards/sky*.png).
#include <Astro.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "CardPreview.h"
#include "SkyVectors.h"
#include "src/sleepcards/CardTime.h"
#include "src/sleepcards/SkyCard.h"
#include "src/sleepcards/SkyPlanets.h"

using namespace sleepcards;
using namespace sleepcards::sky;

namespace {

// The method achieves ~2' in position and ~15 s in events against PyEphem; the limits sit a little
// above that so a regression shows long before it could matter on the card.
constexpr double POS_TOL_DEG = 0.05;  // alt/az (3')
constexpr int64_t EVENT_TOL_S = 60;   // rise/transit/set
constexpr double GRAZE_MARGIN = 2.0;  // skip rise/set of passes whose culmination is this close to the horizon

double angleDiff(double a, double b) {
  double d = std::fmod(a - b, 360.0);
  if (d < -180) d += 360;
  if (d >= 180) d -= 360;
  return std::fabs(d);
}

// A pass that only grazes the horizon makes rise/set ill-conditioned (and whether it happens at all).
bool grazing(const SkyVec& v) {
  return std::fabs(v.upperAlt - RISE_ALT_DEG) < GRAZE_MARGIN || std::fabs(v.lowerAlt - RISE_ALT_DEG) < GRAZE_MARGIN;
}

}  // namespace

TEST(SkyPlanets, PositionsMatchPyEphem) {
  double worstAlt = 0, worstAz = 0, worstRa = 0, worstDec = 0;
  for (const SkyVec& v : SKY_VECTORS) {
    const Planet p = static_cast<Planet>(v.planet);
    const Equatorial eq = planetEquatorial(p, v.t);
    double alt = 0, az = 0;
    planetAltAz(p, v.t, v.lat, v.lon, &alt, &az);
    const double dRa = angleDiff(eq.ra, v.ra) * std::cos(v.dec * M_PI / 180.0);
    const double dDec = std::fabs(eq.dec - v.dec);
    const double dAlt = std::fabs(alt - v.alt);
    // Azimuth is meaningless at the zenith: weigh it by cos(alt), like a distance on the sky.
    const double dAz = angleDiff(az, v.az) * std::cos(v.alt * M_PI / 180.0);
    worstRa = std::fmax(worstRa, dRa);
    worstDec = std::fmax(worstDec, dDec);
    worstAlt = std::fmax(worstAlt, dAlt);
    worstAz = std::fmax(worstAz, dAz);
    EXPECT_LT(dAlt, POS_TOL_DEG) << v.place << " t=" << v.t << " planet " << v.planet;
    EXPECT_LT(dAz, POS_TOL_DEG) << v.place << " t=" << v.t << " planet " << v.planet;
    EXPECT_LT(dRa, 0.05) << v.place << " t=" << v.t << " planet " << v.planet;
    EXPECT_LT(dDec, 0.05) << v.place << " t=" << v.t << " planet " << v.planet;
  }
  std::printf("  worst: RA %.2f' Dec %.2f' alt %.2f' az %.2f' (arc-minutes, %zu samples)\n", worstRa * 60,
              worstDec * 60, worstAlt * 60, worstAz * 60, sizeof(SKY_VECTORS) / sizeof(SKY_VECTORS[0]));
}

TEST(SkyPlanets, EventsMatchPyEphem) {
  int64_t worst = 0;
  int checked = 0, skipped = 0;
  for (const SkyVec& v : SKY_VECTORS) {
    const Planet p = static_cast<Planet>(v.planet);
    PlanetTrack track;
    trackInit(track, p, v.t);
    struct Case {
      const char* name;
      int64_t expected;
      int64_t got;
      bool riseSet;
    } cases[] = {
        {"next rise", v.nextRise, trackNextEvent(track, EventKind::Rise, v.t, v.lat, v.lon), true},
        {"next set", v.nextSet, trackNextEvent(track, EventKind::Set, v.t, v.lat, v.lon), true},
        {"next transit", v.nextTransit, trackNextEvent(track, EventKind::Transit, v.t, v.lat, v.lon), false},
        {"prev rise", v.prevRise, trackPrevEvent(track, EventKind::Rise, v.t, v.lat, v.lon), true},
        {"prev set", v.prevSet, trackPrevEvent(track, EventKind::Set, v.t, v.lat, v.lon), true},
    };
    for (const Case& c : cases) {
      if (c.riseSet && grazing(v)) {
        skipped++;
        continue;
      }
      // PyEphem searches without limit; ours looks EVENT_WINDOW_S either side.
      const bool inWindow = c.expected != 0 && std::llabs(c.expected - v.t) <= EVENT_WINDOW_S - EVENT_TOL_S;
      if (!inWindow) {
        if (c.expected == 0) EXPECT_EQ(c.got, 0) << c.name << " " << v.place << " t=" << v.t << " p" << v.planet;
        skipped++;
        continue;
      }
      const int64_t err = std::llabs(c.got - c.expected);
      worst = err > worst ? err : worst;
      checked++;
      EXPECT_LE(err, EVENT_TOL_S) << c.name << " " << v.place << " t=" << v.t << " planet " << v.planet << " expected "
                                  << c.expected << " got " << c.got;
    }
  }
  std::printf("  events: %d checked, %d skipped (grazing or out of window), worst %lld s\n", checked, skipped,
              static_cast<long long>(worst));
  EXPECT_GT(checked, 1500);
}

TEST(SkyPlanets, EventsTightAgainstPyEphem) {
  // The iteration converges to the second on the method's own positions; what is left is the method's
  // arc-minute error. Hold it to a minute so a regression shows long before the 5-minute contract.
  for (const SkyVec& v : SKY_VECTORS) {
    if (grazing(v)) continue;
    PlanetTrack track;
    trackInit(track, static_cast<Planet>(v.planet), v.t);
    const int64_t got = trackNextEvent(track, EventKind::Transit, v.t, v.lat, v.lon);
    if (v.nextTransit != 0 && v.nextTransit - v.t <= EVENT_WINDOW_S - EVENT_TOL_S) {
      EXPECT_LE(std::llabs(got - v.nextTransit), 60) << v.place << " t=" << v.t << " planet " << v.planet;
    }
  }
}

// ---- the card's pure helpers --------------------------------------------------------------------

TEST(SkyCardHelpers, CompassPoints) {
  EXPECT_EQ(skycard::compassIndex16(0), 0);
  EXPECT_EQ(skycard::compassIndex16(11.2), 0);
  EXPECT_EQ(skycard::compassIndex16(11.3), 1);
  EXPECT_EQ(skycard::compassIndex16(90), 4);
  EXPECT_EQ(skycard::compassIndex16(180), 8);
  EXPECT_EQ(skycard::compassIndex16(270), 12);
  EXPECT_EQ(skycard::compassIndex16(348), 15);
  EXPECT_EQ(skycard::compassIndex16(349), 0);
  EXPECT_EQ(skycard::compassIndex16(359.99), 0);
  EXPECT_EQ(skycard::compassIndex16(-10), 0);
  EXPECT_EQ(skycard::compassIndex16(360 + 45), 2);
  EXPECT_STREQ(skycard::compassName(0), "N");
  EXPECT_STREQ(skycard::compassName(5), "ESE");
  EXPECT_STREQ(skycard::compassName(15), "NNW");
  EXPECT_STREQ(skycard::compassName(16), "");
  EXPECT_STREQ(skycard::compassName(-1), "");
}

TEST(SkyCardHelpers, DomeProjectionIsTheSkyOverhead) {
  int x = 0, y = 0;
  skycard::domeProject(90, 123, 200, 300, 100, x, y);  // the zenith: the middle
  EXPECT_EQ(x, 200);
  EXPECT_EQ(y, 300);
  skycard::domeProject(0, 0, 200, 300, 100, x, y);  // north on the horizon: the top
  EXPECT_EQ(x, 200);
  EXPECT_EQ(y, 200);
  skycard::domeProject(0, 90, 200, 300, 100, x, y);  // east: the LEFT
  EXPECT_EQ(x, 100);
  EXPECT_EQ(y, 300);
  skycard::domeProject(0, 270, 200, 300, 100, x, y);  // west: the right
  EXPECT_EQ(x, 300);
  skycard::domeProject(45, 180, 200, 300, 100, x, y);  // halfway up in the south
  EXPECT_EQ(x, 200);
  EXPECT_EQ(y, 350);
}

namespace {
constexpr int64_t H = 3600;
constexpr int64_t DAY0 = 1793606400;  // an arbitrary local midnight (as UNIX seconds)
}  // namespace

TEST(SkyCardHelpers, ReferenceTime) {
  const int64_t next = DAY0 + 24 * H;
  const int64_t ref21 = DAY0 + 21 * H;
  // Winter evening: 21:00 (sunset 16:50 + 1 h is earlier).
  EXPECT_EQ(skycard::referenceTime(DAY0 + 20 * H, DAY0, next, ref21, DAY0 + 7 * H, DAY0 + 16 * H + 50 * 60), ref21);
  // Midsummer: an hour after a 21:10 sunset.
  EXPECT_EQ(skycard::referenceTime(DAY0 + 20 * H, DAY0, next, ref21, DAY0 + 5 * H, DAY0 + 21 * H + 600),
            DAY0 + 22 * H + 600);
  // Asleep later than that: the moment of sleep.
  EXPECT_EQ(skycard::referenceTime(DAY0 + 23 * H, DAY0, next, ref21, DAY0 + 7 * H, DAY0 + 17 * H), DAY0 + 23 * H);
  // The small hours, before sunrise: the night in progress, now.
  EXPECT_EQ(skycard::referenceTime(DAY0 + 2 * H, DAY0, next, ref21, DAY0 + 7 * H, DAY0 + 17 * H), DAY0 + 2 * H);
  // A morning after sunrise: tonight.
  EXPECT_EQ(skycard::referenceTime(DAY0 + 9 * H, DAY0, next, ref21, DAY0 + 7 * H, DAY0 + 17 * H), ref21);
  // No sun events (polar night / no location): 21:00, or now in the small hours.
  EXPECT_EQ(skycard::referenceTime(DAY0 + 14 * H, DAY0, next, ref21, 0, 0), ref21);
  EXPECT_EQ(skycard::referenceTime(DAY0 + 3 * H, DAY0, next, ref21, 0, 0), DAY0 + 3 * H);
  EXPECT_EQ(skycard::referenceTime(DAY0 + 6 * H, DAY0, next, ref21, 0, 0), ref21);
}

TEST(SkyCardHelpers, NightWindow) {
  const int64_t next = DAY0 + 24 * H;
  int64_t start = 0, end = 0;
  // Evening: this evening's sunset to tomorrow's sunrise.
  skycard::nightWindow(DAY0 + 21 * H, DAY0, next, DAY0 + 7 * H, DAY0 + 17 * H, start, end);
  EXPECT_EQ(start, DAY0 + 17 * H);
  EXPECT_EQ(end, next + 7 * H);
  // Small hours: last evening's sunset to this morning's sunrise.
  skycard::nightWindow(DAY0 + 2 * H, DAY0, next, DAY0 + 7 * H, DAY0 + 17 * H, start, end);
  EXPECT_EQ(start, DAY0 - 7 * H);
  EXPECT_EQ(end, DAY0 + 7 * H);
  // No sun events: a fixed window around ref.
  skycard::nightWindow(DAY0 + 21 * H, DAY0, next, 0, 0, start, end);
  EXPECT_EQ(start, DAY0 + 18 * H);
  EXPECT_EQ(end, DAY0 + 30 * H);
}

TEST(SkyCardHelpers, MoonPass) {
  int64_t rise = 0, set = 0;
  // Up at 21:00: rose 14:00 today, sets 03:00 tomorrow.
  const int64_t r1[2] = {DAY0 + 14 * H, DAY0 + 24 * H + 15 * H};
  const int64_t s1[2] = {DAY0 + 2 * H, DAY0 + 24 * H + 3 * H};
  skycard::moonPassFromEvents(r1, s1, DAY0 + 21 * H, true, rise, set);
  EXPECT_EQ(rise, DAY0 + 14 * H);
  EXPECT_EQ(set, DAY0 + 27 * H);
  // Down at 21:00: rises 23:30, sets 13:00 tomorrow.
  const int64_t r2[2] = {DAY0 + 23 * H + 1800, 0};
  const int64_t s2[2] = {DAY0 + 12 * H, DAY0 + 37 * H};
  skycard::moonPassFromEvents(r2, s2, DAY0 + 21 * H, false, rise, set);
  EXPECT_EQ(rise, DAY0 + 23 * H + 1800);
  EXPECT_EQ(set, DAY0 + 37 * H);
  // Down and no rise in either day: nothing to show.
  const int64_t r3[2] = {0, 0};
  skycard::moonPassFromEvents(r3, s2, DAY0 + 21 * H, false, rise, set);
  EXPECT_EQ(rise, 0);
  EXPECT_EQ(set, 0);
  // Up with the rise the day before the pair: the set alone.
  skycard::moonPassFromEvents(r3, s1, DAY0 + 21 * H, true, rise, set);
  EXPECT_EQ(rise, 0);
  EXPECT_EQ(set, DAY0 + 27 * H);
}

// The planet's state at ref agrees with PyEphem's own altitude, and the pass shown is PyEphem's.
TEST(SkyCardHelpers, PlanetTonightAgreesWithPyEphem) {
  int checked = 0;
  for (const SkyVec& v : SKY_VECTORS) {
    if (std::fabs(v.alt - RISE_ALT_DEG) < 0.3) continue;  // right on the horizon: either answer is fine
    skycard::PlanetTonight p;
    skycard::planetTonight(static_cast<Planet>(v.planet), v.t, v.t - 6 * H, v.t + 6 * H, v.lat, v.lon, p);
    const bool up = p.state == skycard::PlanetState::Up || p.state == skycard::PlanetState::UpAllNight;
    EXPECT_EQ(up, v.alt > RISE_ALT_DEG) << v.place << " t=" << v.t << " planet " << v.planet;
    EXPECT_NEAR(p.alt, v.alt, POS_TOL_DEG);
    if (grazing(v)) continue;
    if (p.state == skycard::PlanetState::Up && v.prevRise != 0 && v.nextSet != 0) {
      EXPECT_LE(std::llabs(p.pass.rise - v.prevRise), EVENT_TOL_S) << v.place << " t=" << v.t;
      EXPECT_LE(std::llabs(p.pass.set - v.nextSet), EVENT_TOL_S) << v.place << " t=" << v.t;
      checked++;
    }
    if ((p.state == skycard::PlanetState::RisesLater || p.state == skycard::PlanetState::BeforeDawn) &&
        v.nextRise != 0) {
      EXPECT_LE(std::llabs(p.pass.rise - v.nextRise), EVENT_TOL_S) << v.place << " t=" << v.t;
      EXPECT_GT(p.pass.rise, v.t);
      EXPECT_LT(p.pass.rise, v.t + 6 * H);
      checked++;
    }
    if (p.state == skycard::PlanetState::SetEarlier && v.prevSet != 0) {
      EXPECT_LE(std::llabs(p.pass.set - v.prevSet), EVENT_TOL_S) << v.place << " t=" << v.t;
      checked++;
    }
  }
  EXPECT_GT(checked, 150);
}

// ---- the card -----------------------------------------------------------------------------------

namespace {

// A context at a local wall time under a POSIX TZ at public test coordinates.
CardContext contextAt(const char* tz, int y, int mo, int d, int hh, int mm, double lat, double lon,
                      bool located = true) {
  CardContext ctx = preview::sampleContext();
  setenv("TZ", tz, 1);
  tzset();
  ctx.utcNow = localDayStart(y, mo, d, ctx.utcOffsetAt) + hh * H + mm * 60;
  ctx.utcOffsetS = libcUtcOffset(ctx.utcNow);
  ctx.localNow = localDateOf(ctx.utcNow, ctx.utcOffsetAt);
  ctx.location.valid = located;
  ctx.location.lat = lat;
  ctx.location.lon = lon;
  return ctx;
}

constexpr const char* TZ_PACIFIC = "PST8PDT,M3.2.0,M11.1.0";
constexpr const char* TZ_ALASKA = "AKST9AKDT,M3.2.0,M11.1.0";
constexpr const char* TZ_TASMANIA = "AEST-10AEDT,M10.1.0,M4.1.0/3";

}  // namespace

TEST(SleepCardSky, DeclinesWithoutTime) {
  CardContext ctx = preview::sampleContext();
  ctx.timeValid = false;
  skycard::SkyFacts f;
  EXPECT_FALSE(skycard::computeSkyFacts(ctx, f));
  ctx = preview::sampleContext();
  ctx.utcNow = 946684800;  // an RTC that reads 2000: not believable
  EXPECT_FALSE(skycard::computeSkyFacts(ctx, f));
}

TEST(SleepCardSky, TwentyOneHundredOnTheWallAlsoOnTheDstDay) {
  // 2026-11-01: the clocks go back at 02:00 in the US; the day is 25 hours long.
  const CardContext ctx = contextAt(TZ_PACIFIC, 2026, 11, 1, 10, 0, 47.61, -122.33);
  skycard::SkyFacts f;
  ASSERT_TRUE(skycard::computeSkyFacts(ctx, f));
  const LocalDate at = localDateOf(f.ref, ctx.utcOffsetAt);
  EXPECT_EQ(at.day, 1);
  EXPECT_EQ(at.hour, 21);
  EXPECT_EQ(at.minute, 0);
}

TEST(SleepCardSky, MoonriseInTheTwentyFifthHour) {
  // 2026-11-01 is 25 hours long in Seattle; the moon rises at 23:19 PST (PyEphem: 2026-11-02
  // 07:19:04Z), in the hour a window of 86400 s from local midnight does not reach.
  // contextAt counts elapsed hours from midnight: 24:30 after it is 23:30 PST on this long day.
  const CardContext ctx = contextAt(TZ_PACIFIC, 2026, 11, 1, 24, 30, 47.61, -122.33);
  ASSERT_EQ(ctx.localNow.day, 1);
  ASSERT_EQ(ctx.localNow.hour, 23);
  skycard::SkyFacts f;
  ASSERT_TRUE(skycard::computeSkyFacts(ctx, f));
  EXPECT_GT(f.moonAlt, f.moonHorizonAlt);  // up at the dome's instant
  EXPECT_NEAR(static_cast<double>(f.moonRise), 1793603944.0, 60.0);
  EXPECT_GT(f.moonSet, f.ref);
}

TEST(SleepCardSky, NoLocationShowsTheMoonOnly) {
  const CardContext ctx = preview::sampleContextNoLocation();
  skycard::SkyFacts f;
  ASSERT_TRUE(skycard::computeSkyFacts(ctx, f));
  EXPECT_FALSE(f.hasPlace);
  EXPECT_GT(f.nextFullMoon, ctx.utcNow);
  EXPECT_NEAR(f.moonIllum, 0.35, 0.05);  // 2026-11-02 21:00 PST: a waning crescent (PyEphem: 35%)
  EXPECT_EQ(f.moonPhase, 7);
}

TEST(SleepCardSky, SampleFacts) {
  // 2026-11-02 21:00 PST from Seattle's public coordinates. Saturn is up in the SSE; the moon is below
  // the horizon; Venus is a morning object after its inferior conjunction.
  const CardContext ctx = preview::sampleContext();
  skycard::SkyFacts f;
  ASSERT_TRUE(skycard::computeSkyFacts(ctx, f));
  EXPECT_EQ(localDateOf(f.ref, ctx.utcOffsetAt).hour, 21);
  EXPECT_EQ(f.planets[3].state, skycard::PlanetState::Up);
  EXPECT_EQ(skycard::compassIndex16(f.planets[3].az), 7);  // SSE
  EXPECT_EQ(f.planets[0].state, skycard::PlanetState::BeforeDawn);
  EXPECT_LT(f.moonAlt, 0);
  EXPECT_GT(f.moonRise, f.ref);
}

// The card computes the second moon day only when the first leaves the pass unfinished; that
// shortcut must give exactly what both days give, evenings and small hours alike, over a lunation.
TEST(SleepCardSky, MoonPassShortcutMatchesBothDays) {
  for (int day = 0; day < 60; day++) {
    for (const int hour : {2, 21, 23}) {
      const CardContext ctx = contextAt(TZ_PACIFIC, 2026, 11, 1, hour, 30, 47.61, -122.33);
      CardContext c = ctx;
      c.utcNow = ctx.utcNow + day * 86400;
      skycard::SkyFacts f;
      ASSERT_TRUE(skycard::computeSkyFacts(c, f));
      const LocalDate d = localDateOf(f.ref, c.utcOffsetAt);
      const int64_t start = localDayStart(d.year, d.month, d.day, c.utcOffsetAt);
      const bool evening = f.ref - start >= 12 * H;
      // The other window is the adjoining 24 hours (not the neighbouring local day).
      const int64_t otherStart = evening ? start + 86400 : start - 86400;
      AstroMoonDay m0{}, m1{};
      astroMoonDay(start, 47.61, -122.33, &m0);
      astroMoonDay(otherStart, 47.61, -122.33, &m1);
      const int64_t rises[2] = {m0.rise, m1.rise};
      const int64_t sets[2] = {m0.set, m1.set};
      int64_t rise = 0, set = 0;
      skycard::moonPassFromEvents(rises, sets, f.ref, f.moonAlt > f.moonHorizonAlt, rise, set);
      EXPECT_EQ(f.moonRise, rise) << "day " << day << " hour " << hour;
      EXPECT_EQ(f.moonSet, set) << "day " << day << " hour " << hour;
      // And the pass is the one in progress or the next.
      if (f.moonRise != 0 && f.moonSet != 0) {
        EXPECT_LT(f.moonRise, f.moonSet);
        EXPECT_GT(f.moonSet, f.ref);
      }
    }
  }
}

TEST(SleepCardSky, RendersWithinBudget) {
  // Each context is made under its own TZ right before it renders: the offset function reads the
  // process TZ, as HalClock's does on the device.
  struct Case {
    const char* name;
    const char* tz;
    int y, mo, d, hh, mm;
    double lat, lon;
    bool located, twelveHour;
  } cases[] = {
      {"sky", TZ_PACIFIC, 2026, 11, 2, 20, 40, 47.61, -122.33, true, false},
      {"sky_nolocation", TZ_PACIFIC, 2026, 11, 2, 20, 40, 0, 0, false, false},
      {"sky_12h", TZ_PACIFIC, 2026, 11, 2, 20, 40, 47.61, -122.33, true, true},
      {"sky_moonup", TZ_PACIFIC, 2026, 11, 20, 20, 40, 47.61, -122.33, true, false},
      {"sky_smallhours", TZ_PACIFIC, 2026, 11, 3, 2, 30, 47.61, -122.33, true, false},
      {"sky_southern", TZ_TASMANIA, 2027, 1, 15, 22, 10, -42.88, 147.33, true, false},
      {"sky_polar_summer", TZ_ALASKA, 2027, 6, 21, 22, 0, 64.84, -147.72, true, false},
      {"sky_polar_winter", TZ_ALASKA, 2027, 12, 21, 18, 0, 71.29, -156.79, true, false},
      {"sky_crowded", TZ_PACIFIC, 2028, 2, 14, 19, 0, 47.61, -122.33, true, false},
  };
  for (const Case& c : cases) {
    CardContext ctx = contextAt(c.tz, c.y, c.mo, c.d, c.hh, c.mm, c.lat, c.lon, c.located);
    ctx.clock12h = c.twelveHour;
    bool declined = false;
    const double ms = preview::renderCardPng(CardId::Sky, ctx, c.name, &declined);
    std::printf("  %-18s %s in %.2f ms -> build/cards/%s.png\n", c.name, declined ? "declined" : "drawn", ms, c.name);
    EXPECT_FALSE(declined) << c.name;
    EXPECT_LT(ms, preview::HOST_RUNAWAY_MS) << c.name;
  }
  // The pure computation alone, averaged (what the device repeats in software floating point).
  const CardContext ctx = preview::sampleContext();
  constexpr int RUNS = 50;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < RUNS; i++) {
    skycard::SkyFacts f;
    skycard::computeSkyFacts(ctx, f);
  }
  const double each = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / RUNS;
  std::printf("  computeSkyFacts: %.3f ms each on the host\n", each);
  EXPECT_LT(each, 2.0);  // a runaway guard: the device is ~3 orders slower (measure it there)
}
