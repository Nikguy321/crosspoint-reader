#include "WeatherFetch.h"

#include <BoardConfig.h>

#include <cstdio>

#if FREEINK_DEVICE_X4PRO
#include <Arduino.h>
#include <BookSyncStore.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_attr.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "activities/RenderLock.h"
#include "network/GeolocateClient.h"
#include "network/WeatherProtocol.h"
#include "sleepcards/CardTime.h"
#include "sleepcards/LocationFix.h"
#include "sleepcards/SleepCardSettings.h"
#include "sleepcards/WeatherCache.h"
#include "util/TaskWatchdog.h"
#endif

namespace WeatherFetch {

#if FREEINK_DEVICE_X4PRO
namespace {

constexpr const char* FORECAST_HOST = "api.open-meteo.com";
constexpr const char* ALERTS_HOST = "api.weather.gov";
constexpr const char* CACHE_DIR = "/.crosspoint/sleepcards";
constexpr uint32_t CONNECT_TIMEOUT_MS = 4000;
// Not worth starting a TLS request with less than this left of the budget.
constexpr uint32_t MIN_REQUEST_MS = 3000;
// A sync refreshes a cache over an hour old; the live keeper one over three hours old.
constexpr uint32_t SYNC_MAX_AGE_S = 3600;
constexpr uint32_t SYNC_RETRY_S = 600;
constexpr uint32_t KEEPER_MAX_AGE_S = 3 * 3600;
constexpr uint32_t KEEPER_RETRY_S = 30 * 60;
// The reader's clock and the server's Date agree within this: the stamp is trusted.
constexpr int64_t TRUSTED_SKEW_S = 300;
constexpr size_t OUTCOME_CAP = 48;

static_assert(static_cast<uint8_t>(sleepcards::LocationSource::Typed) == weather::PLACE_TYPED &&
                  static_cast<uint8_t>(sleepcards::LocationSource::Wifi) == weather::PLACE_WIFI &&
                  static_cast<uint8_t>(sleepcards::LocationSource::Internet) == weather::PLACE_INTERNET &&
                  static_cast<uint8_t>(sleepcards::LocationSource::WifiAuto) == weather::PLACE_WIFI_AUTO,
              "the cache's place sources are LocationFix's");

// The last attempt and its outcome, kept across the silent reboot that ends every sync and across
// deep sleep (RTC memory; a cold boot forgets them, which costs at most one early retry).
constexpr uint32_t RTC_MAGIC = 0x57544852;  // "WTHR"
RTC_NOINIT_ATTR uint32_t rtcMagic;
RTC_NOINIT_ATTR int64_t rtcLastAttempt;
RTC_NOINIT_ATTR char rtcOutcome[OUTCOME_CAP];

bool answered = false;       // due() has answered this boot
bool approved = false;       // ... and said run
bool alertsPending = false;  // the keeper's next step asks for the alerts
// The forecast half of a fetch, for its one log line.
int forecastStatus = 0;
unsigned long forecastMs = 0;

int64_t lastAttempt() { return rtcMagic == RTC_MAGIC ? rtcLastAttempt : 0; }

void stampAttempt(const int64_t utc) {
  rtcLastAttempt = utc;
  if (rtcMagic != RTC_MAGIC) rtcOutcome[0] = '\0';
  rtcMagic = RTC_MAGIC;
}

void outcome(const char* line) {
  LOG_INF("WX", "weather: %s", line);
  if (rtcMagic != RTC_MAGIC) return;
  std::snprintf(rtcOutcome, sizeof(rtcOutcome), "%s", line);
}

// What the reader knows now: its clock and the saved location (with where it came from).
struct Here {
  bool clockValid = false;
  int64_t utc = 0;
  bool located = false;
  double lat = 0;
  double lon = 0;
  sleepcards::LocationFix fix;
};

Here readHere() {
  Here h;
  time_t utc = 0;
  halClock.invalidate();
  if (halClock.isAvailable() && halClock.utcEpoch(utc) && sleepcards::plausibleTime(static_cast<int64_t>(utc))) {
    h.clockValid = true;
    h.utc = static_cast<int64_t>(utc);
  }
  h.located = sleepcards::parseLocation(SETTINGS.sleepCardLocation, h.lat, h.lon);
  h.fix = sleepcards::describeLocation(SETTINGS.sleepCardLocationFix, SETTINGS.sleepCardLocation);
  return h;
}

// The book-sync peer's or hub's own hotspot: no internet behind it.
bool onDeviceNetwork() {
  const std::string ssid = std::string(WiFi.SSID().c_str());
  const BookSync::Config sync = BOOKSYNC_STORE.getConfig();
  return !ssid.empty() && (ssid == sync.peerSsid || ssid == sync.hubSsid);
}

// ---- the cache file (the SD card shares SPI with the panel: under the render lock) -------------

int32_t readCache(char* buf, const size_t cap) {
  RenderLock lock;
  HalFile file;
  if (!Storage.exists(weather::CACHE_PATH) || !Storage.openFileForRead("WX", weather::CACHE_PATH, file)) return -1;
  const int got = file.read(buf, cap);
  return got < 0 ? -1 : got;
}

bool writeCache(const char* text, const size_t len) {
  RenderLock lock;
  Storage.ensureDirectoryExists(CACHE_DIR);
  {
    HalFile file;
    if (!Storage.openFileForWrite("WX", weather::CACHE_TMP_PATH, file)) return false;
    const bool written = file.write(text, len) == len;
    file.close();  // before the rename
    if (!written) {
      Storage.remove(weather::CACHE_TMP_PATH);
      return false;
    }
  }
  // FAT's rename refuses an existing target.
  if (Storage.exists(weather::CACHE_PATH) && !Storage.remove(weather::CACHE_PATH)) {
    Storage.remove(weather::CACHE_TMP_PATH);
    return false;
  }
  return Storage.rename(weather::CACHE_TMP_PATH, weather::CACHE_PATH);
}

// The cached record (false: none, or unreadable).
bool loadRecord(weather::Record& out) {
  auto text = makeUniqueNoThrow<char[]>(weather::CACHE_CAP + 1);
  if (!text) return false;
  const int32_t got = readCache(text.get(), weather::CACHE_CAP);
  return got > 0 && weather::decodeCache(text.get(), static_cast<size_t>(got), out);
}

// Writes the record when its text differs from the file's; changed says whether it did.
bool saveRecord(const weather::Record& record, bool& changed) {
  changed = false;
  auto text = makeUniqueNoThrow<char[]>(weather::CACHE_CAP + 1);
  auto old = makeUniqueNoThrow<char[]>(weather::CACHE_CAP + 1);
  if (!text || !old) return false;
  const size_t len = weather::encodeCache(record, text.get(), weather::CACHE_CAP + 1);
  if (len == 0 || len > weather::CACHE_CAP) return false;
  const int32_t oldLen = readCache(old.get(), weather::CACHE_CAP);
  if (oldLen == static_cast<int32_t>(len) && std::memcmp(old.get(), text.get(), len) == 0) return true;
  if (!writeCache(text.get(), len)) return false;
  changed = true;
  return true;
}

// ---- the requests ------------------------------------------------------------------------------

struct Reply {
  GeolocateClient::Result result = GeolocateClient::Result::Transport;
  int status = 0;
  size_t length = 0;
  char date[40] = "";
};

// One bounded GET: DNS within DNS_MS, the rest within what is left of BUDGET_MS. The failure word
// for the log, or nullptr when the server answered (reply holds what it said).
const char* get(const char* url, const char* host, const char* accept, char* body, const size_t cap, Reply& reply) {
  const unsigned long started = millis();
  if (!GeolocateClient::enoughHeap()) return "no-memory";
  if (!GeolocateClient::resolveWithin(host, DNS_MS)) return "dns";
  resetTaskWatchdogIfSubscribed();
  const unsigned long elapsed = millis() - started;
  if (elapsed + MIN_REQUEST_MS > BUDGET_MS) return "timeout";
  GeolocateClient::Options options;
  options.requestTimeoutMs = static_cast<uint32_t>(BUDGET_MS - elapsed);
  options.connectTimeoutMs = std::min<uint32_t>(options.requestTimeoutMs, CONNECT_TIMEOUT_MS);
  options.quiet = true;
  options.accept = accept;
  options.dateOut = reply.date;
  options.dateCap = sizeof(reply.date);
  reply.result = GeolocateClient::request(url, nullptr, body, cap, reply.length, reply.status, options);
  resetTaskWatchdogIfSubscribed();
  switch (reply.result) {
    case GeolocateClient::Result::NoMemory:
      return "no-memory";
    case GeolocateClient::Result::Transport:
      return "unreachable";
    default:
      return nullptr;
  }
}

// The forecast, parsed into a new record (the old alerts kept when it is for the same place) and
// saved. nullptr when saved, else the failure word (the old cache stays).
const char* fetchForecast(const Here& here, bool& changed) {
  changed = false;
  forecastStatus = 0;
  const unsigned long started = millis();
  auto url = makeUniqueNoThrow<char[]>(weather::URL_CAP);
  auto body = makeUniqueNoThrow<char[]>(weather::FORECAST_BODY_CAP + 1);
  auto record = makeUniqueNoThrow<weather::Record>();
  auto old = makeUniqueNoThrow<weather::Record>();
  auto reply = makeUniqueNoThrow<Reply>();
  if (!url || !body || !record || !old || !reply) return "no-memory";
  if (weather::buildForecastUrl(here.lat, here.lon, url.get(), weather::URL_CAP) == 0) return "no-location";
  const char* failure =
      get(url.get(), FORECAST_HOST, "application/json", body.get(), weather::FORECAST_BODY_CAP + 1, *reply);
  forecastMs = millis() - started;
  if (failure != nullptr) return failure;
  forecastStatus = reply->status;
  if (reply->result == GeolocateClient::Result::TooLarge) return "too-large";
  if (reply->status != 200) return "http";
  // A captive portal's HTML 200 or a cut-off body never replaces the cache.
  if (!weather::parseForecast(body.get(), reply->length, record->forecast)) return "parse";
  body.reset();

  // The fetch time: the reader's clock, trusted when it was set from the internet and the server's
  // Date agrees with it; without a set clock, the server's Date (not trusted).
  int64_t serverUtc = 0;
  const bool haveDate = weather::parseHttpDate(reply->date, serverUtc);
  if (here.clockValid) {
    record->fetchUtc = here.utc;
    record->clockTrusted =
        SETTINGS.clockHasBeenSynced != 0 && (!haveDate || std::llabs(serverUtc - here.utc) <= TRUSTED_SKEW_S);
  } else if (haveDate) {
    record->fetchUtc = serverUtc;
    record->clockTrusted = false;
  } else {
    return "no-clock";
  }
  record->lat = weather::roundCoordinate(here.lat);
  record->lon = weather::roundCoordinate(here.lon);
  record->placeSource = static_cast<uint8_t>(here.fix.source);
  record->placeYmd =
      here.fix.year == 0 ? 0 : static_cast<uint32_t>(here.fix.year * 10000 + here.fix.month * 100 + here.fix.day);
  // The alerts: outside the NWS area never asked; else the old ones (same place) until the
  // alerts request answers.
  if (!weather::inNwsArea(record->lat, record->lon)) {
    record->alerts.status = weather::AlertsStatus::OutsideUs;
    record->alerts.asOf = record->fetchUtc;
  } else if (loadRecord(*old) &&
             weather::distanceKm(old->lat, old->lon, record->lat, record->lon) <= weather::SAME_PLACE_KM) {
    record->alerts = old->alerts;
  }
  if (!saveRecord(*record, changed)) return "save";
  return nullptr;
}

// The alerts for the cached forecast's point, folded into the cache. The token for the log line:
// the HTTP status, or the failure word (the old alerts then stay, marked not rechecked).
void fetchAlerts(const int64_t nowUtc, char* token, const size_t tokenCap, bool& changed) {
  changed = false;
  std::snprintf(token, tokenCap, "-");
  auto record = makeUniqueNoThrow<weather::Record>();
  if (!record) {
    std::snprintf(token, tokenCap, "no-memory");
    return;
  }
  if (!loadRecord(*record) || !weather::inNwsArea(record->lat, record->lon)) return;
  auto scratch = makeUniqueNoThrow<weather::Alerts>();
  auto url = makeUniqueNoThrow<char[]>(weather::URL_CAP);
  auto reply = makeUniqueNoThrow<Reply>();
  // Large: the PSRAM heap (allocations over 4 KB land there).
  auto body = makeUniqueNoThrow<char[]>(weather::ALERTS_BODY_CAP + 1);
  if (!scratch || !url || !reply || !body ||
      weather::buildAlertsUrl(record->lat, record->lon, url.get(), weather::URL_CAP) == 0) {
    std::snprintf(token, tokenCap, "no-memory");
    return;
  }
  const char* failure =
      get(url.get(), ALERTS_HOST, weather::NWS_ACCEPT, body.get(), weather::ALERTS_BODY_CAP + 1, *reply);
  weather::Transfer transfer = weather::Transfer::Failed;
  if (failure == nullptr) {
    transfer = reply->result == GeolocateClient::Result::TooLarge ? weather::Transfer::TooLarge : weather::Transfer::Ok;
    std::snprintf(token, tokenCap, "%d", reply->status);
  } else {
    std::snprintf(token, tokenCap, "%s", failure);
  }
  if (weather::alertsFromReply(transfer, reply->status, body.get(), reply->length, reply->length, nowUtc, *scratch)) {
    record->alerts = *scratch;
  } else {
    if (failure == nullptr && reply->status == 200) std::snprintf(token, tokenCap, "parse");
    record->alerts.recheckFailed = true;
  }
  body.reset();
  if (!saveRecord(*record, changed)) std::snprintf(token, tokenCap, "save");
}

void logDone(const char* alertsToken, const unsigned long alertsMs) {
  char line[OUTCOME_CAP];
  const unsigned long ms = forecastMs + alertsMs;
  std::snprintf(line, sizeof(line), "ok %d/%s %lu.%lu s", forecastStatus, alertsToken, ms / 1000, (ms % 1000) / 100);
  outcome(line);
}

void logFailed(const char* why) {
  char line[OUTCOME_CAP];
  if (std::strcmp(why, "http") == 0) {
    std::snprintf(line, sizeof(line), "failed http %d", forecastStatus);
  } else {
    std::snprintf(line, sizeof(line), "failed %s", why);
  }
  outcome(line);
}

weather::Situation situation(const Here& here, const uint32_t maxAgeS, const uint32_t retryS) {
  weather::Situation s;
  s.enabled = SETTINGS.weatherEnabled != 0;
  s.connected = WiFi.status() == WL_CONNECTED;
  s.deviceNetwork = s.connected && onDeviceNetwork();
  s.clockValid = here.clockValid;
  s.locationValid = here.located;
  s.now = here.utc;
  s.lastAttempt = lastAttempt();
  s.lat = here.lat;
  s.lon = here.lon;
  s.maxAgeS = maxAgeS;
  s.retryS = retryS;
  char header[weather::HEADER_CAP];
  const int32_t got = s.enabled && s.connected ? readCache(header, sizeof(header)) : -1;
  weather::Header h;
  if (got > 0 && weather::decodeHeader(header, static_cast<size_t>(got), h)) {
    s.cacheValid = true;
    s.cacheFetch = h.fetchUtc;
    s.cacheLat = h.lat;
    s.cacheLon = h.lon;
  }
  return s;
}

// The forecast and, inside the NWS area, the alerts, now; one log line.
void fetchBoth(const Here& here, bool& changed) {
  stampAttempt(here.utc);
  bool forecastChanged = false;
  const char* failure = fetchForecast(here, forecastChanged);
  changed = forecastChanged;
  if (failure != nullptr) {
    logFailed(failure);
    return;
  }
  char token[16];
  const unsigned long started = millis();
  bool alertsChanged = false;
  fetchAlerts(here.utc, token, sizeof(token), alertsChanged);
  changed = changed || alertsChanged;
  logDone(token, std::strcmp(token, "-") == 0 ? 0 : millis() - started);
}

}  // namespace

bool due(const bool jobOnline) {
  if (SETTINGS.weatherEnabled == 0 || answered) return false;
  answered = true;
  if (deepSleepStarting()) {
    outcome("skipped sleeping");
    return false;
  }
  if (!jobOnline) {
    outcome("skipped job-offline");
    return false;
  }
  const Here here = readHere();
  const weather::Decision decision = weather::decide(situation(here, SYNC_MAX_AGE_S, SYNC_RETRY_S));
  if (decision != weather::Decision::Run) {
    char line[OUTCOME_CAP];
    std::snprintf(line, sizeof(line), "skipped %s", weather::decisionName(decision));
    outcome(line);
    return false;
  }
  approved = true;
  return true;
}

void run() {
  if (!approved) return;
  approved = false;
  bool changed = false;
  fetchBoth(readHere(), changed);
}

KeeperStep keeperStep(bool& cacheChanged) {
  cacheChanged = false;
  if (alertsPending) {
    alertsPending = false;
    char token[16];
    const unsigned long started = millis();
    fetchAlerts(readHere().utc, token, sizeof(token), cacheChanged);
    logDone(token, millis() - started);
    return KeeperStep::Done;
  }
  const Here here = readHere();
  if (weather::decide(situation(here, KEEPER_MAX_AGE_S, KEEPER_RETRY_S)) != weather::Decision::Run) {
    return KeeperStep::Nothing;
  }
  stampAttempt(here.utc);
  const char* failure = fetchForecast(here, cacheChanged);
  if (failure != nullptr) {
    logFailed(failure);
    return KeeperStep::Failed;
  }
  if (!weather::inNwsArea(weather::roundCoordinate(here.lat), weather::roundCoordinate(here.lon))) {
    logDone("-", 0);
    return KeeperStep::Done;
  }
  alertsPending = true;
  return KeeperStep::More;
}

void benchFetch(char* out, const size_t cap) {
  const Here here = readHere();
  weather::Situation s = situation(here, 0, 0);
  s.lastAttempt = 0;  // the bench asks now, whatever the wait
  s.cacheValid = false;
  const weather::Decision decision = weather::decide(s);
  if (decision != weather::Decision::Run) {
    std::snprintf(out, cap, "skipped %s", weather::decisionName(decision));
    return;
  }
  bool changed = false;
  fetchBoth(here, changed);
  std::snprintf(out, cap, "%s changed=%d", rtcMagic == RTC_MAGIC ? rtcOutcome : "?", changed ? 1 : 0);
}

void benchShow(void (*line)(void* ctx, const char* text), void* ctx) {
  char text[200];
  const Here here = readHere();
  std::snprintf(text, sizeof(text), "on=%u units=%s shuffle=%u sleep_mode_weather=%d clock=%d located=%d",
                static_cast<unsigned>(SETTINGS.weatherEnabled), SETTINGS.weatherUnits == 1 ? "us" : "metric",
                static_cast<unsigned>(SETTINGS.shuffleWeather),
                SETTINGS.sleepScreen == CrossPointSettings::WEATHER ? 1 : 0, here.clockValid ? 1 : 0,
                here.located ? 1 : 0);
  line(ctx, text);
  const int64_t last = lastAttempt();
  std::snprintf(text, sizeof(text), "last=%s attempt_age_s=%" PRId64,
                rtcMagic == RTC_MAGIC && rtcOutcome[0] != '\0' ? rtcOutcome : "-",
                last != 0 && here.clockValid ? here.utc - last : static_cast<int64_t>(-1));
  line(ctx, text);
  auto record = makeUniqueNoThrow<weather::Record>();
  if (!record || !loadRecord(*record)) {
    line(ctx, "cache=0");
    return;
  }
  const weather::Forecast& f = record->forecast;
  static constexpr const char* PLACES[] = {"typed", "wifi", "ip", "wifi-auto"};
  // How far the cache's point is from the saved location: no coordinates on the wire.
  const double km = here.located ? weather::distanceKm(record->lat, record->lon, here.lat, here.lon) : -1.0;
  std::snprintf(text, sizeof(text),
                "cache=1 fetched_utc=%" PRId64 " age_s=%" PRId64 " trusted=%d place=%s fixdate=%" PRIu32
                " dist_km=%.1f offset_s=%" PRId32,
                record->fetchUtc, here.clockValid ? here.utc - record->fetchUtc : static_cast<int64_t>(-1),
                record->clockTrusted ? 1 : 0, PLACES[record->placeSource < 4 ? record->placeSource : 0],
                record->placeYmd, km, f.utcOffsetS);
  line(ctx, text);
  std::snprintf(text, sizeof(text),
                "current=%d temp_c10=%d code=%d hours=%u days=%u first_hour_utc=%" PRId64 " last_day_utc=%" PRId64,
                f.current.valid ? 1 : 0, f.current.tempC10, f.current.code, static_cast<unsigned>(f.hourCount),
                static_cast<unsigned>(f.dayCount), f.hourCount ? f.hours[0].t : 0,
                f.dayCount ? f.days[f.dayCount - 1].t : 0);
  line(ctx, text);
  static constexpr const char* STATUS[] = {"not-checked", "none", "listed", "outside-us", "too-many"};
  const weather::Alerts& a = record->alerts;
  std::snprintf(text, sizeof(text), "alerts=%s asof_utc=%" PRId64 " total=%u kept=%u recheck_failed=%d",
                STATUS[static_cast<uint8_t>(a.status) <= 4 ? static_cast<uint8_t>(a.status) : 0], a.asOf,
                static_cast<unsigned>(a.total), static_cast<unsigned>(a.count), a.recheckFailed ? 1 : 0);
  line(ctx, text);
  for (uint8_t i = 0; i < a.count; i++) {
    std::snprintf(text, sizeof(text), "alert severity=%u ends_utc=%" PRId64 " event=%s",
                  static_cast<unsigned>(a.list[i].severity), a.list[i].endsOrExpires(), a.list[i].event);
    line(ctx, text);
  }
}

bool benchClear() {
  rtcMagic = 0;
  alertsPending = false;
  RenderLock lock;
  Storage.remove(weather::CACHE_TMP_PATH);
  return !Storage.exists(weather::CACHE_PATH) || Storage.remove(weather::CACHE_PATH);
}

#else

// X4 Pro only (the sleep cards and their network live there).
bool due(bool) { return false; }
void run() {}
KeeperStep keeperStep(bool& cacheChanged) {
  cacheChanged = false;
  return KeeperStep::Nothing;
}
void benchFetch(char* out, const size_t cap) {
  if (out != nullptr && cap > 0) std::snprintf(out, cap, "board");
}
void benchShow(void (*)(void*, const char*), void*) {}
bool benchClear() { return false; }

#endif

}  // namespace WeatherFetch
