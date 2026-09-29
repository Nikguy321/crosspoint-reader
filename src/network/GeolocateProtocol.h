#pragma once

#include <cstddef>
#include <cstdint>

// "Locate me" (Display > Sleep Screen Cards, X4 Pro): the pure half of the lookup, host-tested in
// test/geolocate. Builds the beaconDB request from a Wi-Fi scan, reads the two services' answers
// and picks the fix to offer. The device half (the Wi-Fi join, the scan, the HTTPS calls) is
// LocateMeActivity + GeolocateClient.
//
//   Wi-Fi:  POST https://api.beacondb.net/v1/geolocate (the MLS / Ichnaea geolocate API)
//           {"considerIp":false,"wifiAccessPoints":[{"macAddress":"aa:bb:..","signalStrength":-60},..]}
//           -> {"location":{"lat":..,"lng":..},"accuracy":<m>}   404 {"error":{..}} = no estimate
//   IP:     GET https://ipwho.is/?fields=success,message,latitude,longitude,city,region
//           -> {"success":true,"latitude":..,"longitude":..,"city":"..","region":".."}
namespace geolocate {

constexpr size_t MAX_REQUEST_APS = 20;
// The geolocate API needs two access points for a Wi-Fi estimate.
constexpr size_t MIN_REQUEST_APS = 2;
// A Wi-Fi answer vaguer than this falls back to the internet-address lookup.
constexpr uint32_t WIFI_GOOD_ACCURACY_M = 5000;
// An internet-address lookup gives no accuracy. This nominal city-level figure only ranks it
// against a Wi-Fi fix; it is never shown as a number (a mobile carrier or VPN address can be far
// further off).
constexpr uint32_t IP_ACCURACY_M = 25000;
// The largest response body either service may send back.
constexpr size_t RESPONSE_CAP = 4096;
// Twenty access points at their longest ({"macAddress":"..","signalStrength":-127}, and a comma: 57
// bytes) plus the envelope (42) and the terminator.
constexpr size_t REQUEST_CAP = 1280;
constexpr size_t SSID_CAP = 33;
constexpr size_t PLACE_CAP = 64;

struct AccessPoint {
  uint8_t mac[6] = {};
  int16_t rssi = -127;  // dBm
  char ssid[SSID_CAP] = "";
};

// May this access point be sent? Not hidden (empty SSID), not an opted-out SSID (ending in
// "_nomap" or "_optout", any case), and a globally administered unicast BSSID (no locally
// administered bit, no group bit, not all zeros): randomised and phone-hotspot addresses move
// with their owner and say nothing about a place.
bool usableAccessPoint(const AccessPoint& ap);

// The geolocate request body for the strongest usable access points (up to MAX_REQUEST_APS, by
// RSSI, a BSSID seen twice counted once at its stronger reading). Returns how many went in, or 0
// (out = "") when fewer than MIN_REQUEST_APS are usable or the body does not fit cap.
size_t buildRequestBody(const AccessPoint* aps, size_t count, char* out, size_t cap);

enum class FixSource : uint8_t { None = 0, Wifi = 1, Ip = 2 };

struct Fix {
  FixSource source = FixSource::None;
  double lat = 0;
  double lon = 0;
  uint32_t accuracyM = 0;
  char place[PLACE_CAP] = "";  // "Seattle, Washington"; "" when the service gives none
  bool valid() const { return source != FixSource::None; }
};

// beaconDB's answer: a location with lat/lng inside +-90/+-180 (not 0,0) and a positive accuracy.
// Refused: an error object, a "fallback" estimate (IP or cell, not Wi-Fi), a missing field, a
// value without its own key (a key too long for the tokenizer is dropped by it), a key without a
// value, and anything but one JSON object. The tokenizer does not check commas, so a missing
// comma between two members is not caught. out is reset either way.
bool parseBeaconDbResponse(const char* body, size_t len, Fix& out);

// ipwho.is's answer: "success":true and latitude/longitude as above; the place is "city, region"
// from whichever of the two is present (\uXXXX escapes decoded, control characters dropped),
// accuracy IP_ACCURACY_M. out is reset either way.
bool parseIpWhoisResponse(const char* body, size_t len, Fix& out);

// Does the Wi-Fi fix leave the internet-address lookup still to do?
bool needsIpLookup(const Fix& wifi);

// The fix to offer: the Wi-Fi fix when it is within WIFI_GOOD_ACCURACY_M, otherwise the tighter
// of the valid ones (the Wi-Fi fix on a tie). nullptr when neither is valid.
const Fix* chooseFix(const Fix& wifi, const Fix& ip);

// How one service's request went.
enum class Reply : uint8_t {
  NotAsked,  // skipped (too few access points, or the Wi-Fi fix was good enough)
  NoMemory,  // not started: too little internal RAM for a TLS session
  NoAnswer,  // DNS, connect, TLS or a timeout: the service was never heard from
  Answered,  // the service sent a complete response (whatever it said)
};

enum class LookupFailure : uint8_t {
  Unreachable,  // neither service was heard from: the network does not reach the internet
  TooFew,       // the internet works, but too few access points for a Wi-Fi lookup
  NotFound,     // the services answered with no usable location
  NoMemory,     // a request could not start for want of RAM
};

// Why no fix came back (chooseFix gave nullptr), from the access points sent and each reply. The
// internet-address lookup runs whenever the Wi-Fi fix fails, so its reply decides: heard from ->
// too few access points (none sent) or not found; out of memory -> NoMemory; never heard from ->
// NotFound when beaconDB did answer, else Unreachable.
LookupFailure classifyFailure(size_t apsSent, Reply beacon, Reply ip);

}  // namespace geolocate
