#include "ClockZone.h"

#include <tz_grid.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "util/Timezones.h"

namespace clockzone {
namespace {

bool modelled(const TzZone* zone) {
  return zone != nullptr && (zone->rule == TZ_RULE_NONE || zone->rule == TZ_RULE_USCAN);
}

int64_t floorDiv(const int64_t a, const int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

// ---- POSIX TZ text --------------------------------------------------------------------------

// Appends text to out[len..cap); false when it does not fit.
bool append(char* out, const size_t cap, size_t& len, const char* text) {
  const size_t n = strlen(text);
  if (len + n >= cap) return false;
  memcpy(out + len, text, n + 1);
  len += n;
  return true;
}

// A zone abbreviation as POSIX spells it: alphabetic ones as they are, others ("-05") quoted.
bool appendName(char* out, const size_t cap, size_t& len, const char* abbr) {
  const size_t n = strlen(abbr);
  if (n < 3) return false;
  bool alpha = true;
  for (size_t i = 0; i < n; i++) {
    if (!isalpha(static_cast<unsigned char>(abbr[i]))) alpha = false;
  }
  if (alpha) return append(out, cap, len, abbr);
  return append(out, cap, len, "<") && append(out, cap, len, abbr) && append(out, cap, len, ">");
}

// A POSIX offset is hours WEST of UTC: minutes east -300 is "5", +330 is "-5:30".
bool appendOffset(char* out, const size_t cap, size_t& len, const int minutesEast) {
  const int west = -minutesEast;
  const int size = west < 0 ? -west : west;
  char text[12];
  if (size % 60 == 0) {
    snprintf(text, sizeof(text), "%s%d", west < 0 ? "-" : "", size / 60);
  } else {
    snprintf(text, sizeof(text), "%s%d:%02d", west < 0 ? "-" : "", size / 60, size % 60);
  }
  return append(out, cap, len, text);
}

// The canonical form of a POSIX TZ rule for comparing clocks: each zone abbreviation becomes '#',
// each offset its signed seconds, the daylight offset is written out (POSIX: one hour ahead of
// standard when left out), and each change's time of day too (02:00 when left out). Only what
// a TimezoneInfo or zonePosix() spells is accepted: "std offset [dst [offset] [,start[/time],end[/time]]]".
class Canonical {
 public:
  explicit Canonical(const char* tz) : p(tz) {}

  bool build(char* out, const size_t cap) {
    size_t len = 0;
    int32_t stdSeconds = 0;
    if (p == nullptr || !skipName() || !offset(stdSeconds) || !put(out, cap, len, "#") ||
        !putSeconds(out, cap, len, stdSeconds)) {
      return false;
    }
    if (*p == '\0') return true;
    int32_t dstSeconds = stdSeconds - 3600;
    if (!skipName()) return false;
    if (*p != '\0' && *p != ',' && !offset(dstSeconds)) return false;
    if (!put(out, cap, len, "#") || !putSeconds(out, cap, len, dstSeconds)) return false;
    // The two changes: ",date[/time]" each. The date is kept as written (Mm.w.d, Jn or n).
    for (int i = 0; i < 2 && *p == ','; i++) {
      p++;
      const char* date = p;
      while (*p != '\0' && *p != ',' && *p != '/') p++;
      if (p == date || !put(out, cap, len, ",")) return false;
      const size_t n = static_cast<size_t>(p - date);
      if (len + n >= cap) return false;
      memcpy(out + len, date, n);
      len += n;
      out[len] = '\0';
      int32_t at = 7200;
      if (*p == '/') {
        p++;
        if (!offset(at)) return false;
      }
      if (!put(out, cap, len, "/") || !putSeconds(out, cap, len, at)) return false;
    }
    return *p == '\0';
  }

 private:
  const char* p;

  static bool put(char* out, const size_t cap, size_t& len, const char* text) { return append(out, cap, len, text); }

  static bool putSeconds(char* out, const size_t cap, size_t& len, const int32_t seconds) {
    char text[16];
    snprintf(text, sizeof(text), "%ld", static_cast<long>(seconds));
    return append(out, cap, len, text);
  }

  // "<...>" (any characters but '>') or three or more letters.
  bool skipName() {
    const char* start = p;
    if (*p == '<') {
      const char* close = strchr(p + 1, '>');
      if (close == nullptr || close - (p + 1) < 3) return false;
      p = close + 1;
      return true;
    }
    while (isalpha(static_cast<unsigned char>(*p))) p++;
    return p - start >= 3;
  }

  // [+|-]hh[:mm[:ss]] in seconds.
  bool offset(int32_t& seconds) {
    int sign = 1;
    if (*p == '+' || *p == '-') {
      if (*p == '-') sign = -1;
      p++;
    }
    int32_t parts[3] = {0, 0, 0};
    for (int i = 0; i < 3; i++) {
      if (!isdigit(static_cast<unsigned char>(*p))) return false;
      int32_t value = 0;
      int digits = 0;
      while (isdigit(static_cast<unsigned char>(*p)) && digits < 3) {
        value = value * 10 + (*p - '0');
        p++;
        digits++;
      }
      parts[i] = value;
      if (*p != ':' || i == 2) break;
      p++;
    }
    seconds = sign * (parts[0] * 3600 + parts[1] * 60 + parts[2]);
    return true;
  }
};

constexpr size_t CANONICAL_CAP = 64;

Verdict refusal(const uint8_t why) {
  switch (why) {
    case TZ_WHY_BORDER:
      return Verdict::NearLine;
    case TZ_WHY_WATER:
      return Verdict::AtSea;
    case TZ_WHY_UNKNOWN_RULE:
      return Verdict::Unmodelled;
    default:
      return Verdict::Outside;
  }
}

bool measured(const geolocate::Fix& fix) {
  if (fix.source != geolocate::FixSource::Phone && fix.source != geolocate::FixSource::Wifi) return false;
  if (!std::isfinite(fix.lat) || !std::isfinite(fix.lon)) return false;
  if (fix.lat < -90.0 || fix.lat > 90.0 || fix.lon < -180.0 || fix.lon > 180.0) return false;
  return fix.accuracyM <= static_cast<uint32_t>(MARGIN_M);
}

// The location's verdict alone (Set when a different clock could be set), before the settings.
Verdict locate(const geolocate::Fix& fix, Decision& d) {
  if (!measured(fix)) return Verdict::NotMeasured;
  const auto latE7 = static_cast<int32_t>(std::llround(fix.lat * 1e7));
  const auto lonE7 = static_cast<int32_t>(std::llround(fix.lon * 1e7));
  uint8_t why = TZ_WHY_BAD_INPUT;
  const int zone = lookupClear(latE7, lonE7, MARGIN_M + static_cast<int32_t>(fix.accuracyM), &why);
  if (zone < 0) return refusal(why);
  d.gridZone = static_cast<int16_t>(zone);
  d.entry = static_cast<int16_t>(entryForZone(zone));
  return d.entry < 0 ? Verdict::NoEntry : Verdict::Set;
}

}  // namespace

int lookupClear(const int32_t latE7, const int32_t lonE7, const int32_t marginM, uint8_t* why) {
  uint8_t centreWhy = TZ_WHY_BAD_INPUT;
  const int centre = tzGridLookup(latE7, lonE7, &centreWhy);
  if (centre < 0 || marginM <= 0) {
    if (why != nullptr) *why = centreWhy;
    return centre;
  }
  // Every cell of the box that holds the circle is looked up at its centre: the map marks the cells
  // on both sides of every change of clock, so a change anywhere in the box puts a refused cell in
  // it - walking the cells is exact at the map's resolution. The box is never short: a degree of
  // latitude is at least 110.54 km, and a degree of longitude is taken at the box's poleward edge
  // on a sphere of the equator's radius, where it is shortest.
  constexpr double PI = 3.14159265358979323846;
  const double dLat = marginM / 110540.0;
  const double poleward = std::fabs(latE7 * 1e-7) + dLat;
  if (poleward >= 85.0) {  // the map stops at 72 N
    if (why != nullptr) *why = TZ_WHY_BORDER;
    return TZ_BORDER;
  }
  const double dLon = marginM / (111320.0 * std::cos(poleward * PI / 180.0));
  const TzGridInfo* g = tzGridInfo();
  const int64_t res = g->resE7;
  const int64_t latLo = static_cast<int64_t>(latE7) - static_cast<int64_t>(dLat * 1e7) - 1;
  const int64_t latHi = static_cast<int64_t>(latE7) + static_cast<int64_t>(dLat * 1e7) + 1;
  const int64_t lonLo = static_cast<int64_t>(lonE7) - static_cast<int64_t>(dLon * 1e7) - 1;
  const int64_t lonHi = static_cast<int64_t>(lonE7) + static_cast<int64_t>(dLon * 1e7) + 1;
  // Cells on unbounded axes aligned with the map's (360 degrees is a whole number of cells).
  const int64_t r0 = floorDiv(latLo - g->lat0E7, res);
  const int64_t r1 = floorDiv(latHi - g->lat0E7, res);
  const int64_t c0 = floorDiv(lonLo - g->lon0E7, res);
  const int64_t c1 = floorDiv(lonHi - g->lon0E7, res);
  constexpr int64_t E7_180 = 1800000000LL;
  constexpr int64_t E7_360 = 3600000000LL;
  for (int64_t r = r0; r <= r1; r++) {
    const int64_t lat = g->lat0E7 + r * res + res / 2;
    for (int64_t c = c0; c <= c1; c++) {
      int64_t lon = g->lon0E7 + c * res + res / 2;
      while (lon > E7_180) lon -= E7_360;
      while (lon < -E7_180) lon += E7_360;
      const int zone = tzGridLookup(static_cast<int32_t>(lat), static_cast<int32_t>(lon), nullptr);
      if (zone < 0 || (zone != centre && !tzGridSameClock(zone, centre))) {
        if (why != nullptr) *why = TZ_WHY_BORDER;
        return TZ_BORDER;
      }
    }
  }
  if (why != nullptr) *why = TZ_WHY_ZONE;
  return centre;
}

bool zonePosix(const int gridZone, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  const TzZone* zone = tzGridZone(gridZone);
  if (!modelled(zone)) return false;
  size_t len = 0;
  bool ok = appendName(out, cap, len, zone->abbrStd) && appendOffset(out, cap, len, zone->stdMinutes);
  if (ok && zone->rule == TZ_RULE_USCAN) {
    // The 2007 US/Canada rule, as tz_grid computes it: 2nd Sunday of March to the 1st Sunday of
    // November, both at 02:00 local (POSIX's default time).
    ok = appendName(out, cap, len, zone->abbrDst) && append(out, cap, len, ",M3.2.0,M11.1.0");
  }
  if (!ok) out[0] = '\0';
  return ok;
}

bool sameClock(const char* posixA, const char* posixB) {
  char a[CANONICAL_CAP];
  char b[CANONICAL_CAP];
  return Canonical(posixA).build(a, sizeof(a)) && Canonical(posixB).build(b, sizeof(b)) && strcmp(a, b) == 0;
}

int entryForZone(const int gridZone) {
  const TzZone* zone = tzGridZone(gridZone);
  if (!modelled(zone)) return -1;
  const TimezoneInfo* entries = timezones::table();
  const size_t n = timezones::count();
  for (size_t i = 0; i < n; i++) {
    if (entries[i].iana != nullptr && strcmp(entries[i].iana, zone->iana) == 0) return static_cast<int>(i);
  }
  char rule[CANONICAL_CAP];
  if (!zonePosix(gridZone, rule, sizeof(rule))) return -1;
  for (size_t i = 0; i < n; i++) {
    if (strcmp(entries[i].posixTz, rule) == 0) return static_cast<int>(i);
  }
  return -1;
}

Decision decide(const geolocate::Fix& fix, const Settings& settings) {
  Decision d;
  d.verdict = locate(fix, d);
  if (!settings.enabled) {
    d.verdict = Verdict::Off;
  } else if (d.verdict == Verdict::Set) {
    const TimezoneInfo* entries = timezones::table();
    if (!settings.dstAuto) {
      d.verdict = Verdict::ManualDst;
    } else if (settings.current < timezones::count() &&
               sameClock(entries[settings.current].posixTz, entries[d.entry].posixTz)) {
      d.verdict = Verdict::Same;
    }
  }
  return d;
}

const char* zoneName(const int gridZone) {
  const TzZone* zone = tzGridZone(gridZone);
  return zone != nullptr ? zone->display : "";
}

Region zoneRegion(const int gridZone, bool* noDst) {
  if (noDst != nullptr) *noDst = false;
  const TzZone* zone = tzGridZone(gridZone);
  if (zone == nullptr || zone->display == nullptr) return Region::None;
  struct Named {
    const char* name;
    Region region;
  };
  static constexpr Named NAMES[] = {
      {"Hawaii", Region::Hawaii},      {"Aleutian", Region::Aleutian}, {"Alaska", Region::Alaska},
      {"Pacific", Region::Pacific},    {"Mountain", Region::Mountain}, {"Central", Region::Central},
      {"Eastern", Region::Eastern},    {"Atlantic", Region::Atlantic}, {"Newfoundland", Region::Newfoundland},
      {"St Pierre", Region::StPierre},
  };
  static constexpr char NO_DST[] = ", no DST";
  constexpr size_t NO_DST_LEN = sizeof(NO_DST) - 1;
  size_t len = strlen(zone->display);
  bool withoutDst = false;
  if (len > NO_DST_LEN && strcmp(zone->display + len - NO_DST_LEN, NO_DST) == 0) {
    len -= NO_DST_LEN;
    withoutDst = true;
  }
  for (const Named& named : NAMES) {
    if (strlen(named.name) == len && strncmp(zone->display, named.name, len) == 0) {
      if (noDst != nullptr) *noDst = withoutDst;
      return named.region;
    }
  }
  return Region::None;
}

const char* verdictName(const Verdict verdict) {
  switch (verdict) {
    case Verdict::Set:
      return "set";
    case Verdict::Same:
      return "same";
    case Verdict::Off:
      return "off";
    case Verdict::ManualDst:
      return "manual-dst";
    case Verdict::NotMeasured:
      return "not-measured";
    case Verdict::NearLine:
      return "near-line";
    case Verdict::AtSea:
      return "at-sea";
    case Verdict::Outside:
      return "outside";
    case Verdict::Unmodelled:
      return "unmodelled";
    case Verdict::NoEntry:
      return "no-entry";
  }
  return "?";
}

}  // namespace clockzone
