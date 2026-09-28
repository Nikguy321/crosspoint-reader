// lib/Almanac against PyEphem (AlmanacVectors.h, scripts/gen_almanac_vectors.py), with the
// tolerances the WiPhone's own test holds the same code to: sun events 60 s, moon events 120 s,
// every event within 1 s of its own crossing, quarters 300 s. Never loosen a tolerance or edit
// the vectors to make this pass - fix Astro.cpp.
#include <Astro.h>
#include <LegalLight.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "AlmanacVectors.h"

namespace {

enum Ev { EV_DAWN, EV_RISE, EV_NOON, EV_SET, EV_DUSK, EV_MRISE, EV_MSET, EV_MTRANSIT, EV_MUNDER, EV_N };
const char* const EV_NAME[EV_N] = {"sun dawn", "sunrise", "solar noon",   "sunset",        "sun dusk",
                                   "moonrise", "moonset", "moon transit", "moon underfoot"};

double wrap180(double a) {
  a = std::fmod(a + 180.0, 360.0);
  if (a < 0) a += 360.0;
  return a - 180.0;
}

// The event's function at t: altitude relative to its threshold (deg), from Astro.cpp's public API.
double eventFn(const int ev, const int64_t t, const double lat, const double lon) {
  double alt = 0;
  double az = 0;
  if (ev == EV_DAWN || ev == EV_DUSK) {
    astroSunPos(t, lat, lon, &alt, &az);
    return alt + 6.0;
  }
  if (ev == EV_RISE || ev == EV_SET) {
    astroSunPos(t, lat, lon, &alt, &az);
    return alt + 0.5667 + astroSunSemidiameter(t);
  }
  astroMoonPos(t, lat, lon, &alt, &az);
  return alt + 0.5667 + astroMoonSemidiameter(t, lat, lon);
}

// How close the day's extreme came to the threshold (deg), sampled each minute: a presence
// disagreement is only allowed when the body merely grazes the horizon that day.
double grazingMargin(const int ev, const int64_t t0, const double lat, const double lon) {
  if (ev == EV_NOON || ev == EV_MTRANSIT || ev == EV_MUNDER) return 1e9;
  double lo = 1e9;
  double hi = -1e9;
  for (int64_t t = t0; t <= t0 + 86400; t += 60) {
    const double f = eventFn(ev, t, lat, lon);
    lo = std::min(lo, f);
    hi = std::max(hi, f);
  }
  return std::min(std::fabs(lo), std::fabs(hi));
}

void dayEvents(const AlmDayVec& v, int64_t got[EV_N]) {
  AstroSunDay s{};
  AstroMoonDay m{};
  astroSunDay(v.t0, v.lat, v.lon, &s);
  astroMoonDay(v.t0, v.lat, v.lon, &m);
  const int64_t g[EV_N] = {s.dawn, s.rise, s.noon, s.set, s.dusk, m.rise, m.set, m.transit, m.under};
  for (int e = 0; e < EV_N; e++) got[e] = g[e];
}

}  // namespace

TEST(Almanac, DayEventsMatchPyEphem) {
  int compared = 0;
  int grazing = 0;
  for (const auto& v : ALM_DAYS) {
    int64_t got[EV_N];
    dayEvents(v, got);
    const int64_t want[EV_N] = {v.dawn, v.rise, v.noon, v.set, v.dusk, v.mrise, v.mset, v.mtransit, v.munder};
    for (int e = 0; e < EV_N; e++) {
      if (!want[e] && !got[e]) continue;
      if (want[e] && got[e]) {
        const int64_t tol = e < EV_MRISE ? 60 : 120;
        const long long err = std::llabs(static_cast<long long>(got[e] - want[e]));
        compared++;
        // A body that only grazes its threshold that day (Fairbanks' civil dawn and dusk in late July,
        // 0.007 deg apart) turns a hundredth of a degree into a minute or two: the time is
        // ill-conditioned there, so those events get a small allowance (counted below).
        if (err > tol && grazingMargin(e, v.t0, v.lat, v.lon) <= 0.1) {
          EXPECT_LE(err, tol + 120) << v.place << " " << EV_NAME[e] << " t0=" << v.t0 << " (grazing)";
          grazing++;
          continue;
        }
        EXPECT_LE(err, tol) << v.place << " " << EV_NAME[e] << " t0=" << v.t0 << " got " << got[e] << " want "
                            << want[e];
        continue;
      }
      const double margin = grazingMargin(e, v.t0, v.lat, v.lon);
      EXPECT_LE(margin, 0.1) << v.place << " " << EV_NAME[e] << " t0=" << v.t0 << ": presence differs (got " << got[e]
                             << " want " << want[e] << ")";
      grazing++;
    }
  }
  EXPECT_GT(compared, 2000);
  EXPECT_LE(grazing * 50, compared) << "grazing allowances must stay under 2% of the events";
  std::printf("  %d events compared, %d grazing allowances\n", compared, grazing);
}

