// Locate Me's pure half: the beaconDB request body (MAC formatting, the privacy filters, the
// strongest 20), the beaconDB and ipwho.is response parsers (good answers, errors, garbage,
// missing fields, out-of-range coordinates) and the rule that picks the fix to offer.
//
// Fixtures are public Seattle landmarks only; BSSIDs come from the IANA documentation block
// 00:00:5e:00:53:xx (RFC 7042), so no real access point appears here.
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "network/GeolocateProtocol.h"

using namespace geolocate;

namespace {

AccessPoint ap(const uint8_t last, const int rssi, const char* ssid = "CoffeeShop", const uint8_t first = 0x00) {
  AccessPoint a;
  const uint8_t mac[6] = {first, 0x00, 0x5e, 0x00, 0x53, last};
  std::memcpy(a.mac, mac, 6);
  a.rssi = static_cast<int16_t>(rssi);
  std::snprintf(a.ssid, sizeof(a.ssid), "%s", ssid);
  return a;
}

std::string body(const std::vector<AccessPoint>& aps, size_t* count = nullptr) {
  char out[REQUEST_CAP];
  const size_t n = buildRequestBody(aps.data(), aps.size(), out, sizeof(out));
  if (count) *count = n;
  return out;
}

std::string entry(const char* mac, const int rssi) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "{\"macAddress\":\"%s\",\"signalStrength\":%d}", mac, rssi);
  return buf;
}

bool beacon(const char* json, Fix& out) { return parseBeaconDbResponse(json, std::strlen(json), out); }
bool ipwho(const char* json, Fix& out) { return parseIpWhoisResponse(json, std::strlen(json), out); }

// Space Needle.
constexpr const char* NEEDLE_OK = R"({"location":{"lat":47.6205,"lng":-122.3493},"accuracy":38.2})";

}  // namespace

// ---- request body -----------------------------------------------------------------------------

TEST(GeolocateRequest, FormatsLowercaseMacsStrongestFirst) {
  size_t n = 0;
  const std::string json = body({ap(0x0a, -71), ap(0xbc, -48)}, &n);
  EXPECT_EQ(n, 2u);
  EXPECT_EQ(json, std::string("{\"considerIp\":false,\"wifiAccessPoints\":[") + entry("00:00:5e:00:53:bc", -48) + "," +
                      entry("00:00:5e:00:53:0a", -71) + "]}");
}

TEST(GeolocateRequest, FiltersHiddenOptedOutAndLocalAddresses) {
  size_t n = 0;
  const std::string json = body({ap(1, -40, ""),                 // hidden
                                 ap(2, -41, "Home_nomap"),       // opted out
                                 ap(3, -42, "Cabin_NoMap"),      // any case
                                 ap(4, -43, "Office_optout"),    // the other convention
                                 ap(5, -44, "Phone", 0x02),      // locally administered
                                 ap(6, -45, "Phone", 0x06),      // locally administered, another prefix
                                 ap(7, -46, "Group", 0x01),      // group bit
                                 ap(8, -70, "Library"),          // kept
                                 ap(9, -80, "nomap_Cafe"),       // "nomap" not at the end: kept
                                 ap(10, -90, "Museum_nomap ")},  // not the suffix: kept
                                &n);
  EXPECT_EQ(n, 3u);
  EXPECT_EQ(json.find("00:00:5e:00:53:01"), std::string::npos);
  EXPECT_EQ(json.find("02:00:5e"), std::string::npos);
  EXPECT_EQ(json.find("06:00:5e"), std::string::npos);
  EXPECT_EQ(json.find("01:00:5e"), std::string::npos);
  EXPECT_NE(json.find(entry("00:00:5e:00:53:08", -70)), std::string::npos);
  EXPECT_NE(json.find(entry("00:00:5e:00:53:09", -80)), std::string::npos);
  EXPECT_NE(json.find(entry("00:00:5e:00:53:0a", -90)), std::string::npos);
}

