#include "WeatherCard.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CardDraw.h"
#include "CardText.h"
#include "CardTime.h"
#include "SkyCard.h"
#include "fontIds.h"
#include "images/CardDigits.h"
#include "network/WeatherData.h"

namespace sleepcards {
namespace weathercard {
namespace {

using weather::NO_VALUE;

bool sameOrLater(const LocalDate& a, const LocalDate& b) {
  return daysFromCivil(a.year, a.month, a.day) >= daysFromCivil(b.year, b.month, b.day);
}

// Rounded half away from zero; a value that rounds to zero is 0, never -0.
int roundTenths(const int tenths) {
  const int r = tenths >= 0 ? (tenths + 5) / 10 : -((-tenths + 5) / 10);
  return r == 0 ? 0 : r;
}

// Great-circle distance (km), as WeatherProtocol's; the card side does not link the parsers.
double distanceKm(const double lat1, const double lon1, const double lat2, const double lon2) {
  constexpr double RAD = 3.14159265358979323846 / 180.0;
  const double dLat = (lat2 - lat1) * RAD;
  const double dLon = (lon2 - lon1) * RAD;
  const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(lat1 * RAD) * std::cos(lat2 * RAD) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * 6371.0 * std::asin(std::min(1.0, std::sqrt(a)));
}
constexpr double SAME_PLACE_KM = 5.0;

}  // namespace

// ---- units --------------------------------------------------------------------------------------

int temperature(const int16_t c10, const WeatherUnits units) {
  if (units == WeatherUnits::Us) {
    // F = C * 9/5 + 32, in tenths first so the rounding happens once.
    const int f10 = static_cast<int>(std::lround(c10 * 9.0 / 5.0)) + 320;
    return roundTenths(f10);
  }
  return roundTenths(c10);
}

void formatTemperature(const int16_t c10, const WeatherUnits units, char* out, const size_t cap) {
  if (c10 == NO_VALUE) {
    std::snprintf(out, cap, "%s", tr(STR_WX_NO_VALUE));
    return;
  }
  std::snprintf(out, cap, tr(STR_WX_DEGREES), temperature(c10, units));
}

int windSpeed(const int16_t ms10, const WeatherUnits units) {
  // m/s -> km/h x3.6, -> mph x2.23694.
  const double factor = units == WeatherUnits::Us ? 2.2369363 : 3.6;
  return static_cast<int>(std::lround(ms10 * factor / 10.0));
}

void formatWind(const int16_t dirFrom, const int16_t ms10, const int16_t gustMs10, const WeatherUnits units, char* out,
                const size_t cap) {
  if (ms10 == NO_VALUE) {
    std::snprintf(out, cap, "%s", tr(STR_WX_NO_VALUE));
    return;
  }
  const int speed = windSpeed(ms10, units);
  if (speed < 1) {
    std::snprintf(out, cap, "%s", tr(STR_WX_CALM));
    return;
  }
  const char* unit = units == WeatherUnits::Us ? tr(STR_WX_MPH) : tr(STR_WX_KMH);
  const int n = dirFrom == NO_VALUE
                    ? std::snprintf(out, cap, tr(STR_WX_WIND_SPEED), speed, unit)
                    : std::snprintf(out, cap, tr(STR_WX_WIND_FROM),
                                    skycard::compassName(skycard::compassIndex16(dirFrom)), speed, unit);
  if (gustMs10 == NO_VALUE || n <= 0 || static_cast<size_t>(n) >= cap) return;
  const int gust = windSpeed(gustMs10, units);
  if (gust > speed) std::snprintf(out + n, cap - static_cast<size_t>(n), tr(STR_WX_GUSTS), gust);
}

void formatPrecipitation(const int16_t mm10, const WeatherUnits units, char* out, const size_t cap) {
  if (mm10 == NO_VALUE || mm10 < 0) {
    std::snprintf(out, cap, "%s", tr(STR_WX_NO_VALUE));
    return;
  }
  if (units == WeatherUnits::Us) {
    // Hundredths of an inch: mm / 25.4.
    const long h = std::lround(mm10 * 10.0 / 25.4);
    std::snprintf(out, cap, tr(STR_WX_IN), static_cast<int>(h / 100), static_cast<int>(h % 100));
    return;
  }
  std::snprintf(out, cap, tr(STR_WX_MM), mm10 / 10, mm10 % 10);
}

void formatPressure(const int16_t hpa10, const WeatherUnits units, char* out, const size_t cap) {
  if (hpa10 == NO_VALUE || hpa10 <= 0) {
    std::snprintf(out, cap, "%s", tr(STR_WX_NO_VALUE));
    return;
  }
  if (units == WeatherUnits::Us) {
    // Hundredths of an inch of mercury: hPa x 0.0295300.
    const long h = std::lround(hpa10 * 0.295300);
    std::snprintf(out, cap, tr(STR_WX_INHG), static_cast<int>(h / 100), static_cast<int>(h % 100));
    return;
  }
  std::snprintf(out, cap, tr(STR_WX_HPA), roundTenths(hpa10));
}

void conditionName(const int16_t code, char* out, const size_t cap) {
  StrId id;
  switch (code) {
      // clang-format off
    case 0: id = StrId::STR_WMO_CLEAR; break;
    case 1: id = StrId::STR_WMO_MOSTLY_CLEAR; break;
    case 2: id = StrId::STR_WMO_PARTLY_CLOUDY; break;
    case 3: id = StrId::STR_WMO_OVERCAST; break;
    case 45: id = StrId::STR_WMO_FOG; break;
    case 48: id = StrId::STR_WMO_RIME_FOG; break;
    case 51: id = StrId::STR_WMO_LT_DRIZZLE; break;
    case 53: id = StrId::STR_WMO_DRIZZLE; break;
    case 55: id = StrId::STR_WMO_HVY_DRIZZLE; break;
    case 56: id = StrId::STR_WMO_LT_FRZ_DRIZZLE; break;
    case 57: id = StrId::STR_WMO_FRZ_DRIZZLE; break;
    case 61: id = StrId::STR_WMO_LT_RAIN; break;
    case 63: id = StrId::STR_WMO_RAIN; break;
    case 65: id = StrId::STR_WMO_HEAVY_RAIN; break;
    case 66: id = StrId::STR_WMO_LT_FRZ_RAIN; break;
    case 67: id = StrId::STR_WMO_FRZ_RAIN; break;
    case 71: id = StrId::STR_WMO_LT_SNOW; break;
    case 73: id = StrId::STR_WMO_SNOW; break;
    case 75: id = StrId::STR_WMO_HEAVY_SNOW; break;
    case 77: id = StrId::STR_WMO_SNOW_GRAINS; break;
    case 80: id = StrId::STR_WMO_LT_SHOWERS; break;
    case 81: id = StrId::STR_WMO_SHOWERS; break;
    case 82: id = StrId::STR_WMO_HVY_SHOWERS; break;
    case 85: id = StrId::STR_WMO_SNOW_SHOWERS; break;
    case 86: id = StrId::STR_WMO_HVY_SNOW_SHOWERS; break;
    case 95: id = StrId::STR_WMO_TSTORM; break;
    case 96: id = StrId::STR_WMO_TSTORM_HAIL; break;
    case 97: id = StrId::STR_WMO_HVY_TSTORM; break;
    case 99: id = StrId::STR_WMO_TSTORM_HVY_HAIL; break;
      // clang-format on
    default:
      if (code == NO_VALUE) {
        std::snprintf(out, cap, "%s", tr(STR_WX_NO_VALUE));
      } else {
        std::snprintf(out, cap, tr(STR_WX_CODE), static_cast<int>(code));
      }
      return;
  }
  std::snprintf(out, cap, "%s", I18N.get(id));
}

Icon iconFor(const int16_t code) {
  if (code == 0 || code == 1) return Icon::Clear;
  if (code == 2) return Icon::PartlyCloudy;
  if (code == 3) return Icon::Overcast;
  if (code == 45 || code == 48) return Icon::Fog;
  if (code == 51 || code == 53 || code == 55) return Icon::Drizzle;
  if (code == 61 || code == 63 || code == 65) return Icon::Rain;
  if (code == 56 || code == 57 || code == 66 || code == 67) return Icon::Freezing;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Icon::Snow;
  if (code >= 80 && code <= 82) return Icon::Showers;
  if (code >= 95 && code <= 99) return Icon::Thunder;
  return Icon::Unknown;
}

// ---- facts --------------------------------------------------------------------------------------

Decline computeWeatherFacts(const CardContext& ctx, const weather::Record& r, WeatherFacts& out) {
  weather::resetInPlace(out);
  if (!ctx.timeValid || !plausibleTime(ctx.utcNow) || ctx.utcOffsetAt == nullptr) return Decline::NoClock;
  if (!ctx.settings.weatherOn) return Decline::Off;
  if (!ctx.location.valid) return Decline::NoLocation;
  const weather::Forecast& f = r.forecast;
  if (r.fetchUtc <= 0 || f.dayCount == 0) return Decline::NoCache;
  const int64_t now = ctx.utcNow;
  if (r.fetchUtc > now + 600) return Decline::FromTheFuture;
  if (now - r.fetchUtc > MAX_AGE_S) return Decline::Stale;
  if (distanceKm(ctx.location.lat, ctx.location.lon, r.lat, r.lon) > SAME_PLACE_KM) return Decline::Moved;

  // Days by their middle, in the reader's zone; today's first.
  const LocalDate today = localDateOf(now, ctx.utcOffsetAt);
  uint8_t first = f.dayCount;
  for (uint8_t i = 0; i < f.dayCount; i++) {
    out.dayDate[i] = localDateOf(f.days[i].t + 43200, ctx.utcOffsetAt);
    if (first == f.dayCount && sameOrLater(out.dayDate[i], today)) first = i;
  }
  if (first == f.dayCount) return Decline::Ended;
  out.firstDay = first;
  out.dayCount = static_cast<uint8_t>(f.dayCount - first);

  // Hours from the one covering now.
  for (uint8_t i = 0; i < f.hourCount; i++) {
    if (f.hours[i].t + 3600 <= now) continue;
    out.firstHour = i;
    out.hourCount = static_cast<uint8_t>(std::min<int>(STRIP_HOURS, f.hourCount - i));
    break;
  }

  // Now: the fetch's current conditions within the hour, else the forecast hour covering now.
  if (f.current.valid && now - r.fetchUtc <= CURRENT_FOR_S && now >= r.fetchUtc - 600) {
    const weather::Current& c = f.current;
    out.haveNow = true;
    out.nowIsCurrent = true;
    out.tempC10 = c.tempC10;
    out.feelsC10 = c.feelsC10;
    out.windMs10 = c.windMs10;
    out.gustMs10 = c.gustMs10;
    out.windDir = c.windDir;
    out.humidity = c.humidity;
    out.pressureHpa10 = c.pressureHpa10;
    out.code = c.code;
  } else if (out.hourCount > 0 && f.hours[out.firstHour].t <= now && f.hours[out.firstHour].tempC10 != NO_VALUE) {
    const weather::Hour& h = f.hours[out.firstHour];
    out.haveNow = true;
    out.nowHourUtc = h.t;
    out.tempC10 = h.tempC10;
    out.windMs10 = h.windMs10;
    out.code = h.code;  // no direction: the hourly series does not carry one
  }

  // Alerts still in force now (offline, an alert ends at `ends`, or `expires` without one).
  for (uint8_t i = 0; i < r.alerts.count && i < weather::MAX_ALERTS; i++) {
    const int64_t end = r.alerts.list[i].endsOrExpires();
    if (end != 0 && end <= now) continue;
    out.alertIndex[out.alertCount++] = i;
  }

  out.offsetMismatch = f.utcOffsetS != ctx.utcOffsetAt(r.fetchUtc);
  return Decline::None;
}

bool cacheLooksUsable(const CardContext& ctx) {
  if (ctx.io == nullptr || !ctx.settings.weatherOn || !ctx.location.valid || !ctx.timeValid ||
      !plausibleTime(ctx.utcNow) || ctx.utcOffsetAt == nullptr) {
    return false;
  }
  char buf[weather::HEADER_CAP];
  const int32_t got = ctx.io->readFileAt(weather::CACHE_PATH, 0, buf, sizeof(buf));
  weather::Header h;
  if (got <= 0 || !weather::decodeHeader(buf, static_cast<size_t>(got), h)) return false;
  if (h.fetchUtc > ctx.utcNow + 600 || ctx.utcNow - h.fetchUtc > MAX_AGE_S) return false;
  if (distanceKm(ctx.location.lat, ctx.location.lon, h.lat, h.lon) > SAME_PLACE_KM) return false;
  return sameOrLater(localDateOf(h.lastDayUtc + 43200, ctx.utcOffsetAt), localDateOf(ctx.utcNow, ctx.utcOffsetAt));
}

uint16_t alertsInForce(const weather::Alerts& alerts, const WeatherFacts& facts) {
  const int ended = std::max(0, static_cast<int>(alerts.count) - static_cast<int>(facts.alertCount));
  return static_cast<uint16_t>(std::max(0, static_cast<int>(alerts.total) - ended));
}

void formatAlertTime(const CardContext& ctx, const weather::Alert& alert, char* out, const size_t cap) {
  if (cap == 0) return;
  out[0] = '\0';
  char clock[16];
  const auto at = [&](const int64_t utc, char* text, const size_t textCap) {
    formatClock(utc, ctx.utcOffsetAt, ctx.clock12h, text, textCap);
    return weekdayShortName(localDateOf(utc, ctx.utcOffsetAt).weekday);
  };
  const int64_t end = alert.endsOrExpires();
  if (alert.onset > ctx.utcNow) {
    const char* day = at(alert.onset, clock, sizeof(clock));
    const int n = std::snprintf(out, cap, tr(STR_WX_ALERT_FROM), clock, day);
    // An end at or before the onset (a message that expires before its event begins) is no range.
    if (end == 0 || end <= alert.onset || n <= 0 || static_cast<size_t>(n) + 1 >= cap) return;
    char clock2[16];
    const char* day2 = at(end, clock2, sizeof(clock2));
    out[n] = ' ';
    std::snprintf(out + n + 1, cap - static_cast<size_t>(n) - 1, tr(STR_WX_ALERT_UNTIL), clock2, day2);
  } else if (end != 0) {
    const char* day = at(end, clock, sizeof(clock));
    std::snprintf(out, cap, tr(STR_WX_ALERT_UNTIL), clock, day);
  }
}

}  // namespace weathercard

// ---- drawing ------------------------------------------------------------------------------------

namespace {

using weathercard::Icon;
using weathercard::WeatherFacts;

constexpr int W = 480;
constexpr int H = 800;
constexpr int CX = W / 2;
constexpr int LEFT = SCREEN_MARGIN;
constexpr int RIGHT = W - SCREEN_MARGIN;
constexpr int CONTENT_W = RIGHT - LEFT;
constexpr int TOP = 22;
constexpr int BOTTOM = H - FOOTER_HEIGHT - 16;

constexpr int ALERT_LINE_H = 24;
constexpr int NOW_BLOCK_H = 164;
constexpr int STRIP_COL_W = 17;
constexpr int STRIP_H = 170;
constexpr int DAY_ROW_H = 42;
constexpr int BOTTOM_LINES_H = 50;
constexpr int MIN_GAP = 8;
constexpr double PI_D = 3.14159265358979323846;  // not PI: Arduino.h defines it

// "7:10 AM" and "Thu" of an instant, in the reader's zone (absolute: the card stays up for days).
void clockAndDay(const CardContext& ctx, const int64_t utc, char* clock, const size_t clockCap, const char** day) {
  formatClock(utc, ctx.utcOffsetAt, ctx.clock12h, clock, clockCap);
  *day = weekdayShortName(localDateOf(utc, ctx.utcOffsetAt).weekday);
}

// ---- icons ----

// A cloud of three bumps on a flat base, filling ink (outline: the inside left white).
void cloudShape(GfxRenderer& r, const int cx, const int cy, const int u, const bool black, const int shrink = 0) {
  draw::fillCircle(r, cx - 7 * u / 4, cy + u, 2 * u - shrink, black);
  draw::fillCircle(r, cx + u / 2, cy - u / 2, 3 * u - shrink, black);
  draw::fillCircle(r, cx + 9 * u / 4, cy + 3 * u / 4, 2 * u - shrink + u / 4, black);
  r.fillRect(cx - 7 * u / 4, cy + u + shrink / 2, 4 * u, 2 * u - shrink, black);
}

void drawCloud(GfxRenderer& r, const int cx, const int cy, const int u, const bool outline) {
  cloudShape(r, cx, cy, u, true);
  if (outline) cloudShape(r, cx, cy, u, false, std::max(2, u / 2));
}

void drawSun(GfxRenderer& r, const int cx, const int cy, const int u) {
  draw::fillCircle(r, cx, cy, 2 * u);
  for (int i = 0; i < 8; i++) {
    const double a = PI_D * i / 4;
    const int x0 = cx + static_cast<int>(std::lround(std::cos(a) * 3 * u));
    const int y0 = cy + static_cast<int>(std::lround(std::sin(a) * 3 * u));
    const int x1 = cx + static_cast<int>(std::lround(std::cos(a) * 4 * u));
    const int y1 = cy + static_cast<int>(std::lround(std::sin(a) * 4 * u));
    r.drawLine(x0, y0, x1, y1, std::max(1, u / 2), true);
  }
}

// What falls under a cloud: rain streaks, drizzle dots, snow crosses.
void drawFall(GfxRenderer& r, const int cx, const int top, const int u, const Icon kind) {
  for (int i = -1; i <= 1; i++) {
    const int x = cx + i * 2 * u;
    const bool snowHere = kind == Icon::Snow || (kind == Icon::Freezing && i == 0);
    if (snowHere) {
      const int y = top + 3 * u / 2;
      r.drawLine(x - u / 2 - 1, y, x + u / 2 + 1, y, std::max(1, u / 3), true);
      r.drawLine(x, y - u / 2 - 1, x, y + u / 2 + 1, std::max(1, u / 3), true);
    } else if (kind == Icon::Drizzle) {
      draw::fillCircle(r, x, top + u, std::max(1, u / 3));
      draw::fillCircle(r, x - u / 2, top + 5 * u / 2, std::max(1, u / 3));
    } else {
      r.drawLine(x + u / 2, top, x - u / 2, top + 2 * u, std::max(1, u / 2), true);
    }
  }
}

// A weather icon in a box about 10u wide and 9u tall, centred on (cx, cy).
void drawIcon(GfxRenderer& r, const int cx, const int cy, const int u, const Icon icon) {
  switch (icon) {
    case Icon::Clear:
      drawSun(r, cx, cy, u);
      return;
    case Icon::PartlyCloudy:
      drawSun(r, cx - u * 3 / 2, cy - u * 3 / 2, u * 3 / 4 + 1);
      drawCloud(r, cx + u / 2, cy + u, u, true);
      return;
    case Icon::Overcast:
      drawCloud(r, cx, cy, u + u / 4, false);
      return;
    case Icon::Fog:
      for (int i = 0; i < 4; i++) {
        const int y = cy - 3 * u + i * 2 * u;
        const int inset = (i % 2) * u;
        r.fillRect(cx - 4 * u + inset, y, 8 * u - inset, std::max(2, u / 2), true);
      }
      return;
    case Icon::Showers:
      drawSun(r, cx + 2 * u, cy - 2 * u, u * 3 / 4 + 1);
      drawCloud(r, cx - u / 2, cy - u, u, true);
      drawFall(r, cx - u / 2, cy + 2 * u, u, Icon::Rain);
      return;
    case Icon::Thunder: {
      drawCloud(r, cx, cy - u, u, false);
      const int xs[] = {cx + u / 2, cx - u, cx, cx - u / 2, cx + 3 * u / 2, cx + u / 2};
      const int ys[] = {cy + u, cy + 3 * u, cy + 3 * u, cy + 5 * u, cy + 2 * u, cy + 2 * u};
      r.fillPolygon(xs, ys, 6, true);
      return;
    }
    case Icon::Drizzle:
    case Icon::Rain:
    case Icon::Freezing:
    case Icon::Snow:
      drawCloud(r, cx, cy - u, u, icon != Icon::Rain);
      drawFall(r, cx, cy + 2 * u, u, icon);
      return;
    case Icon::Unknown:
    default:
      draw::drawCircle(r, cx, cy, 3 * u, std::max(1, u / 2));
      return;
  }
}

// An arrow pointing DOWNWIND (where the wind goes): the text beside it still says "from".
void drawWindArrow(GfxRenderer& r, const int cx, const int cy, const int length, const int16_t dirFrom) {
  if (dirFrom == weather::NO_VALUE) return;
  const double a = (dirFrom + 180.0) * PI_D / 180.0;
  const double vx = std::sin(a);
  const double vy = -std::cos(a);
  const int half = length / 2;
  const int tipX = cx + static_cast<int>(std::lround(vx * half));
  const int tipY = cy + static_cast<int>(std::lround(vy * half));
  const int tailX = cx - static_cast<int>(std::lround(vx * half));
  const int tailY = cy - static_cast<int>(std::lround(vy * half));
  const double head = length * 0.42;
  const double wide = length * 0.26;
  const int baseX = tipX - static_cast<int>(std::lround(vx * head));
  const int baseY = tipY - static_cast<int>(std::lround(vy * head));
  r.drawLine(tailX, tailY, baseX, baseY, 3, true);
  const int xs[3] = {tipX, baseX + static_cast<int>(std::lround(-vy * wide)),
                     baseX - static_cast<int>(std::lround(-vy * wide))};
  const int ys[3] = {tipY, baseY + static_cast<int>(std::lround(vx * wide)),
                     baseY - static_cast<int>(std::lround(vx * wide))};
  r.fillPolygon(xs, ys, 3, true);
}

// ---- blocks ----

int alertsBlockHeight(const weather::Record& rec, const WeatherFacts& f) {
  const weather::Alerts& a = rec.alerts;
  if (f.alertCount > 0) {
    const int shown = std::min<int>(f.alertCount, 2);
    return 12 + shown * 54 + (a.recheckFailed ? 24 : 0) + 4;
  }
  if (a.status == weather::AlertsStatus::TooMany) return 12 + 29 + 24 + 8;
  return ALERT_LINE_H;
}

void drawAlertsBlock(const CardContext& ctx, const weather::Record& rec, const WeatherFacts& f, GfxRenderer& r,
                     const int y, const int h) {
  const weather::Alerts& a = rec.alerts;
  char clock[16];
  const char* day = "";
  char line[96];
  if (f.alertCount == 0 && a.status != weather::AlertsStatus::TooMany) {
    // One small line saying what the last check found - never a bare "No alerts".
    clockAndDay(ctx, a.asOf, clock, sizeof(clock), &day);
    if (a.status == weather::AlertsStatus::None) {
      std::snprintf(line, sizeof(line), a.recheckFailed ? tr(STR_WX_ALERTS_NONE_STALE) : tr(STR_WX_ALERTS_NONE), clock,
                    day);
    } else if (a.status == weather::AlertsStatus::Listed) {
      // "Ended" only when every alert in force at the check was listed: the ones not kept (the
      // least severe) have unknown end times.
      const uint16_t inForce = weathercard::alertsInForce(a, f);
      if (inForce == 0) {
        std::snprintf(line, sizeof(line), tr(STR_WX_ALERTS_ENDED), clock, day);
      } else {
        std::snprintf(line, sizeof(line), tr(STR_WX_ALERTS_MAY_REMAIN), clock, day, static_cast<unsigned>(inForce));
      }
    } else if (a.status == weather::AlertsStatus::OutsideUs) {
      std::snprintf(line, sizeof(line), "%s", tr(STR_WX_ALERTS_US_ONLY));
    } else {
      std::snprintf(line, sizeof(line), "%s", tr(STR_WX_ALERTS_NOT_CHECKED));
    }
    draw::drawTextCenteredAt(r, UI_10_FONT_ID, CX, y, r.truncatedText(UI_10_FONT_ID, line, CONTENT_W).c_str());
    return;
  }

  // A black band, white type, as the Day card's legal light.
  r.fillRoundedRect(LEFT, y, CONTENT_W, h, 10, Color::Black);
  const int tx = LEFT + 14;
  const int tw = CONTENT_W - 28;
  int ty = y + 10;
  if (f.alertCount == 0) {
    std::snprintf(line, sizeof(line), tr(STR_WX_ALERTS_TOO_MANY), static_cast<unsigned>(a.total));
    r.drawText(UI_12_FONT_ID, tx, ty, r.truncatedText(UI_12_FONT_ID, line, tw, EpdFontFamily::BOLD).c_str(), false,
               EpdFontFamily::BOLD);
    ty += r.getLineHeight(UI_12_FONT_ID);
    clockAndDay(ctx, a.asOf, clock, sizeof(clock), &day);
    std::snprintf(line, sizeof(line), tr(STR_WX_ALERTS_CHECKED_AT), clock, day);
    r.drawText(UI_10_FONT_ID, tx, ty, line, false);
    return;
  }
  const int shown = std::min<int>(f.alertCount, 2);
  // The rest that may be in force: listed ones not shown, and those the check did not keep (never
  // the listed ones already over).
  const int more = static_cast<int>(weathercard::alertsInForce(a, f)) - shown;
  char moreText[24] = "";
  if (more > 0) std::snprintf(moreText, sizeof(moreText), tr(STR_WX_ALERT_MORE), static_cast<unsigned>(more));
  const int moreW = more > 0 ? r.getTextWidth(UI_10_FONT_ID, moreText, EpdFontFamily::BOLD) + 10 : 0;
  for (int i = 0; i < shown; i++) {
    const weather::Alert& al = a.list[f.alertIndex[i]];
    weathercard::formatAlertTime(ctx, al, line, sizeof(line));
    // "+N more" sits at the end of the last alert's time line, or of its name when the time line
    // (a 12-hour range, say) leaves no room for it.
    const bool last = i == shown - 1 && more > 0;
    const bool moreOnName = last && r.getTextWidth(UI_10_FONT_ID, line) + moreW > tw;
    const int nameW = moreOnName ? tw - moreW : tw;
    r.drawText(UI_12_FONT_ID, tx, ty, r.truncatedText(UI_12_FONT_ID, al.event, nameW, EpdFontFamily::BOLD).c_str(),
               false, EpdFontFamily::BOLD);
    if (moreOnName) draw::drawTextRight(r, UI_10_FONT_ID, RIGHT - 14, ty + 3, moreText, false, EpdFontFamily::BOLD);
    ty += r.getLineHeight(UI_12_FONT_ID);
    r.drawText(UI_10_FONT_ID, tx, ty, r.truncatedText(UI_10_FONT_ID, line, tw).c_str(), false);
    if (last && !moreOnName)
      draw::drawTextRight(r, UI_10_FONT_ID, RIGHT - 14, ty, moreText, false, EpdFontFamily::BOLD);
    ty += r.getLineHeight(UI_10_FONT_ID) + 1;
  }
  if (a.recheckFailed) {
    clockAndDay(ctx, a.asOf, clock, sizeof(clock), &day);
    std::snprintf(line, sizeof(line), tr(STR_WX_ALERTS_NOT_RECHECKED), clock, day);
    r.drawText(UI_10_FONT_ID, tx, ty, line, false);
  }
}

// The big temperature from the pre-drawn digits, with a drawn minus bar and degree ring (the
// digit set has only 0-9). Returns the width used.
int drawBigTemperature(GfxRenderer& r, const int x, const int top, const int value, const WeatherUnits units) {
  const CardDigitFont& digits = CARD_DIGITS_MEDIUM;
  char text[8];
  std::snprintf(text, sizeof(text), "%d", std::abs(value));
  int cx = x;
  if (value < 0) {
    r.fillRect(cx, top + digits.height / 2 - 5, 26, 10, true);
    cx += 34;
  }
  draw::drawDigits(r, digits, cx, top, text);
  cx += draw::digitsWidth(digits, text) + 16;
  draw::drawCircle(r, cx, top + 12, 11, 4);
  cx += 14;
  r.drawText(NOTOSANS_18_FONT_ID, cx, top - 4, units == WeatherUnits::Us ? "F" : "C", true, EpdFontFamily::BOLD);
  return cx + r.getTextWidth(NOTOSANS_18_FONT_ID, "F", EpdFontFamily::BOLD) - x;
}

void drawNowBlock(const CardContext& ctx, const WeatherFacts& f, GfxRenderer& r, const int y) {
  const WeatherUnits units = ctx.settings.weatherUnits;
  char label[48];
  if (f.nowIsCurrent) {
    std::snprintf(label, sizeof(label), "%s", tr(STR_WX_NOW));
  } else {
    char clock[16];
    formatClock(f.nowHourUtc, ctx.utcOffsetAt, ctx.clock12h, clock, sizeof(clock));
    std::snprintf(label, sizeof(label), tr(STR_WX_FORECAST_FOR), clock);
  }
  r.drawText(UI_10_FONT_ID, LEFT, y, label, true, EpdFontFamily::BOLD);

  const int top = y + 28;
  const int used = f.tempC10 != weather::NO_VALUE
                       ? drawBigTemperature(r, LEFT, top, weathercard::temperature(f.tempC10, units), units)
                       : 0;
  // The condition beside it: icon and words, how it feels, humidity and pressure.
  const int colX = LEFT + std::max(used + 24, 190);
  const int colW = RIGHT - colX;
  drawIcon(r, colX + 22, top + 20, 4, weathercard::iconFor(f.code));
  char text[64];
  weathercard::conditionName(f.code, text, sizeof(text));
  r.drawText(NOTOSANS_16_FONT_ID, colX + 54, top - 2,
             r.truncatedText(NOTOSANS_16_FONT_ID, text, colW - 54, EpdFontFamily::BOLD).c_str(), true,
             EpdFontFamily::BOLD);
  if (f.feelsC10 != weather::NO_VALUE) {
    char t[16];
    weathercard::formatTemperature(f.feelsC10, units, t, sizeof(t));
    std::snprintf(text, sizeof(text), tr(STR_WX_FEELS), t);
    r.drawText(UI_12_FONT_ID, colX, top + 44, text);
  }
  // Humidity and pressure; the humidity alone where both do not fit.
  if (f.humidity != weather::NO_VALUE || f.pressureHpa10 != weather::NO_VALUE) {
    char humidity[24] = "";
    char pressure[24] = "";
    if (f.humidity != weather::NO_VALUE) {
      std::snprintf(humidity, sizeof(humidity), tr(STR_WX_HUMIDITY), static_cast<int>(f.humidity));
    }
    if (f.pressureHpa10 != weather::NO_VALUE)
      weathercard::formatPressure(f.pressureHpa10, units, pressure, sizeof(pressure));
    std::snprintf(text, sizeof(text), "%s%s%s", humidity, humidity[0] && pressure[0] ? " · " : "", pressure);
    if (r.getTextWidth(UI_10_FONT_ID, text) > colW && humidity[0]) std::snprintf(text, sizeof(text), "%s", humidity);
    r.drawText(UI_10_FONT_ID, colX, top + 72, r.truncatedText(UI_10_FONT_ID, text, colW).c_str());
  }

  // Wind: the arrow goes downwind, the words say where it comes from.
  const int windY = top + CARD_DIGITS_MEDIUM.height + 10;
  // No arrow for calm air ("Calm": under 1 km/h or 1 mph), whatever direction the model gives it.
  const bool arrow = f.windDir != weather::NO_VALUE && f.windMs10 != weather::NO_VALUE &&
                     weathercard::windSpeed(f.windMs10, units) >= 1;
  if (arrow) drawWindArrow(r, LEFT + 14, windY + 14, 26, f.windDir);
  weathercard::formatWind(f.windDir, f.windMs10, f.gustMs10, units, text, sizeof(text));
  const int windX = LEFT + (arrow ? 36 : 0);
  r.drawText(UI_12_FONT_ID, windX, windY, r.truncatedText(UI_12_FONT_ID, text, RIGHT - windX).c_str());
}

// "21" / "9 PM" for an hour label.
void hourLabel(const CardContext& ctx, const int64_t utc, char* out, const size_t cap) {
  const LocalDate d = localDateOf(utc, ctx.utcOffsetAt);
  if (!ctx.clock12h) {
    std::snprintf(out, cap, "%02d", d.hour);
    return;
  }
  const int h12 = d.hour % 12 == 0 ? 12 : d.hour % 12;
  std::snprintf(out, cap, "%d %s", h12, d.hour < 12 ? tr(STR_TIME_AM) : tr(STR_TIME_PM));
}

void drawStrip(const CardContext& ctx, const weather::Record& rec, const WeatherFacts& f, GfxRenderer& r, const int y) {
  const WeatherUnits units = ctx.settings.weatherUnits;
  const weather::Hour* hours = rec.forecast.hours + f.firstHour;
  const int n = f.hourCount;
  char text[32];
  std::snprintf(text, sizeof(text), tr(STR_WX_NEXT_HOURS), n);
  r.drawText(UI_10_FONT_ID, LEFT, y, text, true, EpdFontFamily::BOLD);
  draw::drawTextRight(r, SMALL_FONT_ID, RIGHT, y + 1, tr(STR_WX_RAIN_CHANCE));

  const int x0 = LEFT + (CONTENT_W - STRIP_COL_W * weathercard::STRIP_HOURS) / 2;
  const int lineTop = y + 50;  // the temperature line's band (its high labelled above, its low below)
  const int lineH = 40;
  const int barTop = lineTop + lineH + 26;
  const int barH = 28;
  const int labelY = barTop + barH + 3;

  int lo = 1000;
  int hi = -1000;
  for (int i = 0; i < n; i++) {
    if (hours[i].tempC10 == weather::NO_VALUE) continue;
    lo = std::min(lo, weathercard::temperature(hours[i].tempC10, units));
    hi = std::max(hi, weathercard::temperature(hours[i].tempC10, units));
  }
  const auto yOf = [&](const int t) {
    if (hi <= lo) return lineTop + lineH / 2;
    return lineTop + lineH - (t - lo) * lineH / (hi - lo);
  };

  // Column i is the hour from hours[i].t, whose tick sits at its left edge. Open-Meteo gives each
  // hour's rain chance for the hour BEFORE its time: column i's bar is hours[i + 1]'s (the series
  // runs past the strip; the last bar is left out when it does not).
  const weather::Hour* allEnd = rec.forecast.hours + rec.forecast.hourCount;
  r.fillRect(x0, barTop + barH, STRIP_COL_W * n, 1, true);
  for (int i = 0; i < n && hours + i + 1 < allEnd; i++) {
    const int pop = hours[i + 1].pop;
    if (pop == weather::NO_VALUE || pop <= 0) continue;
    const int bh = std::max(2, std::min(100, pop) * barH / 100);
    draw::fillRectDithered(r, x0 + i * STRIP_COL_W + 2, barTop + barH - bh, STRIP_COL_W - 4, bh, 9);
    r.fillRect(x0 + i * STRIP_COL_W + 2, barTop + barH - bh, STRIP_COL_W - 4, 1, true);
  }
  // Temperature line: each value is at its instant, on its hour's tick (a gap where one is missing).
  const auto xOf = [&](const int i) { return x0 + i * STRIP_COL_W; };
  int hiAt = -1;
  int loAt = -1;
  for (int i = 0; i < n; i++) {
    if (hours[i].tempC10 == weather::NO_VALUE) continue;
    const int t = weathercard::temperature(hours[i].tempC10, units);
    if (hiAt < 0 || t > weathercard::temperature(hours[hiAt].tempC10, units)) hiAt = i;
    if (loAt < 0 || t < weathercard::temperature(hours[loAt].tempC10, units)) loAt = i;
    if (i + 1 < n && hours[i + 1].tempC10 != weather::NO_VALUE) {
      const int t2 = weathercard::temperature(hours[i + 1].tempC10, units);
      r.drawLine(xOf(i), yOf(t), xOf(i + 1), yOf(t2), 2, true);
    }
  }
  // The draw time, marked over the whole strip (a triangle above the line, a dashed line down).
  const bool nowShown = n > 0 && hours[0].t <= ctx.utcNow;
  const int nowX = nowShown ? x0 + static_cast<int>((ctx.utcNow - hours[0].t) * STRIP_COL_W / 3600) : 0;
  // A label's centre, moved right of the now marker when it would run into it.
  const auto clearOfNow = [&](const int x, const char* label) {
    const int half = r.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD) / 2;
    const int cx = std::clamp(x, LEFT + half, RIGHT - half);
    if (!nowShown || std::abs(cx - nowX) >= half + 10) return cx;
    return std::min(nowX + 10 + half, RIGHT - half);
  };
  // The high and the low, labelled at their points.
  if (hiAt >= 0) {
    weathercard::formatTemperature(hours[hiAt].tempC10, units, text, sizeof(text));
    draw::fillCircle(r, xOf(hiAt), yOf(weathercard::temperature(hours[hiAt].tempC10, units)), 3);
    draw::drawTextCenteredAt(r, UI_10_FONT_ID, clearOfNow(xOf(hiAt), text), lineTop - 25, text, true,
                             EpdFontFamily::BOLD);
  }
  if (loAt >= 0 && hi > lo) {
    weathercard::formatTemperature(hours[loAt].tempC10, units, text, sizeof(text));
    draw::fillCircle(r, xOf(loAt), yOf(weathercard::temperature(hours[loAt].tempC10, units)), 3);
    draw::drawTextCenteredAt(r, UI_10_FONT_ID, clearOfNow(xOf(loAt), text), lineTop + lineH + 2, text, true,
                             EpdFontFamily::BOLD);
  }
  // Hour labels every 3 h, with ticks.
  for (int i = 0; i < n; i++) {
    const LocalDate d = localDateOf(hours[i].t, ctx.utcOffsetAt);
    if (d.hour % 3 != 0) continue;
    const int x = xOf(i);
    r.fillRect(x, barTop + barH, 1, 4, true);
    hourLabel(ctx, hours[i].t, text, sizeof(text));
    draw::drawTextCenteredAt(r, SMALL_FONT_ID, std::clamp(x, LEFT + 12, RIGHT - 12), labelY, text);
  }
  if (nowShown) {
    draw::drawDashedVLine(r, nowX, lineTop - 4, barTop + barH, 3, 3);
    const int xs[3] = {nowX - 5, nowX + 5, nowX};
    const int ys[3] = {lineTop - 10, lineTop - 10, lineTop - 3};
    r.fillPolygon(xs, ys, 3, true);
  }
}

// Day-row columns (right edges from LEFT, except the name's left edge).
constexpr int DAY_ICON_X = 96;
constexpr int DAY_HIGH_R = 166;
constexpr int DAY_LOW_R = 214;
constexpr int DAY_RAIN_R = 284;
constexpr int DAY_HEADER_H = 22;

// The column heads: the wind column says once that its direction is where the wind comes FROM.
void drawDayHeader(const CardContext& ctx, GfxRenderer& r, const int y) {
  draw::drawTextRight(r, SMALL_FONT_ID, LEFT + DAY_HIGH_R, y, tr(STR_WX_COL_HIGH));
  draw::drawTextRight(r, SMALL_FONT_ID, LEFT + DAY_LOW_R, y, tr(STR_WX_COL_LOW));
  draw::drawTextRight(r, SMALL_FONT_ID, LEFT + DAY_RAIN_R, y, tr(STR_WX_COL_RAIN));
  char wind[32];
  std::snprintf(wind, sizeof(wind), tr(STR_WX_COL_WIND),
                ctx.settings.weatherUnits == WeatherUnits::Us ? tr(STR_WX_MPH) : tr(STR_WX_KMH));
  draw::drawTextRight(r, SMALL_FONT_ID, RIGHT, y, wind);
}

void drawDayRow(const CardContext& ctx, const weather::Day& d, const LocalDate& date, const bool today, GfxRenderer& r,
                const int y) {
  const WeatherUnits units = ctx.settings.weatherUnits;
  const char* name = today ? tr(STR_WX_TODAY) : weekdayShortName(date.weekday);
  const int textY = y + 6;
  r.drawText(UI_12_FONT_ID, LEFT, textY, r.truncatedText(UI_12_FONT_ID, name, 76, EpdFontFamily::BOLD).c_str(), true,
             EpdFontFamily::BOLD);
  drawIcon(r, LEFT + DAY_ICON_X, y + 19, 3, weathercard::iconFor(d.code));
  char text[32];
  weathercard::formatTemperature(d.maxC10, units, text, sizeof(text));
  draw::drawTextRight(r, UI_12_FONT_ID, LEFT + DAY_HIGH_R, textY, text, true, EpdFontFamily::BOLD);
  weathercard::formatTemperature(d.minC10, units, text, sizeof(text));
  draw::drawTextRight(r, UI_12_FONT_ID, LEFT + DAY_LOW_R, textY, text);
  if (d.pop != weather::NO_VALUE) {
    std::snprintf(text, sizeof(text), "%d%%", static_cast<int>(d.pop));
    draw::drawTextRight(r, UI_12_FONT_ID, LEFT + DAY_RAIN_R, textY, text);
  }
  // "SSW 32": the day's dominant direction (FROM, as the column head says) and its top speed.
  if (d.windMaxMs10 != weather::NO_VALUE) {
    const int speed = weathercard::windSpeed(d.windMaxMs10, units);
    if (d.windDir != weather::NO_VALUE && speed >= 1) {
      std::snprintf(text, sizeof(text), "%s %d", skycard::compassName(skycard::compassIndex16(d.windDir)), speed);
    } else {
      std::snprintf(text, sizeof(text), "%d", speed);
    }
    draw::drawTextRight(r, UI_12_FONT_ID, RIGHT, textY, text);
  }
}

// "Wi-Fi fix Nov 2" / "typed location": where the location the forecast is for came from.
void placeLine(const weather::Record& rec, char* out, const size_t cap) {
  switch (rec.placeSource) {
    case weather::PLACE_WIFI:
    case weather::PLACE_WIFI_AUTO:
      if (rec.placeYmd != 0) {
        char date[24];
        const int month = static_cast<int>(rec.placeYmd / 100 % 100);
        std::snprintf(date, sizeof(date), tr(STR_MONTH_DAY_FORMAT), monthShortName(month),
                      static_cast<unsigned>(rec.placeYmd % 100));
        std::snprintf(out, cap, tr(STR_WX_PLACE_WIFI), date);
      } else {
        std::snprintf(out, cap, "%s", tr(STR_WX_PLACE_WIFI_UNDATED));
      }
      return;
    case weather::PLACE_INTERNET:
      std::snprintf(out, cap, "%s", tr(STR_WX_PLACE_IP));
      return;
    default:
      std::snprintf(out, cap, "%s", tr(STR_WX_PLACE_TYPED));
      return;
  }
}

void drawBottomLines(const CardContext& ctx, const weather::Record& rec, const WeatherFacts& f, GfxRenderer& r,
                     const int y) {
  char clock[16];
  const char* day = "";
  clockAndDay(ctx, rec.fetchUtc, clock, sizeof(clock), &day);
  char from[96];
  std::snprintf(from, sizeof(from), rec.clockTrusted ? tr(STR_WX_FORECAST_FROM) : tr(STR_WX_CLOCK_UNSYNCED), clock,
                day);
  char place[48];
  placeLine(rec, place, sizeof(place));
  char line[160];
  std::snprintf(line, sizeof(line), "%s  ·  %s", from, place);
  if (r.getTextWidth(UI_10_FONT_ID, line) > CONTENT_W) std::snprintf(line, sizeof(line), "%s", from);
  draw::drawTextCenteredAt(r, UI_10_FONT_ID, CX, y, line);
  const bool nws =
      rec.alerts.status != weather::AlertsStatus::OutsideUs && rec.alerts.status != weather::AlertsStatus::NotChecked;
  draw::drawTextCenteredAt(r, SMALL_FONT_ID, CX, y + 24, nws ? tr(STR_WX_CREDIT_NWS) : tr(STR_WX_CREDIT));
  if (f.offsetMismatch) {
    const int32_t off = rec.forecast.utcOffsetS;
    char zone[16];
    const int mins = std::abs(off) / 60;
    if (mins % 60 == 0) {
      std::snprintf(zone, sizeof(zone), "%c%d", off < 0 ? '-' : '+', mins / 60);
    } else {
      std::snprintf(zone, sizeof(zone), "%c%d:%02d", off < 0 ? '-' : '+', mins / 60, mins % 60);
    }
    std::snprintf(line, sizeof(line), tr(STR_WX_OFFSET_WARNING), zone);
    draw::drawTextCenteredAt(r, SMALL_FONT_ID, CX, y - 24, line, true, EpdFontFamily::BOLD);
  }
}

// A short rule between blocks, where the gap has room for one.
void hairline(GfxRenderer& r, const int y, const int gap) {
  if (gap >= 22) r.fillRect(LEFT + 40, y, CONTENT_W - 80, 1, true);
}

}  // namespace

