#include "SkyCard.h"

#include <Astro.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LegalLight.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "CardDraw.h"
#include "CardText.h"
#include "CardTime.h"
#include "MoonLabel.h"
#include "fontIds.h"

namespace sleepcards {
namespace skycard {
namespace {

constexpr double K_PI = 3.14159265358979323846;
constexpr double D2R = K_PI / 180.0;
constexpr int64_t HOUR_S = 3600;
// A planet rising this close to sunrise is a morning object ("up before dawn").
constexpr int64_t DAWN_WINDOW_S = 2 * HOUR_S;
}  // namespace

int64_t referenceTime(const int64_t now, const int64_t dayStart, const int64_t nextDayStart, const int64_t ref21,
                      const int64_t sunrise, const int64_t sunset) {
  // Asleep in the small hours: the night in progress is "tonight".
  const int64_t noon = dayStart + (nextDayStart - dayStart) / 2;
  if (now < noon && (sunrise != 0 ? now < sunrise : now < dayStart + 5 * HOUR_S)) return now;
  int64_t ref = ref21;
  if (sunset != 0 && sunset > noon && sunset + HOUR_S > ref) ref = sunset + HOUR_S;
  return now > ref ? now : ref;
}

void nightWindow(const int64_t ref, const int64_t dayStart, const int64_t nextDayStart, const int64_t sunrise,
                 const int64_t sunset, int64_t& start, int64_t& end) {
  const int64_t dayLen = nextDayStart - dayStart;
  if (ref < dayStart + dayLen / 2) {  // the small hours: last evening's sunset, this morning's sunrise
    start = sunset != 0 ? sunset - dayLen : ref - 3 * HOUR_S;
    end = sunrise != 0 ? sunrise : ref + 9 * HOUR_S;
  } else {  // the evening: this evening's sunset, tomorrow morning's sunrise
    start = sunset != 0 ? sunset : ref - 3 * HOUR_S;
    end = sunrise != 0 ? sunrise + dayLen : ref + 9 * HOUR_S;
  }
  if (start > ref) start = ref - 3 * HOUR_S;
  if (end <= ref) end = ref + 9 * HOUR_S;
}

int compassIndex16(const double azDeg) {
  double a = std::fmod(azDeg, 360.0);
  if (a < 0) a += 360.0;
  return static_cast<int>(std::floor(a / 22.5 + 0.5)) % 16;
}

const char* compassName(const int index16) {
  static constexpr StrId NAMES[16] = {
      StrId::STR_SKY_DIR_N, StrId::STR_SKY_DIR_NNE, StrId::STR_SKY_DIR_NE, StrId::STR_SKY_DIR_ENE,
      StrId::STR_SKY_DIR_E, StrId::STR_SKY_DIR_ESE, StrId::STR_SKY_DIR_SE, StrId::STR_SKY_DIR_SSE,
      StrId::STR_SKY_DIR_S, StrId::STR_SKY_DIR_SSW, StrId::STR_SKY_DIR_SW, StrId::STR_SKY_DIR_WSW,
      StrId::STR_SKY_DIR_W, StrId::STR_SKY_DIR_WNW, StrId::STR_SKY_DIR_NW, StrId::STR_SKY_DIR_NNW,
  };
  if (index16 < 0 || index16 >= 16) return "";
  return I18N.get(NAMES[index16]);
}

void domeProject(const double altDeg, const double azDeg, const int cx, const int cy, const int radius, int& x,
                 int& y) {
  const double rr = radius * (90.0 - altDeg) / 90.0;
  x = cx - static_cast<int>(std::lround(rr * std::sin(azDeg * D2R)));
  y = cy - static_cast<int>(std::lround(rr * std::cos(azDeg * D2R)));
}

void planetTonight(const sky::Planet p, const int64_t ref, const int64_t nightStart, const int64_t nightEnd,
                   const double latDeg, const double lonDeg, PlanetTonight& out) {
  using sky::PlanetPass;
  out = PlanetTonight{};
  out.planet = p;
  sky::PlanetTrack track;
  sky::trackInit(track, p, ref);
  double ra = 0, dec = 0;
  sky::trackRaDec(track, ref, ra, dec);
  sky::horizontalOf(ra, dec, ref, latDeg, lonDeg, &out.alt, &out.az);

  // The passes whose transits are nearest ref: yesterday's [0], the nearest [1], the next [2] and
  // the one after [3]. [1] and [2] settle almost every case: [0] only matters when neither is up
  // nor set by ref (then it may have set this evening), [3] only when [2] is not a normal pass
  // (a planet turning circumpolar or never-up far north) and so cannot be the next rising.
  constexpr int PASSES = 4;
  PlanetPass passes[PASSES];
  bool have[PASSES] = {false, true, true, false};
  const int64_t t1 = sky::trackTransitNear(track, ref, latDeg, lonDeg);
  const auto passAt = [&](const int k) {
    const int64_t transit = k == 1 ? t1 : sky::trackTransitNear(track, t1 + (k - 1) * 86164, latDeg, lonDeg);
    passes[k] = sky::trackPass(track, transit, latDeg, lonDeg);
  };
  passAt(1);
  passAt(2);

  const PlanetPass* shown = nullptr;
  const PlanetPass* next = nullptr;
  const PlanetPass* prev = nullptr;
  const auto sortPasses = [&] {
    shown = nullptr;
    next = nullptr;
    prev = nullptr;
    for (int k = 0; k < PASSES; k++) {
      if (!have[k]) continue;
      const PlanetPass& pass = passes[k];
      if (pass.kind != PlanetPass::Kind::Normal) continue;
      if (pass.rise <= ref && ref < pass.set) shown = &pass;
      if (pass.rise > ref && next == nullptr) next = &pass;
      if (pass.set <= ref) prev = &pass;
    }
  };
  sortPasses();
  bool more = false;
  if (shown == nullptr && prev == nullptr) {
    passAt(0);
    have[0] = true;
    more = true;
  }
  if (next == nullptr) {
    passAt(3);
    have[3] = true;
    more = true;
  }
  if (more) sortPasses();
  if (shown != nullptr) out.state = PlanetState::Up;

  if (shown == nullptr) {
    const PlanetPass& nearest = passes[1];
    if (nearest.kind == PlanetPass::Kind::AlwaysUp) {
      shown = &nearest;
      out.state = PlanetState::UpAllNight;
    } else if (nearest.kind == PlanetPass::Kind::NeverUp && next == nullptr && prev == nullptr) {
      shown = &nearest;
      out.state = PlanetState::NeverUp;
    } else if (next != nullptr && next->rise < nightEnd) {
      shown = next;
      out.state = next->rise >= nightEnd - DAWN_WINDOW_S ? PlanetState::BeforeDawn : PlanetState::RisesLater;
    } else if (prev != nullptr && prev->set > nightStart) {
      shown = prev;
      out.state = PlanetState::SetEarlier;
    } else {
      shown = next != nullptr ? next : (prev != nullptr ? prev : &nearest);
      out.state = PlanetState::NotTonight;
    }
  }
  out.pass = *shown;
  double alt = 0;
  sky::trackRaDec(track, out.pass.transit, ra, dec);
  sky::horizontalOf(ra, dec, out.pass.transit, latDeg, lonDeg, &alt, &out.transitAz);
  if (out.pass.kind == PlanetPass::Kind::Normal) {
    sky::trackRaDec(track, out.pass.rise, ra, dec);
    sky::horizontalOf(ra, dec, out.pass.rise, latDeg, lonDeg, &alt, &out.riseAz);
    sky::trackRaDec(track, out.pass.set, ra, dec);
    sky::horizontalOf(ra, dec, out.pass.set, latDeg, lonDeg, &alt, &out.setAz);
  }
}

double moonHorizonAlt(const int64_t t, const double latDeg, const double lonDeg) {
  return -(REFRACTION_DEG + astroMoonSemidiameter(t, latDeg, lonDeg));
}

void moonPassFromEvents(const int64_t rises[2], const int64_t sets[2], const int64_t ref, const bool upAtRef,
                        int64_t& rise, int64_t& set) {
  rise = 0;
  set = 0;
  if (upAtRef) {
    for (int i = 0; i < 2; i++) {
      if (rises[i] != 0 && rises[i] <= ref && rises[i] > rise) rise = rises[i];
      if (sets[i] != 0 && sets[i] > ref && (set == 0 || sets[i] < set)) set = sets[i];
    }
    return;
  }
  for (int i = 0; i < 2; i++) {
    if (rises[i] != 0 && rises[i] > ref && (rise == 0 || rises[i] < rise)) rise = rises[i];
  }
  if (rise == 0) return;
  for (int i = 0; i < 2; i++) {
    if (sets[i] != 0 && sets[i] > rise && (set == 0 || sets[i] < set)) set = sets[i];
  }
}

bool computeSkyFacts(const CardContext& ctx, SkyFacts& out) {
  out = SkyFacts{};
  if (!ctx.timeValid || !plausibleTime(ctx.utcNow) || ctx.utcOffsetAt == nullptr) return false;
  const int64_t now = ctx.utcNow;
  const LocalDate today = localDateOf(now, ctx.utcOffsetAt);
  const int64_t dayStart = localDayStart(today.year, today.month, today.day, ctx.utcOffsetAt);
  const int64_t nextDayStart = localNextDayStart(today.year, today.month, today.day, ctx.utcOffsetAt);
  int64_t ref21 = dayStart + 21 * HOUR_S;
  ref21 += ctx.utcOffsetAt(dayStart) - ctx.utcOffsetAt(ref21);  // 21:00 on the wall, also on a DST day

  out.hasPlace = ctx.location.valid;
  out.lat = ctx.location.lat;
  out.lon = ctx.location.lon;

  AstroSunDay sun{};
  if (out.hasPlace) astroSunDay(dayStart, out.lat, out.lon, &sun);
  out.ref = referenceTime(now, dayStart, nextDayStart, ref21, sun.rise, sun.set);
  out.date = localDateOf(out.ref, ctx.utcOffsetAt);
  // The evening's date, also when the reference slides past midnight (a late sunset + 1 h).
  if (out.ref >= nextDayStart && now < nextDayStart) out.date = today;

  // The moon's phase is geocentric: no place needed.
  AstroMoonPhase phase{};
  astroMoonPhaseWith(out.ref, 0, &phase);
  out.moonIllum = phase.illum;
  out.moonWaxing = phase.sep > 0;
  const int rawPhase = (phase.phase >= 0 && phase.phase < 8) ? phase.phase : 0;
  const auto onDate = [&](const int64_t t) {
    if (t <= 0) return false;
    const LocalDate d = localDateOf(t, ctx.utcOffsetAt);
    return d.year == out.date.year && d.month == out.date.month && d.day == out.date.day;
  };
  if (!out.hasPlace) out.nextFullMoon = astroNextMoonPhase(out.ref, 2);
  if (rawPhase == 4) {
    // Only inside the full band: is the full moon itself on the evening's date?
    out.fullMoonToday = onDate(out.nextFullMoon != 0 ? out.nextFullMoon : astroNextMoonPhase(out.ref, 2)) ||
                        onDate(astroPrevMoonPhase(out.ref, 2));
  }
  out.moonPhase = moonlabel::displayPhase(rawPhase, out.moonWaxing, out.fullMoonToday);
  if (!out.hasPlace) return true;

  // The moon's pass tonight from two local days' events: the day of ref and the one after (the
  // evening) or before (the small hours).
  astroMoonPos(out.ref, out.lat, out.lon, &out.moonAlt, &out.moonAz);
  astroSunPos(out.ref, out.lat, out.lon, &out.sunAlt, &out.sunAz);
  const LocalDate refDate = localDateOf(out.ref, ctx.utcOffsetAt);
  const int64_t refDayStart = localDayStart(refDate.year, refDate.month, refDate.day, ctx.utcOffsetAt);
  out.moonHorizonAlt = moonHorizonAlt(out.ref, out.lat, out.lon);
  const bool upAtRef = out.moonAlt > out.moonHorizonAlt;
  AstroMoonDay m0{};
  astroMoonDay(refDayStart, out.lat, out.lon, &m0);
  int64_t rises[2] = {m0.rise, 0};
  int64_t sets[2] = {m0.set, 0};
  moonPassFromEvents(rises, sets, out.ref, upAtRef, out.moonRise, out.moonSet);
  // The neighbouring 24 hours only when the day of ref leaves the pass unfinished (a moon day is
  // ~3,650 libm calls; with both ends found in the day of ref, the other window cannot change
  // them). The window is the adjoining 86400 s, not the neighbouring local day, so nothing is
  // skipped or scanned twice when the local day is 23 or 25 hours long.
  if (out.moonRise == 0 || out.moonSet == 0) {
    const bool evening = out.ref - refDayStart >= 12 * HOUR_S;
    const int64_t otherStart = refDayStart + (evening ? 86400 : -86400);
    AstroMoonDay m1{};
    astroMoonDay(otherStart, out.lat, out.lon, &m1);
    rises[1] = m1.rise;
    sets[1] = m1.set;
    moonPassFromEvents(rises, sets, out.ref, upAtRef, out.moonRise, out.moonSet);
  }

  int64_t nightStart = 0, nightEnd = 0;
  nightWindow(out.ref, dayStart, nextDayStart, sun.rise, sun.set, nightStart, nightEnd);
  for (int i = 0; i < sky::PLANET_COUNT; i++) {
    planetTonight(static_cast<sky::Planet>(i), out.ref, nightStart, nightEnd, out.lat, out.lon, out.planets[i]);
  }
  return true;
}

}  // namespace skycard

// ---- drawing ------------------------------------------------------------------------------------

namespace {

using skycard::PlanetState;
using skycard::PlanetTonight;
using skycard::SkyFacts;

constexpr double DRAW_D2R = 3.14159265358979323846 / 180.0;
constexpr int W = 480;
constexpr int CX = W / 2;
constexpr int CONTENT_W = W - 2 * SCREEN_MARGIN;

// The header.
constexpr int HEADER_Y = 10;
constexpr int DATE_Y = 52;

// The dome.
constexpr int DOME_R = 110;
constexpr int DOME_CY = 232;
constexpr double OBLIQUITY_DEG = 23.44;  // for the ecliptic's path across the dome
constexpr int PLANET_DOT_R = 5;
constexpr int DOME_MOON_R = 11;
constexpr int SUN_R = 6;
constexpr double SUN_UP_ALT = -0.8333;  // the upper limb on the 34' horizon (Astro.h)

// The moon block and the planet table.
constexpr int MOON_BLOCK_Y = 392;
constexpr int MOON_R = 32;
constexpr int TABLE_Y = 490;
constexpr int ROW_H = 54;
constexpr int NAME_X = SCREEN_MARGIN + 20;
constexpr int COL_RISE = 250;
constexpr int COL_HIGH = 338;
constexpr int COL_SET = 426;
constexpr int COL_HALF = 42;  // half a time column

const char* planetName(const sky::Planet p) {
  switch (p) {
    case sky::Planet::Venus:
      return tr(STR_SKY_VENUS);
    case sky::Planet::Mars:
      return tr(STR_SKY_MARS);
    case sky::Planet::Jupiter:
      return tr(STR_SKY_JUPITER);
    case sky::Planet::Saturn:
    default:
      return tr(STR_SKY_SATURN);
  }
}

const char* phaseName(const int phase) {
  switch (phase) {
    case 1:
      return tr(STR_SKY_PHASE_WAXING_CRESCENT);
    case 2:
      return tr(STR_SKY_PHASE_FIRST_QUARTER);
    case 3:
      return tr(STR_SKY_PHASE_WAXING_GIBBOUS);
    case 4:
      return tr(STR_SKY_PHASE_FULL);
    case 5:
      return tr(STR_SKY_PHASE_WANING_GIBBOUS);
    case 6:
      return tr(STR_SKY_PHASE_LAST_QUARTER);
    case 7:
      return tr(STR_SKY_PHASE_WANING_CRESCENT);
    case 0:
    default:
      return tr(STR_SKY_PHASE_NEW);
  }
}

void clockText(const CardContext& ctx, const int64_t utc, char* out, const size_t cap) {
  if (utc == 0) {
    std::snprintf(out, cap, "--:--");
    return;
  }
  formatClock(almanac::roundToNearestMinute(utc), ctx.utcOffsetAt, ctx.clock12h, out, cap);
}

// The planet's mark, the same on the dome and in the table: a dot, Saturn with its ring.
void drawPlanetMark(GfxRenderer& r, const sky::Planet p, const int x, const int y, const bool filled = true) {
  if (p == sky::Planet::Saturn) {
    // A flat ring through the disc, cleared where it crosses so it reads as a ring, not a blob.
    draw::drawEllipse(r, x, y, PLANET_DOT_R + 6, 2, 1);
    draw::fillCircle(r, x, y, PLANET_DOT_R + 1, false);
  }
  if (filled) {
    draw::fillCircle(r, x, y, PLANET_DOT_R);
  } else {
    draw::drawCircle(r, x, y, PLANET_DOT_R, 2);
  }
}

// The sun on the dome: a disc ringed with rays, cleared behind so the rings do not cross it.
void drawSunMark(GfxRenderer& r, const int x, const int y) {
  draw::fillCircle(r, x, y, SUN_R + 6, false);
  draw::drawCircle(r, x, y, SUN_R, 2);
  for (int k = 0; k < 8; k++) {
    const double a = k * 45.0 * DRAW_D2R;
    r.drawLine(x + static_cast<int>(std::lround((SUN_R + 2) * std::cos(a))),
               y + static_cast<int>(std::lround((SUN_R + 2) * std::sin(a))),
               x + static_cast<int>(std::lround((SUN_R + 5) * std::cos(a))),
               y + static_cast<int>(std::lround((SUN_R + 5) * std::sin(a))), true);
  }
}

// Dotted circle (an altitude ring on the dome): one dot every `stepDeg`.
void drawDottedCircle(GfxRenderer& r, const int cx, const int cy, const int radius, const double stepDeg) {
  for (double a = 0; a < 360.0; a += stepDeg) {
    r.drawPixel(cx + static_cast<int>(std::lround(radius * std::cos(a * DRAW_D2R))),
                cy + static_cast<int>(std::lround(radius * std::sin(a * DRAW_D2R))), true);
  }
}

void drawHeader(const CardContext& ctx, GfxRenderer& r, const SkyFacts& f) {
  draw::drawTextCenteredAt(r, NOTOSANS_16_FONT_ID, CX, HEADER_Y, tr(STR_SLEEP_SKY), true, EpdFontFamily::BOLD);
  char line[80];
  if (f.hasPlace) {
    char clock[16];
    clockText(ctx, f.ref, clock, sizeof(clock));
    std::snprintf(line, sizeof(line), tr(STR_SKY_DATE_AT), weekdayName(f.date.weekday), monthName(f.date.month),
                  f.date.day, clock);
  } else {
    std::snprintf(line, sizeof(line), tr(STR_SKY_DATE), weekdayName(f.date.weekday), monthName(f.date.month),
                  f.date.day);
  }
  draw::drawTextCenteredAt(r, UI_10_FONT_ID, CX, DATE_Y, line);
}

struct LabelBox {
  int x0, y0, x1, y1;
};

bool overlaps(const LabelBox& a, const LabelBox& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

// A body's name next to its mark on the dome, on the side with room: right, left, below, above.
void placeLabel(GfxRenderer& r, const char* text, const int x, const int y, const int clearance, LabelBox* placed,
                int& count, const int cap) {
  const int w = r.getTextWidth(SMALL_FONT_ID, text);
  const int h = r.getLineHeight(SMALL_FONT_ID);
  // Prefer the side away from the dome's edge so the label stays inside the sky; in a crowd, step
  // further out (with a leader line back to the mark) before giving up and overlapping.
  const bool eastHalf = x > CX + DOME_R / 3;
  constexpr int STEPS = 3;
  constexpr int STEP_PX = 14;
  LabelBox chosen{};
  bool found = false;
  int chosenStep = 0;
  for (int step = 0; step < STEPS && !found; step++) {
    const int gap = clearance + 3 + step * STEP_PX;
    const LabelBox options[4] = {
        {x + gap, y - h / 2, x + gap + w, y + h / 2},
        {x - gap - w, y - h / 2, x - gap, y + h / 2},
        {x - w / 2, y + gap, x + w / 2, y + gap + h},
        {x - w / 2, y - gap - h, x + w / 2, y - gap},
    };
    const int order[4] = {eastHalf ? 1 : 0, eastHalf ? 0 : 1, 2, 3};
    if (step == 0) chosen = options[order[0]];
    for (const int o : order) {
      const LabelBox& box = options[o];
      bool free = box.x0 >= 0 && box.x1 <= W;
      for (int i = 0; i < count && free; i++) {
        if (overlaps(box, placed[i])) free = false;
      }
      if (free) {
        chosen = box;
        chosenStep = step;
        found = true;
        break;
      }
    }
  }
  if (chosenStep > 0) {
    // A leader from the mark's edge to the nearest point of the label.
    const int lx = x < chosen.x0 ? chosen.x0 - 2 : (x > chosen.x1 ? chosen.x1 + 2 : x);
    const int ly = y < chosen.y0 ? chosen.y0 - 1 : (y > chosen.y1 ? chosen.y1 + 1 : y);
    const double dx = lx - x, dy = ly - y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len > clearance + 2) {
      r.drawLine(x + static_cast<int>(dx / len * (clearance + 2)), y + static_cast<int>(dy / len * (clearance + 2)), lx,
                 ly, true);
    }
  }
  // A white pad keeps the name readable where it crosses the ecliptic or a ring.
  r.fillRect(chosen.x0 - 1, chosen.y0, chosen.x1 - chosen.x0 + 2, chosen.y1 - chosen.y0, false);
  r.drawText(SMALL_FONT_ID, chosen.x0, chosen.y0, text);
  if (count < cap) placed[count++] = chosen;
}

// The ecliptic (the path of the sun, the moon and the planets) above the horizon, dashed.
void drawEcliptic(GfxRenderer& r, const SkyFacts& f) {
  constexpr double STEP = 4.0;
  const double se = std::sin(OBLIQUITY_DEG * DRAW_D2R), ce = std::cos(OBLIQUITY_DEG * DRAW_D2R);
  int px = 0, py = 0;
  bool prevUp = false;
  int k = 0;
  for (double lam = 0; lam <= 360.0; lam += STEP, k++) {
    const double sl = std::sin(lam * DRAW_D2R), cl = std::cos(lam * DRAW_D2R);
    const double ra = std::atan2(sl * ce, cl) / DRAW_D2R;
    const double dec = std::asin(sl * se) / DRAW_D2R;
    double alt = 0, az = 0;
    sky::horizontalOf(ra, dec, f.ref, f.lat, f.lon, &alt, &az);
    int x = 0, y = 0;
    skycard::domeProject(alt, az, CX, DOME_CY, DOME_R, x, y);
    const bool up = alt > 0.5;
    if (up && prevUp && (k % 2 == 0)) r.drawLine(px, py, x, y, true);
    px = x;
    py = y;
    prevUp = up;
  }
}

void drawDome(const CardContext& ctx, GfxRenderer& r, const SkyFacts& f) {
  (void)ctx;
  const int cx = CX;
  const int cy = DOME_CY;
  // Altitude rings at 30 and 60 degrees, the horizon, the zenith.
  drawDottedCircle(r, cx, cy, DOME_R * 2 / 3, 3.0);
  drawDottedCircle(r, cx, cy, DOME_R / 3, 6.0);
  draw::drawCircle(r, cx, cy, DOME_R, 2);
  r.drawLine(cx - 4, cy, cx + 4, cy, true);
  r.drawLine(cx, cy - 4, cx, cy + 4, true);
  drawEcliptic(r, f);
  // Ticks every 45 degrees of azimuth, outside the horizon.
  for (int k = 0; k < 8; k++) {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const double az = k * 45.0;
    const int len = (k % 2 == 0) ? 8 : 5;
    skycard::domeProject(0.0, az, cx, cy, DOME_R + 2, x0, y0);
    skycard::domeProject(0.0, az, cx, cy, DOME_R + 2 + len, x1, y1);
    r.drawLine(x0, y0, x1, y1, 2, true);
  }
  // N up, E on the left (a map of the sky overhead).
  const int lh = r.getLineHeight(UI_12_FONT_ID);
  const int gap = 12;
  draw::drawTextCenteredAt(r, UI_12_FONT_ID, cx, cy - DOME_R - gap - lh, skycard::compassName(0), true,
                           EpdFontFamily::BOLD);
  draw::drawTextCenteredAt(r, UI_12_FONT_ID, cx, cy + DOME_R + gap - 2, skycard::compassName(8), true,
                           EpdFontFamily::BOLD);
  draw::drawTextRight(r, UI_12_FONT_ID, cx - DOME_R - gap, cy - lh / 2, skycard::compassName(4), true,
                      EpdFontFamily::BOLD);
  r.drawText(UI_12_FONT_ID, cx + DOME_R + gap, cy - lh / 2, skycard::compassName(12), true, EpdFontFamily::BOLD);

  // The bodies above the horizon: every mark first (the labels then avoid all of them).
  constexpr int LABEL_CAP = 4 + 2 + 2 * sky::PLANET_COUNT;  // N E S W, the sun, the moon, marks and names
  LabelBox placed[LABEL_CAP];
  int count = 0;
  const int lw = r.getTextWidth(UI_12_FONT_ID, "W", EpdFontFamily::BOLD) + 4;
  placed[count++] = {cx - lw, cy - DOME_R - gap - lh, cx + lw, cy - DOME_R - 2};
  placed[count++] = {cx - lw, cy + DOME_R + 2, cx + lw, cy + DOME_R + gap + lh};
  placed[count++] = {cx - DOME_R - gap - 2 * lw, cy - lh / 2, cx - DOME_R - 2, cy + lh / 2};
  placed[count++] = {cx + DOME_R + 2, cy - lh / 2, cx + DOME_R + gap + 2 * lw, cy + lh / 2};
  int bodies = 0;
  if (f.sunAlt > SUN_UP_ALT) {
    int x = 0, y = 0;
    skycard::domeProject(f.sunAlt < 0 ? 0 : f.sunAlt, f.sunAz, cx, cy, DOME_R, x, y);
    drawSunMark(r, x, y);
    placed[count++] = {x - SUN_R - 5, y - SUN_R - 5, x + SUN_R + 5, y + SUN_R + 5};
  }
  // The moon first and the planets over it: a planet in conjunction stays visible on the moon.
  if (f.moonAlt > f.moonHorizonAlt) {
    int x = 0, y = 0;
    skycard::domeProject(f.moonAlt < 0 ? 0 : f.moonAlt, f.moonAz, cx, cy, DOME_R, x, y);
    // The lit limb faces the sun. On this dome west is on the right in either hemisphere, so an
    // evening moon is lit on the right here even where the moon block below (drawn as seen from
    // the southern hemisphere) shows it lit on the left. The sun's side comes from its own
    // projection, below the horizon too; straight above or below it, the waxing rule.
    int sx = 0, sy = 0;
    skycard::domeProject(f.sunAlt, f.sunAz, cx, cy, DOME_R, sx, sy);
    const bool litRight = sx != x ? sx > x : f.moonWaxing;
    draw::fillCircle(r, x, y, DOME_MOON_R + 2, false);
    draw::drawMoon(r, x, y, DOME_MOON_R, f.moonIllum, litRight, false);
    if (count < LABEL_CAP) placed[count++] = {x - DOME_MOON_R, y - DOME_MOON_R, x + DOME_MOON_R, y + DOME_MOON_R};
  }
  for (const PlanetTonight& p : f.planets) {
    if (p.state != PlanetState::Up && p.state != PlanetState::UpAllNight) continue;
    int x = 0, y = 0;
    skycard::domeProject(p.alt < 0 ? 0 : p.alt, p.az, cx, cy, DOME_R, x, y);
    // A 1 px halo: enough to part a planet from the ecliptic, too little to hide the moon under it.
    draw::fillCircle(r, x, y, PLANET_DOT_R + 1, false);
    drawPlanetMark(r, p.planet, x, y);
    if (count < LABEL_CAP)
      placed[count++] = {x - PLANET_DOT_R - 6, y - PLANET_DOT_R, x + PLANET_DOT_R + 6, y + PLANET_DOT_R};
    bodies++;
  }
  // Labels after every mark, so none is drawn over a later mark.
  for (const PlanetTonight& p : f.planets) {
    if (p.state != PlanetState::Up && p.state != PlanetState::UpAllNight) continue;
    int x = 0, y = 0;
    skycard::domeProject(p.alt < 0 ? 0 : p.alt, p.az, cx, cy, DOME_R, x, y);
    // Saturn's ring reaches PLANET_DOT_R + 6 either side: its name starts clear of it.
    placeLabel(r, planetName(p.planet), x, y, PLANET_DOT_R + (p.planet == sky::Planet::Saturn ? 8 : 0), placed, count,
               LABEL_CAP);
  }
  if (bodies == 0) {
    draw::drawTextCenteredAt(r, SMALL_FONT_ID, cx, cy + DOME_R / 2 - r.getLineHeight(SMALL_FONT_ID) / 2,
                             tr(STR_SKY_NOTHING_UP));
  }
}

// Returns the block's bottom (its text may run past the moon on a second moonrise/moonset line).
int drawMoonBlock(const CardContext& ctx, GfxRenderer& r, const SkyFacts& f, const int y) {
  const int mx = SCREEN_MARGIN + MOON_R;
  // As seen from where the user is: lit on the left in the southern hemisphere for a waxing moon.
  draw::drawMoon(r, mx, y + MOON_R, MOON_R, f.moonIllum, f.moonWaxing, f.lat < 0);
  const int tx = SCREEN_MARGIN + 2 * MOON_R + 22;
  int ty = y - 4;
  r.drawText(NOTOSANS_14_FONT_ID, tx, ty, phaseName(f.moonPhase), true, EpdFontFamily::BOLD);
  ty += r.getLineHeight(NOTOSANS_14_FONT_ID);
  char line[64];
  std::snprintf(line, sizeof(line), tr(STR_SKY_ILLUMINATED),
                moonlabel::illuminationPercent(f.moonIllum, f.fullMoonToday));
  r.drawText(UI_10_FONT_ID, tx, ty, line);
  ty += r.getLineHeight(UI_10_FONT_ID) + 2;
  // The pass in chronological order: "Moonrise 22:14   Moonset 12:03".
  char rise[16], set[16], a[40], b[40];
  clockText(ctx, f.moonRise, rise, sizeof(rise));
  clockText(ctx, f.moonSet, set, sizeof(set));
  std::snprintf(a, sizeof(a), tr(STR_SKY_MOONRISE), rise);
  std::snprintf(b, sizeof(b), tr(STR_SKY_MOONSET), set);
  const bool setFirst = f.moonRise == 0 || (f.moonSet != 0 && f.moonSet < f.moonRise);
  const bool both = f.moonRise != 0 && f.moonSet != 0;
  const int moonBottom = y + 2 * MOON_R;
  if (f.moonRise == 0 && f.moonSet == 0) return std::max(moonBottom, ty);
  const char* first = setFirst ? b : a;
  const char* second = setFirst ? a : b;
  // The next event in bold, the other after it; on a second line when they do not fit on one
  // (a 12-hour clock), never in a smaller face (the small font has no bold).
  const int maxW = W - SCREEN_MARGIN - tx;
  constexpr int GAP = 14;
  const int font = UI_10_FONT_ID;
  const int firstW = r.getTextWidth(font, first, EpdFontFamily::BOLD);
  r.drawText(font, tx, ty, first, true, EpdFontFamily::BOLD);
  ty += r.getLineHeight(font);
  if (both) {
    if (firstW + GAP + r.getTextWidth(font, second) <= maxW) {
      r.drawText(font, tx + firstW + GAP, ty - r.getLineHeight(font), second);
    } else {
      r.drawText(font, tx, ty, second);
      ty += r.getLineHeight(font);
    }
  }
  return std::max(moonBottom, ty);
}

// A time with a direction under it (a table cell), centred on cx. With the 12-hour clock the
// AM/PM follows the time on its baseline in the small face ("10:43 PM"), so three fit side by side.
void drawCell(GfxRenderer& r, const int cx, const int y, const char* time, const char* suffix, const char* below) {
  const int timeW = r.getTextWidth(UI_10_FONT_ID, time, EpdFontFamily::BOLD);
  constexpr int SUFFIX_GAP = 3;
  const int suffixW = suffix != nullptr && suffix[0] != '\0' ? SUFFIX_GAP + r.getTextWidth(SMALL_FONT_ID, suffix) : 0;
  const int x = cx - (timeW + suffixW) / 2;
  r.drawText(UI_10_FONT_ID, x, y, time, true, EpdFontFamily::BOLD);
  if (suffixW > 0) {
    const int baselineShift = r.getFontAscenderSize(UI_10_FONT_ID) - r.getFontAscenderSize(SMALL_FONT_ID);
    r.drawText(SMALL_FONT_ID, x + timeW + SUFFIX_GAP, y + baselineShift, suffix);
  }
  if (below != nullptr && below[0] != '\0') {
    draw::drawTextCenteredAt(r, SMALL_FONT_ID, cx, y + r.getLineHeight(UI_10_FONT_ID), below);
  }
}

void drawTimeCell(const CardContext& ctx, GfxRenderer& r, const int cx, const int y, const int64_t utc,
                  const char* below) {
  char t[16];
  char suffix[8] = "";
  if (utc == 0) {
    std::snprintf(t, sizeof(t), "--:--");
  } else {
    const LocalDate d = localDateOf(almanac::roundToNearestMinute(utc), ctx.utcOffsetAt);
    formatHourMinuteParts(d.hour, d.minute, ctx.clock12h, t, sizeof(t), suffix, sizeof(suffix));
  }
  drawCell(r, cx, y, t, suffix, below);
}

void drawPlanetRow(const CardContext& ctx, GfxRenderer& r, const PlanetTonight& p, const int y) {
  const bool up = p.state == PlanetState::Up || p.state == PlanetState::UpAllNight;
  drawPlanetMark(r, p.planet, SCREEN_MARGIN + 6, y + r.getLineHeight(UI_12_FONT_ID) / 2, up);
  r.drawText(UI_12_FONT_ID, NAME_X, y, planetName(p.planet), true, EpdFontFamily::BOLD);

  char status[48];
  const int altDeg = static_cast<int>(std::lround(p.alt < 0 ? 0 : p.alt));
  switch (p.state) {
    case PlanetState::Up:
      std::snprintf(status, sizeof(status), tr(STR_SKY_UP_AT), skycard::compassName(skycard::compassIndex16(p.az)),
                    altDeg);
      break;
    case PlanetState::UpAllNight:
      std::snprintf(status, sizeof(status), tr(STR_SKY_UP_ALL_NIGHT),
                    skycard::compassName(skycard::compassIndex16(p.az)), altDeg);
      break;
    case PlanetState::RisesLater:
      std::snprintf(status, sizeof(status), "%s", tr(STR_SKY_RISES_LATER));
      break;
    case PlanetState::BeforeDawn:
      std::snprintf(status, sizeof(status), "%s", tr(STR_SKY_BEFORE_DAWN));
      break;
    case PlanetState::SetEarlier:
      std::snprintf(status, sizeof(status), "%s", tr(STR_SKY_SET_EARLIER));
      break;
    case PlanetState::NotTonight:
      std::snprintf(status, sizeof(status), "%s", tr(STR_SKY_NOT_TONIGHT));
      break;
    case PlanetState::NeverUp:
    default:
      std::snprintf(status, sizeof(status), "%s", tr(STR_SKY_NEVER_UP));
      break;
  }
  const int statusW = COL_RISE - COL_HALF - NAME_X - 6;
  draw::drawWrapped(r, SMALL_FONT_ID, NAME_X, y + r.getLineHeight(UI_12_FONT_ID) - 1, statusW, status, 1);

  const bool normal = p.pass.kind == sky::PlanetPass::Kind::Normal;
  const bool never = p.pass.kind == sky::PlanetPass::Kind::NeverUp;
  const int cellY = y + 1;
  if (normal) {
    drawTimeCell(ctx, r, COL_RISE, cellY, p.pass.rise, skycard::compassName(skycard::compassIndex16(p.riseAz)));
    drawTimeCell(ctx, r, COL_SET, cellY, p.pass.set, skycard::compassName(skycard::compassIndex16(p.setAz)));
  } else {
    drawCell(r, COL_RISE, cellY, "-", nullptr, nullptr);
    drawCell(r, COL_SET, cellY, "-", nullptr, nullptr);
  }
  if (never) {
    drawCell(r, COL_HIGH, cellY, "-", nullptr, nullptr);
  } else {
    char where[24];
    std::snprintf(where, sizeof(where), "%s %d\xC2\xB0", skycard::compassName(skycard::compassIndex16(p.transitAz)),
                  static_cast<int>(std::lround(p.pass.transitAlt)));
    drawTimeCell(ctx, r, COL_HIGH, cellY, p.pass.transit, where);
  }
}

void drawPlanetTable(const CardContext& ctx, GfxRenderer& r, const SkyFacts& f, const int top) {
  r.drawLine(SCREEN_MARGIN, top, W - SCREEN_MARGIN, top, true);
  const int hy = top + 8;
  draw::drawTextCenteredAt(r, SMALL_FONT_ID, COL_RISE, hy, tr(STR_SKY_RISES));
  draw::drawTextCenteredAt(r, SMALL_FONT_ID, COL_HIGH, hy, tr(STR_SKY_HIGHEST));
  draw::drawTextCenteredAt(r, SMALL_FONT_ID, COL_SET, hy, tr(STR_SKY_SETS));
  int y = hy + r.getLineHeight(SMALL_FONT_ID) + 6;
  // The rows close up a little when the moon block above ran long; the footer stays clear.
  const int footerTop = r.getScreenHeight() - FOOTER_HEIGHT - 4;
  const int rowH = std::min(ROW_H, (footerTop - y) / sky::PLANET_COUNT);
  for (const PlanetTonight& p : f.planets) {
    drawPlanetRow(ctx, r, p, y);
    y += rowH;
  }
}

// No location: the moon's phase alone, large, and where to set the place.
void drawMoonOnly(const CardContext& ctx, GfxRenderer& r, const SkyFacts& f) {
  constexpr int BIG_MOON_R = 100;
  constexpr int MOON_CY = 268;
  draw::drawMoon(r, CX, MOON_CY, BIG_MOON_R, f.moonIllum, f.moonWaxing, false);
  int y = MOON_CY + BIG_MOON_R + 24;
  draw::drawTextCenteredAt(r, NOTOSANS_18_FONT_ID, CX, y, phaseName(f.moonPhase), true, EpdFontFamily::BOLD);
  y += r.getLineHeight(NOTOSANS_18_FONT_ID) + 2;
  char line[64];
  std::snprintf(line, sizeof(line), tr(STR_SKY_ILLUMINATED),
                moonlabel::illuminationPercent(f.moonIllum, f.fullMoonToday));
  draw::drawTextCenteredAt(r, UI_12_FONT_ID, CX, y, line);
  y += r.getLineHeight(UI_12_FONT_ID) + 6;
  if (f.nextFullMoon != 0) {
    const LocalDate d = localDateOf(f.nextFullMoon, ctx.utcOffsetAt);
    char date[40];
    std::snprintf(date, sizeof(date), tr(STR_SKY_DATE), weekdayShortName(d.weekday), monthShortName(d.month), d.day);
    std::snprintf(line, sizeof(line), tr(STR_SKY_NEXT_FULL), date);
    draw::drawTextCenteredAt(r, UI_12_FONT_ID, CX, y, line);
  }
  y += r.getLineHeight(UI_12_FONT_ID);

  constexpr int BOX_H = 96;
  const int boxY = y + 48;
  r.drawRoundedRect(SCREEN_MARGIN, boxY, CONTENT_W, BOX_H, 2, 10, true);
  draw::drawTextCenteredAt(r, UI_12_FONT_ID, CX, boxY + 18, tr(STR_SET_LOCATION_IN_SETTINGS), true,
                           EpdFontFamily::BOLD);
  draw::drawWrapped(r, UI_10_FONT_ID, SCREEN_MARGIN + 16, boxY + 50, CONTENT_W - 32, tr(STR_SKY_NEEDS_LOCATION), 2,
                    draw::Align::Center);
}

}  // namespace

bool renderSkyCard(const CardContext& ctx, GfxRenderer& renderer) {
  // SkyFacts (~490 B) exceeds the stack budget.
  auto factsOwner = makeUniqueNoThrow<skycard::SkyFacts>();
  if (!factsOwner) {
    LOG_ERR("CARD", "OOM: sky facts");
    return false;
  }
  skycard::SkyFacts& facts = *factsOwner;
  if (!skycard::computeSkyFacts(ctx, facts)) return false;
  drawHeader(ctx, renderer, facts);
  if (!facts.hasPlace) {
    drawMoonOnly(ctx, renderer, facts);
    return true;
  }
  drawDome(ctx, renderer, facts);
  const int moonBottom = drawMoonBlock(ctx, renderer, facts, MOON_BLOCK_Y);
  drawPlanetTable(ctx, renderer, facts, std::max(TABLE_Y, moonBottom + 6));
  return true;
}

}  // namespace sleepcards
