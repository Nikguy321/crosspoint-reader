#pragma once

#include <cstddef>
#include <cstdint>

#include "SleepCard.h"
#include "WeatherCache.h"

// WEATHER: the forecast network/WeatherFetch last saved (sleepcards/WeatherCache), for the saved
// location. Top to bottom: NWS alerts (a black band when any are in force, else one small line
// saying what the last check found), the conditions now, the next 24 hours (temperature line,
// rain-chance bars), the next days, and when the forecast was fetched, from where, and the credit.
// Entry point: sleepcards::renderWeatherCard() (declared in SleepCard.h).
//
// Honesty rules, because a sleep card stays on the glass for days: every time is ABSOLUTE ("Forecast
// from 7:10 AM Thu"), never an age; "Now" comes from Open-Meteo's current conditions only within an
// hour of the fetch, otherwise from the hourly forecast and labelled so; hours already past are
// dropped; "No alerts" is said only after a check that found none, with its time; days are labelled
// by the date of their middle (daily.time is the LOCATION's midnight) in the reader's own zone.
// The card declines (Shuffle moves on, or the logo shows) without Weather on, a set clock, a
// location, a readable cache, or when the forecast is over 36 h old, for a place over 5 km away, or
// has no day left from today on. It never fetches; drawing reads one file of at most 4 KB.
namespace sleepcards::weathercard {

// The cache older than this is not shown.
constexpr int64_t MAX_AGE_S = 36 * 3600;
// "Now" from the fetch's current conditions within this long of the fetch.
constexpr int64_t CURRENT_FOR_S = 3600;
// The hours the strip shows, from the one covering the draw time.
constexpr uint8_t STRIP_HOURS = 24;

enum class Decline : uint8_t { None, NoClock, Off, NoLocation, NoCache, Stale, FromTheFuture, Moved, Ended };

struct WeatherFacts {
  // The conditions block: false past the last hourly entry (the days show alone).
  bool haveNow = false;
  bool nowIsCurrent = false;  // Open-Meteo's current conditions; else the hourly forecast at nowHourUtc
  int64_t nowHourUtc = 0;
  int16_t tempC10 = weather::NO_VALUE;
  int16_t feelsC10 = weather::NO_VALUE;  // current conditions only
  int16_t windMs10 = weather::NO_VALUE;
  int16_t gustMs10 = weather::NO_VALUE;
  int16_t windDir = weather::NO_VALUE;
  int16_t humidity = weather::NO_VALUE;
  int16_t pressureHpa10 = weather::NO_VALUE;
  int16_t code = weather::NO_VALUE;

  uint8_t firstHour = 0;  // the strip: forecast.hours[firstHour ..] (hourCount of them)
  uint8_t hourCount = 0;
  uint8_t firstDay = 0;  // the day rows: forecast.days[firstDay ..], today's first
  uint8_t dayCount = 0;
  LocalDate dayDate[weather::MAX_DAYS];  // each day's label date (by its middle), reader's zone

  uint8_t alertIndex[weather::MAX_ALERTS];  // alerts.list entries still in force at the draw time
  uint8_t alertCount = 0;

  // The location's UTC offset at the fetch differs from the reader's then: its clock's zone may be
  // wrong (or the reader travelled). The card says so; it never changes a clock.
  bool offsetMismatch = false;
};

// Everything the card shows from one cache record, at ctx.utcNow. Decline::None = draw.
Decline computeWeatherFacts(const CardContext& ctx, const weather::Record& record, WeatherFacts& out);

// Shuffle's check: the cache header (one small read) says the card has something to show.
bool cacheLooksUsable(const CardContext& ctx);

// Alerts that may still be in force at the draw time: the listed ones not yet over
// (facts.alertCount) plus those the check counted but did not keep (only the MAX_ALERTS most severe
// are kept; the rest have unknown end times). A listed alert known to have ended is not counted.
uint16_t alertsInForce(const weather::Alerts& alerts, const WeatherFacts& facts);

// An alert's time line at ctx.utcNow: "until 4:00 PM Wed" once it has begun, "from 10:00 Tue
// until 16:00 Wed" before it does, "from 10:00 Wed" when its end (an `expires` without `ends`)
// falls at or before its onset, or "" with neither.
void formatAlertTime(const CardContext& ctx, const weather::Alert& alert, char* out, size_t cap);

// ---- units (the cache is SI; converted only here) ----------------------------------------------
// Whole degrees in the setting's unit, rounded half away from zero (never "-0").
int temperature(int16_t c10, WeatherUnits units);
// "11°" / "52°"; "--" for no value.
void formatTemperature(int16_t c10, WeatherUnits units, char* out, size_t cap);
// Whole km/h or mph.
int windSpeed(int16_t ms10, WeatherUnits units);
// "from NW 14 km/h, gusts 30" (the direction the wind comes FROM); "Calm" under 1 km/h / 1 mph;
// gusts only when given and stronger.
void formatWind(int16_t dirFrom, int16_t ms10, int16_t gustMs10, WeatherUnits units, char* out, size_t cap);
// "2.7 mm" / "0.11 in".
void formatPrecipitation(int16_t mm10, WeatherUnits units, char* out, size_t cap);
// "1018 hPa" / "30.06 inHg".
void formatPressure(int16_t hpa10, WeatherUnits units, char* out, size_t cap);
// The short WMO wording the three devices share ("Mostly clear", "Lt rain"); "Code 42" for one
// not in the table, "--" for none.
void conditionName(int16_t code, char* out, size_t cap);

enum class Icon : uint8_t {
  Clear,
  PartlyCloudy,
  Overcast,
  Fog,
  Drizzle,
  Rain,
  Freezing,
  Snow,
  Showers,
  Thunder,
  Unknown
};
Icon iconFor(int16_t code);

}  // namespace sleepcards::weathercard