// The instant is the first whole second at or past the crossing. The vectors (60/120 s) cannot
// see a 1 s slip, so each event of the northern rows is checked against its own function: on the
// near side at instant - 1, on the far side at the instant. The day scan interpolates the theory,
// which may move a crossing within a hair of a whole second across it: allowed within 1 s, for at
// most 5% of events.
TEST(Almanac, EventsLandOnTheirCrossingToTheSecond) {
  static constexpr int DIR[EV_N] = {+1, +1, +1, -1, -1, +1, -1, +1, +1};
  int n = 0;
  int exact = 0;
  for (const auto& v : ALM_DAYS) {
    if (!(v.lat > 40.0)) continue;
    int64_t got[EV_N];
    dayEvents(v, got);
    for (int e = 0; e < EV_N; e++) {
      if (!got[e]) continue;
      bool far[4];
      for (int j = 0; j < 4; j++) {
        const int64_t t = got[e] - 2 + j;
        double alt = 0;
        double az = 0;
        double f = 0;
        if (e == EV_NOON) {
          astroSunPos(t, v.lat, v.lon, &alt, &az);
          f = az - 180.0;
        } else if (e == EV_MTRANSIT || e == EV_MUNDER) {
          astroMoonPos(t, v.lat, v.lon, &alt, &az);
          f = e == EV_MTRANSIT ? az - 180.0 : (az < 180.0 ? az : az - 360.0);
        } else {
          f = eventFn(e, t, v.lat, v.lon);
        }
        far[j] = DIR[e] > 0 ? f >= 0.0 : f < 0.0;
      }
      n++;
      if (!far[1] && far[2]) {
        exact++;
      } else {
        EXPECT_TRUE((!far[0] && far[1]) || (!far[2] && far[3]))
            << v.place << " " << EV_NAME[e] << " t0=" << v.t0 << ": " << got[e] << " not within 1 s of the crossing";
      }
    }
  }
  ASSERT_GT(n, 500);
  EXPECT_GE(exact * 100, n * 95) << exact << " of " << n << " exact";
}

TEST(Almanac, PositionsAndPhaseMatchPyEphem) {
  for (const auto& v : ALM_NOWS) {
    double malt = 0;
    double maz = 0;
    double salt = 0;
    double saz = 0;
    astroMoonPos(v.t, v.lat, v.lon, &malt, &maz);
    astroSunPos(v.t, v.lat, v.lon, &salt, &saz);
    AstroMoonPhase ph{};
    astroMoonPhaseAt(v.t, &ph);
    EXPECT_LE(std::fabs(malt - v.moonAlt), 0.1) << v.place << " t=" << v.t;
    if (v.moonAlt < 85.0) EXPECT_LE(std::fabs(wrap180(maz - v.moonAz)), 0.2) << v.place << " t=" << v.t;
    EXPECT_LE(std::fabs(salt - v.sunAlt), 0.05) << v.place << " t=" << v.t;
    EXPECT_LE(std::fabs(wrap180(saz - v.sunAz)), 0.05) << v.place << " t=" << v.t;
    EXPECT_LE(std::fabs(ph.illum - v.illum), 0.005) << v.place << " t=" << v.t;
    EXPECT_LE(std::fabs(ph.sep - v.elong), 0.1) << v.place << " t=" << v.t;
    const double E = ph.elong;
    const int band = (E < 12 || E >= 348) ? 0
                     : E < 78             ? 1
                     : E < 102            ? 2
                     : E < 168            ? 3
                     : E < 192            ? 4
                     : E < 258            ? 5
                     : E < 282            ? 6
                                          : 7;
    EXPECT_EQ(ph.phase, band) << v.place << " t=" << v.t;
    EXPECT_EQ(ph.sep > 0, E < 180.0) << v.place << " t=" << v.t;
    EXPECT_LT(std::fabs(ph.ageDays - E / (360.0 / 29.530589)), 1.5) << v.place << " t=" << v.t;
  }
}

