// Locate Me's Wi-Fi half: the two beaconDB request bodies (MAC formatting, the privacy filters,
// the -85 dBm floor, the halves dealt by signal rank), the beaconDB response parser (good answers,
// errors, garbage, missing fields, out-of-range coordinates) and the two-half agreement check.
// There is no internet-address lookup to test: none exists.
//
// Fixtures are public Seattle landmarks only. BSSIDs use IANA's own OUI 00:00:5e (RFC 7042), which
// no vendor's hardware carries, so no real access point appears here: one device per number in the
// fourth octet (00:00:5e:<n>:53:00, 64 Ki apart), and the documentation block 00:00:5e:00:53:xx
// for several addresses of one device.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "network/GeolocateProtocol.h"

using namespace geolocate;

namespace {

// Any address.
AccessPoint at(const uint8_t (&mac)[6], const int rssi, const char* ssid = "CoffeeShop") {
  AccessPoint a;
  std::memcpy(a.mac, mac, 6);
  a.rssi = static_cast<int16_t>(rssi);
  std::snprintf(a.ssid, sizeof(a.ssid), "%s", ssid);
  return a;
}

// Device n: 00:00:5e:<n>:53:00 (first: the first octet, for the locally administered and group bits).
AccessPoint ap(const uint8_t n, const int rssi, const char* ssid = "CoffeeShop", const uint8_t first = 0x00) {
  const uint8_t mac[6] = {first, 0x00, 0x5e, n, 0x53, 0x00};
  return at(mac, rssi, ssid);
}

// One more address of a single device: 00:00:5e:00:53:<last> (the documentation block).
AccessPoint radio(const uint8_t last, const int rssi, const char* ssid = "TravelRouter") {
  const uint8_t mac[6] = {0x00, 0x00, 0x5e, 0x00, 0x53, last};
  return at(mac, rssi, ssid);
}

struct Bodies {
  Split split;
  std::string a;
  std::string b;
};

Bodies bodies(const std::vector<AccessPoint>& aps, const uint8_t* exclude = nullptr) {
  char a[REQUEST_CAP];
  char b[REQUEST_CAP];
  Bodies out;
  out.split = buildSplitRequests(aps.data(), aps.size(), exclude, a, b, sizeof(a));
  out.a = a;
  out.b = b;
  return out;
}

std::string entry(const char* mac, const int rssi) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "{\"macAddress\":\"%s\",\"signalStrength\":%d}", mac, rssi);
  return buf;
}

const std::string ENVELOPE =
    "{\"considerIp\":false,\"fallbacks\":{\"ipf\":false,\"lacf\":false},\"wifiAccessPoints\":[";

bool beacon(const char* json, Fix& out) { return parseBeaconDbResponse(json, std::strlen(json), out); }

// Space Needle.
constexpr const char* NEEDLE_OK = R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38.2})";

// Four usable access points, strongest first: 01 (-50), 02 (-60), 03 (-70), 04 (-80).
std::vector<AccessPoint> four() { return {ap(3, -70), ap(1, -50), ap(4, -80), ap(2, -60)}; }

}  // namespace

// ---- request bodies -------------------------------------------------------------------------------

TEST(GeolocateRequest, DealsTheHalvesByRankWithoutIpOrFallbacks) {
  const Bodies r = bodies(four());
  ASSERT_TRUE(r.split.built);
  EXPECT_EQ(r.split.usable, 4u);
  EXPECT_EQ(r.split.devices, 4u);
  EXPECT_EQ(r.split.halfA, 2u);
  EXPECT_EQ(r.split.halfB, 2u);
  // Ranks 1 and 3 in A, 2 and 4 in B, each strongest first; lowercase colon MACs.
  EXPECT_EQ(r.a, ENVELOPE + entry("00:00:5e:01:53:00", -50) + "," + entry("00:00:5e:03:53:00", -70) + "]}");
  EXPECT_EQ(r.b, ENVELOPE + entry("00:00:5e:02:53:00", -60) + "," + entry("00:00:5e:04:53:00", -80) + "]}");
  // beaconDB's own IP fallback is off twice over.
  EXPECT_NE(r.a.find("\"considerIp\":false"), std::string::npos);
  EXPECT_NE(r.a.find("\"ipf\":false"), std::string::npos);
  // An odd count: A gets the extra one.
  std::vector<AccessPoint> five = four();
  five.push_back(ap(5, -85));
  const Bodies o = bodies(five);
  ASSERT_TRUE(o.split.built);
  EXPECT_EQ(o.split.halfA, 3u);
  EXPECT_EQ(o.split.halfB, 2u);
  EXPECT_NE(o.a.find(entry("00:00:5e:05:53:00", -85)), std::string::npos);  // -85 dBm itself is in
}

