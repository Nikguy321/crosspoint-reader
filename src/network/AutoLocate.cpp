#include "AutoLocate.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_X4PRO
#include <Arduino.h>
#include <BookSyncStore.h>
#include <HalClock.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_attr.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "network/GeolocateProtocol.h"
#include "network/LocateRun.h"
#include "sleepcards/AutoLocatePolicy.h"
#include "sleepcards/CardTime.h"
#include "util/TaskWatchdog.h"
#include "util/Timezones.h"
#endif

namespace AutoLocate {

#if FREEINK_DEVICE_X4PRO
namespace {

namespace policy = sleepcards::autolocate;

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

// The lookup itself; returns the log word of a failure (tooFew: a skip, not a failure), or nullptr
// with what was saved in `saved`. `zone` is the clock zone decision for the fix (Time Zone From
// Location; Off when none was made): set with the saved location, or put back when the save
// failed after it was made.
const char* lookUpAndSave(bool& tooFew, const char*& saved, clockzone::Decision& zone) {
  tooFew = false;
  saved = "";
  zone = clockzone::Decision{};
  zone.verdict = clockzone::Verdict::Off;
  // From here on this is the day's one try, whatever comes of it - except a Wi-Fi half that could
  // not start for want of TLS heap (below). The phone needs no TLS, so it is always asked.
  const uint32_t triedBefore = triedYmd;
  const uint32_t magicBefore = triedMagic;
  triedYmd = policy::ymd(now.year, now.month, now.day);
  triedMagic = TRIED_MAGIC;

  LocateRun::Outcome outcome;
  {
    LocateRun::Options options;
    options.wifiBudgetMs = BUDGET_MS;
    options.quiet = true;
    LocateRun::run(outcome, options);
  }
  resetTaskWatchdogIfSubscribed();

  if (!outcome.fix.valid()) {
    switch (outcome.wifi) {
      case geolocate::WifiVerdict::TooFew:
        tooFew = true;
        return "too-few-aps";
      case geolocate::WifiVerdict::NoMemory:
        // Nothing was scanned or sent: a later sync today may try again.
        triedYmd = triedBefore;
        triedMagic = magicBefore;
        return "no-memory";
      case geolocate::WifiVerdict::Unreachable:
        return outcome.dnsFailed ? "dns" : "unreachable";
      case geolocate::WifiVerdict::Vague:
        return "too-vague";
      case geolocate::WifiVerdict::Disagree:
        return "disagree";
      default:
        return "no-fix";
    }
  }

  const bool fromPhone = outcome.fix.source == geolocate::FixSource::Phone;
  sleepcards::LocationSource source =
      fromPhone ? sleepcards::LocationSource::Phone : sleepcards::LocationSource::WifiAuto;
  switch (policy::judgeFix(source, outcome.fix.lat, outcome.fix.lon, outcome.fix.accuracyM)) {
    case policy::Verdict::Save:
      break;
    case policy::Verdict::TooVague:
      return "too-vague";
    case policy::Verdict::NotMeasured:
    case policy::Verdict::Invalid:
      return "no-fix";
  }

  double lat = outcome.fix.lat;
  double lon = outcome.fix.lon;
  uint32_t accuracyM = outcome.fix.accuracyM;
  // A Wi-Fi answer does not replace a tighter stored fix of the same place.
  if (!fromPhone && policy::keepTighterFix(now.location, now.record, lat, lon, accuracyM, source) &&
      source == sleepcards::LocationSource::Phone) {
    saved = "kept phone";  // the phone's own record stands as it is
    return nullptr;
  }
  // The clock zone from this run's own measurement comes first, so the record (and the day's try)
  // is dated in the zone the clock now keeps, as Locate Me's is. A failure below puts it back:
  // the clock keeps what the stored settings say.
  const uint8_t zoneBefore = SETTINGS.clockTimezone;
  zone = timezones::zoneForFix(outcome.fix);
  const bool zoneSet = timezones::applyZoneForFix(zone);
  const auto putZoneBack = [&] {
    if (!zoneSet) return;
    SETTINGS.clockTimezone = zoneBefore;
    timezones::applyToClock();
  };
  int year = now.year;
  int month = now.month;
  int day = now.day;
  if (zoneSet) {
    struct tm local{};
    if (halClock.localTime(local)) {
      year = local.tm_year + 1900;
      month = local.tm_mon + 1;
      day = local.tm_mday;
    }
  }
  char location[sizeof(SETTINGS.sleepCardLocation)];
  char record[sizeof(SETTINGS.sleepCardLocationFix)];
  if (!policy::autoRecord(source, lat, lon, accuracyM, year, month, day, location, sizeof(location), record,
                          sizeof(record))) {
    putZoneBack();
    return "no-fix";
  }
  std::snprintf(SETTINGS.sleepCardLocation, sizeof(SETTINGS.sleepCardLocation), "%s", location);
  std::snprintf(SETTINGS.sleepCardLocationFix, sizeof(SETTINGS.sleepCardLocationFix), "%s", record);
  if (!SETTINGS.saveToFile()) {
    putZoneBack();
    return "save";
  }
  triedYmd = policy::ymd(year, month, day);
  saved = fromPhone ? "saved phone" : "saved wifi";
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

void rearm() {
  answered = false;
  approved = false;
}

void run() {
  if (!approved) return;
  approved = false;
  bool tooFew = false;
  const char* saved = "";
  clockzone::Decision zone;
  const char* failure = lookUpAndSave(tooFew, saved, zone);
  if (failure == nullptr && zone.verdict == clockzone::Verdict::Set) {
    LOG_INF("GEO", "autolocate: %s, clock set %s", saved, timezones::table()[zone.entry].name);
  } else if (failure == nullptr && zone.verdict != clockzone::Verdict::Off) {
    LOG_INF("GEO", "autolocate: %s, clock %s", saved, clockzone::verdictName(zone.verdict));
  } else if (failure == nullptr) {
    LOG_INF("GEO", "autolocate: %s", saved);
  } else if (tooFew) {
    skipped(failure);
  } else if (zone.verdict == clockzone::Verdict::Set) {
    LOG_INF("GEO", "autolocate: failed %s, clock unchanged", failure);
  } else {
    failed(failure);
  }
}

#else

// X4 Pro only (the sleep cards and the location lookup live there).
bool due(bool) { return false; }
void run() {}
void rearm() {}

#endif

}  // namespace AutoLocate