TEST(Almanac, NextQuartersMatchPyEphem) {
  static const char* const Q_NAME[4] = {"new", "first quarter", "full", "last quarter"};
  for (const auto& v : ALM_PHASES) {
    for (int q = 0; q < 4; q++) {
      const int64_t t = astroNextMoonPhase(v.from, q);
      EXPECT_LE(std::llabs(static_cast<long long>(t - v.next[q])), 300)
          << "next " << Q_NAME[q] << " from " << v.from << ": got " << t << " want " << v.next[q];
      EXPECT_LE(std::llabs(static_cast<long long>(astroPrevMoonPhase(t, q) - t)), 1);
    }
  }
}

// The theories against published worked examples (as the WiPhone's own test holds them).
TEST(Almanac, WorkedExamples) {
  EXPECT_EQ(astroJd(0), 2440587.5);
  EXPECT_EQ(astroJd(946728000LL), 2451545.0);  // J2000.0
  // Meeus, Astronomical Algorithms, example 47.a: 1992 April 12, 0h TD -> the geometric lambda
  // 133.162655, beta -3.229126, distance 368409.7 km.
  double lon = 0, lat = 0, km = 0;
  const int64_t td0 = 703036800LL - static_cast<int64_t>(astroDeltaT());
  astroMoonEcliptic(td0, &lon, &lat, &km);
  EXPECT_NEAR(lon, 133.162655, 2e-6);
  EXPECT_NEAR(lat, -3.229126, 2e-6);
  EXPECT_NEAR(km, 368409.7, 0.1);
  EXPECT_NEAR(astroMoonDistanceKm(td0), 368409.7, 0.1);
}

TEST(Almanac, CivilDateRoundTrip) {
  int y = 0, m = 0, d = 0, wd = 0, yd = 0;
  astroCivil(0, &y, &m, &d, &wd, &yd);
  EXPECT_TRUE(y == 1970 && m == 1 && d == 1 && wd == 4 && yd == 1);  // a Thursday
  astroCivil(1709164800LL, &y, &m, &d, &wd, &yd);                    // 2024-02-29
  EXPECT_TRUE(y == 2024 && m == 2 && d == 29 && yd == 60);
  // Every day 1990..2100 there and back.
  for (int64_t day = astroDaysFromCivil(1990, 1, 1); day < astroDaysFromCivil(2100, 1, 1); day += 1) {
    astroCivil(day * 86400 + 43200, &y, &m, &d, &wd, &yd);
    ASSERT_EQ(astroDaysFromCivil(y, m, d), day);
    ASSERT_EQ(wd, static_cast<int>(((day % 7) + 11) % 7));  // 1970-01-01 (day 0) was a Thursday (4)
  }
}

TEST(Almanac, QuarterTimesAreUnbiased) {
  // The mean signed error of the quarters against PyEphem stays small (a systematic slip would
  // move every calendar glyph the same way).
  long long sum = 0;
  int n = 0;
  for (const auto& v : ALM_PHASES) {
    for (int q = 0; q < 4; q++) {
      sum += astroNextMoonPhase(v.from, q) - v.next[q];
      n++;
    }
  }
  ASSERT_GT(n, 0);
  EXPECT_LE(std::llabs(sum / n), 45);
}

TEST(LegalLight, RoundsInwardToTheMinute) {
  // 06:33:20 first light reads 06:34; 19:28:59 last light reads 19:28.
  EXPECT_EQ(almanac::roundUpToMinute(1000 * 60 + 20), 1001 * 60);
  EXPECT_EQ(almanac::roundUpToMinute(1000 * 60), 1000 * 60);
  EXPECT_EQ(almanac::roundDownToMinute(1000 * 60 + 59), 1000 * 60);
  EXPECT_EQ(almanac::roundToNearestMinute(1000 * 60 + 29), 1000 * 60);
  EXPECT_EQ(almanac::roundToNearestMinute(1000 * 60 + 30), 1001 * 60);
  EXPECT_EQ(almanac::roundUpToMinute(-61), -60);
  EXPECT_EQ(almanac::roundDownToMinute(-1), -60);
}

