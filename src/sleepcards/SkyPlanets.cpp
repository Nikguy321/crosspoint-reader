#include "SkyPlanets.h"

#include <cmath>

// Paul Schlyter, "How to compute planetary positions" (stjarnhimlen.se/comp/ppcomp.html): the
// orbital elements below are his, of the equinox of date, with d = days since 1999-12-31 0h UT.
namespace sleepcards::sky {
namespace {

constexpr double K_PI = 3.14159265358979323846;
constexpr double D2R = K_PI / 180.0;
constexpr double R2D = 180.0 / K_PI;

constexpr int64_t UNIX_J2000 = 946728000;           // 2000-01-01 12:00 UT
constexpr int64_t UNIX_SCHLYTER_EPOCH = 946598400;  // 1999-12-31 00:00 UT (his d = 0)
constexpr double SIDEREAL_RATE = 360.98564736629;   // deg of hour angle per day (the planet's own drift aside)
constexpr int64_t SIDEREAL_DAY_S = 86164;
constexpr int64_t DAY_S = 86400;

double rev(double x) {
  double r = std::fmod(x, 360.0);
  if (r < 0) r += 360.0;
  if (r >= 360.0) r -= 360.0;
  return r;
}
double wrap180(double x) {
  const double r = rev(x);
  return r >= 180.0 ? r - 360.0 : r;
}
double sind(double x) { return std::sin(x * D2R); }
double cosd(double x) { return std::cos(x * D2R); }
double clamp1(double x) { return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x); }

struct Elements {
  double N, i, w, a, e, M;  // deg, deg, deg, AU, -, deg
};

Elements elementsOf(const Planet p, const double d) {
  switch (p) {
    case Planet::Venus:
      return {76.6799 + 2.46590E-5 * d, 3.3946 + 2.75E-8 * d,      54.8910 + 1.38374E-5 * d, 0.723330,
              0.006773 - 1.302E-9 * d,  48.0052 + 1.6021302244 * d};
    case Planet::Mars:
      return {49.5574 + 2.11081E-5 * d, 1.8497 - 1.78E-8 * d,      286.5016 + 2.92961E-5 * d, 1.523688,
              0.093405 + 2.516E-9 * d,  18.6021 + 0.5240207766 * d};
    case Planet::Jupiter:
      return {100.4542 + 2.76854E-5 * d, 1.3030 - 1.557E-7 * d,     273.8777 + 1.64505E-5 * d, 5.20256,
              0.048498 + 4.469E-9 * d,   19.8950 + 0.0830853001 * d};
    case Planet::Saturn:
    default:
      return {113.6634 + 2.38980E-5 * d, 2.4886 - 1.081E-7 * d,      339.3939 + 2.97661E-5 * d, 9.55475,
              0.055546 - 9.499E-9 * d,   316.9670 + 0.0334442282 * d};
  }
}

// Kepler's equation for the eccentric anomaly, deg (e <= 0.1 here: 3-4 Newton steps).
double eccentricAnomaly(const double Mdeg, const double e) {
  const double M = rev(Mdeg);
  double E = M + e * R2D * sind(M) * (1.0 + e * cosd(M));
  for (int k = 0; k < 8; k++) {
    const double dE = (E - e * R2D * sind(E) - M) / (1.0 - e * cosd(E));
    E -= dE;
    if (std::fabs(dE) < 1e-7) break;
  }
  return E;
}

// Heliocentric ecliptic rectangular coordinates of date, AU.
void heliocentric(const Planet p, const double d, double& x, double& y, double& z) {
  const Elements el = elementsOf(p, d);
  const double E = eccentricAnomaly(el.M, el.e);
  const double xv = el.a * (cosd(E) - el.e);
  const double yv = el.a * std::sqrt(1.0 - el.e * el.e) * sind(E);
  const double v = std::atan2(yv, xv) * R2D;
  const double r = std::sqrt(xv * xv + yv * yv);

  const double sN = sind(el.N), cN = cosd(el.N), ci = cosd(el.i), si = sind(el.i);
  const double vw = v + el.w;
  const double svw = sind(vw), cvw = cosd(vw);
  x = r * (cN * cvw - sN * svw * ci);
  y = r * (sN * cvw + cN * svw * ci);
  z = r * svw * si;

  if (p != Planet::Jupiter && p != Planet::Saturn) return;
  // The great inequality and friends: Jupiter and Saturn pull each other off their mean orbits by
  // up to ~0.3 / ~0.8 deg in longitude.
  const double Mj = 19.8950 + 0.0830853001 * d;
  const double Ms = 316.9670 + 0.0334442282 * d;
  double lon = std::atan2(y, x) * R2D;
  double lat = std::atan2(z, std::sqrt(x * x + y * y)) * R2D;
  if (p == Planet::Jupiter) {
    lon += -0.332 * sind(2 * Mj - 5 * Ms - 67.6) - 0.056 * sind(2 * Mj - 2 * Ms + 21) +
           0.042 * sind(3 * Mj - 5 * Ms + 21) - 0.036 * sind(Mj - 2 * Ms) + 0.022 * cosd(Mj - Ms) +
           0.023 * sind(2 * Mj - 3 * Ms + 52) - 0.016 * sind(Mj - 5 * Ms - 69);
  } else {
    lon += 0.812 * sind(2 * Mj - 5 * Ms - 67.6) - 0.229 * cosd(2 * Mj - 4 * Ms - 2) + 0.119 * sind(Mj - 2 * Ms - 3) +
           0.046 * sind(2 * Mj - 6 * Ms - 69) + 0.014 * sind(Mj - 3 * Ms + 32);
    lat += -0.020 * cosd(2 * Mj - 4 * Ms - 2) + 0.018 * sind(2 * Mj - 6 * Ms - 49);
  }
  const double cl = cosd(lat);
  x = r * cosd(lon) * cl;
  y = r * sind(lon) * cl;
  z = r * sind(lat);
}

// The sun's geocentric ecliptic rectangular coordinates of date, AU (z = 0).
void sunGeocentric(const double d, double& x, double& y) {
  const double w = 282.9404 + 4.70935E-5 * d;
  const double e = 0.016709 - 1.151E-9 * d;
  const double M = 356.0470 + 0.9856002585 * d;
  const double E = eccentricAnomaly(M, e);
  const double xv = cosd(E) - e;
  const double yv = std::sqrt(1.0 - e * e) * sind(E);
  const double v = std::atan2(yv, xv) * R2D;
  const double r = std::sqrt(xv * xv + yv * yv);
  x = r * cosd(v + w);
  y = r * sind(v + w);
}

// Quadratic interpolation through three equally spaced values at s = -1, 0, 1.
double interp3(const double fm, const double f0, const double fp, const double s) {
  return f0 + s * (fp - fm) * 0.5 + s * s * (fp - 2.0 * f0 + fm) * 0.5;
}

// cos of the hour angle at which a body of declination dec is on the rise/set horizon.
double cosRiseHourAngle(const double decDeg, const double latDeg) {
  const double lat = latDeg > 89.9 ? 89.9 : (latDeg < -89.9 ? -89.9 : latDeg);
  return (sind(RISE_ALT_DEG) - sind(lat) * sind(decDeg)) / (cosd(lat) * cosd(decDeg));
}

int64_t toSecond(const double t) { return static_cast<int64_t>(std::ceil(t)); }

// Iterate an event whose hour angle is `target(dec)` from a first guess; returns the instant.
// sign: -1 rise (H = -H0), +1 set (H = +H0), 0 transit (H = 0).
double iterateEvent(const PlanetTrack& track, double t, const int sign, const double latDeg, const double lonDeg) {
  for (int k = 0; k < 12; k++) {
    double ra = 0, dec = 0;
    trackRaDec(track, static_cast<int64_t>(std::llround(t)), ra, dec);
    double target = 0;
    if (sign != 0) {
      const double h0 = std::acos(clamp1(cosRiseHourAngle(dec, latDeg))) * R2D;
      target = sign * h0;
    }
    const double H = localSiderealDeg(static_cast<int64_t>(std::llround(t)), lonDeg) - ra;
    const double dt = -wrap180(H - target) / SIDEREAL_RATE * DAY_S;
    t += dt;
    if (std::fabs(dt) < 0.5) break;
  }
  return t;
}

}  // namespace