TEST(GeolocateRequest, FiltersHiddenOptedOutLocalAndVrrpAddresses) {
  constexpr uint8_t VRRP4[6] = {0x00, 0x00, 0x5e, 0x00, 0x01, 0x10};
  constexpr uint8_t VRRP6[6] = {0x00, 0x00, 0x5e, 0x00, 0x02, 0x11};
  const Bodies r = bodies({ap(1, -40, ""),                // hidden
                           ap(2, -41, "Home_nomap"),      // opted out
                           ap(3, -42, "Cabin_NoMap"),     // any case
                           ap(4, -43, "Office_optout"),   // the other convention
                           ap(5, -44, "Phone", 0x02),     // locally administered
                           ap(6, -45, "Phone", 0x06),     // locally administered, another prefix
                           ap(7, -46, "Group", 0x01),     // group bit
                           at(VRRP4, -47, "Vrrp"),        // VRRP IPv4 virtual router
                           at(VRRP6, -47, "Vrrp6"),       // VRRP IPv6
                           ap(8, -60, "Library"),         // kept
                           ap(9, -70, "nomap_Cafe"),      // "nomap" not at the end: kept
                           ap(10, -75, "Museum_nomap "),  // not the suffix: kept
                           ap(11, -80, "Bakery")});       // kept
  EXPECT_EQ(r.split.usable, 4u);
  ASSERT_TRUE(r.split.built);
  const std::string both = r.a + r.b;
  for (const char* gone :
       {"00:00:5e:01:53:00", "02:00:5e", "06:00:5e", "01:00:5e", "00:00:5e:00:01:10", "00:00:5e:00:02:11"}) {
    EXPECT_EQ(both.find(gone), std::string::npos) << gone;
  }
  for (const char* kept : {"00:00:5e:08:53:00", "00:00:5e:09:53:00", "00:00:5e:0a:53:00", "00:00:5e:0b:53:00"}) {
    EXPECT_NE(both.find(kept), std::string::npos) << kept;
  }
  // VRRP's range only: the addresses next to it are ordinary ones.
  constexpr uint8_t VRRP4_ANY[6] = {0x00, 0x00, 0x5e, 0x00, 0x01, 0x01};
  constexpr uint8_t VRRP6_ANY[6] = {0x00, 0x00, 0x5e, 0x00, 0x02, 0x01};
  constexpr uint8_t NEXT[6] = {0x00, 0x00, 0x5e, 0x00, 0x03, 0x01};
  EXPECT_FALSE(usableAccessPoint(at(VRRP4_ANY, -50, "x")));
  EXPECT_FALSE(usableAccessPoint(at(VRRP6_ANY, -50, "x")));
  EXPECT_TRUE(usableAccessPoint(at(NEXT, -50, "x")));
}

TEST(GeolocateRequest, AllZeroBssidIsRefused) {
  AccessPoint zero = ap(0, -30);
  std::memset(zero.mac, 0, 6);
  EXPECT_FALSE(usableAccessPoint(zero));
  EXPECT_TRUE(usableAccessPoint(ap(1, -30)));
}

TEST(GeolocateRequest, WeakAccessPointsDoNotCount) {
  std::vector<AccessPoint> aps = {ap(1, -50), ap(2, -60), ap(3, -70), ap(4, -86), ap(5, -95)};
  const Bodies r = bodies(aps);
  EXPECT_EQ(r.split.usable, 3u);
  EXPECT_FALSE(r.split.built);
  EXPECT_EQ(r.a, "");
  EXPECT_EQ(r.b, "");
}

