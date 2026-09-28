#include "DayCard.h"

#include <Astro.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LegalLight.h>

#include <cmath>
#include <cstdio>

#include "CardDraw.h"
#include "CardText.h"
#include "CardTime.h"
#include "MoonLabel.h"
#include "fontIds.h"
#include "images/CardDigits.h"

namespace sleepcards {
namespace daycard {
namespace {

constexpr double SUN_RISE_ALT = -0.8333;  // the upper limb on the 34' horizon (Astro.h)

LocalDate nextDate(const LocalDate& d) {
  LocalDate n;
  civilFromDays(daysFromCivil(d.year, d.month, d.day) + 1, n.year, n.month, n.day);
  n.weekday = weekdayOf(n.year, n.month, n.day);
  return n;
}

bool legalEnded(const almanac::LegalWindow& w, const int64_t now) {
  using Kind = almanac::LegalWindow::Kind;
  return (w.kind == Kind::Window || w.kind == Kind::UntilOnly) && now > w.last;
}

// The sun block's facts from one local day's events.
void fillSun(DayFacts& out, const AstroSunDay& sun, const int64_t dayStart, const double lat, const double lon) {
  out.sunrise = sun.rise ? almanac::roundToNearestMinute(sun.rise) : 0;
  out.sunset = sun.set ? almanac::roundToNearestMinute(sun.set) : 0;
  out.dayLengthS = -1;
  if (sun.rise && sun.set) {
    out.sunKind = SunKind::Normal;
    if (sun.set > sun.rise) out.dayLengthS = sun.set - sun.rise;
  } else if (sun.rise) {
    out.sunKind = SunKind::RiseOnly;
  } else if (sun.set) {
    out.sunKind = SunKind::SetOnly;
  } else {
    double alt = 0;
    double az = 0;
    astroSunPos(sun.noon ? sun.noon : dayStart + 43200, lat, lon, &alt, &az);
    out.sunKind = alt > SUN_RISE_ALT ? SunKind::UpAllDay : SunKind::DownAllDay;
  }
}

}  // namespace

bool computeDayFacts(const CardContext& ctx, DayFacts& out) {
  out = DayFacts{};
  if (!ctx.timeValid || !plausibleTime(ctx.utcNow) || ctx.utcOffsetAt == nullptr) return false;
  const int64_t now = ctx.utcNow;
  out.date = localDateOf(now, ctx.utcOffsetAt);
  out.dayStart = localDayStart(out.date.year, out.date.month, out.date.day, ctx.utcOffsetAt);

  // The moon needs no place: its phase is geocentric. No age (that would be a second search).
  AstroMoonPhase phase{};
  astroMoonPhaseWith(now, 0, &phase);
  out.moonIllum = phase.illum;
  out.moonWaxing = phase.sep > 0;
  const int rawPhase = (phase.phase >= 0 && phase.phase < 8) ? phase.phase : 0;
  out.nextFullMoon = astroNextMoonPhase(now, 2);
  if (out.nextFullMoon > 0) {
    out.nextFullDate = localDateOf(out.nextFullMoon, ctx.utcOffsetAt);
    out.daysToFullMoon =
        static_cast<int>(daysFromCivil(out.nextFullDate.year, out.nextFullDate.month, out.nextFullDate.day) -
                         daysFromCivil(out.date.year, out.date.month, out.date.day));
  }
  out.fullMoonToday = out.daysToFullMoon == 0;
  if (!out.fullMoonToday && rawPhase == 4) {
    // Inside the full band but not before today's full moon: was it earlier today?
    const int64_t prev = astroPrevMoonPhase(now, 2);
    if (prev > 0) {
      const LocalDate d = localDateOf(prev, ctx.utcOffsetAt);
      out.fullMoonToday = d.year == out.date.year && d.month == out.date.month && d.day == out.date.day;
    }
  }
  out.moonPhase = moonlabel::displayPhase(rawPhase, out.moonWaxing, out.fullMoonToday);

  if (!ctx.location.valid) return true;
  const double lat = ctx.location.lat;
  const double lon = ctx.location.lon;
  out.moonSouthern = lat < 0;

  AstroSunDay sun{};
  astroSunDay(out.dayStart, lat, lon, &sun);
  fillSun(out, sun, out.dayStart, lat, lon);

  // Legal light: today's window until it has ended, then tomorrow's (the evening before a hunt
  // is when the next morning matters) - each only on a day the Hunting Season setting covers.
  // With tomorrow's window the sun block shows tomorrow's sun too, so the two agree.
  const SleepCardSettings& s = ctx.settings;
  if (s.huntMode == HuntMode::Off) return true;
  out.legalRule = s.legalRule == LegalLightRule::CivilTwilight ? almanac::LegalRule::CivilTwilight
                                                               : almanac::LegalRule::ThirtyMinutes;
  const almanac::LegalWindow today = almanac::legalWindow(sun, out.dayStart, lat, lon, out.legalRule);
  if (!legalEnded(today, now)) {
    if (huntingSeasonOn(s, out.date.year, out.date.month, out.date.day)) {
      out.legalDay = LegalDay::Today;
      out.legal = today;
    }
    return true;
  }
  const LocalDate tomorrow = nextDate(out.date);
  if (!huntingSeasonOn(s, tomorrow.year, tomorrow.month, tomorrow.day)) return true;
  const int64_t tomorrowStart = localNextDayStart(out.date.year, out.date.month, out.date.day, ctx.utcOffsetAt);
  AstroSunDay sun2{};
  astroSunDay(tomorrowStart, lat, lon, &sun2);
  out.legalDay = LegalDay::Tomorrow;
  out.legal = almanac::legalWindow(sun2, tomorrowStart, lat, lon, out.legalRule);
  fillSun(out, sun2, tomorrowStart, lat, lon);
  out.sunTomorrow = true;
  return true;
}

void formatDayLength(int64_t seconds, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return;
  if (seconds < 0) seconds = 0;
  const int64_t minutes = almanac::floorDiv(seconds + 30, 60);
  std::snprintf(out, cap, tr(STR_DAYCARD_HOURS_MINUTES), static_cast<int>(minutes / 60),
                static_cast<int>(minutes % 60));
}

}  // namespace daycard

namespace {

using daycard::DayFacts;
using daycard::formatDayLength;
using daycard::LegalDay;
using daycard::SunKind;

// Layout (portrait 480x800; the footer owns the bottom FOOTER_HEIGHT px).
constexpr int W = 480;
constexpr int H = 800;
constexpr int CX = W / 2;
constexpr int CONTENT_W = W - 2 * SCREEN_MARGIN;
constexpr int TOP = 22;
constexpr int BOTTOM = H - FOOTER_HEIGHT - 16;

// The big day number (src/images/CardDigits.h); smaller when the legal-light band needs room.
constexpr int SUN_BLOCK_H = 154;
constexpr int SUN_MESSAGE_H = 86;
constexpr int LEGAL_BLOCK_H = 132;
constexpr int MOON_RADIUS = 56;
constexpr int MOON_BLOCK_H = 140;

const char* phaseName(const int phase) {
  switch (phase) {
    case 0:
      return tr(STR_DAYCARD_PHASE_NEW);
    case 1:
      return tr(STR_DAYCARD_PHASE_WAXING_CRESCENT);
    case 2:
      return tr(STR_DAYCARD_PHASE_FIRST_QUARTER);
    case 3:
      return tr(STR_DAYCARD_PHASE_WAXING_GIBBOUS);
    case 4:
      return tr(STR_DAYCARD_PHASE_FULL);
    case 5:
      return tr(STR_DAYCARD_PHASE_WANING_GIBBOUS);
    case 6:
      return tr(STR_DAYCARD_PHASE_LAST_QUARTER);
    default:
      return tr(STR_DAYCARD_PHASE_WANING_CRESCENT);
  }
}

void clockOf(const CardContext& ctx, const int64_t utc, char* out, const size_t cap) {
  if (utc == 0) {
    std::snprintf(out, cap, "%s", tr(STR_DAYCARD_NO_EVENT));
    return;
  }
  formatClock(utc, ctx.utcOffsetAt, ctx.clock12h, out, cap);
}

// A sun on the horizon with a small arrow under it: rising (up) or setting (down). (cx, horizon)
// is the middle of the horizon line; the rays reach radius + 11 above it, the arrow 17 below.
void drawSunIcon(GfxRenderer& r, const int cx, const int horizon, const int radius, const bool rising) {
  constexpr int RAYS = 7;
  constexpr double HALF_TURN = 3.14159265358979323846;  // radians; not PI or M_PI: Arduino.h defines PI as a macro
  for (int i = 0; i < RAYS; i++) {
    const double a = HALF_TURN * i / (RAYS - 1);
    const int x0 = cx + static_cast<int>(std::lround(std::cos(a) * (radius + 5)));
    const int y0 = horizon - static_cast<int>(std::lround(std::sin(a) * (radius + 5)));
    const int x1 = cx + static_cast<int>(std::lround(std::cos(a) * (radius + 11)));
    const int y1 = horizon - static_cast<int>(std::lround(std::sin(a) * (radius + 11)));
    r.drawLine(x0, y0, x1, y1, 3, true);
  }
  draw::fillCircle(r, cx, horizon, radius, true);
  r.fillRect(cx - radius - 16, horizon + 1, 2 * radius + 32, radius + 12, false);
  r.fillRect(cx - radius - 16, horizon - 1, 2 * radius + 32, 3, true);
  const int ay = horizon + 7;
  const int tip = rising ? ay : ay + 10;
  const int base = rising ? ay + 10 : ay;
  const int xs[3] = {cx - 8, cx + 8, cx};
  const int ys[3] = {base, base, tip};
  r.fillPolygon(xs, ys, 3, true);
}

constexpr int WEEKDAY_TO_DIGITS = 6;
constexpr int DIGITS_TO_MONTH = 22;

int dateBlockHeight(const GfxRenderer& r, const CardDigitFont& digits) {
  return r.getLineHeight(NOTOSANS_18_FONT_ID) + WEEKDAY_TO_DIGITS + digits.height + DIGITS_TO_MONTH +
         r.getLineHeight(NOTOSANS_16_FONT_ID);
}

// Weekday, the big day number, month and year.
void drawDateBlock(const DayFacts& f, GfxRenderer& r, int y, const CardDigitFont& digits) {
  draw::drawTextCenteredAt(r, NOTOSANS_18_FONT_ID, CX, y, weekdayName(f.date.weekday));
  y += r.getLineHeight(NOTOSANS_18_FONT_ID) + WEEKDAY_TO_DIGITS;
  char day[4];
  std::snprintf(day, sizeof(day), "%d", f.date.day);
  draw::drawCenteredDigits(r, digits, CX, y, day);
  y += digits.height + DIGITS_TO_MONTH;
  char monthYear[40];
  std::snprintf(monthYear, sizeof(monthYear), tr(STR_DAYCARD_MONTH_YEAR), monthName(f.date.month), f.date.year);
  draw::drawTextCenteredAt(r, NOTOSANS_16_FONT_ID, CX, y, monthYear, true, EpdFontFamily::BOLD);
}

void drawSunColumn(const CardContext& ctx, GfxRenderer& r, const int cx, const int y, const bool rising,
                   const int64_t when, const bool tomorrow) {
  drawSunIcon(r, cx, y + 30, 16, rising);
  const char* label = tomorrow ? (rising ? tr(STR_DAYCARD_SUNRISE_TOMORROW) : tr(STR_DAYCARD_SUNSET_TOMORROW))
                               : (rising ? tr(STR_DAYCARD_SUNRISE) : tr(STR_DAYCARD_SUNSET));
  draw::drawTextCenteredAt(r, UI_10_FONT_ID, cx, y + 54, label);
  char clock[16];
  clockOf(ctx, when, clock, sizeof(clock));
  draw::drawTextCenteredAt(r, NOTOSANS_18_FONT_ID, cx, y + 74, clock, true, EpdFontFamily::BOLD);
}

int sunBlockHeight(const DayFacts& f) {
  return f.sunKind == SunKind::Normal || f.sunKind == SunKind::RiseOnly || f.sunKind == SunKind::SetOnly
             ? SUN_BLOCK_H
             : SUN_MESSAGE_H;
}

void drawSunBlock(const CardContext& ctx, const DayFacts& f, GfxRenderer& r, const int y) {
  if (f.sunKind == SunKind::NoLocation || f.sunKind == SunKind::UpAllDay || f.sunKind == SunKind::DownAllDay) {
    // A lone sun on the horizon and one line under it.
    const bool up = f.sunKind != SunKind::DownAllDay;
    drawSunIcon(r, CX, y + 30, 16, up);
    const char* msg = f.sunKind == SunKind::NoLocation ? tr(STR_SET_LOCATION_IN_SETTINGS)
                      : up                             ? tr(STR_DAYCARD_SUN_UP_ALL_DAY)
                                                       : tr(STR_DAYCARD_SUN_DOWN_ALL_DAY);
    draw::drawTextCenteredAt(r, UI_12_FONT_ID, CX, y + 58, msg, true, EpdFontFamily::BOLD);
    return;
  }
  const int colL = SCREEN_MARGIN + CONTENT_W / 4;
  const int colR = W - SCREEN_MARGIN - CONTENT_W / 4;
  drawSunColumn(ctx, r, colL, y, true, f.sunrise, f.sunTomorrow);
  drawSunColumn(ctx, r, colR, y, false, f.sunset, f.sunTomorrow);
  draw::drawDashedHLine(r, colL + 50, colR - 50, y + 30, 4, 5, true);
  if (f.dayLengthS >= 0) {
    char span[24];
    formatDayLength(f.dayLengthS, span, sizeof(span));
    char line[48];
    std::snprintf(line, sizeof(line), tr(STR_DAYCARD_DAY_LENGTH), span);
    draw::drawTextCenteredAt(r, UI_12_FONT_ID, CX, y + 126, line);
  }
}

// Legal light: a black band with the window in big white type.
void drawLegalBlock(const CardContext& ctx, const DayFacts& f, GfxRenderer& r, const int y) {
  using Kind = almanac::LegalWindow::Kind;
  r.fillRoundedRect(SCREEN_MARGIN, y, CONTENT_W, LEGAL_BLOCK_H, 10, Color::Black);
  const char* title = f.legalDay == LegalDay::Tomorrow ? tr(STR_DAYCARD_LEGAL_TOMORROW) : tr(STR_DAYCARD_LEGAL_TODAY);
  draw::drawTextCenteredAt(r, UI_10_FONT_ID, CX, y + 12, title, false, EpdFontFamily::BOLD);

  char a[16];
  char b[16];
  char text[48];
  switch (f.legal.kind) {
    case Kind::Window:
      clockOf(ctx, f.legal.first, a, sizeof(a));
      clockOf(ctx, f.legal.last, b, sizeof(b));
      std::snprintf(text, sizeof(text), tr(STR_DAYCARD_TIME_RANGE), a, b);
      break;
    case Kind::FromOnly:
      clockOf(ctx, f.legal.first, a, sizeof(a));
      std::snprintf(text, sizeof(text), tr(STR_DAYCARD_LEGAL_FROM), a);
      break;
    case Kind::UntilOnly:
      clockOf(ctx, f.legal.last, b, sizeof(b));
      std::snprintf(text, sizeof(text), tr(STR_DAYCARD_LEGAL_UNTIL), b);
      break;
    case Kind::AllDay:
      std::snprintf(text, sizeof(text), "%s", tr(STR_DAYCARD_LEGAL_ALL_DAY));
      break;
    default:
      std::snprintf(text, sizeof(text), "%s", tr(STR_DAYCARD_LEGAL_NONE));
      break;
  }
  // As big as fits: 1.5x in 24-hour time, smaller for the wider 12-hour form.
  float scale = 1.5f;
  while (scale > 1.0f &&
         draw::textWidthScaled(r, NOTOSANS_18_FONT_ID, text, scale, EpdFontFamily::BOLD) > CONTENT_W - 24) {
    scale -= 0.1f;
  }
  // On one baseline whatever the scale.
  const int baseline = y + 92;
  const int top = baseline - static_cast<int>(r.getFontAscenderSize(NOTOSANS_18_FONT_ID) * scale);
  draw::drawCenteredTextScaled(r, NOTOSANS_18_FONT_ID, CX, top, text, scale, false, EpdFontFamily::BOLD);
  const char* rule =
      f.legalRule == almanac::LegalRule::CivilTwilight ? tr(STR_DAYCARD_RULE_CIVIL) : tr(STR_DAYCARD_RULE_30_MIN);
  // White on black: the larger UI face where it fits (the small one fills in on the panel).
  const int ruleFont = r.getTextWidth(UI_10_FONT_ID, rule) <= CONTENT_W - 24 ? UI_10_FONT_ID : SMALL_FONT_ID;
  draw::drawTextCenteredAt(r, ruleFont, CX, y + LEGAL_BLOCK_H - 34, rule, false);
}

// The moon as seen tonight, its phase, illumination and the next full moon.
void drawMoonBlock(const DayFacts& f, GfxRenderer& r, const int y) {
  const int mx = SCREEN_MARGIN + MOON_RADIUS + 2;
  const int my = y + MOON_BLOCK_H / 2;
  draw::drawMoon(r, mx, my, MOON_RADIUS, f.moonIllum, f.moonWaxing, f.moonSouthern);

  const int tx = mx + MOON_RADIUS + 22;
  const int tw = W - SCREEN_MARGIN - tx;
  int ty = y - 4;
  r.drawText(NOTOSANS_16_FONT_ID, tx, ty,
             r.truncatedText(NOTOSANS_16_FONT_ID, phaseName(f.moonPhase), tw, EpdFontFamily::BOLD).c_str(), true,
             EpdFontFamily::BOLD);
  ty += r.getLineHeight(NOTOSANS_16_FONT_ID) - 4;
  char line[48];
  std::snprintf(line, sizeof(line), tr(STR_DAYCARD_ILLUMINATED),
                moonlabel::illuminationPercent(f.moonIllum, f.fullMoonToday));
  r.drawText(UI_12_FONT_ID, tx, ty, line);
  ty += r.getLineHeight(UI_12_FONT_ID) + 16;

  if (f.nextFullMoon <= 0) return;
  r.fillRect(tx, ty - 9, 36, 2, true);
  if (f.daysToFullMoon == 0) {
    std::snprintf(line, sizeof(line), "%s", tr(STR_DAYCARD_NEXT_FULL_TODAY));
  } else if (f.daysToFullMoon == 1) {
    std::snprintf(line, sizeof(line), "%s", tr(STR_DAYCARD_NEXT_FULL_TOMORROW));
  } else {
    char date[32];
    std::snprintf(date, sizeof(date), tr(STR_DAYCARD_SHORT_DATE), weekdayShortName(f.nextFullDate.weekday),
                  monthShortName(f.nextFullDate.month), f.nextFullDate.day);
    std::snprintf(line, sizeof(line), tr(STR_DAYCARD_NEXT_FULL), date);
  }
  r.drawText(UI_12_FONT_ID, tx, ty, r.truncatedText(UI_12_FONT_ID, line, tw, EpdFontFamily::BOLD).c_str(), true,
             EpdFontFamily::BOLD);
  if (f.daysToFullMoon > 1) {
    ty += r.getLineHeight(UI_12_FONT_ID);
    std::snprintf(line, sizeof(line), tr(STR_DAYCARD_IN_DAYS), f.daysToFullMoon);
    r.drawText(UI_10_FONT_ID, tx, ty, line);
  }
}

// A short rule between blocks, where the gap has room for one.
void hairline(GfxRenderer& r, const int y, const int gap) {
  if (gap >= 28) r.fillRect(SCREEN_MARGIN + 40, y, CONTENT_W - 80, 1, true);
}

}  // namespace

bool renderDayCard(const CardContext& ctx, GfxRenderer& renderer) {
  DayFacts f;
  if (!daycard::computeDayFacts(ctx, f)) return false;
  GfxRenderer& r = renderer;

  // Blocks top to bottom; the free height is shared out as equal gaps (with a hairline in each).
  const bool legal = f.legalDay != LegalDay::None;
  const CardDigitFont& digits = legal ? CARD_DIGITS_MEDIUM : CARD_DIGITS_LARGE;
  const int dateH = dateBlockHeight(r, digits);
  const int sunH = sunBlockHeight(f);
  const int legalH = legal ? LEGAL_BLOCK_H : 0;
  const int blocks = legal ? 4 : 3;
  const int gap = (BOTTOM - TOP - dateH - sunH - legalH - MOON_BLOCK_H) / (blocks - 1);

  int y = TOP;
  drawDateBlock(f, r, y, digits);
  y += dateH + gap;
  hairline(r, y - gap / 2, gap);
  drawSunBlock(ctx, f, r, y);
  y += sunH + gap;
  if (legal) {
    drawLegalBlock(ctx, f, r, y);
    y += legalH + gap;
  } else {
    hairline(r, y - gap / 2, gap);
  }
  drawMoonBlock(f, r, y);
  return true;
}

}  // namespace sleepcards
