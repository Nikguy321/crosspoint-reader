#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

// The Weather card's parsed records (network/WeatherProtocol fills them, sleepcards/WeatherCache
// stores them, sleepcards/WeatherCard draws them). Plain data, so the card side builds on the host
// without the parsers.
namespace weather {

// Both URLs fit (the forecast query is ~520 bytes).
constexpr size_t URL_CAP = 640;
// Open-Meteo's answer to the query below: ~2.9 KB measured (48 hours, 7 days).
constexpr size_t FORECAST_BODY_CAP = 16384;
// NWS: ~230 B with no alert, 7-9 KB per alert as served (a state-wide answer ~32 KB). A body
// larger than this still proves alerts are in force (AlertsStatus::TooMany). PSRAM.
constexpr size_t ALERTS_BODY_CAP = 262144;

constexpr uint8_t MAX_HOURS = 48;
constexpr uint8_t MAX_DAYS = 7;
constexpr uint8_t MAX_ALERTS = 3;
// A null or missing value in any series.
constexpr int16_t NO_VALUE = INT16_MIN;

// Values are fixed-point SI: tenths of a degree C, of a m/s, of a hPa, of a mm; whole % and degrees.
struct Current {
  bool valid = false;
  int16_t tempC10 = NO_VALUE;
  int16_t feelsC10 = NO_VALUE;
  int16_t humidity = NO_VALUE;
  int16_t pressureHpa10 = NO_VALUE;
  int16_t windMs10 = NO_VALUE;
  int16_t gustMs10 = NO_VALUE;
  int16_t windDir = NO_VALUE;  // meteorological: where the wind blows FROM, 0 = north
  int16_t code = NO_VALUE;     // WMO weather code
};

struct Hour {
  int64_t t = 0;  // UTC start of the hour
  int16_t tempC10 = NO_VALUE;
  int16_t pop = NO_VALUE;  // chance of precipitation, %
  int16_t code = NO_VALUE;
  int16_t windMs10 = NO_VALUE;
};

struct Day {
  int64_t t = 0;  // the LOCATION's local midnight as UTC: label it by the date of t + 43200
  int16_t code = NO_VALUE;
  int16_t maxC10 = NO_VALUE;
  int16_t minC10 = NO_VALUE;
  int16_t pop = NO_VALUE;
  int16_t precipMm10 = NO_VALUE;
  int16_t windMaxMs10 = NO_VALUE;
  int16_t gustMaxMs10 = NO_VALUE;
  int16_t windDir = NO_VALUE;  // dominant, FROM
};

struct Forecast {
  int32_t utcOffsetS = 0;  // the location's offset at the fetch (only to warn when the reader's differs)
  Current current;
  uint8_t hourCount = 0;
  Hour hours[MAX_HOURS];
  uint8_t dayCount = 0;
  Day days[MAX_DAYS];
};

// In the order an alert outranks another; persisted by value: append only.
enum class Severity : uint8_t { Unknown = 0, Minor = 1, Moderate = 2, Severe = 3, Extreme = 4 };
enum class Urgency : uint8_t { Unknown = 0, Past = 1, Future = 2, Expected = 3, Immediate = 4 };
enum class MessageType : uint8_t { Other = 0, Alert = 1, Update = 2 };

constexpr size_t EVENT_CAP = 40;  // the longest NWS event name is 32 characters
constexpr size_t HEADLINE_CAP = 128;

struct Alert {
  Severity severity = Severity::Unknown;
  Urgency urgency = Urgency::Unknown;
  MessageType type = MessageType::Other;
  int64_t onset = 0;    // UTC; 0 = not given
  int64_t ends = 0;     // UTC end of the event; 0 = not given (use expires)
  int64_t expires = 0;  // UTC expiry of the MESSAGE (may fall before onset)
  char event[EVENT_CAP] = "";
  char headline[HEADLINE_CAP] = "";  // "" when the service sends null
  // When the event is over: ends, else expires (0 = unknown).
  int64_t endsOrExpires() const { return ends != 0 ? ends : expires; }
};

// What the last alerts check says. Persisted by value: append only.
enum class AlertsStatus : uint8_t {
  NotChecked = 0,  // never answered (the request failed, or none was made yet)
  None = 1,        // the service answered: nothing in force here
  Listed = 2,      // the service answered with alerts
  OutsideUs = 3,   // the point is outside the NWS area (never asked, or a 400 "out of bounds")
  TooMany = 4,     // the answer was too big to read: alerts are in force ("N+")
};

struct Alerts {
  AlertsStatus status = AlertsStatus::NotChecked;
  int64_t asOf = 0;    // UTC of the check the list comes from (0 = none)
  uint16_t total = 0;  // alerts in force at the check (Listed), or the lower bound (TooMany)
  uint8_t count = 0;   // the top MAX_ALERTS of them, most severe first
  Alert list[MAX_ALERTS];
  // A later check failed: this answer (status, list, asOf) is the last one that came, kept as it was.
  bool recheckFailed = false;
};

// Back to the default value in place: the records are a few hundred bytes to 1.6 KB, and `x = T{}`
// would build the temporary on the (loop task's) stack first.
template <typename T>
void resetInPlace(T& value) {
  value.~T();
  new (&value) T();
}

}  // namespace weather