TEST(GeolocateRequest, TheJoinedHotspotCanBeLeftOut) {
  // The phone answered on this network: its hotspot's own BSSID travels with it.
  const uint8_t hotspot[6] = {0x00, 0x00, 0x5e, 0x01, 0x53, 0x00};
  std::vector<AccessPoint> aps = four();
  const Bodies kept = bodies(aps);
  EXPECT_NE((kept.a + kept.b).find("00:00:5e:01:53:00"), std::string::npos);
  const Bodies left = bodies(aps, hotspot);
  EXPECT_EQ(left.split.usable, 3u);
  EXPECT_FALSE(left.split.built);
  aps.push_back(ap(6, -82));
  const Bodies five = bodies(aps, hotspot);
  ASSERT_TRUE(five.split.built);
  EXPECT_EQ((five.a + five.b).find("00:00:5e:01:53:00"), std::string::npos);
  // The strongest left becomes rank 1.
  EXPECT_EQ(five.a.find(entry("00:00:5e:02:53:00", -60)), ENVELOPE.size());
}

TEST(GeolocateRequest, KeepsTheStrongestFortyInTwoHalves) {
  std::vector<AccessPoint> aps;
  for (int i = 0; i < 50; i++) aps.push_back(ap(static_cast<uint8_t>(i), -84 + (i % 40) / 2 + i / 40));
  const Bodies r = bodies(aps);
  ASSERT_TRUE(r.split.built);
  EXPECT_EQ(r.split.usable, 2 * MAX_REQUEST_APS);
  EXPECT_EQ(r.split.halfA, MAX_REQUEST_APS);
  EXPECT_EQ(r.split.halfB, MAX_REQUEST_APS);
  // Strongest first within each half.
  size_t prev = 0;
  int prevRssi = 1;
  for (size_t pos = r.a.find("signalStrength\":"); pos != std::string::npos;
       pos = r.a.find("signalStrength\":", pos + 1)) {
    const int rssi = std::atoi(r.a.c_str() + pos + 16);
    EXPECT_LE(rssi, prevRssi);
    prevRssi = rssi;
    EXPECT_GT(pos, prev);
    prev = pos;
  }
}

TEST(GeolocateRequest, ABssidSeenTwiceCountsOnceAtItsStrongest) {
  const Bodies r = bodies({ap(1, -80), ap(2, -60), ap(1, -50), ap(3, -70), ap(4, -75)});
  ASSERT_TRUE(r.split.built);
  EXPECT_EQ(r.split.usable, 4u);
  EXPECT_EQ(r.a, ENVELOPE + entry("00:00:5e:01:53:00", -50) + "," + entry("00:00:5e:03:53:00", -70) + "]}");
  // Two readings of one access point are not two access points.
  EXPECT_FALSE(bodies({ap(1, -80), ap(1, -50), ap(2, -60), ap(3, -61)}).split.built);
}

TEST(GeolocateRequest, OneDevicesAddressesAreOneDevice) {
  const uint8_t a[6] = {0x00, 0x00, 0x5e, 0x00, 0x53, 0x10};
  const uint8_t b[6] = {0x00, 0x00, 0x5e, 0x00, 0x53, 0x13};  // 2.4/5 GHz, main and guest
  const uint8_t c[6] = {0x00, 0x00, 0x5e, 0x00, 0x54, 0x0f};  // across the fifth octet, 255 on
  const uint8_t d[6] = {0x00, 0x00, 0x5e, 0x00, 0x54, 0x10};  // 256 on: another device
  const uint8_t e[6] = {0x00, 0x00, 0x5f, 0x00, 0x53, 0x10};  // another OUI
  const uint8_t f[6] = {0x00, 0x00, 0x5e, 0x01, 0x53, 0x10};  // 64 Ki on
  EXPECT_TRUE(sameDevice(a, a));
  EXPECT_TRUE(sameDevice(a, b));
  EXPECT_TRUE(sameDevice(b, a));
  EXPECT_TRUE(sameDevice(a, c));
  EXPECT_FALSE(sameDevice(a, d));
  EXPECT_FALSE(sameDevice(a, e));
  EXPECT_FALSE(sameDevice(a, f));
}

