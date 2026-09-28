#!/usr/bin/env python3
"""Generate test/almanac/AlmanacVectors.h: PyEphem ground truth for lib/Almanac.

lib/Almanac/Astro.cpp (ported from the WiPhone firmware) computes sun and moon
events and moon phases offline. None of it can be checked by looking at it -
a wrong moonrise looks exactly as plausible as a right one - so the host test
replays what an independent, established reference says for the same inputs.
This is the WiPhone's tools/gen_almanac_vectors.py (same definitions, same
PyEphem calls), cut down to the sun, the moon and the phases, over PUBLIC
places only (city centres and the like; nothing of anyone's).

  * sun + moon: PyEphem (libastro, the XEphem engine), pressure 0 and the
    standard -0:34 horizon, upper limb for rise/set - the almanac definition.
    Civil twilight: the centre at -6 degrees.
  * phases: PyEphem's next_new_moon / next_first_quarter_moon / ...

    pip install --user ephem
    python3 scripts/gen_almanac_vectors.py

A day is a LOCAL day: the window [t0, t0 + 86400) where t0 is local midnight as
UNIX seconds under the place's fixed offset. Each event is the FIRST of its kind
in that window, 0 = none (the moon skips a rise about once a month; the sun at
71 N skips everything for weeks).
"""
import argparse
import math
import os
import sys

try:
    import ephem
except ImportError as e:
    sys.exit("need ephem: pip install --user ephem (%s)" % e)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EPOCH = ephem.Date("1970/1/1 00:00:00")

# name, lat, lon, fixed UTC offset in hours (standard-ish; the WINDOW is what is tested)
PLACES = [
    ("seattle", 47.6062, -122.3321, -8),
    ("fairbanks", 64.8378, -147.7164, -9),     # the moon goes circumpolar near the standstill
    ("utqiagvik", 71.2906, -156.7886, -9),     # polar night and midnight sun
    ("quito", -0.1807, -78.4678, -5),          # the equator
    ("hobart", -42.8821, 147.3272, 10),        # the south, east of Greenwich
]


def unix(d):
    return int(round((float(d) - float(EPOCH)) * 86400.0))


def edate(u):
    return ephem.Date(EPOCH + u / 86400.0)


def observer(lat, lon, horizon="-0:34"):
    o = ephem.Observer()
    o.lat, o.lon = str(lat), str(lon)
    o.elevation = 0
    o.pressure = 0          # no refraction model: the -0:34 horizon IS the standard refraction
    o.horizon = horizon
    return o


def first_in(fn, t0, obs=None):
    """The first event fn() finds at or after t0, if it is inside the local day; else 0.

    PyEphem raises NeverUpError / AlwaysUpError when the body is circumpolar or never up AT THE
    SEARCH'S START, even if the moon's declination changes enough to rise later that day, so on
    a raise the search is retried from each later hour of the window; the first hit wins."""
    starts = [t0] + ([t0 + h * 3600 for h in range(1, 24)] if obs is not None else [])
    for s in starts:
        if obs is not None:
            obs.date = edate(s)
        try:
            t = unix(fn())
        except (ephem.NeverUpError, ephem.AlwaysUpError, ephem.CircumpolarError):
            continue
        return t if t0 <= t < t0 + 86400 else 0
    return 0


def sun_day(lat, lon, t0):
    o = observer(lat, lon)
    s = ephem.Sun()
    rise = first_in(lambda: o.next_rising(s), t0, o)
    sset = first_in(lambda: o.next_setting(s), t0, o)
    noon = first_in(lambda: o.next_transit(s), t0, o)
    c = observer(lat, lon, "-6")
    dawn = first_in(lambda: c.next_rising(s, use_center=True), t0, c)
    dusk = first_in(lambda: c.next_setting(s, use_center=True), t0, c)
    return dict(dawn=dawn, rise=rise, noon=noon, set=sset, dusk=dusk)


def moon_day(lat, lon, t0):
    o = observer(lat, lon)
    m = ephem.Moon()
    out = {}
    for k, fn in (("rise", o.next_rising), ("set", o.next_setting),
                  ("transit", o.next_transit), ("under", o.next_antitransit)):
        out[k] = first_in(lambda: fn(m), t0, o)
    return out


