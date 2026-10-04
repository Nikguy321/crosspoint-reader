#include "WeatherProtocol.h"

#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace weather {
namespace {

// Forecast variables, as asked for and as stored. The URL and the parser use the same names.
constexpr const char* FORECAST_QUERY =
    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,pressure_msl,wind_speed_10m,"
    "wind_direction_10m,wind_gusts_10m,weather_code"
    "&hourly=temperature_2m,precipitation_probability,weather_code,wind_speed_10m&forecast_hours=48"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,"
    "wind_speed_10m_max,wind_gusts_10m_max,wind_direction_10m_dominant&forecast_days=7"
    "&timeformat=unixtime&timezone=auto&wind_speed_unit=ms";

// 2020-01-01 .. 2100-01-01: a stamp outside is not a forecast time.
constexpr int64_t MIN_TIME = 1577836800;
constexpr int64_t MAX_TIME = 4102444800;

bool validPoint(const double lat, const double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

// A JSON number exactly as the grammar spells it (the tokenizer hands over any run of [-+.eE0-9]).
bool parseJsonNumber(const char* text, const size_t len, double& out) {
  size_t i = 0;
  if (i < len && text[i] == '-') i++;
  if (i >= len || text[i] < '0' || text[i] > '9') return false;
  if (text[i] == '0') {
    i++;
  } else {
    while (i < len && text[i] >= '0' && text[i] <= '9') i++;
  }
  if (i < len && text[i] == '.') {
    i++;
    const size_t start = i;
    while (i < len && text[i] >= '0' && text[i] <= '9') i++;
    if (i == start) return false;
  }
  if (i < len && (text[i] == 'e' || text[i] == 'E')) {
    i++;
    if (i < len && (text[i] == '+' || text[i] == '-')) i++;
    const size_t start = i;
    while (i < len && text[i] >= '0' && text[i] <= '9') i++;
    if (i == start) return false;
  }
  if (i != len || len >= 32) return false;
  char buf[32];
  std::memcpy(buf, text, len);
  buf[len] = '\0';
  const double value = std::strtod(buf, nullptr);
  if (!std::isfinite(value)) return false;
  out = value;
  return true;
}

int16_t toFixed(const double value, const double scale) {
  const double v = std::round(value * scale);
  return static_cast<int16_t>(std::clamp(v, -32767.0, 32767.0));
}

bool keyIs(const char* key, const char* name) { return std::strcmp(key, name) == 0; }

// Days since 1970-01-01 of a civil date (proleptic Gregorian).
int64_t daysFromCivil(int y, const int m, const int d) {
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool leapYear(const int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int monthDays(const int y, const int m) {
  static constexpr int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && leapYear(y) ? 29 : DAYS[m - 1];
}

// n digits at p as a number; false unless all are digits.
bool digits(const char* p, const int n, int& out) {
  out = 0;
  for (int i = 0; i < n; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    out = out * 10 + (p[i] - '0');
  }
  return true;
}

bool civilToUtc(const int y, const int mo, const int d, const int h, const int mi, const int s, int64_t& utc) {
  if (y < 1970 || y > 2199 || mo < 1 || mo > 12 || d < 1 || d > monthDays(y, mo) || h > 23 || mi > 59 || s > 60) {
    return false;
  }
  utc = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
  return true;
}

// Printable ASCII of a JSON string (escapes pass through the tokenizer as text: "é" -> "?").
void cleanText(const char* text, const size_t len, char* out, const size_t cap) {
  size_t n = 0;
  for (size_t i = 0; i < len && n + 1 < cap; i++) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == '\\' && i + 5 < len && text[i + 1] == 'u') {
      out[n++] = '?';
      i += 5;
      continue;
    }
    if (c < 0x20 || c >= 0x7F) {
      if (n > 0 && out[n - 1] != ' ') out[n++] = ' ';
      continue;
    }
    out[n++] = static_cast<char>(c);
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
}

constexpr double PI_D = 3.14159265358979323846;  // not PI: Arduino.h defines it as a macro

}  // namespace

// ---- requests ----------------------------------------------------------------------------------

double roundCoordinate(const double degrees) {
  const double r = std::round(degrees * 100.0) / 100.0;
  return r == 0.0 ? 0.0 : r;  // no -0
}

size_t buildForecastUrl(const double lat, const double lon, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (!validPoint(lat, lon)) return 0;
  const int n = std::snprintf(out, cap, "%s?latitude=%.2f&longitude=%.2f%s", OPEN_METEO_URL, roundCoordinate(lat),
                              roundCoordinate(lon), FORECAST_QUERY);
  if (n <= 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

namespace {
// Up to 4 decimals with the trailing zeros (and a bare point) dropped: "47.61", "45".
void shortCoordinate(const double v, char* out, const size_t cap) {
  std::snprintf(out, cap, "%.4f", v);
  char* dot = std::strchr(out, '.');
  if (dot == nullptr) return;
  char* end = out + std::strlen(out);
  while (end > dot + 1 && end[-1] == '0') *--end = '\0';
  if (end == dot + 1) *dot = '\0';
}
}  // namespace

size_t buildAlertsUrl(const double lat, const double lon, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (!validPoint(lat, lon)) return 0;
  char a[16];
  char b[16];
  shortCoordinate(roundCoordinate(lat), a, sizeof(a));
  shortCoordinate(roundCoordinate(lon), b, sizeof(b));
  const int n = std::snprintf(out, cap, "%s?point=%s,%s&status=actual", NWS_ALERTS_URL, a, b);
  if (n <= 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

bool inNwsArea(const double lat, const double lon) {
  if (!validPoint(lat, lon)) return false;
  struct Box {
    double south, north, west, east;
  };
  static constexpr Box AREAS[] = {
      {23.0, 50.0, -131.0, -65.0},     // the contiguous states and their coastal waters
      {51.0, 72.0, -180.0, -129.5},    // Alaska
      {51.0, 55.0, 172.0, 180.0},      // the western Aleutians
      {18.5, 22.5, -161.0, -154.5},    // Hawaii
      {17.5, 18.8, -68.0, -64.5},      // Puerto Rico and the Virgin Islands
      {13.0, 21.0, 144.5, 146.5},      // Guam and the Northern Marianas
      {-15.0, -10.5, -171.5, -168.0},  // American Samoa
  };
  for (const Box& b : AREAS) {
    if (lat >= b.south && lat <= b.north && lon >= b.west && lon <= b.east) return true;
  }
  return false;
}

// ---- Open-Meteo --------------------------------------------------------------------------------

namespace {
ForecastParser& fp(void* ctx) { return *static_cast<ForecastParser*>(ctx); }

const JsonCallbacks FORECAST_CALLBACKS = {
    nullptr,
    [](void* c, const char* k, size_t n) { fp(c).onKey(k, n); },
    [](void* c, const char* v, size_t n) { fp(c).onValue(v, n, false, false); },
    [](void* c, const char* v, size_t n) { fp(c).onValue(v, n, true, false); },
    [](void* c, bool v) { fp(c).onBool(v); },
    [](void* c) { fp(c).onValue("", 0, false, true); },
    [](void* c) { fp(c).onOpen(false); },
    [](void* c) { fp(c).onClose(); },
    [](void* c) { fp(c).onOpen(true); },
    [](void* c) { fp(c).onClose(); },
};

JsonCallbacks forecastCallbacks(ForecastParser* self) {
  JsonCallbacks cb = FORECAST_CALLBACKS;
  cb.ctx = self;
  return cb;
}

constexpr uint8_t SECTION_NONE = 0;
constexpr uint8_t SECTION_CURRENT = 1;
constexpr uint8_t SECTION_HOURLY = 2;
constexpr uint8_t SECTION_DAILY = 3;

uint8_t sectionOf(const char* key) {
  if (keyIs(key, "current")) return SECTION_CURRENT;
  if (keyIs(key, "hourly")) return SECTION_HOURLY;
  if (keyIs(key, "daily")) return SECTION_DAILY;
  return SECTION_NONE;
}
}  // namespace

ForecastParser::ForecastParser() : json(forecastCallbacks(this)) {}

void ForecastParser::onKey(const char* key, const size_t len) {
  if (depth == 0 || isArray[depth]) return;
  if (len >= KEY_CAP) {
    keys[depth][0] = '\0';  // matches nothing
    return;
  }
  std::memcpy(keys[depth], key, len);
  keys[depth][len] = '\0';
}

void ForecastParser::takeValue() {
  if (depth == 0) {
    bad = true;  // a value outside the root object
    return;
  }
  if (isArray[depth]) {
    if (index[depth] < 255) index[depth]++;
  }
}

void ForecastParser::onOpen(const bool array) {
  if (rootClosed || depth >= MAX_DEPTH || (depth == 0 && array)) {
    bad = true;
    return;
  }
  if (depth > 0) takeValue();
  if (depth == 0) rootOpened = true;
  depth++;
  keys[depth][0] = '\0';
  isArray[depth] = array;
  index[depth] = 0;
}

void ForecastParser::onClose() {
  if (depth == 0) {
    bad = true;
    return;
  }
  depth--;
  if (depth == 0) rootClosed = true;
}

void ForecastParser::onBool(const bool value) {
  if (depth == 1 && keyIs(keys[1], "error") && value) errorAnswer = true;
  // A true/false inside current, hourly or daily is a value of the wrong type.
  if (depth >= 2 && sectionOf(keys[1]) != SECTION_NONE) bad = true;
  takeValue();
}

int16_t* ForecastParser::field(const uint8_t section, const char* key, const uint8_t i, bool& tenths) {
  tenths = false;
  if (section == SECTION_CURRENT) {
    Current& c = f.current;
    tenths = true;
    if (keyIs(key, "temperature_2m")) return &c.tempC10;
    if (keyIs(key, "apparent_temperature")) return &c.feelsC10;
    if (keyIs(key, "pressure_msl")) return &c.pressureHpa10;
    if (keyIs(key, "wind_speed_10m")) return &c.windMs10;
    if (keyIs(key, "wind_gusts_10m")) return &c.gustMs10;
    tenths = false;
    if (keyIs(key, "relative_humidity_2m")) return &c.humidity;
    if (keyIs(key, "wind_direction_10m")) return &c.windDir;
    if (keyIs(key, "weather_code")) return &c.code;
    return nullptr;
  }
  if (section == SECTION_HOURLY) {
    if (i >= MAX_HOURS) return nullptr;
    Hour& h = f.hours[i];
    tenths = true;
    if (keyIs(key, "temperature_2m")) return &h.tempC10;
    if (keyIs(key, "wind_speed_10m")) return &h.windMs10;
    tenths = false;
    if (keyIs(key, "precipitation_probability")) return &h.pop;
    if (keyIs(key, "weather_code")) return &h.code;
    return nullptr;
  }
  if (section == SECTION_DAILY) {
    if (i >= MAX_DAYS) return nullptr;
    Day& d = f.days[i];
    tenths = true;
    if (keyIs(key, "temperature_2m_max")) return &d.maxC10;
    if (keyIs(key, "temperature_2m_min")) return &d.minC10;
    if (keyIs(key, "precipitation_sum")) return &d.precipMm10;
    if (keyIs(key, "wind_speed_10m_max")) return &d.windMaxMs10;
    if (keyIs(key, "wind_gusts_10m_max")) return &d.gustMaxMs10;
    tenths = false;
    if (keyIs(key, "weather_code")) return &d.code;
    if (keyIs(key, "precipitation_probability_max")) return &d.pop;
    if (keyIs(key, "wind_direction_10m_dominant")) return &d.windDir;
    return nullptr;
  }
  return nullptr;
}

void ForecastParser::onValue(const char* text, const size_t len, const bool isNumber, const bool isNull) {
  if (depth == 0) {
    bad = true;
    return;
  }
  const uint8_t i = isArray[depth] ? index[depth] : 0;
  takeValue();
  double v = 0;
  if (isNumber && !parseJsonNumber(text, len, v)) {
    bad = true;
    return;
  }

  if (depth == 1) {
    if (keyIs(keys[1], "utc_offset_seconds")) {
      if (!isNumber || v != std::floor(v) || std::fabs(v) > 18 * 3600) {
        bad = true;
        return;
      }
      f.utcOffsetS = static_cast<int32_t>(v);
      offsetSeen = true;
    }
    return;
  }
  const uint8_t section = sectionOf(keys[1]);
  if (section == SECTION_NONE) return;

  // current: {"time":..,"interval":..,"temperature_2m":..}; hourly / daily: {"time":[..],"<var>":[..]}
  const bool inCurrent = section == SECTION_CURRENT && depth == 2 && !isArray[2];
  const bool inSeries = section != SECTION_CURRENT && depth == 3 && isArray[3] && !isArray[2];
  if (!inCurrent && !inSeries) return;
  const char* key = keys[2];

  if (inSeries && keyIs(key, "time")) {
    if (!isNumber || v != std::floor(v) || v < MIN_TIME || v > MAX_TIME) {
      bad = true;
      return;
    }
    const auto t = static_cast<int64_t>(v);
    if (section == SECTION_HOURLY && i < MAX_HOURS) {
      f.hours[i].t = t;
      hourTimes = static_cast<uint8_t>(i + 1);
    } else if (section == SECTION_DAILY && i < MAX_DAYS) {
      f.days[i].t = t;
      dayTimes = static_cast<uint8_t>(i + 1);
    }
    return;
  }
  bool tenths = false;
  int16_t* target = field(section, key, i, tenths);
  if (target == nullptr) return;
  if (isNull) {
    *target = NO_VALUE;
    return;
  }
  if (!isNumber) {
    bad = true;  // a string where a number belongs
    return;
  }
  *target = toFixed(v, tenths ? 10.0 : 1.0);
  if (inCurrent) f.current.valid = true;
}

bool ForecastParser::finish(Forecast& out) {
  resetInPlace(out);
  if (bad || errorAnswer || json.hasError() || !rootOpened || !rootClosed || !offsetSeen || hourTimes == 0 ||
      dayTimes == 0) {
    return false;
  }
  for (uint8_t i = 1; i < hourTimes; i++) {
    if (f.hours[i].t <= f.hours[i - 1].t) return false;
  }
  for (uint8_t i = 1; i < dayTimes; i++) {
    if (f.days[i].t <= f.days[i - 1].t) return false;
  }
  out = f;
  out.hourCount = hourTimes;
  out.dayCount = dayTimes;
  // Entries past the time series (a longer value array) are not hours.
  for (uint8_t i = hourTimes; i < MAX_HOURS; i++) out.hours[i] = Hour{};
  for (uint8_t i = dayTimes; i < MAX_DAYS; i++) out.days[i] = Day{};
  // "current" with no temperature is not a reading.
  if (out.current.tempC10 == NO_VALUE) out.current.valid = false;
  return true;
}

bool parseForecast(const char* body, const size_t len, Forecast& out) {
  resetInPlace(out);
  if (body == nullptr) return false;
  auto parser = makeUniqueNoThrow<ForecastParser>();  // ~1.7 KB: not on the loop task's stack
  if (!parser) return false;
  parser->feed(body, len);
  return parser->finish(out);
}

// ---- NWS alerts --------------------------------------------------------------------------------

namespace {

constexpr uint8_t ALERT_DEPTH = 12;  // geometry polygons nest 6 deep below the root
constexpr size_t ALERT_KEY_CAP = 24;

Severity severityOf(const char* s) {
  if (keyIs(s, "Extreme")) return Severity::Extreme;
  if (keyIs(s, "Severe")) return Severity::Severe;
  if (keyIs(s, "Moderate")) return Severity::Moderate;
  if (keyIs(s, "Minor")) return Severity::Minor;
  return Severity::Unknown;
}

Urgency urgencyOf(const char* s) {
  if (keyIs(s, "Immediate")) return Urgency::Immediate;
  if (keyIs(s, "Expected")) return Urgency::Expected;
  if (keyIs(s, "Future")) return Urgency::Future;
  if (keyIs(s, "Past")) return Urgency::Past;
  return Urgency::Unknown;
}

// a before b in the list: more severe, then more urgent, then the earlier onset.
bool outranks(const Alert& a, const Alert& b) {
  if (a.severity != b.severity) return a.severity > b.severity;
  if (a.urgency != b.urgency) return a.urgency > b.urgency;
  return a.onset < b.onset;
}

struct AlertScan {
  int64_t now = 0;
  Alerts* out = nullptr;
  int64_t updated = 0;
  char keys[ALERT_DEPTH + 1][ALERT_KEY_CAP] = {};
  bool isArray[ALERT_DEPTH + 1] = {};
  uint8_t depth = 0;
  bool rootOpened = false;
  bool rootClosed = false;
  bool bad = false;
  bool collection = false;  // "type": "FeatureCollection"
  bool featuresSeen = false;

  // The feature being read.
  Alert alert;
  bool actual = false;
  bool cancel = false;

  // Inside features[i] (depth 3), or its properties (depth 4).
  bool atFeature() const { return depth == 3 && isArray[2] && !isArray[3] && keyIs(keys[1], "features"); }
  bool atProperties() const {
    return depth == 4 && isArray[2] && !isArray[3] && !isArray[4] && keyIs(keys[1], "features") &&
           keyIs(keys[3], "properties");
  }

  void keep() {
    if (!actual || cancel || alert.urgency == Urgency::Past || alert.event[0] == '\0') return;
    const int64_t end = alert.endsOrExpires();
    if (end != 0 && end <= now) return;  // already over
    if (out->total < UINT16_MAX) out->total++;
    // Insert into the top MAX_ALERTS, most severe first.
    uint8_t pos = out->count;
    while (pos > 0 && outranks(alert, out->list[pos - 1])) pos--;
    if (pos >= MAX_ALERTS) return;
    const uint8_t last = std::min<uint8_t>(out->count, MAX_ALERTS - 1);
    for (uint8_t i = last; i > pos; i--) out->list[i] = out->list[i - 1];
    out->list[pos] = alert;
    if (out->count < MAX_ALERTS) out->count++;
  }
};

AlertScan& as(void* ctx) { return *static_cast<AlertScan*>(ctx); }

void alertKey(void* ctx, const char* key, const size_t len) {
  AlertScan& a = as(ctx);
  if (a.depth == 0 || a.isArray[a.depth]) return;
  if (len >= ALERT_KEY_CAP) {
    a.keys[a.depth][0] = '\0';
    return;
  }
  std::memcpy(a.keys[a.depth], key, len);
  a.keys[a.depth][len] = '\0';
}

void alertOpen(void* ctx, const bool array) {
  AlertScan& a = as(ctx);
  if (a.rootClosed || (a.depth == 0 && array)) {
    a.bad = true;
    return;
  }
  if (a.depth >= ALERT_DEPTH) {
    a.bad = true;
    return;
  }
  if (a.depth == 0) a.rootOpened = true;
  if (a.depth == 1 && array && keyIs(a.keys[1], "features")) a.featuresSeen = true;
  a.depth++;
  a.keys[a.depth][0] = '\0';
  a.isArray[a.depth] = array;
  if (a.atFeature()) {
    resetInPlace(a.alert);
    a.actual = false;
    a.cancel = false;
  }
}

void alertClose(void* ctx) {
  AlertScan& a = as(ctx);
  if (a.depth == 0) {
    a.bad = true;
    return;
  }
  if (a.atFeature()) a.keep();
  a.depth--;
  if (a.depth == 0) a.rootClosed = true;
}

void alertString(void* ctx, const char* value, const size_t len) {
  AlertScan& a = as(ctx);
  if (a.depth == 0) {
    a.bad = true;
    return;
  }
  if (a.depth == 1) {
    if (keyIs(a.keys[1], "type")) a.collection = len == 17 && std::memcmp(value, "FeatureCollection", 17) == 0;
    if (keyIs(a.keys[1], "updated")) parseIso8601(value, a.updated);
    return;
  }
  if (!a.atProperties()) return;
  const char* key = a.keys[4];
  char word[24];
  cleanText(value, len, word, sizeof(word));
  if (keyIs(key, "event")) {
    cleanText(value, len, a.alert.event, sizeof(a.alert.event));
  } else if (keyIs(key, "headline")) {
    cleanText(value, len, a.alert.headline, sizeof(a.alert.headline));
  } else if (keyIs(key, "severity")) {
    a.alert.severity = severityOf(word);
  } else if (keyIs(key, "urgency")) {
    a.alert.urgency = urgencyOf(word);
  } else if (keyIs(key, "status")) {
    a.actual = keyIs(word, "Actual");
  } else if (keyIs(key, "messageType")) {
    a.cancel = keyIs(word, "Cancel");
    a.alert.type = keyIs(word, "Alert")    ? MessageType::Alert
                   : keyIs(word, "Update") ? MessageType::Update
                                           : MessageType::Other;
  } else if (keyIs(key, "onset") || keyIs(key, "ends") || keyIs(key, "expires")) {
    char stamp[40];
    cleanText(value, len, stamp, sizeof(stamp));
    int64_t t = 0;
    if (!parseIso8601(stamp, t)) t = 0;
    if (keyIs(key, "onset")) a.alert.onset = t;
    if (keyIs(key, "ends")) a.alert.ends = t;
    if (keyIs(key, "expires")) a.alert.expires = t;
  }
}

void alertScalar(void* ctx) {
  AlertScan& a = as(ctx);
  if (a.depth == 0) a.bad = true;
}

}  // namespace

bool parseAlerts(const char* body, const size_t len, const int64_t nowUtc, Alerts& out, int64_t& updatedUtc) {
  resetInPlace(out);
  updatedUtc = 0;
  if (body == nullptr) return false;
  // The path keys and the tokenizer (~1.2 KB) live on the heap, not the loop task's stack.
  struct Work {
    AlertScan scan;
    StreamingJsonParser parser{JsonCallbacks{}};
  };
  auto work = makeUniqueNoThrow<Work>();
  if (!work) return false;
  AlertScan& scan = work->scan;
  scan.now = nowUtc;
  scan.out = &out;
  JsonCallbacks cb{};
  cb.ctx = &scan;
  cb.onKey = &alertKey;
  cb.onString = &alertString;
  cb.onNumber = [](void* c, const char*, size_t) { alertScalar(c); };
  cb.onBool = [](void* c, bool) { alertScalar(c); };
  cb.onNull = &alertScalar;
  cb.onObjectStart = [](void* c) { alertOpen(c, false); };
  cb.onObjectEnd = &alertClose;
  cb.onArrayStart = [](void* c) { alertOpen(c, true); };
  cb.onArrayEnd = &alertClose;
  work->parser = StreamingJsonParser(cb);
  StreamingJsonParser& parser = work->parser;
  parser.feed(body, len);
  const bool ok =
      !scan.bad && !parser.hasError() && scan.rootOpened && scan.rootClosed && scan.collection && scan.featuresSeen;
  if (!ok) {
    resetInPlace(out);
    return false;
  }
  updatedUtc = scan.updated;
  out.status = out.total > 0 ? AlertsStatus::Listed : AlertsStatus::None;
  out.asOf = nowUtc;
  return true;
}

uint16_t tooManyLowerBound(const size_t bodyBytes) {
  const size_t n = bodyBytes / 16384;
  return static_cast<uint16_t>(std::clamp<size_t>(n, 1, 999));
}

bool alertsFromReply(const Transfer transfer, const int httpStatus, const char* body, const size_t len,
                     const size_t bodyBytes, const int64_t nowUtc, Alerts& out) {
  if (transfer == Transfer::TooLarge) {
    resetInPlace(out);
    out.status = AlertsStatus::TooMany;
    out.asOf = nowUtc;
    out.total = tooManyLowerBound(bodyBytes);
    return true;
  }
  if (transfer != Transfer::Ok || body == nullptr) return false;
  if (httpStatus == 200) {
    int64_t updated = 0;
    return parseAlerts(body, len, nowUtc, out, updated);
  }
  if (httpStatus == 400) {
    // {"title":"Invalid Parameter","status":400,"detail":"Parameter \"point\" is invalid: out of bounds"}
    const char* detail = nullptr;
    for (size_t i = 0; i + 8 <= len; i++) {
      if (std::memcmp(body + i, "\"detail\"", 8) == 0) {
        detail = body + i;
        break;
      }
    }
    if (detail == nullptr) return false;
    const size_t rest = len - static_cast<size_t>(detail - body);
    const char* lineEnd = static_cast<const char*>(std::memchr(detail, '\n', rest));
    const size_t span = lineEnd ? static_cast<size_t>(lineEnd - detail) : rest;
    constexpr char NEEDLE[] = "out of bounds";
    for (size_t i = 0; i + sizeof(NEEDLE) - 1 <= span; i++) {
      if (std::memcmp(detail + i, NEEDLE, sizeof(NEEDLE) - 1) == 0) {
        resetInPlace(out);
        out.status = AlertsStatus::OutsideUs;
        out.asOf = nowUtc;
        return true;
      }
    }
  }
  return false;
}

// ---- times -------------------------------------------------------------------------------------

bool parseIso8601(const char* text, int64_t& utc) {
  utc = 0;
  if (text == nullptr) return false;
  const size_t n = std::strlen(text);
  if (n < 19 || text[4] != '-' || text[7] != '-' || (text[10] != 'T' && text[10] != ' ') || text[13] != ':' ||
      text[16] != ':') {
    return false;
  }
  int y, mo, d, h, mi, s;
  if (!digits(text, 4, y) || !digits(text + 5, 2, mo) || !digits(text + 8, 2, d) || !digits(text + 11, 2, h) ||
      !digits(text + 14, 2, mi) || !digits(text + 17, 2, s)) {
    return false;
  }
  size_t i = 19;
  if (i < n && text[i] == '.') {
    i++;
    const size_t start = i;
    while (i < n && text[i] >= '0' && text[i] <= '9') i++;
    if (i == start) return false;
  }
  int offset = 0;
  if (i < n && text[i] == 'Z') {
    i++;
  } else if (i < n && (text[i] == '+' || text[i] == '-')) {
    const int sign = text[i] == '-' ? -1 : 1;
    i++;
    int oh = 0;
    int om = 0;
    if (i + 2 > n || !digits(text + i, 2, oh)) return false;
    i += 2;
    if (i < n && text[i] == ':') i++;
    if (i + 2 > n || !digits(text + i, 2, om)) return false;
    i += 2;
    if (oh > 18 || om > 59) return false;
    offset = sign * (oh * 3600 + om * 60);
  } else {
    return false;  // a local time with no offset is not an instant
  }
  if (i != n) return false;
  int64_t local = 0;
  if (!civilToUtc(y, mo, d, h, mi, s, local)) return false;
  utc = local - offset;
  return true;
}

bool parseHttpDate(const char* text, int64_t& utc) {
  utc = 0;
  // "Sat, 03 Oct 2026 22:40:22 GMT" (RFC 9110 IMF-fixdate)
  if (text == nullptr || std::strlen(text) != 29 || text[3] != ',' || text[4] != ' ' || text[7] != ' ' ||
      text[11] != ' ' || text[16] != ' ' || text[19] != ':' || text[22] != ':' || std::strcmp(text + 25, " GMT") != 0) {
    return false;
  }
  static constexpr char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int month = 0;
  for (int m = 0; m < 12; m++) {
    if (std::memcmp(text + 8, MONTHS + m * 3, 3) == 0) month = m + 1;
  }
  int d, y, h, mi, s;
  if (month == 0 || !digits(text + 5, 2, d) || !digits(text + 12, 4, y) || !digits(text + 17, 2, h) ||
      !digits(text + 20, 2, mi) || !digits(text + 23, 2, s)) {
    return false;
  }
  return civilToUtc(y, month, d, h, mi, s, utc);
}

// ---- when to fetch -----------------------------------------------------------------------------

double distanceKm(const double lat1, const double lon1, const double lat2, const double lon2) {
  constexpr double R = 6371.0;
  const double toRad = PI_D / 180.0;
  const double dLat = (lat2 - lat1) * toRad;
  const double dLon = (lon2 - lon1) * toRad;
  const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(lat1 * toRad) * std::cos(lat2 * toRad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * R * std::asin(std::min(1.0, std::sqrt(a)));
}

Decision decide(const Situation& s) {
  if (!s.enabled) return Decision::Off;
  if (!s.connected) return Decision::NotConnected;
  if (s.deviceNetwork) return Decision::DeviceNetwork;
  if (!s.clockValid) return Decision::NoClock;
  if (!s.locationValid) return Decision::NoLocation;
  if (s.lastAttempt != 0 && s.lastAttempt <= s.now && s.now - s.lastAttempt < static_cast<int64_t>(s.retryS)) {
    return Decision::RetryWait;
  }
  if (s.cacheValid && s.cacheFetch <= s.now && s.now - s.cacheFetch < static_cast<int64_t>(s.maxAgeS) &&
      distanceKm(s.cacheLat, s.cacheLon, s.lat, s.lon) <= SAME_PLACE_KM) {
    return Decision::Fresh;
  }
  return Decision::Run;
}

const char* decisionName(const Decision d) {
  switch (d) {
    case Decision::Run:
      return "run";
    case Decision::Off:
      return "off";
    case Decision::NotConnected:
      return "no-wifi";
    case Decision::DeviceNetwork:
      return "device-network";
    case Decision::NoClock:
      return "no-clock";
    case Decision::NoLocation:
      return "no-location";
    case Decision::RetryWait:
      return "retry-wait";
    case Decision::Fresh:
      return "fresh";
  }
  return "?";
}

}  // namespace weather