TEST(GeolocateRequest, ADevicesAddressesAllGoIntoOneHalf) {
  // A travel router that beaconDB knows where it used to be: four BSSIDs from one base address,
  // the strongest signals here. Dealt by BSSID they would answer for both halves alone.
  std::vector<AccessPoint> aps = {radio(0x10, -40), radio(0x11, -42), radio(0x12, -46), radio(0x13, -48),
                                  ap(1, -70),       ap(2, -75),       ap(3, -80)};
  const Bodies r = bodies(aps);
  ASSERT_TRUE(r.split.built);
  EXPECT_EQ(r.split.usable, 7u);
  EXPECT_EQ(r.split.devices, 4u);
  // Devices by their strongest address: the router (A), 1 (B), 2 (A), 3 (B).
  for (const char* router : {"00:00:5e:00:53:10", "00:00:5e:00:53:11", "00:00:5e:00:53:12", "00:00:5e:00:53:13"}) {
    EXPECT_NE(r.a.find(router), std::string::npos) << router;
    EXPECT_EQ(r.b.find(router), std::string::npos) << router;
  }
  EXPECT_EQ(r.split.halfA, 5u);
  EXPECT_EQ(r.split.halfB, 2u);
  EXPECT_EQ(r.b, ENVELOPE + entry("00:00:5e:01:53:00", -70) + "," + entry("00:00:5e:03:53:00", -80) + "]}");
  // With only two other devices there are three in all: too few for two independent halves.
  aps.pop_back();
  const Bodies few = bodies(aps);
  EXPECT_EQ(few.split.usable, 6u);
  EXPECT_EQ(few.split.devices, 3u);
  EXPECT_FALSE(few.split.built);
  EXPECT_EQ(few.a, "");
  // Ten BSSIDs of one device are still one device.
  std::vector<AccessPoint> one;
  for (uint8_t i = 0; i < 10; i++) one.push_back(radio(static_cast<uint8_t>(0x20 + i), -50 - i));
  EXPECT_EQ(bodies(one).split.devices, 1u);
  EXPECT_FALSE(bodies(one).split.built);
}

TEST(GeolocateRequest, AWholeDeviceIsLeftOut) {
  // The joined hotspot's other band is its own device too.
  const uint8_t hotspot[6] = {0x00, 0x00, 0x5e, 0x00, 0x53, 0x10};
  std::vector<AccessPoint> aps = {radio(0x10, -35), radio(0x14, -38), ap(1, -60), ap(2, -65), ap(3, -70), ap(4, -75)};
  const Bodies r = bodies(aps, hotspot);
  ASSERT_TRUE(r.split.built);
  EXPECT_EQ(r.split.usable, 4u);
  EXPECT_EQ(r.split.devices, 4u);
  EXPECT_EQ((r.a + r.b).find("00:00:5e:00:53:1"), std::string::npos);
}

TEST(GeolocateRequest, FewerThanFourUsableGivesNothing) {
  EXPECT_FALSE(bodies({}).split.built);
  EXPECT_FALSE(bodies({ap(1, -50), ap(2, -60), ap(3, -70)}).split.built);
  EXPECT_FALSE(bodies({ap(1, -50), ap(2, -60), ap(3, -70), ap(4, -60, "")}).split.built);
  char a[8] = "x";
  char b[8] = "y";
  EXPECT_FALSE(buildSplitRequests(nullptr, 3, nullptr, a, b, sizeof(a)).built);
  EXPECT_STREQ(a, "");
  EXPECT_STREQ(b, "");
}

TEST(GeolocateRequest, ClampsAPositiveSignalStrength) {
  const Bodies r = bodies({ap(1, 5), ap(2, -60), ap(3, -70), ap(4, -80)});
  EXPECT_NE(r.a.find(entry("00:00:5e:01:53:00", 0)), std::string::npos);
}

