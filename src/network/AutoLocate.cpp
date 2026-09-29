#include "AutoLocate.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_X4PRO
#include <Arduino.h>
#include <BookSyncStore.h>
#include <HalClock.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "network/GeolocateClient.h"
#include "network/GeolocateProtocol.h"
#include "sleepcards/AutoLocatePolicy.h"
#include "sleepcards/CardTime.h"
#include "util/TaskWatchdog.h"
#endif

namespace AutoLocate {

#if FREEINK_DEVICE_X4PRO
namespace {

namespace policy = sleepcards::autolocate;

constexpr const char* BEACONDB_URL = "https://api.beacondb.net/v1/geolocate";
constexpr const char* BEACONDB_HOST = "api.beacondb.net";
constexpr uint32_t CONNECT_TIMEOUT_MS = 4000;
// Not worth starting a TLS request with less than this left of the budget.
constexpr uint32_t MIN_REQUEST_MS = 3000;
// Scan results kept for the request (the strongest 20 are sent), as Locate Me.
constexpr size_t MAX_SCAN = 48;

// The day of the last try, kept across the silent reboot that ends every sync and across deep
// sleep (RTC memory; a cold boot forgets it, which costs at most one more try that day).
constexpr uint32_t TRIED_MAGIC = 0x41554C43;  // "AULC"
RTC_NOINIT_ATTR uint32_t triedMagic;
RTC_NOINIT_ATTR uint32_t triedYmd;

bool answered = false;  // due() has answered this boot
bool approved = false;  // ... and said run
policy::Situation now;  // what due() saw, for run() (points at the settings' own strings)

void skipped(const char* why) { LOG_INF("GEO", "autolocate: skipped %s", why); }
void failed(const char* why) { LOG_INF("GEO", "autolocate: failed %s", why); }

// A late answer (after the wait gave up) lands here, never on a returned stack frame.
volatile bool dnsDone = false;
volatile bool dnsFound = false;
void onDnsAnswer(const char*, const ip_addr_t* address, void*) {
  dnsFound = address != nullptr;
  dnsDone = true;
}

// The host's address into lwIP's cache within timeoutMs, so the request's own lookup finds it
// there: the one lookup that is not otherwise bounded (the resolver retries for many seconds on
// a network that does not reach the internet). False when it is not known in time.
bool resolveWithin(const char* host, const uint32_t timeoutMs) {
  dnsDone = false;
  dnsFound = false;
  ip_addr_t address;
  LOCK_TCPIP_CORE();
  const err_t err = dns_gethostbyname(host, &address, onDnsAnswer, nullptr);
  UNLOCK_TCPIP_CORE();
  if (err == ERR_OK) return true;  // cached already
  if (err != ERR_INPROGRESS) return false;
  const unsigned long started = millis();
  while (!dnsDone && millis() - started < timeoutMs) delay(20);
  return dnsDone && dnsFound;
}

// The lookup itself; returns the log word of a failure, nullptr when saved.
const char* lookUpAndSave(const unsigned long started, bool& tooFew) {
  tooFew = false;
  // Heap, freed on return (as Locate Me): the scan copy (48 x 40 B), the request (1.3 KB) and the
  // response (4 KB), only for this one run.
  auto aps = makeUniqueNoThrow<geolocate::AccessPoint[]>(MAX_SCAN);
  auto request = makeUniqueNoThrow<char[]>(geolocate::REQUEST_CAP);
  auto response = makeUniqueNoThrow<char[]>(geolocate::RESPONSE_CAP + 1);
  // The TLS session's heap is checked before the scan, which would be wasted without it.
  if (!aps || !request || !response || !GeolocateClient::enoughHeap()) return "no-memory";

  // From here on this is the day's one try, whatever comes of it.
  triedYmd = policy::ymd(now.year, now.month, now.day);
  triedMagic = TRIED_MAGIC;

  // A blocking station scan on the joined radio (RadioPower: the radio lock is already held).
  int16_t found = 0;
  const size_t apCount = GeolocateClient::scanAccessPoints(aps.get(), MAX_SCAN, found);
  const size_t sent = geolocate::buildRequestBody(aps.get(), apCount, request.get(), geolocate::REQUEST_CAP);
  aps.reset();
  if (sent < geolocate::MIN_REQUEST_APS) {
    tooFew = true;
    return "too-few-aps";
  }
  resetTaskWatchdogIfSubscribed();

  unsigned long elapsed = millis() - started;
  if (elapsed + MIN_REQUEST_MS > BUDGET_MS) return "timeout";
  const uint32_t dnsMs = std::min<uint32_t>(DNS_MS, BUDGET_MS - MIN_REQUEST_MS - elapsed);
  if (!resolveWithin(BEACONDB_HOST, dnsMs)) return "dns";
  resetTaskWatchdogIfSubscribed();
  elapsed = millis() - started;
  if (elapsed + MIN_REQUEST_MS > BUDGET_MS) return "timeout";
  GeolocateClient::Options options;
  options.requestTimeoutMs = static_cast<uint32_t>(BUDGET_MS - elapsed);
  options.connectTimeoutMs = std::min<uint32_t>(options.requestTimeoutMs, CONNECT_TIMEOUT_MS);
  options.quiet = true;
  size_t length = 0;
  int httpStatus = 0;
  const auto result = GeolocateClient::request(BEACONDB_URL, request.get(), response.get(), geolocate::RESPONSE_CAP + 1,
                                               length, httpStatus, options);
  request.reset();
  resetTaskWatchdogIfSubscribed();
  if (result == GeolocateClient::Result::NoMemory) return "no-memory";
  if (result == GeolocateClient::Result::Transport) return "unreachable";
  if (result != GeolocateClient::Result::Ok || httpStatus != 200) return "no-fix";

  // beaconDB's own Wi-Fi estimate only: parseBeaconDbResponse refuses its IP/cell "fallback".
  geolocate::Fix fix;
  if (!geolocate::parseBeaconDbResponse(response.get(), length, fix)) return "no-fix";
  switch (policy::judgeFix(fix.source == geolocate::FixSource::Wifi, fix.lat, fix.lon, fix.accuracyM)) {
    case policy::Verdict::Save:
      break;
    case policy::Verdict::TooVague:
      return "too-vague";
    case policy::Verdict::NotWifi:
    case policy::Verdict::Invalid:
      return "no-fix";
  }

  // A tighter stored Wi-Fi fix of the same place is kept (re-dated) rather than traded down.
  double lat = fix.lat;
  double lon = fix.lon;
  uint32_t accuracyM = fix.accuracyM;
  policy::keepTighterFix(now.location, now.record, lat, lon, accuracyM);
  char location[sizeof(SETTINGS.sleepCardLocation)];
  char record[sizeof(SETTINGS.sleepCardLocationFix)];
  if (!policy::autoRecord(lat, lon, accuracyM, now.year, now.month, now.day, location, sizeof(location), record,
                          sizeof(record))) {
    return "no-fix";
  }
  std::snprintf(SETTINGS.sleepCardLocation, sizeof(SETTINGS.sleepCardLocation), "%s", location);
  std::snprintf(SETTINGS.sleepCardLocationFix, sizeof(SETTINGS.sleepCardLocationFix), "%s", record);
  if (!SETTINGS.saveToFile()) return "save";
  return nullptr;
}

}  // namespace

bool due(const bool jobOnline) {
  if (!SETTINGS.autoLocateOnSync || answered) return false;
  answered = true;
  if (deepSleepStarting()) {
    skipped("sleeping");
    return false;
  }

  now = policy::Situation{};
  now.enabled = true;
  now.jobOnline = jobOnline;
  now.triedYmd = triedMagic == TRIED_MAGIC ? triedYmd : 0;
  now.connected = WiFi.status() == WL_CONNECTED;
  // Kept alive for the pointers in `now` while due() decides (run() needs only the settings').
  const std::string ssid = now.connected ? std::string(WiFi.SSID().c_str()) : std::string();
  const BookSync::Config sync = BOOKSYNC_STORE.getConfig();
  now.ssid = ssid.c_str();
  now.peerSsid = sync.peerSsid.c_str();
  now.hubSsid = sync.hubSsid.c_str();
  time_t utc = 0;
  struct tm local{};
  if (halClock.utcEpoch(utc) && sleepcards::plausibleTime(static_cast<int64_t>(utc)) && halClock.localTime(local)) {
    now.clockValid = true;
    now.year = local.tm_year + 1900;
    now.month = local.tm_mon + 1;
    now.day = local.tm_mday;
  }
  now.location = SETTINGS.sleepCardLocation;
  now.record = SETTINGS.sleepCardLocationFix;

  const policy::Decision decision = policy::decide(now);
  now.ssid = now.peerSsid = now.hubSsid = "";
  if (decision != policy::Decision::Run) {
    skipped(policy::decisionName(decision));
    return false;
  }
  approved = true;
  return true;
}

void run() {
  if (!approved) return;
  approved = false;
  const unsigned long started = millis();
  bool tooFew = false;
  const char* failure = lookUpAndSave(started, tooFew);
  if (failure == nullptr) {
    LOG_INF("GEO", "autolocate: saved");
  } else if (tooFew) {
    skipped(failure);
  } else {
    failed(failure);
  }
}

#else

// X4 Pro only (the sleep cards and the location lookup live there).
bool due(bool) { return false; }
void run() {}

#endif

}  // namespace AutoLocate
