// The Weather card's pure half: the two request URLs, the Open-Meteo and NWS parsers on the shared
// fixtures, the alerts outcomes (out of bounds, too big, failures that keep the old list), the
// time-stamp readers, the fetch policy, and the weather.dat cache codec. No network.
//
// Fixtures (fixtures/): captured only at public points - Seattle 47.61,-122.33 and offshore
// 45,-130 - with the echoed position, elevation and title scrubbed; the two-alert body is built
// from real field shapes with every place name replaced ("Example Zone 1"). The variants below
// (nulls, a Test message, a Past one, a Cancel, a null `ends`, HTML) are edits of those files.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "network/WeatherProtocol.h"
#include "sleepcards/WeatherCache.h"

using namespace weather;

namespace {

std::string fixture(const char* name) {
  std::ifstream in(std::string(WEATHER_FIXTURES) + "/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  EXPECT_FALSE(ss.str().empty()) << name;
  return ss.str();
}

std::string replaced(std::string text, const std::string& from, const std::string& to, const int count = 1) {
  size_t at = 0;
  for (int i = 0; i < count; i++) {
    at = text.find(from, at);
    EXPECT_NE(at, std::string::npos) << from;
    if (at == std::string::npos) break;
    text.replace(at, from.size(), to);
    at += to.size();
  }
  return text;
}

std::unique_ptr<Forecast> forecastOf(const std::string& body, bool* ok = nullptr) {
  auto f = std::make_unique<Forecast>();
  const bool parsed = parseForecast(body.data(), body.size(), *f);
  if (ok) *ok = parsed;
  return f;
}

bool parses(const std::string& body) {
  bool ok = false;
  forecastOf(body, &ok);
  return ok;
}

int64_t utc(const int y, const int mo, const int d, const int h, const int mi) {
  std::string iso(32, '\0');
  std::snprintf(iso.data(), iso.size(), "%04d-%02d-%02dT%02d:%02d:00Z", y, mo, d, h, mi);
  int64_t t = 0;
  EXPECT_TRUE(parseIso8601(iso.c_str(), t));
  return t;
}

std::string encoded(const Record& r) {
  std::string out(CACHE_CAP, '\0');
  const size_t n = encodeCache(r, out.data(), out.size());
  out.resize(n);
  return out;
}

// 16:30 CDT on the day the alert fixture was built: both of its alerts are in force.
const int64_t ALERT_NOW = utc(2026, 10, 3, 21, 30);

}  // namespace

// ---- requests -----------------------------------------------------------------------------------

TEST(WeatherRequest, ForecastUrlIsTheSharedQueryAtTwoDecimals) {
  char url[URL_CAP];
  const size_t n = buildForecastUrl(47.6062, -122.3321, url, sizeof(url));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(n, std::strlen(url));
  EXPECT_EQ(std::string(url),
            "https://api.open-meteo.com/v1/forecast?latitude=47.61&longitude=-122.33"
            "&current=temperature_2m,apparent_temperature,relative_humidity_2m,pressure_msl,wind_speed_10m,"
            "wind_direction_10m,wind_gusts_10m,weather_code"
            "&hourly=temperature_2m,precipitation_probability,weather_code,wind_speed_10m&forecast_hours=48"
            "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
            "precipitation_sum,wind_speed_10m_max,wind_gusts_10m_max,wind_direction_10m_dominant&forecast_days=7"
            "&timeformat=unixtime&timezone=auto&wind_speed_unit=ms");
  // The Almanacs own sunrise and sunset; gzip is never asked for.
  EXPECT_EQ(std::string(url).find("sunrise"), std::string::npos);
  EXPECT_EQ(std::string(url).find("gzip"), std::string::npos);
  EXPECT_EQ(std::string(url).find("elevation"), std::string::npos);
}

TEST(WeatherRequest, AlertsUrlStripsZerosAndAsksForActualOnly) {
  char url[URL_CAP];
  ASSERT_GT(buildAlertsUrl(47.6062, -122.3321, url, sizeof(url)), 0u);
  EXPECT_STREQ(url, "https://api.weather.gov/alerts/active?point=47.61,-122.33&status=actual");
  ASSERT_GT(buildAlertsUrl(45.0, -130.0, url, sizeof(url)), 0u);
  EXPECT_STREQ(url, "https://api.weather.gov/alerts/active?point=45,-130&status=actual");
  ASSERT_GT(buildAlertsUrl(61.2, -149.9, url, sizeof(url)), 0u);
  EXPECT_STREQ(url, "https://api.weather.gov/alerts/active?point=61.2,-149.9&status=actual");
}

TEST(WeatherRequest, RoundingIsToAboutAKilometreAndNeverMinusZero) {
  EXPECT_DOUBLE_EQ(roundCoordinate(47.6049), 47.60);
  EXPECT_DOUBLE_EQ(roundCoordinate(47.6051), 47.61);
  EXPECT_DOUBLE_EQ(roundCoordinate(-122.3349), -122.33);
  EXPECT_FALSE(std::signbit(roundCoordinate(-0.004)));
  char url[URL_CAP];
  ASSERT_GT(buildForecastUrl(-0.004, 0.003, url, sizeof(url)), 0u);
  EXPECT_NE(std::string(url).find("latitude=0.00&longitude=0.00&"), std::string::npos);
  ASSERT_GT(buildAlertsUrl(-0.004, 0.003, url, sizeof(url)), 0u);
  EXPECT_NE(std::string(url).find("point=0,0&"), std::string::npos);
}

TEST(WeatherRequest, RefusesBadPointsAndShortBuffers) {
  char url[URL_CAP];
  EXPECT_EQ(buildForecastUrl(91, 0, url, sizeof(url)), 0u);
  EXPECT_STREQ(url, "");
  EXPECT_EQ(buildForecastUrl(0, 181, url, sizeof(url)), 0u);
  EXPECT_EQ(buildForecastUrl(NAN, 0, url, sizeof(url)), 0u);
  EXPECT_EQ(buildAlertsUrl(0, -181, url, sizeof(url)), 0u);
  char small[64];
  EXPECT_EQ(buildForecastUrl(47.61, -122.33, small, sizeof(small)), 0u);
  EXPECT_STREQ(small, "");
}

TEST(WeatherRequest, NwsAreaIsARoughUsBox) {
  EXPECT_TRUE(inNwsArea(47.61, -122.33));   // Seattle
  EXPECT_TRUE(inNwsArea(45.0, -130.0));     // offshore marine zones
  EXPECT_TRUE(inNwsArea(61.22, -149.90));   // Anchorage
  EXPECT_TRUE(inNwsArea(21.31, -157.86));   // Honolulu
  EXPECT_TRUE(inNwsArea(18.47, -66.11));    // San Juan
  EXPECT_TRUE(inNwsArea(13.44, 144.79));    // Guam
  EXPECT_FALSE(inNwsArea(51.5, -0.12));     // London
  EXPECT_FALSE(inNwsArea(-33.87, 151.21));  // Sydney
  EXPECT_FALSE(inNwsArea(64.13, -21.94));   // Reykjavik
  // Inside the box but outside the service: the request is made and its 400 reads as US only.
  EXPECT_TRUE(inNwsArea(49.28, -123.12));  // Vancouver BC
}

// ---- Open-Meteo ---------------------------------------------------------------------------------

TEST(WeatherForecast, ParsesTheSeattleCapture) {
  bool ok = false;
  const auto f = forecastOf(fixture("om_seattle.json"), &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(f->utcOffsetS, -25200);
  EXPECT_TRUE(f->current.valid);
  EXPECT_EQ(f->current.tempC10, 172);
  EXPECT_EQ(f->current.feelsC10, 176);
  EXPECT_EQ(f->current.humidity, 85);
  EXPECT_EQ(f->current.pressureHpa10, 10179);
  EXPECT_EQ(f->current.windMs10, 23);
  EXPECT_EQ(f->current.gustMs10, 36);
  EXPECT_EQ(f->current.windDir, 5);
  EXPECT_EQ(f->current.code, 1);
  ASSERT_EQ(f->hourCount, 48);
  EXPECT_EQ(f->hours[0].t, 1791064800);
  EXPECT_EQ(f->hours[47].t, 1791064800 + 47 * 3600);
  EXPECT_EQ(f->hours[0].tempC10, 171);
  EXPECT_EQ(f->hours[0].code, 2);
  EXPECT_EQ(f->hours[32].pop, 1);
  EXPECT_EQ(f->hours[0].windMs10, 29);  // 2.85 m/s
  ASSERT_EQ(f->dayCount, 7);
  EXPECT_EQ(f->days[0].t, 1791010800);  // the location's midnight (PDT) as UTC
  EXPECT_EQ(f->days[0].code, 45);
  EXPECT_EQ(f->days[6].code, 53);
  EXPECT_EQ(f->days[6].maxC10, 184);
  EXPECT_EQ(f->days[6].minC10, 105);
  EXPECT_EQ(f->days[6].pop, 27);
  EXPECT_EQ(f->days[6].precipMm10, 27);
  EXPECT_EQ(f->days[6].windMaxMs10, 42);
  EXPECT_EQ(f->days[6].gustMaxMs10, 82);
  EXPECT_EQ(f->days[6].windDir, 174);
}

TEST(WeatherForecast, AChunkedBodyFedByteByByteReadsTheSame) {
  const std::string body = fixture("om_seattle.json");
  const auto whole = forecastOf(body);
  auto parser = std::make_unique<ForecastParser>();
  for (const char c : body) parser->feed(&c, 1);
  auto pieces = std::make_unique<Forecast>();
  ASSERT_TRUE(parser->finish(*pieces));
  Record a;
  Record b;
  a.fetchUtc = b.fetchUtc = 1791066600;
  a.forecast = *whole;
  b.forecast = *pieces;
  EXPECT_EQ(encoded(a), encoded(b));
  EXPECT_FALSE(encoded(a).empty());
}

TEST(WeatherForecast, NullsAnywhereAreAccepted) {
  std::string body = fixture("om_seattle.json");
  body = replaced(body, "\"temperature_2m\":[17.1,", "\"temperature_2m\":[null,");
  body = replaced(body, "\"precipitation_probability_max\":[1,", "\"precipitation_probability_max\":[null,");
  body = replaced(body, "\"wind_gusts_10m\":3.6,", "\"wind_gusts_10m\":null,");
  bool ok = false;
  const auto f = forecastOf(body, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(f->hours[0].tempC10, NO_VALUE);
  EXPECT_EQ(f->hours[1].tempC10, 177);
  EXPECT_EQ(f->days[0].pop, NO_VALUE);
  EXPECT_EQ(f->current.gustMs10, NO_VALUE);
  EXPECT_TRUE(f->current.valid);
  // A current block with no temperature is not a reading.
  const auto g = forecastOf(replaced(body, "\"temperature_2m\":17.2", "\"temperature_2m\":null"), &ok);
  ASSERT_TRUE(ok);
  EXPECT_FALSE(g->current.valid);
}

TEST(WeatherForecast, RefusesWhatIsNotAForecast) {
  const std::string body = fixture("om_seattle.json");
  EXPECT_FALSE(parses(R"({"error":true,"reason":"Latitude must be in range of -90 to 90°."})"));
  // A captive portal's 200.
  EXPECT_FALSE(parses("<!DOCTYPE html><html><head><style>body{margin:0}</style></head><body>Sign in</body></html>"));
  EXPECT_FALSE(parses(""));
  EXPECT_FALSE(parses(body.substr(0, body.size() / 2)));  // cut off
  EXPECT_FALSE(parses(body.substr(0, body.size() - 1)));  // the last brace missing
  EXPECT_FALSE(parses("[" + body + "]"));                 // not an object
  EXPECT_FALSE(parses(body + body));                      // two roots
  EXPECT_FALSE(parses(replaced(body, "\"utc_offset_seconds\":-25200,", "")));
  EXPECT_FALSE(parses(replaced(body, "\"time\":[1791064800,", "\"time\":[\"2026-10-03T15:00\",")));
  EXPECT_FALSE(parses(replaced(body, "\"time\":[1791064800,1791068400", "\"time\":[1791068400,1791064800")));
  EXPECT_FALSE(parses(replaced(body, "\"temperature_2m\":[17.1,", "\"temperature_2m\":[\"warm\",")));
  EXPECT_FALSE(parses(replaced(body, "\"temperature_2m_max\":[18.0,", "\"temperature_2m_max\":[true,")));
  EXPECT_FALSE(parses(replaced(body, "\"weather_code\":1}", "\"weather_code\":\"1\"}")));
  EXPECT_FALSE(parses(replaced(body, "\"time\":[1791010800,", "\"time\":[null,")));
}

TEST(WeatherForecast, ExtraValuesBeyondTheTimesAreIgnored) {
  std::string body = fixture("om_seattle.json");
  // Hourly times cut to two entries: the 48 temperatures past them are not hours.
  const size_t start = body.find("\"hourly\":{\"time\":[") + std::strlen("\"hourly\":{\"time\":[");
  const size_t end = body.find(']', start);
  body.replace(start, end - start, "1791064800,1791068400");
  bool ok = false;
  const auto f = forecastOf(body, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(f->hourCount, 2);
  EXPECT_EQ(f->hours[2].t, 0);
  EXPECT_EQ(f->hours[2].tempC10, NO_VALUE);
}

// ---- NWS alerts ---------------------------------------------------------------------------------

TEST(WeatherAlerts, AnEmptyAnswerIsNoneWithItsUpdatedStamp) {
  for (const char* name : {"nws_empty.json", "nws_marine_empty.json"}) {
    const std::string body = fixture(name);
    Alerts a;
    int64_t updated = 0;
    ASSERT_TRUE(parseAlerts(body.data(), body.size(), ALERT_NOW, a, updated)) << name;
    EXPECT_EQ(a.status, AlertsStatus::None);
    EXPECT_EQ(a.total, 0);
    EXPECT_EQ(a.count, 0);
    EXPECT_EQ(a.asOf, ALERT_NOW);
    EXPECT_GT(updated, utc(2026, 10, 3, 22, 0));
  }
}

TEST(WeatherAlerts, TwoAlertsSortedBySeverityThenUrgency) {
  const std::string body = fixture("nws_two_alerts.json");
  EXPECT_GT(body.size(), 14000u);
  Alerts a;
  int64_t updated = 0;
  ASSERT_TRUE(parseAlerts(body.data(), body.size(), ALERT_NOW, a, updated));
  EXPECT_EQ(updated, utc(2026, 10, 3, 22, 0));
  EXPECT_EQ(a.status, AlertsStatus::Listed);
  ASSERT_EQ(a.count, 2);
  EXPECT_EQ(a.total, 2);
  // Both Severe: the Immediate warning outranks the Future watch.
  EXPECT_STREQ(a.list[0].event, "Flash Flood Warning");
  EXPECT_EQ(a.list[0].severity, Severity::Severe);
  EXPECT_EQ(a.list[0].urgency, Urgency::Immediate);
  EXPECT_EQ(a.list[0].type, MessageType::Alert);
  EXPECT_EQ(a.list[0].onset, utc(2026, 10, 3, 21, 18));  // 16:18-05:00
  EXPECT_EQ(a.list[0].ends, utc(2026, 10, 4, 0, 30));    // 19:30-05:00
  EXPECT_STREQ(a.list[1].event, "Flood Watch");
  EXPECT_EQ(a.list[1].urgency, Urgency::Future);
  EXPECT_EQ(a.list[1].ends, utc(2026, 10, 5, 5, 0));
  EXPECT_EQ(a.list[1].expires, utc(2026, 10, 4, 11, 45));  // the message expires before the event ends
  EXPECT_NE(std::string(a.list[1].headline).find("Flood Watch issued October 3"), std::string::npos);
}

TEST(WeatherAlerts, AlertsThatAreOverAreDropped) {
  const std::string body = fixture("nws_two_alerts.json");
  Alerts a;
  int64_t updated = 0;
  // After the warning ends: the watch alone (its message expired, its event has not).
  ASSERT_TRUE(parseAlerts(body.data(), body.size(), utc(2026, 10, 4, 12, 0), a, updated));
  ASSERT_EQ(a.count, 1);
  EXPECT_STREQ(a.list[0].event, "Flood Watch");
  ASSERT_TRUE(parseAlerts(body.data(), body.size(), utc(2026, 10, 6, 0, 0), a, updated));
  EXPECT_EQ(a.status, AlertsStatus::None);
}

TEST(WeatherAlerts, TestPastAndCancelAreFilteredAndNullsRead) {
  const std::string body = fixture("nws_two_alerts.json");
  Alerts a;
  int64_t updated = 0;
  // A "Test" message never shows as a warning.
  std::string test = replaced(body, "\"status\": \"Actual\"", "\"status\": \"Test\"");
  ASSERT_TRUE(parseAlerts(test.data(), test.size(), ALERT_NOW, a, updated));
  ASSERT_EQ(a.count, 1);
  EXPECT_STREQ(a.list[0].event, "Flash Flood Warning");
  // Urgency "Past" ("has been replaced") is hidden.
  std::string past = replaced(body, "\"urgency\": \"Immediate\"", "\"urgency\": \"Past\"");
  ASSERT_TRUE(parseAlerts(past.data(), past.size(), ALERT_NOW, a, updated));
  ASSERT_EQ(a.count, 1);
  EXPECT_STREQ(a.list[0].event, "Flood Watch");
  std::string cancel = replaced(body, "\"messageType\": \"Alert\"", "\"messageType\": \"Cancel\"", 2);
  ASSERT_TRUE(parseAlerts(cancel.data(), cancel.size(), ALERT_NOW, a, updated));
  EXPECT_EQ(a.status, AlertsStatus::None);
  // A null `ends` (Special Weather Statements) falls back to `expires`; a null headline reads "".
  std::string nulls = replaced(body, "\"ends\": \"2026-10-05T00:00:00-05:00\"", "\"ends\": null");
  nulls = replaced(nulls, "\"headline\": \"Flood Watch issued", "\"headline\": null, \"x\": \"");
  ASSERT_TRUE(parseAlerts(nulls.data(), nulls.size(), ALERT_NOW, a, updated));
  ASSERT_EQ(a.count, 2);
  EXPECT_EQ(a.list[1].ends, 0);
  EXPECT_EQ(a.list[1].endsOrExpires(), utc(2026, 10, 4, 11, 45));
  EXPECT_STREQ(a.list[1].headline, "");
}

TEST(WeatherAlerts, MoreThanThreeKeepsTheTopThreeAndTheCount) {
  // Five features: severities Minor, Extreme, Moderate, Severe, Minor.
  std::string features;
  const char* sev[] = {"Minor", "Extreme", "Moderate", "Severe", "Minor"};
  for (int i = 0; i < 5; i++) {
    char f[512];
    std::snprintf(f, sizeof(f),
                  R"(%s{"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-90.1,29.9],[-90.0,29.9],)"
                  R"([-90.0,30.0],[-90.1,29.9]]]},"properties":{"event":"Event %d","severity":"%s",)"
                  R"("urgency":"Expected","status":"Actual","messageType":"Alert","onset":null,)"
                  R"("ends":"2026-10-04T12:00:00Z","expires":"2026-10-04T06:00:00Z","headline":null}})",
                  i ? "," : "", i, sev[i]);
    features += f;
  }
  const std::string body =
      R"({"type":"FeatureCollection","features":[)" + features + R"(],"updated":"2026-10-03T22:00:00+00:00"})";
  Alerts a;
  int64_t updated = 0;
  ASSERT_TRUE(parseAlerts(body.data(), body.size(), ALERT_NOW, a, updated));
  EXPECT_EQ(a.total, 5);
  ASSERT_EQ(a.count, 3);
  EXPECT_STREQ(a.list[0].event, "Event 1");  // Extreme
  EXPECT_STREQ(a.list[1].event, "Event 3");  // Severe
  EXPECT_STREQ(a.list[2].event, "Event 2");  // Moderate
}

TEST(WeatherAlerts, RefusesWhatIsNotACollection) {
  Alerts a;
  int64_t updated = 0;
  const std::string bad[] = {
      "",
      "<html><body>Access Denied</body></html>",
      fixture("nws_400.json"),
      R"({"type":"Feature","features":[]})",
      R"({"type":"FeatureCollection"})",
      fixture("nws_two_alerts.json").substr(0, 9000),
  };
  for (const std::string& body : bad) {
    EXPECT_FALSE(parseAlerts(body.data(), body.size(), ALERT_NOW, a, updated)) << body.substr(0, 40);
    EXPECT_EQ(a.status, AlertsStatus::NotChecked);
  }
}

TEST(WeatherAlerts, RepliesMapToTheFourStates) {
  Alerts a;
  const std::string empty = fixture("nws_empty.json");
  ASSERT_TRUE(alertsFromReply(Transfer::Ok, 200, empty.data(), empty.size(), empty.size(), ALERT_NOW, a));
  EXPECT_EQ(a.status, AlertsStatus::None);
  // Outside the NWS area: a 400 whose detail says so.
  const std::string oob = fixture("nws_400.json");
  ASSERT_TRUE(alertsFromReply(Transfer::Ok, 400, oob.data(), oob.size(), oob.size(), ALERT_NOW, a));
  EXPECT_EQ(a.status, AlertsStatus::OutsideUs);
  EXPECT_EQ(a.asOf, ALERT_NOW);
  // Another 400 is a bug, not "outside the US": keep the old list.
  const std::string other = replaced(oob, "out of bounds", "not a number", 2);
  EXPECT_FALSE(alertsFromReply(Transfer::Ok, 400, other.data(), other.size(), other.size(), ALERT_NOW, a));
  // "out of bounds" anywhere but the detail does not count.
  const std::string title = R"({"title":"out of bounds","status":400,"detail":"Bad request"})";
  EXPECT_FALSE(alertsFromReply(Transfer::Ok, 400, title.data(), title.size(), title.size(), ALERT_NOW, a));
  // Too big to read still proves alerts exist.
  ASSERT_TRUE(alertsFromReply(Transfer::TooLarge, 200, nullptr, 0, 300000, ALERT_NOW, a));
  EXPECT_EQ(a.status, AlertsStatus::TooMany);
  EXPECT_EQ(a.total, 18);
  EXPECT_EQ(a.count, 0);
  // Failures keep the old record.
  const std::string html = "<html>Access Denied</html>";
  EXPECT_FALSE(alertsFromReply(Transfer::Ok, 403, html.data(), html.size(), html.size(), ALERT_NOW, a));
  EXPECT_FALSE(alertsFromReply(Transfer::Ok, 200, html.data(), html.size(), html.size(), ALERT_NOW, a));
  EXPECT_FALSE(alertsFromReply(Transfer::Ok, 503, empty.data(), empty.size(), empty.size(), ALERT_NOW, a));
  EXPECT_FALSE(alertsFromReply(Transfer::Failed, 0, nullptr, 0, 0, ALERT_NOW, a));
  EXPECT_FALSE(alertsFromReply(Transfer::NoMemory, 0, nullptr, 0, 0, ALERT_NOW, a));
  EXPECT_EQ(tooManyLowerBound(1), 1);
  EXPECT_EQ(tooManyLowerBound(ALERTS_BODY_CAP), 16);
}

// ---- time stamps --------------------------------------------------------------------------------

TEST(WeatherTimes, Iso8601WithTheOfficesOffset) {
  int64_t t = 0;
  ASSERT_TRUE(parseIso8601("2026-10-03T16:18:00-05:00", t));
  EXPECT_EQ(t, 1791062280);
  ASSERT_TRUE(parseIso8601("2026-10-03T21:18:00Z", t));
  EXPECT_EQ(t, 1791062280);
  ASSERT_TRUE(parseIso8601("2026-10-03T21:18:00+00:00", t));
  EXPECT_EQ(t, 1791062280);
  ASSERT_TRUE(parseIso8601("2026-10-04T06:48:00+0930", t));
  EXPECT_EQ(t, 1791062280);
  ASSERT_TRUE(parseIso8601("2026-10-03T21:18:00.250Z", t));
  EXPECT_EQ(t, 1791062280);
  for (const char* bad :
       {"", "2026-10-03", "2026-10-03T21:18:00", "2026-13-03T21:18:00Z", "2026-02-30T00:00:00Z", "2026-10-03T24:00:00Z",
        "2026-10-03T21:18:00-5:00", "2026-10-03T21:18:00Zx", "26-10-03T21:18:00Z"}) {
    EXPECT_FALSE(parseIso8601(bad, t)) << bad;
  }
  EXPECT_FALSE(parseIso8601(nullptr, t));
}

TEST(WeatherTimes, HttpDate) {
  int64_t t = 0;
  ASSERT_TRUE(parseHttpDate("Sat, 03 Oct 2026 22:40:22 GMT", t));
  EXPECT_EQ(t, utc(2026, 10, 3, 22, 40) + 22);
  EXPECT_FALSE(parseHttpDate("Sat, 03 Okt 2026 22:40:22 GMT", t));
  EXPECT_FALSE(parseHttpDate("Saturday, 03-Oct-26 22:40:22 GMT", t));
  EXPECT_FALSE(parseHttpDate("Sat, 03 Oct 2026 22:40:22 UTC", t));
  EXPECT_FALSE(parseHttpDate(nullptr, t));
}

// ---- when to fetch ------------------------------------------------------------------------------

namespace {
Situation staleOnWifi() {
  Situation s;
  s.enabled = s.connected = s.clockValid = s.locationValid = true;
  s.now = utc(2026, 11, 2, 20, 0);
  s.cacheValid = true;
  s.cacheFetch = s.now - 2 * 3600;
  s.cacheLat = s.lat = 47.61;
  s.cacheLon = s.lon = -122.33;
  s.maxAgeS = 3600;
  s.retryS = 600;
  return s;
}
}  // namespace

TEST(WeatherPolicy, OneFetchOnAStaleCacheWithWifi) {
  Situation s = staleOnWifi();
  EXPECT_EQ(decide(s), Decision::Run);
  // The attempt is stamped before its request: every later pass (frame, keeper tick, the restart
  // after a sync) within the wait finds nothing to do, whatever the attempt's outcome.
  s.lastAttempt = s.now;
  int runs = 0;
  for (int i = 1; i < 600; i++) {
    s.now = s.lastAttempt + i;
    if (decide(s) == Decision::Run) runs++;
  }
  EXPECT_EQ(runs, 0);
  s.now = s.lastAttempt + 600;
  EXPECT_EQ(decide(s), Decision::Run);  // the failed attempt's wait is over
}

TEST(WeatherPolicy, NothingWithoutTheGates) {
  Situation s = staleOnWifi();
  s.enabled = false;
  EXPECT_EQ(decide(s), Decision::Off);
  s = staleOnWifi();
  s.connected = false;
  EXPECT_EQ(decide(s), Decision::NotConnected);
  s = staleOnWifi();
  s.deviceNetwork = true;  // the book-sync peer's or hub's hotspot
  EXPECT_EQ(decide(s), Decision::DeviceNetwork);
  s = staleOnWifi();
  s.clockValid = false;
  EXPECT_EQ(decide(s), Decision::NoClock);
  s = staleOnWifi();
  s.locationValid = false;
  EXPECT_EQ(decide(s), Decision::NoLocation);
  EXPECT_STREQ(decisionName(Decision::DeviceNetwork), "device-network");
}

TEST(WeatherPolicy, FreshIsByAgeAndPlace) {
  Situation s = staleOnWifi();
  s.cacheFetch = s.now - 3599;
  EXPECT_EQ(decide(s), Decision::Fresh);
  // Moved more than ~5 km: refetched however fresh (a tolerance, not a grid).
  s.lat = 47.61 + 0.06;  // ~6.7 km north
  EXPECT_EQ(decide(s), Decision::Run);
  s.lat = 47.61 + 0.04;  // ~4.4 km
  EXPECT_EQ(decide(s), Decision::Fresh);
  // A cache stamped in the future (a clock that was wrong then) is refreshed.
  s = staleOnWifi();
  s.cacheFetch = s.now + 7200;
  EXPECT_EQ(decide(s), Decision::Run);
  s = staleOnWifi();
  s.cacheValid = false;
  EXPECT_EQ(decide(s), Decision::Run);
  // The keeper's three hours.
  s = staleOnWifi();
  s.maxAgeS = 3 * 3600;
  EXPECT_EQ(decide(s), Decision::Fresh);
}

TEST(WeatherPolicy, Distance) {
  EXPECT_NEAR(distanceKm(47.61, -122.33, 47.62, -122.33), 1.11, 0.01);
  EXPECT_NEAR(distanceKm(47.61, -122.33, 47.61, -122.32), 0.75, 0.01);
  EXPECT_NEAR(distanceKm(0, 0, 0, 0), 0.0, 1e-9);
}

// ---- the cache ----------------------------------------------------------------------------------

namespace {
std::unique_ptr<Record> seattleRecord() {
  auto r = std::make_unique<Record>();
  r->fetchUtc = 1791066622;
  r->clockTrusted = true;
  r->lat = 47.61;
  r->lon = -122.33;
  r->placeSource = PLACE_WIFI_AUTO;
  r->placeYmd = 20260929;
  EXPECT_TRUE(parseForecast(fixture("om_seattle.json").data(), fixture("om_seattle.json").size(), r->forecast));
  const std::string alerts = fixture("nws_two_alerts.json");
  int64_t updated = 0;
  EXPECT_TRUE(parseAlerts(alerts.data(), alerts.size(), ALERT_NOW, r->alerts, updated));
  return r;
}
}  // namespace

TEST(WeatherCache, RoundTripsUnderFourKilobytes) {
  const auto r = seattleRecord();
  const std::string text = encoded(*r);
  ASSERT_FALSE(text.empty());
  EXPECT_LT(text.size(), CACHE_CAP);
  EXPECT_EQ(text.rfind("end\n"), text.size() - 4);
  EXPECT_EQ(text.find("W1 1791066622 1 47.61 -122.33 1791529200\n"), 0u);
  auto back = std::make_unique<Record>();
  ASSERT_TRUE(decodeCache(text.data(), text.size(), *back));
  EXPECT_EQ(encoded(*back), text);
  EXPECT_EQ(back->placeSource, PLACE_WIFI_AUTO);
  EXPECT_EQ(back->placeYmd, 20260929u);
  EXPECT_EQ(back->forecast.utcOffsetS, -25200);
  EXPECT_EQ(back->forecast.hourCount, 48);
  EXPECT_EQ(back->alerts.count, 2);
  EXPECT_STREQ(back->alerts.list[0].event, "Flash Flood Warning");
  EXPECT_EQ(back->alerts.list[1].ends, r->alerts.list[1].ends);

  Header h;
  ASSERT_TRUE(decodeHeader(text.data(), text.size(), h));
  EXPECT_EQ(h.fetchUtc, 1791066622);
  EXPECT_DOUBLE_EQ(h.lat, 47.61);
  EXPECT_DOUBLE_EQ(h.lon, -122.33);
  EXPECT_EQ(h.lastDayUtc, 1791529200);
  // Shuffle's check reads only the first HEADER_CAP bytes.
  ASSERT_TRUE(decodeHeader(text.data(), HEADER_CAP, h));
}

TEST(WeatherCache, NoValuesAndRecheckFailedRoundTrip) {
  auto r = seattleRecord();
  r->forecast.current.valid = false;
  r->forecast.hours[3].pop = NO_VALUE;
  r->forecast.days[2].windDir = NO_VALUE;
  r->alerts.recheckFailed = true;
  r->alerts.list[0].onset = 0;
  r->placeYmd = 0;
  const std::string text = encoded(*r);
  EXPECT_NE(text.find("\nC -\n"), std::string::npos);
  auto back = std::make_unique<Record>();
  ASSERT_TRUE(decodeCache(text.data(), text.size(), *back));
  EXPECT_FALSE(back->forecast.current.valid);
  EXPECT_EQ(back->forecast.hours[3].pop, NO_VALUE);
  EXPECT_EQ(back->forecast.days[2].windDir, NO_VALUE);
  EXPECT_TRUE(back->alerts.recheckFailed);
  EXPECT_EQ(back->alerts.list[0].onset, 0);
  EXPECT_EQ(back->placeYmd, 0u);
  EXPECT_EQ(encoded(*back), text);
}

TEST(WeatherCache, TheLongestRecordStillFits) {
  auto r = seattleRecord();
  Forecast& f = r->forecast;
  f.current = Current{true, -32767, -32767, -32767, -32767, -32767, -32767, -32767, -32767};
  for (uint8_t i = 0; i < MAX_HOURS; i++) f.hours[i] = Hour{4102444000LL + i, -32767, -32767, -32767, -32767};
  for (uint8_t i = 0; i < MAX_DAYS; i++) {
    f.days[i] = Day{4102444000LL + i, -32767, -32767, -32767, -32767, -32767, -32767, -32767, -32767};
  }
  f.hourCount = MAX_HOURS;
  f.dayCount = MAX_DAYS;
  r->alerts.status = AlertsStatus::Listed;
  r->alerts.count = MAX_ALERTS;
  r->alerts.total = 65535;
  for (Alert& a : r->alerts.list) {
    a.onset = a.ends = a.expires = 4102444000LL;
    std::memset(a.event, 'W', sizeof(a.event) - 1);
    a.event[sizeof(a.event) - 1] = '\0';
    std::memset(a.headline, 'H', sizeof(a.headline) - 1);
    a.headline[sizeof(a.headline) - 1] = '\0';
  }
  r->lat = -89.99;
  r->lon = -179.99;
  const std::string text = encoded(*r);
  ASSERT_FALSE(text.empty());
  EXPECT_LE(text.size(), CACHE_CAP);
  auto back = std::make_unique<Record>();
  EXPECT_TRUE(decodeCache(text.data(), text.size(), *back));
}

TEST(WeatherCache, AnythingDamagedReadsAsNoCache) {
  const auto r = seattleRecord();
  const std::string text = encoded(*r);
  auto back = std::make_unique<Record>();
  const auto refused = [&](const std::string& t, const char* why) {
    back->fetchUtc = 1;
    EXPECT_FALSE(decodeCache(t.data(), t.size(), *back)) << why;
    EXPECT_EQ(back->fetchUtc, 0) << why << ": reset";
    EXPECT_EQ(back->forecast.dayCount, 0) << why;
  };
  refused("", "empty");
  refused(text.substr(0, text.size() - 4), "no end");
  refused(text.substr(0, text.size() - 1), "no last newline");
  refused(text.substr(0, text.size() / 2), "cut");
  refused(text + "H 1 1 1 1 1\n", "a line after end");
  refused(replaced(text, "W1 ", "W2 "), "another version");
  refused(replaced(text, "\nP wifi-auto", "\nP gps"), "an unknown place source");
  refused(replaced(text, "\nH ", "\nX "), "an unknown line");
  refused(replaced(text, "\nH 1791068400", "\nH 1791060000"), "hours out of order");
  refused(replaced(text, "\nD 1791529200", "\nD 1791529201"), "the header's last day disagrees");
  refused(replaced(text, " 172 176 ", " 17x 176 "), "a bad number");
  refused(replaced(text, "\nE Flood Watch\n", "\n"), "an alert without its event line");
  refused(replaced(text, "\nN 2 ", "\nN 1 "), "a list under 'none'");
  refused(replaced(text, "\nA 3 4 1 ", "\nE Stray\nA 3 4 1 "), "an event line with no alert line");
  refused(replaced(text, "\nend\n", "\nL stray\nend\n"), "a headline line with no alert");
  std::string nul = text;
  nul[10] = '\0';
  refused(nul, "a NUL byte");
  std::string moreHours = text;
  const size_t at = moreHours.find("\nD ");
  for (int i = 0; i < 1; i++) moreHours.insert(at + 1, "H 4102000000 1 1 1 1\n");
  refused(moreHours, "49 hours");
  Header h;
  EXPECT_FALSE(decodeHeader("W1 1 1 47.61 -122.33", 21, h));  // no newline
  EXPECT_FALSE(decodeHeader("W1 1 2 47.61 -122.33 1791529200\n", 31, h));
  EXPECT_FALSE(decodeHeader("W1 1 1 97.61 -122.33 1791529200\n", 31, h));
}

TEST(WeatherCache, RefusesToEncodeNothing) {
  Record r;
  char out[CACHE_CAP];
  EXPECT_EQ(encodeCache(r, out, sizeof(out)), 0u);  // no days
  const auto good = seattleRecord();
  char small[256];
  EXPECT_EQ(encodeCache(*good, small, sizeof(small)), 0u);
  EXPECT_STREQ(small, "");
}