Equatorial planetEquatorial(const Planet p, const int64_t t) {
  const double d = static_cast<double>(t - UNIX_SCHLYTER_EPOCH) / DAY_S;
  double xh = 0, yh = 0, zh = 0;
  heliocentric(p, d, xh, yh, zh);
  double xs = 0, ys = 0;
  sunGeocentric(d, xs, ys);
  const double xg = xh + xs;
  const double yg = yh + ys;
  const double zg = zh;
  const double ecl = 23.4393 - 3.563E-7 * d;
  const double xe = xg;
  const double ye = yg * cosd(ecl) - zg * sind(ecl);
  const double ze = yg * sind(ecl) + zg * cosd(ecl);
  Equatorial out;
  out.ra = rev(std::atan2(ye, xe) * R2D);
  out.dec = std::atan2(ze, std::sqrt(xe * xe + ye * ye)) * R2D;
  out.distAu = std::sqrt(xe * xe + ye * ye + ze * ze);
  return out;
}

double localSiderealDeg(const int64_t t, const double lonDeg) {
  const double dd = static_cast<double>(t - UNIX_J2000) / DAY_S;
  const double T = dd / 36525.0;
  return rev(280.46061837 + SIDEREAL_RATE * dd + T * T * 0.000387933 + lonDeg);
}

void horizontalOf(const double raDeg, const double decDeg, const int64_t t, const double latDeg, const double lonDeg,
                  double* altDeg, double* azDeg) {
  const double ha = localSiderealDeg(t, lonDeg) - raDeg;
  const double sh = sind(ha), ch = cosd(ha), sd = sind(decDeg), cd = cosd(decDeg);
  const double sl = sind(latDeg), cl = cosd(latDeg);
  if (altDeg) *altDeg = std::asin(clamp1(sl * sd + cl * cd * ch)) * R2D;
  if (azDeg) *azDeg = rev(std::atan2(sh * cd, ch * sl * cd - sd * cl) * R2D + 180.0);
}