TEST(GeolocateRequest, TwentyLongestEntriesFitTheBuffer) {
  std::vector<AccessPoint> aps;
  for (int i = 0; i < 45; i++) aps.push_back(ap(static_cast<uint8_t>(0xa0 + i), -85));
  const Bodies r = bodies(aps);
  ASSERT_TRUE(r.split.built);
  EXPECT_EQ(r.split.halfA, MAX_REQUEST_APS);
  EXPECT_LT(r.a.size(), REQUEST_CAP);
  EXPECT_EQ(r.a.substr(r.a.size() - 2), "]}");
}

TEST(GeolocateRequest, ABufferTooSmallGivesNothing) {
  const std::vector<AccessPoint> aps = four();
  char small[64];
  char small2[64];
  EXPECT_FALSE(buildSplitRequests(aps.data(), aps.size(), nullptr, small, small2, sizeof(small)).built);
  EXPECT_STREQ(small, "");
  EXPECT_STREQ(small2, "");
  const Bodies full = bodies(aps);
  const size_t longest = std::max(full.a.size(), full.b.size());
  char a[256];
  char b[256];
  ASSERT_LT(longest + 1, sizeof(a));
  EXPECT_FALSE(buildSplitRequests(aps.data(), aps.size(), nullptr, a, b, longest).built);  // one byte short
  EXPECT_STREQ(a, "");
  EXPECT_TRUE(buildSplitRequests(aps.data(), aps.size(), nullptr, a, b, longest + 1).built);
  EXPECT_EQ(std::string(a), full.a);
  EXPECT_EQ(std::string(b), full.b);
}

// ---- beaconDB ---------------------------------------------------------------------------------

TEST(GeolocateBeacon, ReadsAGoodAnswer) {
  Fix fix;
  ASSERT_TRUE(beacon(NEEDLE_OK, fix));
  EXPECT_EQ(fix.source, FixSource::Wifi);
  EXPECT_DOUBLE_EQ(fix.lat, 47.6205);
  EXPECT_DOUBLE_EQ(fix.lon, -122.3493);
  EXPECT_EQ(fix.accuracyM, 39u);  // rounded up: never claim better than the service did
}

TEST(GeolocateBeacon, ToleratesOrderWhitespaceAndExponents) {
  Fix fix;
  ASSERT_TRUE(
      beacon("\n{ \"accuracy\" : 1.2e2 ,\r\n \"location\" : { \"lng\" : -122.3422, \"lat\" : 4.76097e1 } }\n", fix));
  EXPECT_DOUBLE_EQ(fix.lat, 47.6097);
  EXPECT_DOUBLE_EQ(fix.lon, -122.3422);
  EXPECT_EQ(fix.accuracyM, 120u);
}

TEST(GeolocateBeacon, RefusesTheNotFoundError) {
  Fix fix;
  EXPECT_FALSE(beacon(R"({"error":{"code":404,"errors":[{"domain":"geolocation","message":"No location could be )"
                      R"(estimated based on the data provided","reason":"notFound"}],"message":"Not found"}})",
                      fix));
  EXPECT_FALSE(fix.valid());
}

TEST(GeolocateBeacon, RefusesMissingFields) {
  Fix fix;
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493}})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lng":-122.3493},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"lat":47.6205,"lng":-122.3493,"accuracy":30})", fix));  // not under "location"
  EXPECT_FALSE(beacon(R"({"x":{"location":{"lat":47.6205,"lng":-122.3493}},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":"47.6205","lng":"-122.3493"},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({})", fix));
}

TEST(GeolocateBeacon, RefusesOutOfRangeCoordinatesAndAccuracy) {
  Fix fix;
  EXPECT_FALSE(beacon(R"({"location":{"lat":90.5,"lng":-122.3},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":-91,"lng":-122.3},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6,"lng":180.01},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6,"lng":-181},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":0,"lng":0},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6,"lng":-122.3},"accuracy":0})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6,"lng":-122.3},"accuracy":-5})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":1e999,"lng":-122.3},"accuracy":30})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6,"lng":-122.3},"accuracy":1e999})", fix));
  EXPECT_TRUE(beacon(R"({"location":{"lat":90,"lng":-180},"accuracy":30})", fix));  // the edges are places
}