def body_now(body, lat, lon, t):
    o = observer(lat, lon)
    o.date = edate(t)
    body.compute(o)
    return math.degrees(body.alt), math.degrees(body.az)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--header", default=os.path.join(ROOT, "test", "almanac", "AlmanacVectors.h"))
    a = ap.parse_args()

    start = unix(ephem.Date("2026/1/1"))
    end = unix(ephem.Date("2029/1/1"))

    days = []
    for pi, (name, lat, lon, tz) in enumerate(PLACES):
        # 17-day steps, offset per place so the places do not all sample the same moon phase
        u = start + pi * 86400 * 3
        while u < end:
            t0 = u - tz * 3600          # local midnight of the UTC date u, as UNIX seconds
            days.append(dict(place=name, lat=lat, lon=lon, t0=t0, sun=sun_day(lat, lon, t0),
                             moon=moon_day(lat, lon, t0)))
            u += 17 * 86400

    nows = []
    t = start + 1234
    i = 0
    while t < end:
        name, lat, lon, tz = PLACES[i % len(PLACES)]
        malt, maz = body_now(ephem.Moon(), lat, lon, t)
        salt, saz = body_now(ephem.Sun(), lat, lon, t)
        g = ephem.Moon(edate(t))           # geocentric, for phase
        nows.append(dict(place=name, lat=lat, lon=lon, t=t, moon_alt=malt, moon_az=maz,
                         sun_alt=salt, sun_az=saz, illum=float(g.moon_phase),
                         elong=math.degrees(float(g.elong))))
        t += int(71.3 * 3600)
        i += 1

    phases = []
    u = start
    while u < end:
        d = edate(u)
        phases.append(dict(frm=u, new=unix(ephem.next_new_moon(d)),
                           first=unix(ephem.next_first_quarter_moon(d)),
                           full=unix(ephem.next_full_moon(d)),
                           last=unix(ephem.next_last_quarter_moon(d))))
        u += 23 * 86400

    h = ["// GENERATED by scripts/gen_almanac_vectors.py - do not edit. PyEphem %s." % ephem.__version__,
         "// Public places only. t0/t/from are UNIX seconds; 0 = no such event in [t0, t0 + 86400).",
         "#pragma once", "#include <cstdint>", "",
         "struct AlmDayVec {", "  const char* place;", "  double lat, lon;", "  int64_t t0;",
         "  int64_t dawn, rise, noon, set, dusk;", "  int64_t mrise, mset, mtransit, munder;", "};",
         "struct AlmNowVec {", "  const char* place;", "  double lat, lon;", "  int64_t t;",
         "  double moonAlt, moonAz, sunAlt, sunAz, illum, elong;", "};",
         "struct AlmPhaseVec {", "  int64_t from, next[4];  // new, first quarter, full, last quarter", "};",
         ""]
    h.append("static const AlmDayVec ALM_DAYS[] = {")
    for d in days:
        s, m = d["sun"], d["moon"]
        h.append('    {"%s", %.6f, %.6f, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d},' % (
            d["place"], d["lat"], d["lon"], d["t0"], s["dawn"], s["rise"], s["noon"], s["set"],
            s["dusk"], m["rise"], m["set"], m["transit"], m["under"]))
    h.append("};")
    h.append("static const AlmNowVec ALM_NOWS[] = {")
    for n in nows:
        h.append('    {"%s", %.6f, %.6f, %d, %.5f, %.5f, %.5f, %.5f, %.6f, %.5f},' % (
            n["place"], n["lat"], n["lon"], n["t"], n["moon_alt"], n["moon_az"], n["sun_alt"],
            n["sun_az"], n["illum"], n["elong"]))
    h.append("};")
    h.append("static const AlmPhaseVec ALM_PHASES[] = {")
    for p in phases:
        h.append("    {%d, {%d, %d, %d, %d}}," % (p["frm"], p["new"], p["first"], p["full"], p["last"]))
    h.append("};")
    with open(a.header, "w") as f:
        f.write("\n".join(h) + "\n")
    print("days=%d nows=%d phases=%d -> %s" % (len(days), len(nows), len(phases), a.header))


if __name__ == "__main__":
    main()