TEST(GeolocateRequest, AllZeroBssidIsRefused) {
  AccessPoint zero = ap(0, -30);
  std::memset(zero.mac, 0, 6);
  EXPECT_FALSE(usableAccessPoint(zero));
  EXPECT_TRUE(usableAccessPoint(ap(1, -30)));
}

TEST(GeolocateRequest, KeepsTheStrongestTwentyInOrder) {
  std::vector<AccessPoint> aps;
  for (int i = 0; i < 30; i++) aps.push_back(ap(static_cast<uint8_t>(i), -90 + i));  // i = 29 strongest
  size_t n = 0;
  const std::string json = body(aps, &n);
  EXPECT_EQ(n, MAX_REQUEST_APS);
  size_t pos = 0;
  for (int i = 29; i >= 10; i--) {
    char mac[24];
    std::snprintf(mac, sizeof(mac), "00:00:5e:00:53:%02x", i);
    const size_t at = json.find(entry(mac, -90 + i));
    ASSERT_NE(at, std::string::npos) << mac;
    EXPECT_GT(at, pos);
    pos = at;
  }
  for (int i = 0; i < 10; i++) {
    char mac[24];
    std::snprintf(mac, sizeof(mac), "00:00:5e:00:53:%02x", i);
    EXPECT_EQ(json.find(mac), std::string::npos) << mac;
  }
}

TEST(GeolocateRequest, ABssidSeenTwiceCountsOnceAtItsStrongest) {
  size_t n = 0;
  const std::string json = body({ap(1, -80), ap(2, -60), ap(1, -50)}, &n);
  EXPECT_EQ(n, 2u);
  EXPECT_EQ(json, std::string("{\"considerIp\":false,\"wifiAccessPoints\":[") + entry("00:00:5e:00:53:01", -50) + "," +
                      entry("00:00:5e:00:53:02", -60) + "]}");
  // Two readings of one access point are not two access points.
  EXPECT_EQ(body({ap(1, -80), ap(1, -50)}, &n), "");
  EXPECT_EQ(n, 0u);
}

TEST(GeolocateRequest, FewerThanTwoUsableGivesNothing) {
  size_t n = 7;
  EXPECT_EQ(body({}, &n), "");
  EXPECT_EQ(n, 0u);
  EXPECT_EQ(body({ap(1, -50)}, &n), "");
  EXPECT_EQ(body({ap(1, -50), ap(2, -60, "")}, &n), "");
  EXPECT_EQ(n, 0u);
  char out[8] = "x";
  EXPECT_EQ(buildRequestBody(nullptr, 3, out, sizeof(out)), 0u);
  EXPECT_STREQ(out, "");
}

TEST(GeolocateRequest, ClampsSignalStrength) {
  const std::string json = body({ap(1, 5), ap(2, -300)});
  EXPECT_NE(json.find(entry("00:00:5e:00:53:01", 0)), std::string::npos);
  EXPECT_NE(json.find(entry("00:00:5e:00:53:02", -127)), std::string::npos);
}

TEST(GeolocateRequest, TwentyLongestEntriesFitTheBuffer) {
  std::vector<AccessPoint> aps;
  for (int i = 0; i < 25; i++) aps.push_back(ap(static_cast<uint8_t>(0xa0 + i), -127));
  size_t n = 0;
  const std::string json = body(aps, &n);
  EXPECT_EQ(n, MAX_REQUEST_APS);
  EXPECT_LT(json.size(), REQUEST_CAP);
  EXPECT_EQ(json.substr(json.size() - 2), "]}");
}

TEST(GeolocateRequest, ABufferTooSmallGivesNothing) {
  const std::vector<AccessPoint> aps = {ap(1, -50), ap(2, -60)};
  char small[64];
  EXPECT_EQ(buildRequestBody(aps.data(), aps.size(), small, sizeof(small)), 0u);
  EXPECT_STREQ(small, "");
  char exact[160];
  const std::string full = body(aps);
  ASSERT_LT(full.size() + 1, sizeof(exact));
  EXPECT_EQ(buildRequestBody(aps.data(), aps.size(), exact, full.size()), 0u);  // one byte short
  EXPECT_EQ(buildRequestBody(aps.data(), aps.size(), exact, full.size() + 1), 2u);
  EXPECT_EQ(std::string(exact), full);
}