void planetAltAz(const Planet p, const int64_t t, const double latDeg, const double lonDeg, double* altDeg,
                 double* azDeg) {
  const Equatorial eq = planetEquatorial(p, t);
  horizontalOf(eq.ra, eq.dec, t, latDeg, lonDeg, altDeg, azDeg);
}

void trackInit(PlanetTrack& track, const Planet p, const int64_t center) {
  track.planet = p;
  track.center = center;
  for (int k = 0; k < PlanetTrack::NODES; k++) {
    const Equatorial eq = planetEquatorial(p, center + (k - PlanetTrack::NODES / 2) * DAY_S);
    track.ra[k] = eq.ra;
    track.dec[k] = eq.dec;
    if (k > 0) track.ra[k] = track.ra[k - 1] + wrap180(track.ra[k] - track.ra[k - 1]);  // continuous
  }
}

void trackRaDec(const PlanetTrack& track, const int64_t t, double& raDeg, double& decDeg) {
  const double u = static_cast<double>(t - track.center) / DAY_S + PlanetTrack::NODES / 2;
  int i = static_cast<int>(std::lround(u));
  if (i < 1) i = 1;
  if (i > PlanetTrack::NODES - 2) i = PlanetTrack::NODES - 2;
  const double s = u - i;
  raDeg = rev(interp3(track.ra[i - 1], track.ra[i], track.ra[i + 1], s));
  decDeg = interp3(track.dec[i - 1], track.dec[i], track.dec[i + 1], s);
}

int64_t trackTransitNear(const PlanetTrack& track, const int64_t guess, const double latDeg, const double lonDeg) {
  return toSecond(iterateEvent(track, static_cast<double>(guess), 0, latDeg, lonDeg));
}

PlanetPass trackPass(const PlanetTrack& track, const int64_t transit, const double latDeg, const double lonDeg) {
  PlanetPass pass;
  pass.transit = transit;
  double ra = 0, dec = 0;
  trackRaDec(track, transit, ra, dec);
  const double lat = latDeg > 89.9 ? 89.9 : (latDeg < -89.9 ? -89.9 : latDeg);
  pass.transitAlt = 90.0 - std::fabs(lat - dec);
  const double c = cosRiseHourAngle(dec, latDeg);
  if (c <= -1.0) {
    pass.kind = PlanetPass::Kind::AlwaysUp;
    return pass;
  }
  if (c >= 1.0) {
    pass.kind = PlanetPass::Kind::NeverUp;
    return pass;
  }
  const double h0s = std::acos(c) * R2D / SIDEREAL_RATE * DAY_S;
  pass.rise = toSecond(iterateEvent(track, static_cast<double>(transit) - h0s, -1, latDeg, lonDeg));
  pass.set = toSecond(iterateEvent(track, static_cast<double>(transit) + h0s, +1, latDeg, lonDeg));
  return pass;
}

namespace {

int64_t eventOf(const PlanetPass& pass, const EventKind kind) {
  switch (kind) {
    case EventKind::Transit:
      return pass.transit;
    case EventKind::Rise:
      return pass.kind == PlanetPass::Kind::Normal ? pass.rise : 0;
    case EventKind::Set:
    default:
      return pass.kind == PlanetPass::Kind::Normal ? pass.set : 0;
  }
}

// kFirst..kLast passes around `from`; next = true: first event > from, else last event <= from.
int64_t searchEvent(const PlanetTrack& track, const EventKind kind, const int64_t from, const double latDeg,
                    const double lonDeg, const int kFirst, const int kLast, const bool next) {
  const int64_t t0 = trackTransitNear(track, from, latDeg, lonDeg);
  int64_t best = 0;
  for (int k = kFirst; k <= kLast; k++) {
    const int64_t transit = k == 0 ? t0 : trackTransitNear(track, t0 + k * SIDEREAL_DAY_S, latDeg, lonDeg);
    const int64_t ev = eventOf(trackPass(track, transit, latDeg, lonDeg), kind);
    if (ev == 0) continue;
    if (next) {
      if (ev > from && ev <= from + EVENT_WINDOW_S && (best == 0 || ev < best)) best = ev;
    } else {
      if (ev <= from && ev >= from - EVENT_WINDOW_S && (best == 0 || ev > best)) best = ev;
    }
  }
  return best;
}

}  // namespace

int64_t trackNextEvent(const PlanetTrack& track, const EventKind kind, const int64_t from, const double latDeg,
                       const double lonDeg) {
  return searchEvent(track, kind, from, latDeg, lonDeg, -1, 2, true);
}

int64_t trackPrevEvent(const PlanetTrack& track, const EventKind kind, const int64_t from, const double latDeg,
                       const double lonDeg) {
  return searchEvent(track, kind, from, latDeg, lonDeg, -2, 1, false);
}

}  // namespace sleepcards::sky
