#pragma once

#include <cstddef>
#include <cstdint>

// "Locate me" (Display > Sleep Screen Cards, X4 Pro) and "Update location when syncing": the pure
// half of the Wi-Fi lookup, host-tested in test/geolocate. Builds two beaconDB requests from one
// Wi-Fi scan, reads the answers and accepts a location only when the two agree. The device half
// (the scan, the HTTPS calls) is network/LocateRun + GeolocateClient. No internet-address lookup
// exists: a carrier's or VPN's exit city is worse than no location.
//
//   POST https://api.beacondb.net/v1/geolocate (the MLS / Ichnaea geolocate API)
//   {"considerIp":false,"fallbacks":{"ipf":false,"lacf":false},
//    "wifiAccessPoints":[{"macAddress":"aa:bb:..","signalStrength":-60},..]}
//   -> {"location":{"lat":..,"lng":..},"accuracy":<m>}   404 {"error":{..}} = no estimate
//
// beaconDB averages whichever access points it knows and reports how far they are usually heard,
// not whether they agree: one access point that has moved (a household that moved, a travel
// router) drags the answer while the accuracy still reads ~50 m. So the usable access points are
// split into two halves, each half is looked up on its own, and the answer counts only when both
// halves place the reader within their accuracies of each other. One router broadcasts several
// BSSIDs (2.4 and 5 GHz, guest networks) from neighbouring addresses, and beaconDB counts BSSIDs,
// so the halves are dealt by device: the addresses of one device all go into the same half.
namespace geolocate {

// Per request (each half).
constexpr size_t MAX_REQUEST_APS = 20;
// Usable devices needed for the two-half check: two per half.
constexpr size_t MIN_REQUEST_APS = 4;
// Two BSSIDs with the same OUI (first three octets) whose 48-bit values differ by less than this
// are taken as one device: multi-BSSID routers and enterprise access points number their radios
// and networks from one base address (a block of 16 per radio is common).
constexpr uint32_t SAME_DEVICE_SPAN = 256;
// Weaker access points are not used: they may be far away or barely heard.
constexpr int16_t MIN_RSSI_DBM = -85;
// Each half's answer must be at least this tight (metres)...
constexpr uint32_t MAX_HALF_ACCURACY_M = 100;
// ... and the location reported is never claimed tighter than this.
constexpr uint32_t MIN_REPORTED_ACCURACY_M = 50;
// The largest response body beaconDB may send back.
constexpr size_t RESPONSE_CAP = 4096;
// Twenty access points at their longest ({"macAddress":"..","signalStrength":-127}, and a comma: 57
// bytes) plus the envelope (81) and the terminator.
constexpr size_t REQUEST_CAP = 1280;
constexpr size_t SSID_CAP = 33;

struct AccessPoint {
  uint8_t mac[6] = {};
  int16_t rssi = -127;  // dBm
  char ssid[SSID_CAP] = "";
};

// May this access point be sent? Not hidden (empty SSID), not an opted-out SSID (ending in
// "_nomap" or "_optout", any case), a globally administered unicast BSSID (no locally
// administered bit, no group bit, not all zeros: randomised and phone-hotspot addresses move with
// their owner), and not a VRRP virtual router (00:00:5e:00:01:xx / 00:00:5e:00:02:xx, the same
// address on routers everywhere).
bool usableAccessPoint(const AccessPoint& ap);

// Are these two BSSIDs one device's (SAME_DEVICE_SPAN)?
bool sameDevice(const uint8_t* a, const uint8_t* b);

struct Split {
  size_t usable = 0;   // usable access points at MIN_RSSI_DBM or stronger (at most 2 x MAX_REQUEST_APS)
  size_t devices = 0;  // ... and the devices they belong to (sameDevice)
  size_t halfA = 0;    // how many access points went into each body
  size_t halfB = 0;
  bool built = false;  // at least MIN_REQUEST_APS devices and both bodies fit
};

// The two request bodies for one scan: the usable access points at MIN_RSSI_DBM or stronger, the
// strongest 2 x MAX_REQUEST_APS (a BSSID seen twice counted once at its stronger reading; exclude,
// when not nullptr, a device left out: the joined network when it is a phone's hotspot), grouped
// by device, the devices ranked by their strongest access point and dealt alternately into A
// (ranks 1, 3, 5 ..) and B (2, 4, 6 ..), every address of a device in its device's half,
// strongest first, each body up to MAX_REQUEST_APS. Both bodies are "" unless built.
Split buildSplitRequests(const AccessPoint* aps, size_t count, const uint8_t* exclude, char* bodyA, char* bodyB,
                         size_t cap);

enum class FixSource : uint8_t { None = 0, Wifi = 1, Phone = 2 };

struct Fix {
  FixSource source = FixSource::None;
  double lat = 0;
  double lon = 0;
  uint32_t accuracyM = 0;  // 0 = not known (a phone fix without HDOP)
  bool valid() const { return source != FixSource::None; }
};

// beaconDB's answer: a location with lat/lng inside +-90/+-180 (not 0,0) and a positive accuracy.
// Refused: an error object, a "fallback" estimate (IP or cell, not Wi-Fi), a missing field, a
// value without its own key (a key too long for the tokenizer is dropped by it), a key without a
// value, and anything but one JSON object. The tokenizer does not check commas, so a missing
// comma between two members is not caught. out is reset either way.
bool parseBeaconDbResponse(const char* body, size_t len, Fix& out);

// Metres between two points (haversine on the mean Earth radius).
double distanceM(double lat1, double lon1, double lat2, double lon2);

// How one half's request went.
enum class Reply : uint8_t {
  NotAsked,  // skipped (too few access points, or the budget ran out)
  NoMemory,  // not started: too little internal RAM for a TLS session
  NoAnswer,  // DNS, connect, TLS or a timeout: the service was never heard from
  Answered,  // the service sent a complete response (whatever it said)
};

struct HalfAnswer {
  Reply reply = Reply::NotAsked;
  int status = 0;  // HTTP status
  Fix fix;         // parsed from the body (parseBeaconDbResponse); invalid = no usable location
};

enum class WifiVerdict : uint8_t {
  NotAsked,     // the phone answered first: no Wi-Fi lookup was made
  Located,      // both halves agree
  TooFew,       // usable access points from fewer than MIN_REQUEST_APS devices
  NoMemory,     // a request could not start for want of RAM
  Unreachable,  // a half was never heard from (the network may not reach the internet)
  NotFound,     // a half got no location (not HTTP 200, a 404, a fallback estimate)
  Vague,        // a half's accuracy is over MAX_HALF_ACCURACY_M
  Disagree,     // the halves lie further apart than the larger of their accuracies
};

// Is the second half worth asking? Only when the first is an HTTP 200 location within
// MAX_HALF_ACCURACY_M: otherwise the verdict is already the first half's failure.
bool secondHalfNeeded(const HalfAnswer& first);

// The two-half check: each half answered HTTP 200 with a location (no fallback), accuracy 1 to
// MAX_HALF_ACCURACY_M, and the two within the larger accuracy of each other. The first half's
// failure decides when it fails (the second need not have been asked). Located: out = the
// midpoint, accuracy max(MIN_REPORTED_ACCURACY_M, both accuracies, the distance between them).
// apartM = that distance (0 when not compared).
WifiVerdict judgeWifi(const Split& split, const HalfAnswer& a, const HalfAnswer& b, Fix& out, uint32_t& apartM);

// "located", "too-few-aps", ... for log lines.
const char* wifiVerdictName(WifiVerdict v);

}  // namespace geolocate