// ---- beaconDB ---------------------------------------------------------------------------------

TEST(GeolocateBeacon, ReadsAGoodAnswer) {
  Fix fix;
  ASSERT_TRUE(beacon(NEEDLE_OK, fix));
  EXPECT_EQ(fix.source, FixSource::Wifi);
  EXPECT_DOUBLE_EQ(fix.lat, 47.6205);
  EXPECT_DOUBLE_EQ(fix.lon, -122.3493);
  EXPECT_EQ(fix.accuracyM, 39u);  // rounded up: never claim better than the service did
  EXPECT_STREQ(fix.place, "");
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

// ---- ipwho.is ---------------------------------------------------------------------------------

TEST(GeolocateIp, ReadsAGoodAnswer) {
  Fix fix;
  ASSERT_TRUE(ipwho(
      R"({"success":true,"city":"Seattle","region":"Washington","latitude":47.6062,"longitude":-122.3321})", fix));
  EXPECT_EQ(fix.source, FixSource::Ip);
  EXPECT_DOUBLE_EQ(fix.lat, 47.6062);
  EXPECT_DOUBLE_EQ(fix.lon, -122.3321);
  EXPECT_EQ(fix.accuracyM, IP_ACCURACY_M);
  EXPECT_STREQ(fix.place, "Seattle, Washington");
}

TEST(GeolocateIp, ReadsTheFullDefaultAnswerToo) {
  Fix fix;
  ASSERT_TRUE(ipwho(R"({"ip":"192.0.2.1","success":true,"type":"IPv4","continent":"North America",)"
                    R"("country":"United States","region":"Washington","city":"Seattle","latitude":47.6062,)"
                    R"("longitude":-122.3321,"is_eu":false,"flag":{"img":"https:\/\/cdn.example\/us.svg",)"
                    R"("emoji":"x"},"connection":{"asn":64496,"org":"Example"},"timezone":{"id":)"
                    R"("America\/Los_Angeles","offset":-25200,"utc":"-07:00"}})",
                    fix));
  EXPECT_STREQ(fix.place, "Seattle, Washington");
}

TEST(GeolocateIp, PlaceFromWhateverIsThere) {
  Fix fix;
  ASSERT_TRUE(ipwho(R"({"success":true,"city":"Seattle","latitude":47.6062,"longitude":-122.3321})", fix));
  EXPECT_STREQ(fix.place, "Seattle");
  ASSERT_TRUE(ipwho(R"({"success":true,"region":"Washington","latitude":47.6062,"longitude":-122.3321})", fix));
  EXPECT_STREQ(fix.place, "Washington");
  ASSERT_TRUE(ipwho(R"({"success":true,"city":"","region":"","latitude":47.6062,"longitude":-122.3321})", fix));
  EXPECT_STREQ(fix.place, "");
  ASSERT_TRUE(
      ipwho(R"({"success":true,"city":"Singapore","region":"Singapore","latitude":1.29,"longitude":103.85})", fix));
  EXPECT_STREQ(fix.place, "Singapore");
}

TEST(GeolocateIp, CleansThePlaceName) {
  Fix fix;
  ASSERT_TRUE(ipwho(
      R"({"success":true,"city":"Montr\u00e9al","region":"Qu\u00E9bec","latitude":45.5,"longitude":-73.57})", fix));
  EXPECT_STREQ(fix.place,
               "Montr\xC3\xA9"
               "al, Qu\xC3\xA9"
               "bec");
  ASSERT_TRUE(ipwho("{\"success\":true,\"city\":\"  \\tSeattle\\n\",\"latitude\":47.6,\"longitude\":-122.3}", fix));
  EXPECT_STREQ(fix.place, "Seattle");
  ASSERT_TRUE(ipwho(R"({"success":true,"city":"\ud83d\ude00","latitude":47.6,"longitude":-122.3})", fix));
  EXPECT_STREQ(fix.place, "??");  // surrogates are not decoded into broken UTF-8
}

TEST(GeolocateIp, ALongPlaceIsCutAtAWholeCharacter) {
  std::string city;
  for (int i = 0; i < 40; i++) city += "\xC3\xA9";  // 80 bytes of 'é'
  Fix fix;
  const std::string json = R"({"success":true,"city":")" + city + R"(","latitude":47.6,"longitude":-122.3})";
  ASSERT_TRUE(ipwho(json.c_str(), fix));
  const size_t len = std::strlen(fix.place);
  EXPECT_LT(len, PLACE_CAP);
  EXPECT_EQ(len % 2, 0u);
  // "Kent, " + 31 two-byte characters overflows the place by an odd count: the half character goes.
  const std::string both =
      R"({"success":true,"city":"Kent","region":")" + city + R"(","latitude":47.38,"longitude":-122.23})";
  ASSERT_TRUE(ipwho(both.c_str(), fix));
  const std::string place = fix.place;
  EXPECT_EQ(place.rfind("Kent, ", 0), 0u);
  EXPECT_EQ(place.size(), PLACE_CAP - 2);
  EXPECT_EQ((place.size() - 6) % 2, 0u);
}

TEST(GeolocateIp, AValueTooLongForTheTokenizerRefusesTheAnswer) {
  // A 600-byte city is dropped by the tokenizer, leaving "city" without a value.
  const std::string longCity(600, 'x');
  Fix fix;
  const std::string json = R"({"success":true,"city":")" + longCity + R"(","latitude":47.6062,"longitude":-122.3321})";
  EXPECT_FALSE(ipwho(json.c_str(), fix));
  const std::string longKey(600, 'k');
  const std::string key = R"({"success":true,"latitude":47.6,"longitude":-122.3,")" + longKey + R"(":1,"city":"X"})";
  EXPECT_FALSE(ipwho(key.c_str(), fix));
}

TEST(GeolocateIp, RefusesFailuresAndBadCoordinates) {
  Fix fix;
  EXPECT_FALSE(ipwho(R"({"success":false,"message":"Reserved range"})", fix));
  EXPECT_FALSE(ipwho(R"({"success":false,"latitude":47.6,"longitude":-122.3})", fix));
  EXPECT_FALSE(ipwho(R"({"latitude":47.6,"longitude":-122.3})", fix));  // success is required
  EXPECT_FALSE(ipwho(R"({"success":"true","latitude":47.6,"longitude":-122.3})", fix));
  EXPECT_FALSE(ipwho(R"({"success":true,"latitude":47.6})", fix));
  EXPECT_FALSE(ipwho(R"({"success":true,"latitude":95,"longitude":-122.3})", fix));
  EXPECT_FALSE(ipwho(R"({"success":true,"latitude":47.6,"longitude":200})", fix));
  EXPECT_FALSE(ipwho(R"({"success":true,"latitude":0,"longitude":0})", fix));
  EXPECT_FALSE(ipwho(R"({"success":true,"latitude":null,"longitude":-122.3})", fix));
  EXPECT_FALSE(ipwho(R"({"success":true,"connection":{"latitude":47.6,"longitude":-122.3}})", fix));
  EXPECT_FALSE(ipwho(R"({"success":true,"latitude":47.6,"longitude":-122.3)", fix));
  EXPECT_FALSE(ipwho("Too many requests", fix));
  EXPECT_FALSE(ipwho("", fix));
  EXPECT_FALSE(fix.valid());
}

// ---- the choice -------------------------------------------------------------------------------

namespace {
Fix made(const FixSource source, const uint32_t accuracy) {
  Fix f;
  f.source = source;
  f.lat = 47.6205;
  f.lon = -122.3493;
  f.accuracyM = accuracy;
  return f;
}
}  // namespace

TEST(GeolocateChoice, AGoodWifiFixWinsWithoutAskingTheInternetAddress) {
  const Fix wifi = made(FixSource::Wifi, 80);
  EXPECT_FALSE(needsIpLookup(wifi));
  EXPECT_EQ(chooseFix(wifi, Fix{}), &wifi);
  const Fix edge = made(FixSource::Wifi, WIFI_GOOD_ACCURACY_M);
  EXPECT_FALSE(needsIpLookup(edge));
  const Fix ip = made(FixSource::Ip, IP_ACCURACY_M);
  EXPECT_EQ(chooseFix(edge, ip), &edge);
}

TEST(GeolocateChoice, AVagueWifiFixAsksTheInternetAddress) {
  const Fix vague = made(FixSource::Wifi, WIFI_GOOD_ACCURACY_M + 1);
  EXPECT_TRUE(needsIpLookup(vague));
  EXPECT_TRUE(needsIpLookup(Fix{}));
  const Fix ip = made(FixSource::Ip, IP_ACCURACY_M);
  // Still tighter than city level: keep it.
  EXPECT_EQ(chooseFix(vague, ip), &vague);
  const Fix vaguer = made(FixSource::Wifi, 40000);
  EXPECT_EQ(chooseFix(vaguer, ip), &ip);
  const Fix tie = made(FixSource::Wifi, IP_ACCURACY_M);
  EXPECT_EQ(chooseFix(tie, ip), &tie);
  // The internet address failed too: a vague Wi-Fi fix beats none.
  EXPECT_EQ(chooseFix(vaguer, Fix{}), &vaguer);
}

TEST(GeolocateChoice, InternetAddressAloneAndNothing) {
  const Fix ip = made(FixSource::Ip, IP_ACCURACY_M);
  EXPECT_EQ(chooseFix(Fix{}, ip), &ip);
  EXPECT_EQ(chooseFix(Fix{}, Fix{}), nullptr);
}

// ---- why nothing came back --------------------------------------------------------------------

TEST(GeolocateFailure, TheInternetAddressLookupDecides) {
  // It answered: the internet works, so the Wi-Fi count (or the services' knowledge) is the issue.
  EXPECT_EQ(classifyFailure(0, Reply::NotAsked, Reply::Answered), LookupFailure::TooFew);
  EXPECT_EQ(classifyFailure(1, Reply::NotAsked, Reply::Answered), LookupFailure::TooFew);
  EXPECT_EQ(classifyFailure(5, Reply::Answered, Reply::Answered), LookupFailure::NotFound);
  EXPECT_EQ(classifyFailure(5, Reply::NoAnswer, Reply::Answered), LookupFailure::NotFound);
}

TEST(GeolocateFailure, ANetworkWithoutInternetIsUnreachableNotTooFew) {
  // One router whose uplink is down: too few access points AND the address lookup never got
  // through. The fix the user needs is the internet, not more Wi-Fi.
  EXPECT_EQ(classifyFailure(1, Reply::NotAsked, Reply::NoAnswer), LookupFailure::Unreachable);
  EXPECT_EQ(classifyFailure(0, Reply::NotAsked, Reply::NoAnswer), LookupFailure::Unreachable);
  EXPECT_EQ(classifyFailure(8, Reply::NoAnswer, Reply::NoAnswer), LookupFailure::Unreachable);
}

TEST(GeolocateFailure, BeaconDbHeardFromMeansTheInternetWorks) {
  EXPECT_EQ(classifyFailure(8, Reply::Answered, Reply::NoAnswer), LookupFailure::NotFound);
}

TEST(GeolocateFailure, OutOfMemoryIsItsOwnFailure) {
  EXPECT_EQ(classifyFailure(8, Reply::NoMemory, Reply::NoMemory), LookupFailure::NoMemory);
  EXPECT_EQ(classifyFailure(1, Reply::NotAsked, Reply::NoMemory), LookupFailure::NoMemory);
  EXPECT_EQ(classifyFailure(8, Reply::NoMemory, Reply::NoAnswer), LookupFailure::NoMemory);
  EXPECT_EQ(classifyFailure(8, Reply::Answered, Reply::NoMemory), LookupFailure::NoMemory);
}