TEST(GeolocateBeacon, RefusesAFallbackEstimate) {
  Fix fix;
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":25000,"fallback":"ipf"})", fix));
  EXPECT_FALSE(beacon(R"({"fallback":"lacf","location":{"lat":47.6205,"lng":-122.3493},"accuracy":3000})", fix));
}

TEST(GeolocateBeacon, RefusesGarbageAndCutOffBodies) {
  Fix fix;
  EXPECT_FALSE(beacon("", fix));
  EXPECT_FALSE(parseBeaconDbResponse(nullptr, 10, fix));
  EXPECT_FALSE(beacon("hello", fix));
  EXPECT_FALSE(beacon("<html><body>502 Bad Gateway</body></html>", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38)", fix));     // number never ends
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38,)", fix));    // root never closes
  EXPECT_FALSE(beacon(R"([{"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38}])", fix));  // not an object
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38}{})", fix));  // two roots
  EXPECT_FALSE(beacon(R"({"location":{"lat":NaN,"lng":-122.3493},"accuracy":38})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":tru,"lng":-122.3493},"accuracy":38})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":-,"lng":-122.3493},"accuracy":38})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.,"lng":-122.3493},"accuracy":38})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":047.6,"lng":-122.3493},"accuracy":38})", fix));
  EXPECT_FALSE(fix.valid());
}

TEST(GeolocateBeacon, ALongKeyIsNotMistakenForAShortOne) {
  Fix fix;
  EXPECT_FALSE(beacon(R"({"location":{"latitude_in_degrees":47.6,"lng":-122.3},"accuracy":30})", fix));
  EXPECT_TRUE(beacon(R"({"location":{"latitude_in_degrees":1,"lat":47.6,"lng":-122.3},"accuracy":30})", fix));
}

TEST(GeolocateBeacon, AKeyTooLongForTheTokenizerDoesNotLendItsValueToTheLastKey) {
  // The tokenizer drops a key of 511+ bytes silently; its value must not become "accuracy"'s.
  const std::string longKey(600, 'k');
  Fix fix;
  const std::string json = R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38,")" + longKey + R"(":99999})";
  EXPECT_FALSE(beacon(json.c_str(), fix));
  const std::string inLocation = R"({"location":{"lat":47.6205,"lng":-122.3493,")" + longKey + R"(":1},"accuracy":38})";
  EXPECT_FALSE(beacon(inLocation.c_str(), fix));
  EXPECT_FALSE(fix.valid());
}

TEST(GeolocateBeacon, RefusesAKeyWithoutAValueAndTextAroundTheObject) {
  Fix fix;
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38,"extra":})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493,"x"},"accuracy":38})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38}garbage)", fix));
  EXPECT_FALSE(beacon(R"(garbage{"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38})", fix));
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38}})", fix));
  // Whitespace around the object is fine.
  EXPECT_TRUE(beacon(" \r\n\t{\"location\":{\"lat\":47.6205,\"lng\":-122.3493},\"accuracy\":38}\n ", fix));
}

// ---- the two halves ------------------------------------------------------------------------------

namespace {

constexpr double M_PER_DEG_LAT = 111195.0;

Split built4() {
  Split s;
  s.usable = 4;
  s.devices = 4;
  s.halfA = 2;
  s.halfB = 2;
  s.built = true;
  return s;
}

HalfAnswer answer(const double lat, const double lon, const uint32_t accuracy, const int status = 200) {
  HalfAnswer h;
  h.reply = Reply::Answered;
  h.status = status;
  h.fix.source = FixSource::Wifi;
  h.fix.lat = lat;
  h.fix.lon = lon;
  h.fix.accuracyM = accuracy;
  return h;
}

HalfAnswer reply(const Reply r) {
  HalfAnswer h;
  h.reply = r;
  return h;
}

}  // namespace

