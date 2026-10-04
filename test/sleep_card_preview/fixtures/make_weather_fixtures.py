#!/usr/bin/env python3
"""Write the Weather card's preview caches (.crosspoint/sleepcards/weather*.dat), in the W1 format
of src/sleepcards/WeatherCache.h, around the preview's sample moment (2026-11-02 20:40 PST) at
Seattle's public 47.61,-122.33. The forecast is a made-up but plausible November one (rain
coming in off the Pacific); the two alerts use real NWS event names with no place in them.

    weather.dat             fetched 20:05 (35 min before the draw): "Now", no alerts at 20:05
    weather_alert.dat       the same forecast with two of three alerts in force (+1 more)
    weather_usonly.dat      alerts "US only (NWS)" (as a point outside the NWS area would read)
    weather_notchecked.dat  fetched 17:10: "Forecast for 20:00", alerts not checked
    weather_toomany.dat     alerts too many to list (a body too big to read)
    weather_cooling.dat     a cold front: the warmest hour is the one covering now (the high's label
                            beside the now marker), and the second alert shown starts later (a
                            12-hour range with "+1 more")
    weather_lapsed.dat      five alerts at the check, the three kept all over by the draw: the
                            two not kept "may still apply"

    python3 test/sleep_card_preview/fixtures/make_weather_fixtures.py
"""
import calendar
import datetime
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, ".crosspoint", "sleepcards")
PST = -8 * 3600


def utc(y, mo, d, h=0, mi=0, offset=PST):
    return calendar.timegm(datetime.datetime(y, mo, d, h, mi).timetuple()) - offset


# Hourly from 17:00 PST on Nov 2 (a 17:10 fetch's first hour), 48 of them: temperature (tenths of
# a degree C), rain chance %, WMO code, wind (tenths of m/s).
def hours(start):
    out = []
    for i in range(48):
        t = start + i * 3600
        local_h = (17 + i) % 24
        # Mild and wet: coolest before dawn, warmest mid-afternoon, a little cooler the second day.
        temp = round(92 + 28 * math.cos((local_h - 14) / 24 * 2 * math.pi) - (i // 24) * 8)
        if 13 <= i <= 19:
            pop, code, wind = 70 + 3 * (i - 13), 63, 62 + 4 * (i - 13)
        elif 20 <= i <= 26:
            pop, code, wind = 55 - 5 * (i - 20), 80, 70 - 5 * (i - 20)
        elif i < 13:
            pop, code, wind = 15 + 4 * i, 3 if i < 6 else 61, 28 + 2 * i
        else:
            pop, code, wind = max(5, 30 - (i - 26)), 2 if (i % 5) else 3, 30
        out.append((t, temp, pop, code, wind))
    return out


# Daily from Nov 2 (the location's midnight, PST): code, max, min, pop, precip (tenths of mm),
# wind max, gust max (tenths of m/s), dominant direction FROM.
DAYS = [
    (utc(2026, 11, 2), 61, 128, 71, 40, 22, 52, 98, 190),
    (utc(2026, 11, 3), 63, 121, 74, 92, 186, 88, 151, 205),
    (utc(2026, 11, 4), 80, 109, 52, 60, 41, 61, 104, 250),
    (utc(2026, 11, 5), 2, 112, 38, 10, 0, 34, 62, 340),
    (utc(2026, 11, 6), 45, 98, 21, 5, 0, 21, 40, 20),
    (utc(2026, 11, 7), 3, 104, 44, 25, 3, 30, 55, 160),
    (utc(2026, 11, 8), 61, 117, 69, 75, 64, 55, 96, 200),
]


def write(name, fetch, current, alerts_line, alerts=(), cooling=False):
    first_hour = utc(2026, 11, 2, 17) if fetch < utc(2026, 11, 2, 20) else utc(2026, 11, 2, 20)
    hrs = [h for h in hours(utc(2026, 11, 2, 17)) if h[0] >= first_hour][:48]
    if cooling:  # steadily colder from the first hour on
        hrs = [(t, 98 - 4 * i, pop, code, wind) for i, (t, _, pop, code, wind) in enumerate(hrs)]
    lines = ["W1 %d 1 47.61 -122.33 %d" % (fetch, DAYS[-1][0]), "P wifi-auto 20261102 -28800"]
    lines.append(current)
    lines += ["H %d %d %d %d %d" % h for h in hrs]
    lines += ["D %d %d %d %d %d %d %d %d %d" % d for d in DAYS]
    lines.append(alerts_line)
    for sev, urg, typ, onset, ends, expires, event in alerts:
        lines.append("A %d %d %d %s %s %s" % (sev, urg, typ, onset, ends, expires))
        lines.append("E " + event)
        lines.append("L ")
    lines.append("end")
    text = "\n".join(lines) + "\n"
    assert len(text) <= 4096, (name, len(text))
    with open(os.path.join(OUT, name), "w", newline="\n") as f:
        f.write(text)
    print("wrote %s (%d bytes)" % (name, len(text)))


os.makedirs(OUT, exist_ok=True)
FRESH = utc(2026, 11, 2, 20, 5)
EARLY = utc(2026, 11, 2, 17, 10)
# temp feels rh hPa wind gust dir code
NOW = "C 98 61 87 10094 58 104 195 61"

write("weather.dat", FRESH, NOW, "N 1 %d 0 0" % FRESH)
write("weather_alert.dat", FRESH, NOW, "N 2 %d 3 0" % FRESH, alerts=[
    # severity urgency type onset ends expires event (WeatherData.h values; times UTC)
    # most severe first, as the parser keeps them
    (3, 2, 1, utc(2026, 11, 3, 10), utc(2026, 11, 4, 16), utc(2026, 11, 3, 4), "Flood Watch"),
    (2, 4, 1, utc(2026, 11, 2, 16), utc(2026, 11, 3, 4), utc(2026, 11, 3, 4), "Wind Advisory"),
])
write("weather_usonly.dat", FRESH, NOW, "N 3 %d 0 0" % FRESH)
write("weather_notchecked.dat", EARLY, NOW, "N 0 - 0 0")
write("weather_toomany.dat", FRESH, NOW, "N 4 %d 16 0" % FRESH)
write("weather_cooling.dat", FRESH, NOW, "N 2 %d 3 0" % FRESH, alerts=[
    (2, 4, 1, utc(2026, 11, 2, 16), utc(2026, 11, 3, 4), utc(2026, 11, 3, 4), "Wind Advisory"),
    (3, 2, 1, utc(2026, 11, 3, 10), utc(2026, 11, 4, 16), utc(2026, 11, 3, 4), "Flood Watch"),
], cooling=True)
write("weather_lapsed.dat", FRESH, NOW, "N 2 %d 5 0" % FRESH, alerts=[
    (2, 4, 1, utc(2026, 11, 2, 12), utc(2026, 11, 2, 20, 30), utc(2026, 11, 2, 20, 30), "Wind Advisory"),
    (2, 3, 1, utc(2026, 11, 2, 12), utc(2026, 11, 2, 20, 30), utc(2026, 11, 2, 20, 30), "Flood Advisory"),
    (1, 3, 1, utc(2026, 11, 2, 12), utc(2026, 11, 2, 20, 30), utc(2026, 11, 2, 20, 30), "Special Weather Statement"),
])
