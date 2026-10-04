#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

#include "WeatherData.h"

// The Weather card's data (Display > Sleep Screen Cards > Weather, X4 Pro): the pure half, host-tested
// in test/weather_protocol. Builds the two requests, reads the two services' answers into small
// parsed records, and decides when a fetch is due. The device half (the HTTPS calls on a station
// already up, the cache write) is network/WeatherFetch; the card reads the cache
// (sleepcards/WeatherCache) and draws it (sleepcards/WeatherCard).
//
//   Forecast: GET https://api.open-meteo.com/v1/forecast?latitude=47.61&longitude=-122.33&current=...
//             (free, no key, CC BY 4.0: the card credits "Weather: Open-Meteo.com"). SI units and
//             unix times; the card converts at draw time.
//   Alerts:   GET https://api.weather.gov/alerts/active?point=47.61,-122.33&status=actual
//             (US National Weather Service, public domain; US points only).
//
// Both are sent the location rounded to 0.01 degrees (about 1 km), never more.
namespace weather {

constexpr const char* OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast";
constexpr const char* NWS_ALERTS_URL = "https://api.weather.gov/alerts/active";
constexpr const char* NWS_ACCEPT = "application/geo+json";

// ---- requests ----------------------------------------------------------------------------------

// The coordinate as sent: rounded to 0.01 degree (half away from zero), never "-0.00".
double roundCoordinate(double degrees);

// The Open-Meteo forecast URL for the point (rounded here). Returns its length, or 0 (out = "")
// when the point is not a valid location or the URL does not fit cap.
size_t buildForecastUrl(double lat, double lon, char* out, size_t cap);
// The NWS active-alerts URL for the point, rounded to 0.01 as above and written with up to 4
// decimals and no trailing zeros ("47.61,-122.33", "45,-130"), &status=actual.
size_t buildAlertsUrl(double lat, double lon, char* out, size_t cap);

// A rough box around the NWS area (the states, Alaska, Hawaii, Puerto Rico and the Pacific
// territories). Outside it no alerts request is made; inside it the service may still answer
// "out of bounds" (Canada and Mexico near the border), which reads the same.
bool inNwsArea(double lat, double lon);

// ---- answers -----------------------------------------------------------------------------------

// Open-Meteo, fed in pieces of any size (a chunked body byte by byte gives the same result). About
// 1.7 KB: the device keeps it on the heap.
class ForecastParser {
 public:
  ForecastParser();
  ForecastParser(const ForecastParser&) = delete;
  ForecastParser& operator=(const ForecastParser&) = delete;
  void feed(const char* data, size_t len) { json.feed(data, len); }
  // True with out filled when the body was one complete JSON object with the offset, at least one
  // hour and one day, and strictly increasing times; an {"error":true,...} answer, HTML (a captive
  // portal's 200), a cut-off body or a value of the wrong type is refused (out reset).
  bool finish(Forecast& out);

  // The JSON path as the tokenizer walks it (public for the callbacks).
  static constexpr uint8_t MAX_DEPTH = 8;
  static constexpr size_t KEY_CAP = 32;
  void onKey(const char* key, size_t len);
  void onValue(const char* text, size_t len, bool isNumber, bool isNull);
  void onBool(bool value);
  void onOpen(bool array);
  void onClose();

 private:
  void takeValue();
  int16_t* field(uint8_t section, const char* key, uint8_t index, bool& tenths);

  StreamingJsonParser json;
  Forecast f;
  char keys[MAX_DEPTH + 1][KEY_CAP] = {};
  bool isArray[MAX_DEPTH + 1] = {};
  uint8_t index[MAX_DEPTH + 1] = {};
  uint8_t depth = 0;
  bool rootOpened = false;
  bool rootClosed = false;
  bool bad = false;
  bool errorAnswer = false;
  bool offsetSeen = false;
  uint8_t hourTimes = 0;
  uint8_t dayTimes = 0;
};

bool parseForecast(const char* body, size_t len, Forecast& out);

// NWS: the alerts in force at nowUtc (status Actual only; urgency Past, Cancel messages and alerts
// already over dropped), the top MAX_ALERTS by severity, then urgency, then onset; total = all
// kept. updatedUtc = the collection's "updated" stamp (0 when absent). False for anything but a
// complete FeatureCollection (out reset; status stays NotChecked).
bool parseAlerts(const char* body, size_t len, int64_t nowUtc, Alerts& out, int64_t& updatedUtc);

// How the alerts request went (what GeolocateClient::request said).
enum class Transfer : uint8_t { Ok, NoMemory, Failed, TooLarge };

// The new alerts record from one request into out (a scratch record: on false its contents mean
// nothing), or false to KEEP the old one (marked recheckFailed, it shows with its own as-of): a 200 that parses ->
// None/Listed (asOf = nowUtc); a 400 whose detail says "out of bounds" -> OutsideUs; a body too large to read ->
// TooMany with a lower bound from its size (bodyBytes = its Content-Length, or what was read); anything else -> false.
bool alertsFromReply(Transfer transfer, int httpStatus, const char* body, size_t len, size_t bodyBytes, int64_t nowUtc,
                     Alerts& out);

// The lower bound of "N+ alerts" for a body of this size (each alert is under 16 KB as served).
uint16_t tooManyLowerBound(size_t bodyBytes);

// "2026-10-03T16:18:00-05:00" / "...Z" / "...+00:00" -> UTC seconds. False when malformed.
bool parseIso8601(const char* text, int64_t& utc);
// An HTTP Date header, "Sat, 03 Oct 2026 22:40:22 GMT" -> UTC seconds.
bool parseHttpDate(const char* text, int64_t& utc);

// ---- when to fetch -----------------------------------------------------------------------------

// The cache counts as "here" within this distance of the location.
constexpr double SAME_PLACE_KM = 5.0;

double distanceKm(double lat1, double lon1, double lat2, double lon2);

enum class Decision : uint8_t {
  Run,
  Off,            // Weather is off (the opt-in): nothing is sent
  NotConnected,   // no station up: this never starts the radio
  DeviceNetwork,  // the book-sync peer's or hub's hotspot: no internet behind it
  NoClock,        // no set clock to stamp and age the forecast by
  NoLocation,     // no saved location
  RetryWait,      // an attempt was made too recently (whatever came of it)
  Fresh,          // the cache is recent enough and for this place
};

struct Situation {
  bool enabled = false;
  bool connected = false;
  bool deviceNetwork = false;
  bool clockValid = false;
  bool locationValid = false;
  int64_t now = 0;          // UTC
  int64_t lastAttempt = 0;  // UTC of the last attempt (stamped before its request); 0 = none known
  bool cacheValid = false;
  int64_t cacheFetch = 0;  // UTC the cached forecast was fetched
  double cacheLat = 0;
  double cacheLon = 0;
  double lat = 0;  // the saved location now
  double lon = 0;
  uint32_t maxAgeS = 3600;  // a cache older than this is refreshed
  uint32_t retryS = 600;    // no new attempt this soon after the last one
};

// Due when the cache is missing, older than maxAgeS, from the future (a clock that was wrong), or
// for a place more than SAME_PLACE_KM from the location - and no attempt was made in the last
// retryS seconds.
Decision decide(const Situation& s);
// "off", "no-wifi", ... for the one log line.
const char* decisionName(Decision d);

}  // namespace weather