TEST(LegalLight, ThirtyMinuteRuleAndCivilRule) {
  // Synthetic day: dawn, rise, noon, set, dusk.
  const AstroSunDay s{1000000, 1002000, 1020000, 1040000, 1042100};
  auto w = almanac::legalWindow(s, 990000, 47.61, -122.33, almanac::LegalRule::ThirtyMinutes);
  EXPECT_EQ(w.kind, almanac::LegalWindow::Kind::Window);
  EXPECT_EQ(w.first, almanac::roundUpToMinute(1002000 - 1800 + almanac::MODEL_MARGIN_S));
  EXPECT_EQ(w.last, almanac::roundDownToMinute(1040000 + 1800 - almanac::MODEL_MARGIN_S));
  EXPECT_EQ(w.first % 60, 0);
  EXPECT_GE(w.first, 1002000 - 1800);
  EXPECT_LE(w.last, 1040000 + 1800);
  w = almanac::legalWindow(s, 990000, 47.61, -122.33, almanac::LegalRule::CivilTwilight);
  EXPECT_EQ(w.first, almanac::roundUpToMinute(1000000 + almanac::MODEL_MARGIN_S));
  EXPECT_EQ(w.last, almanac::roundDownToMinute(1042100 - almanac::MODEL_MARGIN_S));
  // The margin: a first light 5 s past a minute (within the model's error of it) waits a minute.
  const AstroSunDay edge{0, 1800 * 1000 + 5 + 1800, 0, 1800 * 1000 + 55 + 7200, 0};
  w = almanac::legalWindow(edge, 1799000, 47.61, -122.33, almanac::LegalRule::ThirtyMinutes);
  EXPECT_EQ(w.first, 1800 * 1000 + 60);
  EXPECT_EQ(w.last, 1800 * 1000 + 9000);
}

TEST(LegalLight, PolarDaysAreAllDayOrNone) {
  // Utqiagvik (public coordinates): midsummer never sets, midwinter never rises.
  const double lat = 71.2906;
  const double lon = -156.7886;
  const int64_t june = astroDaysFromCivil(2026, 6, 21) * 86400 + 9 * 3600;
  const int64_t dec = astroDaysFromCivil(2026, 12, 21) * 86400 + 9 * 3600;
  AstroSunDay s{};
  astroSunDay(june, lat, lon, &s);
  EXPECT_EQ(almanac::legalWindow(s, june, lat, lon, almanac::LegalRule::ThirtyMinutes).kind,
            almanac::LegalWindow::Kind::AllDay);
  astroSunDay(dec, lat, lon, &s);
  EXPECT_EQ(almanac::legalWindow(s, dec, lat, lon, almanac::LegalRule::ThirtyMinutes).kind,
            almanac::LegalWindow::Kind::None);
}

// Not a correctness check: the compute budget of one sleep card day (sun + moon + phase + the next
// full moon) on this host, printed for the record. The device is software double-precision
// floating point and slower; the WiPhone measured the sun + moon day at ~148 ms on a 160 MHz ESP32.
TEST(Almanac, FullDayComputeTime) {
  const double lat = 47.61;
  const double lon = -122.33;
  const int64_t t0 = astroDaysFromCivil(2026, 11, 2) * 86400 + 8 * 3600;
  constexpr int RUNS = 50;
  int64_t sink = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < RUNS; i++) {
    AstroSunDay s{};
    AstroMoonDay m{};
    AstroMoonPhase ph{};
    astroSunDay(t0 + i * 86400, lat, lon, &s);
    astroMoonDay(t0 + i * 86400, lat, lon, &m);
    astroMoonPhaseAt(t0 + i * 86400 + 43200, &ph);
    sink += s.rise + m.rise + astroNextMoonPhase(t0 + i * 86400, 2);
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / RUNS;
  std::printf("  one full day (sun + moon + phase + next full) = %.3f ms on this host (sink %lld)\n", ms,
              static_cast<long long>(sink));
  // A runaway guard only: the device is ~1,500x slower at this (software double precision); its
  // real budget is measured there (x4bench.py card sky/day).
  EXPECT_LT(ms, 5.0);
}
