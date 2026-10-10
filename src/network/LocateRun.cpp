#include "LocateRun.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_X4PRO
#include <Arduino.h>
#include <HalClock.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <ctime>

#include "CrossPointSettings.h"
#include "network/GeolocateClient.h"
#include "network/PhoneGps.h"
#include "sleepcards/CardTime.h"
#include "util/TaskWatchdog.h"
#endif

namespace LocateRun {

#if FREEINK_DEVICE_X4PRO
namespace {

constexpr const char* BEACONDB_URL = "https://api.beacondb.net/v1/geolocate";
constexpr const char* BEACONDB_HOST = "api.beacondb.net";
// Scan results kept for the requests (the strongest 40 are used); a crowded scan is cut here.
constexpr size_t MAX_SCAN = 48;
// Budget mode (AutoLocate): the name lookup's cap, each request's connect timeout, and the least
// worth starting a TLS request with.
constexpr uint32_t DNS_MS = 3000;
constexpr uint32_t CONNECT_TIMEOUT_MS = 4000;
constexpr uint32_t MIN_REQUEST_MS = 3000;

// The reader's UTC clock when it has been set from the internet (the phone's sentence times are
// checked against it), else -1, and how far a sentence may be from it: the RTC drifts between
// syncs, so the allowance widens with the time since the last one (phonenmea::skewAllowanceS). A
// fresh RTC read: the cached one can be 10 s old.
int64_t trustedUtcNow(int64_t& maxSkewS) {
  maxSkewS = phonenmea::MAX_SKEW_CAP_S;
  if (!SETTINGS.clockHasBeenSynced || !halClock.isAvailable()) return -1;
  halClock.invalidate();
  time_t utc = 0;
  if (!halClock.utcEpoch(utc) || !sleepcards::plausibleTime(static_cast<int64_t>(utc))) return -1;
  time_t synced = 0;
  const bool knownSync = halClock.lastSyncUtc(synced) && synced <= utc;
  maxSkewS = phonenmea::skewAllowanceS(knownSync ? static_cast<int64_t>(utc - synced) : -1);
  return static_cast<int64_t>(utc);
}

void announce(const Options& options, const Step step) {
  if (options.onStep != nullptr) options.onStep(step, options.ctx);
}

geolocate::Reply replyOf(const GeolocateClient::Result result) {
  switch (result) {
    case GeolocateClient::Result::Ok:
    case GeolocateClient::Result::TooLarge:
      return geolocate::Reply::Answered;
    case GeolocateClient::Result::NoMemory:
      return geolocate::Reply::NoMemory;
    case GeolocateClient::Result::Transport:
      break;
  }
  return geolocate::Reply::NoAnswer;
}

// The time left of a budget that started at `started` (0 = spent or not enough for a request).
uint32_t leftForRequest(const unsigned long started, const uint32_t budgetMs) {
  const unsigned long elapsed = millis() - started;
  if (elapsed + MIN_REQUEST_MS > budgetMs) return 0;
  return static_cast<uint32_t>(budgetMs - elapsed);
}

// One half's request and its parsed answer (HTTP 200 only).
geolocate::HalfAnswer ask(const char* body, char* response, const Options& options, const unsigned long started) {
  geolocate::HalfAnswer half;
  GeolocateClient::Options client;
  client.quiet = options.quiet;
  if (options.wifiBudgetMs > 0) {
    const uint32_t left = leftForRequest(started, options.wifiBudgetMs);
    if (left == 0) return half;  // NotAsked: the budget ran out
    client.requestTimeoutMs = left;
    client.connectTimeoutMs = std::min<uint32_t>(left, CONNECT_TIMEOUT_MS);
  }
  size_t length = 0;
  int status = 0;
  const auto result =
      GeolocateClient::request(BEACONDB_URL, body, response, geolocate::RESPONSE_CAP + 1, length, status, client);
  resetTaskWatchdogIfSubscribed();
  half.reply = replyOf(result);
  half.status = status;
  if (result == GeolocateClient::Result::Ok && status == 200) {
    geolocate::parseBeaconDbResponse(response, length, half.fix);
  }
  return half;
}

void wifiHalf(Outcome& out, const Options& options, const bool phoneListened) {
  const unsigned long started = millis();
  // Heap, freed on return: the scan copy (48 x 40 B), two request bodies (1.3 KB each) and one
  // response (4 KB). Too big for the loop task's stack, and only alive for this one lookup.
  auto aps = makeUniqueNoThrow<geolocate::AccessPoint[]>(MAX_SCAN);
  auto bodyA = makeUniqueNoThrow<char[]>(geolocate::REQUEST_CAP);
  auto bodyB = makeUniqueNoThrow<char[]>(geolocate::REQUEST_CAP);
  auto response = makeUniqueNoThrow<char[]>(geolocate::RESPONSE_CAP + 1);
  // The TLS session's heap is checked before the scan, which would be wasted without it.
  if (!aps || !bodyA || !bodyB || !response || !GeolocateClient::enoughHeap()) {
    LOG_ERR("GEO", "Not enough memory for the Wi-Fi lookup");
    out.wifi = geolocate::WifiVerdict::NoMemory;
    return;
  }

  announce(options, Step::Scan);
  // Something listened on the phone ports here: the joined access point is a phone's hotspot,
  // which travels with it (a hotspot with a randomised address is filtered anyway).
  uint8_t joined[6] = {};
  const bool leaveOut = phoneListened && WiFi.BSSID(joined) != nullptr;
  int16_t found = 0;
  const size_t count = GeolocateClient::scanAccessPoints(aps.get(), MAX_SCAN, found);
  const geolocate::Split split = geolocate::buildSplitRequests(aps.get(), count, leaveOut ? joined : nullptr,
                                                               bodyA.get(), bodyB.get(), geolocate::REQUEST_CAP);
  aps.reset();
  out.seen = found;
  out.usable = split.usable;
  out.devices = split.devices;
  resetTaskWatchdogIfSubscribed();

  geolocate::HalfAnswer a;
  geolocate::HalfAnswer b;
  if (split.built) {
    announce(options, Step::Wifi);
    bool resolved = true;
    if (options.wifiBudgetMs > 0) {
      // The one step the request's own timeouts do not bound.
      const uint32_t left = leftForRequest(started, options.wifiBudgetMs);
      const uint32_t dnsMs = left == 0 ? 0 : std::min<uint32_t>(DNS_MS, left - MIN_REQUEST_MS);
      resolved = dnsMs > 0 && GeolocateClient::resolveWithin(BEACONDB_HOST, dnsMs);
      out.dnsFailed = !resolved;
      resetTaskWatchdogIfSubscribed();
    }
    if (resolved) {
      a = ask(bodyA.get(), response.get(), options, started);
      if (geolocate::secondHalfNeeded(a)) b = ask(bodyB.get(), response.get(), options, started);
    } else {
      a.reply = geolocate::Reply::NoAnswer;
    }
  }
  out.wifi = geolocate::judgeWifi(split, a, b, out.fix, out.apartM);
  out.statusA = a.status;
  out.statusB = b.status;
  out.accuracyA = a.fix.valid() ? a.fix.accuracyM : 0;
  out.accuracyB = b.fix.valid() ? b.fix.accuracyM : 0;
}

}  // namespace

void run(Outcome& out, const Options& options) {
  out = Outcome{};
  announce(options, Step::Phone);
  int64_t maxSkewS = 0;
  const int64_t nowUtcS = trustedUtcNow(maxSkewS);
  out.phoneSkewS = nowUtcS < 0 ? -1 : maxSkewS;
  const PhoneGps::Result phone = PhoneGps::read(nowUtcS, maxSkewS, options.phoneTarget);
  out.phonePort = phone.port;
  out.phoneWhy = phone.why;
  out.phoneStats = phone.stats;
  if (phone.located) {
    out.phone = PhoneVerdict::Located;
    out.fix.source = geolocate::FixSource::Phone;
    out.fix.lat = phone.fix.lat;
    out.fix.lon = phone.fix.lon;
    out.fix.accuracyM = phone.fix.accuracyM;
    LOG_INF("GEO", "locate: phone GPS, accuracy %lu m", static_cast<unsigned long>(out.fix.accuracyM));
    return;
  }
  out.phone = phone.heard ? PhoneVerdict::NoFix : PhoneVerdict::NotFound;
  resetTaskWatchdogIfSubscribed();

  // The joined access point is the phone's hotspot only when the gateway itself listened (a bench
  // stand-in elsewhere on the network says nothing about it).
  const bool gatewayListened = options.phoneTarget.address == 0 && (phone.heard || phone.accepted);
  wifiHalf(out, options, gatewayListened);
  LOG_INF("GEO", "locate: wifi %s (%d seen, %u usable, %u devices, HTTP %d/%d, %lu/%lu m, %lu m apart)",
          geolocate::wifiVerdictName(out.wifi), static_cast<int>(out.seen), static_cast<unsigned>(out.usable),
          static_cast<unsigned>(out.devices), out.statusA, out.statusB, static_cast<unsigned long>(out.accuracyA),
          static_cast<unsigned long>(out.accuracyB), static_cast<unsigned long>(out.apartM));
}

#else

// X4 Pro only (Locate Me and its network live there).
void run(Outcome& out, const Options&) { out = Outcome{}; }

#endif

}  // namespace LocateRun