TEST(GeolocateHalves, TwoAgreeingHalvesGiveTheMidpoint) {
  // 60 m apart north-south, each within 80 m.
  const double north = 47.6205 + 60.0 / M_PER_DEG_LAT;
  Fix out;
  uint32_t apart = 0;
  ASSERT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 80), answer(north, -122.3493, 70), out, apart),
            WifiVerdict::Located);
  EXPECT_EQ(out.source, FixSource::Wifi);
  EXPECT_NEAR(out.lat, (47.6205 + north) / 2, 1e-9);
  EXPECT_NEAR(out.lon, -122.3493, 1e-9);
  EXPECT_NEAR(apart, 60u, 1u);
  EXPECT_EQ(out.accuracyM, 80u);  // the larger accuracy
  // Never claimed tighter than 50 m.
  ASSERT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 10), answer(47.6205, -122.3493, 12), out, apart),
            WifiVerdict::Located);
  EXPECT_EQ(apart, 0u);
  EXPECT_EQ(out.accuracyM, MIN_REPORTED_ACCURACY_M);
  // The distance counts when it is the largest: 95 m apart, accuracies 100 and 30.
  const double north95 = 47.6205 + 95.0 / M_PER_DEG_LAT;
  ASSERT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 30), answer(north95, -122.3493, 100), out, apart),
            WifiVerdict::Located);
  EXPECT_EQ(out.accuracyM, 100u);
}

TEST(GeolocateHalves, AMovedAccessPointMakesThemDisagree) {
  // One half dragged 6.5 km by an access point that moved: refused, though each half reads 50 m.
  const double dragged = 47.6205 + 6500.0 / M_PER_DEG_LAT;
  Fix out;
  uint32_t apart = 0;
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 50), answer(dragged, -122.3493, 50), out, apart),
            WifiVerdict::Disagree);
  EXPECT_FALSE(out.valid());
  EXPECT_NEAR(apart, 6500u, 2u);
  // Just past the larger accuracy.
  const double past = 47.6205 + 81.0 / M_PER_DEG_LAT;
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 80), answer(past, -122.3493, 40), out, apart),
            WifiVerdict::Disagree);
  const double within = 47.6205 + 79.0 / M_PER_DEG_LAT;
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 80), answer(within, -122.3493, 40), out, apart),
            WifiVerdict::Located);
}

TEST(GeolocateHalves, EachHalfMustBeTight) {
  Fix out;
  uint32_t apart = 0;
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 101), answer(47.6205, -122.3493, 40), out, apart),
            WifiVerdict::Vague);
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 40), answer(47.6205, -122.3493, 5000), out, apart),
            WifiVerdict::Vague);
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 100), answer(47.6205, -122.3493, 100), out, apart),
            WifiVerdict::Located);
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 0), answer(47.6205, -122.3493, 40), out, apart),
            WifiVerdict::NotFound);
}

TEST(GeolocateHalves, BothMustAnswer200WithALocation) {
  Fix out;
  uint32_t apart = 0;
  const HalfAnswer good = answer(47.6205, -122.3493, 40);
  EXPECT_EQ(judgeWifi(built4(), good, answer(47.6205, -122.3493, 40, 404), out, apart), WifiVerdict::NotFound);
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 40, 302), good, out, apart), WifiVerdict::NotFound);
  // Answered, but nothing usable in the body (the 404 error, a fallback estimate).
  HalfAnswer empty = reply(Reply::Answered);
  empty.status = 200;
  EXPECT_EQ(judgeWifi(built4(), good, empty, out, apart), WifiVerdict::NotFound);
  // The body parser refuses a fallback, so a fallback answer never reaches the check as a fix.
  Fix fallback;
  EXPECT_FALSE(beacon(R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":50,"fallback":"ipf"})", fallback));
  EXPECT_EQ(judgeWifi(built4(), answer(0.0, 0.0, 40), good, out, apart), WifiVerdict::NotFound);
  EXPECT_FALSE(out.valid());
}

