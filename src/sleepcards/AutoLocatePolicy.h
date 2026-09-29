#pragma once

#include <cstddef>
#include <cstdint>

// "Update location when syncing" (Display > Sleep Screen Cards, X4 Pro): the pure half, host-tested
// in test/sleep_card_location. The device half (network/AutoLocate) runs it while a book sync or
// a clock sync already has Wi-Fi up, and asks beaconDB only - never the internet-address lookup.
namespace sleepcards::autolocate {

// A Wi-Fi answer vaguer than this is not saved.
constexpr uint32_t MAX_ACCURACY_M = 1000;

enum class Decision : uint8_t {
  Run,
  Off,            // the setting is off (nothing is logged)
  JobOffline,     // the job's own request never reached its server: the network is not working
  NotConnected,   // no Wi-Fi up for another job: this never starts the radio
  DeviceNetwork,  // the sync peer's or a hub's own hotspot: no internet behind it
  NoClock,        // no trustworthy date to age the location by, or to stamp the new one with
  AlreadyTried,   // one attempt per day, whatever came of it (a failed one is not repeated each sync)
  Fresh,          // the location was saved today
};

// A date as one number, 20260929 (0 = none).
constexpr uint32_t ymd(const int year, const int month, const int day) {
  return year <= 0 ? 0u : static_cast<uint32_t>(year * 10000 + month * 100 + day);
}

struct Situation {
  bool enabled = false;
  bool jobOnline = false;  // the job's request got an HTTP answer (a sync) or synced the clock
  uint32_t triedYmd = 0;   // the day of the last attempt (ymd; 0 = none known)
  bool connected = false;
  const char* ssid = "";      // the joined network
  const char* peerSsid = "";  // the book-sync peer's hotspot ("" = none)
  const char* hubSsid = "";   // the book-sync hub's hotspot ("" = none)
  bool clockValid = false;
  int year = 0;  // today, local
  int month = 0;
  int day = 0;
  const char* location = "";  // the stored sleepCardLocation ("" = not set)
  const char* record = "";    // the stored sleepCardLocationFix
};

// Whole days since the location was saved (its record's date; a typed one counts too). False when
// that is not known: no location, or no dated record for it.
bool locationAgeDays(const char* location, const char* record, int year, int month, int day, int& days);

// Due when the location is unset, undated, or saved on an earlier day than today (the record keeps
// the date only, so "older than a day" is "not saved today"); a date after today (a clock that was
// wrong then) is due too.
Decision decide(const Situation& s);

// "off", "tried-today", ... for the one log line.
const char* decisionName(Decision d);

enum class Verdict : uint8_t { Save, NotWifi, TooVague, Invalid };
// A fix to save: from Wi-Fi (never an internet-address lookup), within MAX_ACCURACY_M, a real place.
Verdict judgeFix(bool fromWifi, double lat, double lon, uint32_t accuracyM);

// A measured Wi-Fi fix already stored (Locate Me or an earlier refresh) that is at least as tight as
// the new one and agrees with it (the two are within their accuracies of each other) is kept - its
// place and accuracy, re-dated today - rather than traded for the vaguer one. True with the fix to
// save in lat/lon/accuracyM; false (they unchanged) for anything else, typed locations included.
bool keepTighterFix(const char* location, const char* record, double& lat, double& lon, uint32_t& accuracyM);

// The stored location and its "wifi-auto" record for a fix saved on the given day. False when
// either does not fit or the fix is not a valid location.
bool autoRecord(double lat, double lon, uint32_t accuracyM, int year, int month, int day, char* location,
                size_t locationCap, char* record, size_t recordCap);

}  // namespace sleepcards::autolocate
