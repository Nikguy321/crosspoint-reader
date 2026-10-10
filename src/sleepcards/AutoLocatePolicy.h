#pragma once

#include <cstddef>
#include <cstdint>

#include "LocationFix.h"

// "Update location when syncing" (Display > Sleep Screen Cards, X4 Pro): the pure half, host-tested
// in test/sleep_card_location. The device half (network/AutoLocate) runs it while a book sync or
// a clock sync already has Wi-Fi up: the phone's GPS over its hotspot, else nearby Wi-Fi when two
// beaconDB lookups agree (network/LocateRun). Never an internet-address lookup.
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
// that is not known: no location, no dated record for it, or an internet-address location (which
// counts as not set).
bool locationAgeDays(const char* location, const char* record, int year, int month, int day, int& days);

// Due when the location is unset, undated, or saved on an earlier day than today (the record keeps
// the date only, so "older than a day" is "not saved today"); a date after today (a clock that was
// wrong then) is due too.
Decision decide(const Situation& s);

// "off", "tried-today", ... for the one log line.
const char* decisionName(Decision d);

enum class Verdict : uint8_t { Save, NotMeasured, TooVague, Invalid };
// A fix to save: measured (the phone's GPS, or Wi-Fi within MAX_ACCURACY_M; never typed or an
// internet-address lookup) and a real place. A phone fix may not know its accuracy (0).
Verdict judgeFix(LocationSource source, double lat, double lon, uint32_t accuracyM);

// For a new Wi-Fi fix: a measured fix already stored (Locate Me, the phone or an earlier refresh)
// that is at least as tight as the new one and agrees with it (the two are within their accuracies
// of each other) is kept rather than traded for the vaguer one. True with the stored fix in
// lat/lon/accuracyM and source: a Wi-Fi one comes back as "wifi-auto" to save re-dated today; a
// phone one as "phone", which the caller leaves exactly as stored (only the phone writes a phone
// fix). False (all unchanged) for anything else, typed and internet-address locations included.
bool keepTighterFix(const char* location, const char* record, double& lat, double& lon, uint32_t& accuracyM,
                    LocationSource& source);

// The stored location and its record (source "wifi-auto" or "phone") for a fix saved on the given
// day. False when either does not fit or the fix is not a valid location.
bool autoRecord(LocationSource source, double lat, double lon, uint32_t accuracyM, int year, int month, int day,
                char* location, size_t locationCap, char* record, size_t recordCap);

}  // namespace sleepcards::autolocate