TEST(GeolocateHalves, NetworkFailuresAndTooFew) {
  Fix out;
  uint32_t apart = 0;
  const HalfAnswer good = answer(47.6205, -122.3493, 40);
  EXPECT_EQ(judgeWifi(built4(), reply(Reply::NoAnswer), reply(Reply::NoAnswer), out, apart), WifiVerdict::Unreachable);
  EXPECT_EQ(judgeWifi(built4(), good, reply(Reply::NoAnswer), out, apart), WifiVerdict::Unreachable);
  EXPECT_EQ(judgeWifi(built4(), good, reply(Reply::NotAsked), out, apart), WifiVerdict::Unreachable);
  EXPECT_EQ(judgeWifi(built4(), reply(Reply::NoMemory), good, out, apart), WifiVerdict::NoMemory);
  EXPECT_EQ(judgeWifi(built4(), good, reply(Reply::NoMemory), out, apart), WifiVerdict::NoMemory);
  Split few;
  few.usable = 3;
  few.devices = 3;
  EXPECT_EQ(judgeWifi(few, good, good, out, apart), WifiVerdict::TooFew);
  // Four addresses, three devices.
  Split shared = built4();
  shared.devices = 3;
  EXPECT_EQ(judgeWifi(shared, good, good, out, apart), WifiVerdict::TooFew);
  EXPECT_EQ(judgeWifi(Split{}, good, good, out, apart), WifiVerdict::TooFew);
}

TEST(GeolocateHalves, AFailedFirstHalfSparesTheSecondRequest) {
  EXPECT_TRUE(secondHalfNeeded(answer(47.6205, -122.3493, 40)));
  EXPECT_TRUE(secondHalfNeeded(answer(47.6205, -122.3493, 100)));
  EXPECT_FALSE(secondHalfNeeded(answer(47.6205, -122.3493, 101)));
  EXPECT_FALSE(secondHalfNeeded(answer(47.6205, -122.3493, 40, 404)));
  EXPECT_FALSE(secondHalfNeeded(reply(Reply::NoAnswer)));
  EXPECT_FALSE(secondHalfNeeded(reply(Reply::NoMemory)));
  EXPECT_FALSE(secondHalfNeeded(reply(Reply::NotAsked)));
  // The verdict is then the first half's, whatever the (unasked) second says.
  Fix out;
  uint32_t apart = 0;
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 40, 404), reply(Reply::NotAsked), out, apart),
            WifiVerdict::NotFound);
  EXPECT_EQ(judgeWifi(built4(), answer(47.6205, -122.3493, 400), reply(Reply::NotAsked), out, apart),
            WifiVerdict::Vague);
  EXPECT_EQ(judgeWifi(built4(), reply(Reply::NoAnswer), reply(Reply::NotAsked), out, apart), WifiVerdict::Unreachable);
}

TEST(GeolocateHalves, TheMidpointAcrossTheDateLine) {
  // Two answers 60 m apart either side of 180 degrees, near Taveuni (Fiji).
  const double lat = -16.8;
  const double dLon = 30.0 / (M_PER_DEG_LAT * std::cos(lat * 3.141592653589793 / 180.0));
  Fix out;
  uint32_t apart = 0;
  ASSERT_EQ(judgeWifi(built4(), answer(lat, 180.0 - dLon, 70), answer(lat, -180.0 + dLon, 70), out, apart),
            WifiVerdict::Located);
  EXPECT_NEAR(apart, 60u, 1u);
  EXPECT_NEAR(std::fabs(out.lon), 180.0, 1e-6);
}

TEST(GeolocateHalves, DistanceAndNames) {
  // Space Needle to Pike Place Market: about 1.25 km.
  EXPECT_NEAR(distanceM(47.6205, -122.3493, 47.6097, -122.3422), 1300.0, 60.0);
  EXPECT_DOUBLE_EQ(distanceM(47.6205, -122.3493, 47.6205, -122.3493), 0.0);
  for (const WifiVerdict v :
       {WifiVerdict::NotAsked, WifiVerdict::Located, WifiVerdict::TooFew, WifiVerdict::NoMemory,
        WifiVerdict::Unreachable, WifiVerdict::NotFound, WifiVerdict::Vague, WifiVerdict::Disagree}) {
    const std::string name = wifiVerdictName(v);
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(name.find(' '), std::string::npos);
  }
}