bool renderWeatherCard(const CardContext& ctx, GfxRenderer& renderer) {
  if (ctx.io == nullptr) return false;
  // The cache text and its parsed record (~6 KB together) on the heap, only for this draw.
  auto text = makeUniqueNoThrow<char[]>(weather::CACHE_CAP + 1);
  auto rec = makeUniqueNoThrow<weather::Record>();
  auto facts = makeUniqueNoThrow<WeatherFacts>();
  if (!text || !rec || !facts) return false;
  if (!ctx.settings.weatherOn || !ctx.timeValid) return false;
  const int32_t got = ctx.io->readFileAt(weather::CACHE_PATH, 0, text.get(), weather::CACHE_CAP);
  if (got <= 0 || !weather::decodeCache(text.get(), static_cast<size_t>(got), *rec)) return false;
  text.reset();
  WeatherFacts& f = *facts;
  if (weathercard::computeWeatherFacts(ctx, *rec, f) != weathercard::Decline::None) return false;
  GfxRenderer& r = renderer;

  // Blocks top to bottom; the free height is shared out as gaps, and day rows fill what is left.
  const int alertsH = alertsBlockHeight(*rec, f);
  const int nowH = f.haveNow ? NOW_BLOCK_H : 0;
  const int stripH = f.hourCount >= 2 ? STRIP_H : 0;
  const int extraLine = f.offsetMismatch ? 24 : 0;
  const int fixedH = alertsH + nowH + stripH + DAY_HEADER_H + BOTTOM_LINES_H + extraLine;
  const int blocks = 2 + (nowH ? 1 : 0) + (stripH ? 1 : 0);  // alerts, days, [now], [strip]
  const int room = BOTTOM - TOP - fixedH - blocks * MIN_GAP;
  const int days = std::clamp(room / DAY_ROW_H, 1, std::min<int>(f.dayCount, 4));
  const int daysH = days * DAY_ROW_H;
  const int gap = std::max(MIN_GAP, (BOTTOM - TOP - fixedH - daysH) / blocks);

  int y = TOP;
  drawAlertsBlock(ctx, *rec, f, r, y, alertsH);
  y += alertsH + gap;
  if (nowH) {
    hairline(r, y - gap / 2, gap);
    drawNowBlock(ctx, f, r, y);
    y += nowH + gap;
  }
  if (stripH) {
    hairline(r, y - gap / 2, gap);
    drawStrip(ctx, *rec, f, r, y);
    y += stripH + gap;
  }
  hairline(r, y - gap / 2, gap);
  drawDayHeader(ctx, r, y);
  y += DAY_HEADER_H;
  const LocalDate today = localDateOf(ctx.utcNow, ctx.utcOffsetAt);
  for (int i = 0; i < days; i++) {
    const uint8_t at = static_cast<uint8_t>(f.firstDay + i);
    const LocalDate& date = f.dayDate[at];
    const bool isToday = date.year == today.year && date.month == today.month && date.day == today.day;
    drawDayRow(ctx, rec->forecast.days[at], date, isToday, r, y + i * DAY_ROW_H);
  }
  drawBottomLines(ctx, *rec, f, r, BOTTOM - BOTTOM_LINES_H + 6);
  return true;
}

}  // namespace sleepcards
