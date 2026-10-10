#pragma once

#include <cstddef>
#include <cstdint>

#include "GeolocateProtocol.h"
#include "PhoneGps.h"
#include "PhoneNmea.h"

// Where the reader is, for Locate Me, "Update location when syncing" (AutoLocate) and the bench
// console's LOCTEST (X4 Pro), on a station already joined - it never starts the radio:
//   1. the phone's own GPS over its hotspot (network/PhoneGps), when it answers with a fix that
//      passes phonenmea's gates;
//   2. otherwise nearby Wi-Fi: one scan, two beaconDB lookups of separate halves of the access
//      points (the joined access point left out when anything listened on the phone ports of this
//      network), taken only when both answers agree (geolocate::judgeWifi);
//   3. otherwise nothing. There is no internet-address lookup: a wrong place is worse than none.
// Saves nothing: the caller decides. Logs counts and verdicts, never a position or an address.
namespace LocateRun {

enum class Step : uint8_t { Phone, Scan, Wifi };

enum class PhoneVerdict : uint8_t {
  Located,   // a fix passed every gate
  NoFix,     // a phone answered with NMEA, but nothing it sent was a usable fix
  NotFound,  // nothing answered with NMEA
};

struct Options {
  // > 0: the Wi-Fi half (scan, name lookup, both requests) within this many ms, the name lookup
  // capped (AutoLocate, beside another job). 0: the HTTPS client's interactive timeouts (Locate Me).
  uint32_t wifiBudgetMs = 0;
  // No error lines from the HTTPS client (a background caller logs its own outcome).
  bool quiet = false;
  // Called before each step (Locate Me repaints its status line).
  void (*onStep)(Step step, void* ctx) = nullptr;
  void* ctx = nullptr;
  // The bench console's LOCTEST only (LOCPHONE): read the phone source from this address instead
  // of the gateway. Locate Me and AutoLocate leave it as it is.
  PhoneGps::Target phoneTarget;
};

struct Outcome {
  geolocate::Fix fix;  // valid = located: source Phone or Wifi
  PhoneVerdict phone = PhoneVerdict::NotFound;
  uint16_t phonePort = 0;
  const char* phoneWhy = "not-found";
  phonenmea::Stats phoneStats;
  int64_t phoneSkewS = -1;  // the clock allowance the phone's sentences had (-1 = no trusted clock)
  geolocate::WifiVerdict wifi = geolocate::WifiVerdict::NotAsked;
  bool dnsFailed = false;  // budget mode: the name lookup did not answer in time
  int16_t seen = 0;        // access points the scan saw
  size_t usable = 0;       // ... usable for the lookup (geolocate::Split::usable)
  size_t devices = 0;      // ... and the devices they belong to (geolocate::Split::devices)
  int statusA = 0;         // each half's HTTP status (0 = no answer)
  int statusB = 0;
  uint32_t accuracyA = 0;  // each half's accuracy, metres (0 = no location)
  uint32_t accuracyB = 0;
  uint32_t apartM = 0;  // the halves' distance apart (0 = not compared)
};

// The phone, then Wi-Fi. Blocks the calling (loop) task: about 11 s at most for the phone, then
// the scan and two HTTPS requests.
void run(Outcome& out, const Options& options);

}  // namespace LocateRun
