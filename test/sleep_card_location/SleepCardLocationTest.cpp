// Where the sleep-card location came from: the stored record (sleepCardLocationFix), what it
// refuses, which location it applies to, and the "source, accuracy, date" line under the Location
// row. Public Seattle landmarks only.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>

#include "src/sleepcards/AutoLocatePolicy.h"
#include "src/sleepcards/LocationFix.h"

using namespace sleepcards;

namespace {

// Space Needle, as the settings store it.
constexpr const char* NEEDLE = "47.6205,-122.3493";

LocationFix fixOf(const LocationSource source, const uint32_t accuracy, const uint16_t y, const uint8_t m,
                  const uint8_t d, const char* location = NEEDLE) {
  LocationFix f;
  f.source = source;
  f.accuracyM = accuracy;
  f.year = y;
  f.month = m;
  f.day = d;
  std::strncpy(f.location, location, sizeof(f.location) - 1);
  return f;
}

std::string record(const LocationFix& f) {
  char out[LOCATION_FIX_CAP];
  return formatLocationFix(f, out, sizeof(out)) ? out : "<refused>";
}

std::string line(const LocationFix& f) {
  char out[96];
  formatFixLine(f, out, sizeof(out));
  return out;
}

std::string accuracy(const uint32_t m) {
  char out[24];
  formatAccuracy(m, out, sizeof(out));
  return out;
}

}  // namespace

TEST(SleepCardLocation, RecordRoundTrips) {
  EXPECT_EQ(record(fixOf(LocationSource::Wifi, 80, 2026, 9, 29)), "wifi 80 2026-09-29 47.6205,-122.3493");
  EXPECT_EQ(record(fixOf(LocationSource::Internet, 25000, 2026, 1, 5)), "ip 25000 2026-01-05 47.6205,-122.3493");
  EXPECT_EQ(record(fixOf(LocationSource::Typed, 0, 0, 0, 0)), "typed 0 - 47.6205,-122.3493");

  LocationFix back;
  ASSERT_TRUE(parseLocationFix("wifi 80 2026-09-29 47.6205,-122.3493", back));
  EXPECT_EQ(back.source, LocationSource::Wifi);
  EXPECT_EQ(back.accuracyM, 80u);
  EXPECT_EQ(back.year, 2026);
  EXPECT_EQ(back.month, 9);
  EXPECT_EQ(back.day, 29);
  EXPECT_STREQ(back.location, NEEDLE);
  ASSERT_TRUE(parseLocationFix("typed 0 - -33.8568,151.2153", back));  // Sydney Opera House
  EXPECT_EQ(back.year, 0);
  EXPECT_STREQ(back.location, "-33.8568,151.2153");
}

TEST(SleepCardLocation, TheLongestRecordFits) {
  EXPECT_EQ(record(fixOf(LocationSource::Typed, MAX_FIX_ACCURACY_M, 2099, 12, 31, "-89.9999,-179.9999")),
            "typed 999999 2099-12-31 -89.9999,-179.9999");
  EXPECT_EQ(record(fixOf(LocationSource::Wifi, MAX_FIX_ACCURACY_M + 1, 2026, 9, 29)), "<refused>");
}

TEST(SleepCardLocation, RecordRefusesWhatItDidNotWrite) {
  LocationFix f;
  for (const char* bad : {"", "wifi", "wifi 80", "wifi 80 2026-09-29", "gps 80 2026-09-29 47.6205,-122.3493",
                          "WIFI 80 2026-09-29 47.6205,-122.3493", "wifi -80 2026-09-29 47.6205,-122.3493",
                          "wifi 1234567 2026-09-29 47.6205,-122.3493", "wifi 8a 2026-09-29 47.6205,-122.3493",
                          "wifi 80 2026-02-30 47.6205,-122.3493", "wifi 80 2026-13-01 47.6205,-122.3493",
                          "wifi 80 1999-09-29 47.6205,-122.3493", "wifi 80 2026-9-29 47.6205,-122.3493",
                          "wifi 80 2026-09-29 47.62,-122.35",  // not the stored form
                          "wifi 80 2026-09-29 91.0000,-122.3493", "wifi 80 2026-09-29 47.6205,-122.3493 x",
                          "wifi  80 2026-09-29 47.6205,-122.3493", "wifi 80 2026-09-29 47.6205,-122.3493 ",
                          " wifi 80 2026-09-29 47.6205,-122.3493"}) {
    EXPECT_FALSE(parseLocationFix(bad, f)) << bad;
  }
  EXPECT_FALSE(parseLocationFix(nullptr, f));
  EXPECT_EQ(record(fixOf(LocationSource::Wifi, 80, 2026, 2, 30)), "<refused>");
  EXPECT_EQ(record(fixOf(LocationSource::Wifi, 80, 2026, 9, 29, "")), "<refused>");
  EXPECT_EQ(record(fixOf(LocationSource::Wifi, 80, 2026, 9, 29, "north")), "<refused>");
}

TEST(SleepCardLocation, NormalizerKeepsEmptyAndCanonical) {
  char out[LOCATION_FIX_CAP] = "x";
  EXPECT_TRUE(normalizeLocationFix("", out, sizeof(out)));
  EXPECT_STREQ(out, "");
  EXPECT_TRUE(normalizeLocationFix("ip 25000 2026-09-29 47.6205,-122.3493", out, sizeof(out)));
  EXPECT_STREQ(out, "ip 25000 2026-09-29 47.6205,-122.3493");
  EXPECT_FALSE(normalizeLocationFix("somewhere nice", out, sizeof(out)));
}

TEST(SleepCardLocation, TheRecordAppliesOnlyToItsOwnLocation) {
  const char* rec = "wifi 80 2026-09-29 47.6205,-122.3493";
  LocationFix f = describeLocation(rec, NEEDLE);
  EXPECT_EQ(f.source, LocationSource::Wifi);
  EXPECT_EQ(f.accuracyM, 80u);

  // Changed elsewhere (the web page): typed in, nothing else known.
  f = describeLocation(rec, "47.6097,-122.3422");
  EXPECT_EQ(f.source, LocationSource::Typed);
  EXPECT_EQ(f.accuracyM, 0u);
  EXPECT_EQ(f.year, 0);
  EXPECT_STREQ(f.location, "47.6097,-122.3422");

  f = describeLocation("", NEEDLE);
  EXPECT_EQ(f.source, LocationSource::Typed);
  EXPECT_STREQ(f.location, NEEDLE);
  f = describeLocation("garbage", NEEDLE);
  EXPECT_EQ(f.source, LocationSource::Typed);
  f = describeLocation(rec, "");
  EXPECT_STREQ(f.location, "");
}

TEST(SleepCardLocation, AccuracyReadsAsMuchAsItKnows) {
  EXPECT_EQ(accuracy(0), "0 m");
  EXPECT_EQ(accuracy(80), "80 m");
  EXPECT_EQ(accuracy(999), "999 m");
  EXPECT_EQ(accuracy(1000), "1.0 km");
  EXPECT_EQ(accuracy(2549), "2.5 km");
  EXPECT_EQ(accuracy(2550), "2.6 km");
  EXPECT_EQ(accuracy(9949), "9.9 km");
  EXPECT_EQ(accuracy(9950), "10 km");
  EXPECT_EQ(accuracy(25000), "25 km");
}

TEST(SleepCardLocation, TheLineUnderTheLocationRow) {
  EXPECT_EQ(line(fixOf(LocationSource::Wifi, 80, 2026, 9, 29)),
            "From Wi-Fi, \xC2\xB1"
            "80 m, Sep 29");
  // An address lookup measures nothing: "city level", never an invented distance.
  EXPECT_EQ(line(fixOf(LocationSource::Internet, 25000, 2026, 10, 1)), "From internet address, city level, Oct 1");
  EXPECT_EQ(line(fixOf(LocationSource::Internet, 25000, 0, 0, 0)), "From internet address, city level");
  EXPECT_EQ(line(fixOf(LocationSource::Wifi, 2500, 0, 0, 0)),
            "From Wi-Fi, \xC2\xB1"
            "2.5 km");
  EXPECT_EQ(line(fixOf(LocationSource::Typed, 0, 2026, 9, 29)), "Typed in, Sep 29");
  EXPECT_EQ(line(fixOf(LocationSource::Typed, 0, 0, 0, 0)), "Typed in");
  EXPECT_EQ(line(fixOf(LocationSource::Typed, 0, 0, 0, 0, "")), "");
  // The line for a location changed elsewhere, from the stored record.
  EXPECT_EQ(line(describeLocation("wifi 80 2026-09-29 47.6205,-122.3493", "47.6097,-122.3422")), "Typed in");
}

namespace {
std::string retyped(const char* before, const char* after, const char* rec, const uint16_t y = 2026,
                    const uint8_t m = 9, const uint8_t d = 30) {
  char out[LOCATION_FIX_CAP] = "junk";
  return recordForTypedLocation(before, after, rec, y, m, d, out, sizeof(out)) ? out : "<refused>";
}
}  // namespace

TEST(SleepCardLocation, ConfirmingTheSameLocationKeepsWhereItCameFrom) {
  // Opened the keyboard on a Wi-Fi fix and pressed OK without changing it.
  EXPECT_EQ(retyped(NEEDLE, NEEDLE, "wifi 80 2026-09-29 47.6205,-122.3493"), "wifi 80 2026-09-29 47.6205,-122.3493");
  EXPECT_EQ(retyped(NEEDLE, NEEDLE, "ip 25000 2026-09-29 47.6205,-122.3493"), "ip 25000 2026-09-29 47.6205,-122.3493");
  // No record for it (set before records existed, or on the web page): typed in, date unknown.
  EXPECT_EQ(retyped(NEEDLE, NEEDLE, ""), "typed 0 - 47.6205,-122.3493");
  EXPECT_EQ(retyped(NEEDLE, NEEDLE, "wifi 80 2026-09-29 47.6097,-122.3422"), "typed 0 - 47.6205,-122.3493");
}

TEST(SleepCardLocation, ANewLocationIsTypedInToday) {
  // Pike Place Market, typed over a Wi-Fi fix of the Space Needle.
  EXPECT_EQ(retyped(NEEDLE, "47.6097,-122.3422", "wifi 80 2026-09-29 47.6205,-122.3493"),
            "typed 0 2026-09-30 47.6097,-122.3422");
  EXPECT_EQ(retyped("", NEEDLE, ""), "typed 0 2026-09-30 47.6205,-122.3493");
  // The clock is not set: no date.
  EXPECT_EQ(retyped("", NEEDLE, "", 0, 0, 0), "typed 0 - 47.6205,-122.3493");
  // Cleared: no record.
  EXPECT_EQ(retyped(NEEDLE, "", "wifi 80 2026-09-29 47.6205,-122.3493"), "");
}

// ---- "Update location when syncing" -------------------------------------------------------------------

TEST(SleepCardLocation, AnAutomaticWifiFixHasItsOwnRecordAndLine) {
  EXPECT_EQ(record(fixOf(LocationSource::WifiAuto, 80, 2026, 9, 29)), "wifi-auto 80 2026-09-29 47.6205,-122.3493");
  LocationFix back;
  ASSERT_TRUE(parseLocationFix("wifi-auto 80 2026-09-29 47.6205,-122.3493", back));
  EXPECT_EQ(back.source, LocationSource::WifiAuto);
  EXPECT_EQ(line(back),
            "From Wi-Fi (auto), \xC2\xB1"
            "80 m, Sep 29");
  // The longest record still fits the setting.
  EXPECT_EQ(record(fixOf(LocationSource::WifiAuto, MAX_FIX_ACCURACY_M, 2099, 12, 31, "-89.9999,-179.9999")),
            "wifi-auto 999999 2099-12-31 -89.9999,-179.9999");
  EXPECT_FALSE(parseLocationFix("wifi-autox 80 2026-09-29 47.6205,-122.3493", back));
}

namespace {
autolocate::Situation dueSituation() {
  autolocate::Situation s;
  s.enabled = true;
  s.jobOnline = true;
  s.connected = true;
  s.ssid = "HomeNet";
  s.peerSsid = "WiPhone-Books";
  s.hubSsid = "COVEY";
  s.clockValid = true;
  s.year = 2026;
  s.month = 9;
  s.day = 30;
  s.location = NEEDLE;
  s.record = "wifi 80 2026-09-28 47.6205,-122.3493";
  return s;
}
}  // namespace

TEST(SleepCardLocation, AutoLocateRunsOnlyWhenDueOnAnInternetNetwork) {
  using autolocate::Decision;
  EXPECT_EQ(autolocate::decide(dueSituation()), Decision::Run);
  auto s = dueSituation();
  s.enabled = false;
  EXPECT_EQ(autolocate::decide(s), Decision::Off);
  // A sync that never reached its server: the network is not working, no lookup on top.
  s = dueSituation();
  s.jobOnline = false;
  EXPECT_EQ(autolocate::decide(s), Decision::JobOffline);
  // One try a day, whatever came of it.
  s = dueSituation();
  s.triedYmd = autolocate::ymd(2026, 9, 30);
  EXPECT_EQ(autolocate::decide(s), Decision::AlreadyTried);
  s.triedYmd = autolocate::ymd(2026, 9, 29);
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  s.triedYmd = autolocate::ymd(2026, 10, 1);  // a later day (the clock was wrong then): a new day
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  EXPECT_EQ(autolocate::ymd(2026, 9, 30), 20260930u);
  EXPECT_EQ(autolocate::ymd(0, 0, 0), 0u);
  s = dueSituation();
  s.connected = false;
  EXPECT_EQ(autolocate::decide(s), Decision::NotConnected);
  // The peer's and the hub's hotspots reach no internet.
  s = dueSituation();
  s.ssid = "WiPhone-Books";
  EXPECT_EQ(autolocate::decide(s), Decision::DeviceNetwork);
  s.ssid = "COVEY";
  EXPECT_EQ(autolocate::decide(s), Decision::DeviceNetwork);
  s.ssid = "covey";  // SSIDs compare exactly
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  s.hubSsid = "";
  s.ssid = "";  // an empty name never matches an unset hub
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  s = dueSituation();
  s.clockValid = false;
  EXPECT_EQ(autolocate::decide(s), Decision::NoClock);
}

TEST(SleepCardLocation, AutoLocateAgeCountsWholeDaysOfAnyRecord) {
  using autolocate::Decision;
  auto s = dueSituation();
  // Saved today (from anywhere, typed too): fresh.
  s.record = "wifi-auto 80 2026-09-30 47.6205,-122.3493";
  EXPECT_EQ(autolocate::decide(s), Decision::Fresh);
  s.record = "typed 0 2026-09-30 47.6205,-122.3493";
  EXPECT_EQ(autolocate::decide(s), Decision::Fresh);
  // Yesterday or before: due; typed locations count too.
  s.record = "typed 0 2026-09-29 47.6205,-122.3493";
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  s.record = "ip 25000 2025-12-31 47.6205,-122.3493";
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  // Unknown age: no date, no record, a record for another place, no location at all.
  s.record = "typed 0 - 47.6205,-122.3493";
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  s.record = "";
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  s.record = "wifi 80 2026-09-30 47.6097,-122.3422";
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  s.location = "";
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  // A record dated after today (the clock was wrong then): due, so it gets a true date.
  s = dueSituation();
  s.record = "wifi 80 2026-10-02 47.6205,-122.3493";
  EXPECT_EQ(autolocate::decide(s), Decision::Run);
  int days = 0;
  ASSERT_TRUE(autolocate::locationAgeDays(NEEDLE, "wifi 80 2026-02-28 47.6205,-122.3493", 2026, 3, 1, days));
  EXPECT_EQ(days, 1);
  ASSERT_TRUE(autolocate::locationAgeDays(NEEDLE, "wifi 80 2025-09-30 47.6205,-122.3493", 2026, 9, 30, days));
  EXPECT_EQ(days, 365);
  EXPECT_FALSE(autolocate::locationAgeDays("", "", 2026, 9, 30, days));
}

TEST(SleepCardLocation, AutoLocateSavesOnlyATightWifiFix) {
  using autolocate::Verdict;
  EXPECT_EQ(autolocate::judgeFix(true, 47.6205, -122.3493, 80), Verdict::Save);
  EXPECT_EQ(autolocate::judgeFix(true, 47.6205, -122.3493, 1000), Verdict::Save);
  EXPECT_EQ(autolocate::judgeFix(true, 47.6205, -122.3493, 1001), Verdict::TooVague);
  // Never an internet-address fix, however it is dressed up.
  EXPECT_EQ(autolocate::judgeFix(false, 47.6205, -122.3493, 80), Verdict::NotWifi);
  EXPECT_EQ(autolocate::judgeFix(true, 0.0, 0.0, 80), Verdict::Invalid);
  EXPECT_EQ(autolocate::judgeFix(true, 91.0, 0.5, 80), Verdict::Invalid);
  EXPECT_EQ(autolocate::judgeFix(true, 47.6205, -122.3493, 0), Verdict::Invalid);
  EXPECT_EQ(autolocate::judgeFix(true, std::nan(""), 1.0, 80), Verdict::Invalid);

  char location[32], rec[LOCATION_FIX_CAP];
  ASSERT_TRUE(
      autolocate::autoRecord(47.62051, -122.34929, 80, 2026, 9, 30, location, sizeof(location), rec, sizeof(rec)));
  EXPECT_STREQ(location, NEEDLE);
  EXPECT_STREQ(rec, "wifi-auto 80 2026-09-30 47.6205,-122.3493");
  // The record describes the location saved beside it: the Location row reads it back.
  EXPECT_EQ(line(describeLocation(rec, location)),
            "From Wi-Fi (auto), \xC2\xB1"
            "80 m, Sep 30");
  EXPECT_FALSE(autolocate::autoRecord(47.6, -122.3, 80, 2026, 9, 30, location, sizeof(location), rec, 10));
  EXPECT_FALSE(autolocate::autoRecord(147.6, -122.3, 80, 2026, 9, 30, location, sizeof(location), rec, sizeof(rec)));
  EXPECT_STREQ(location, "");
}

TEST(SleepCardLocation, AutoLocateDecisionNamesAreLogWords) {
  using autolocate::Decision;
  for (const Decision d : {Decision::Run, Decision::Off, Decision::JobOffline, Decision::AlreadyTried,
                           Decision::NotConnected, Decision::DeviceNetwork, Decision::NoClock, Decision::Fresh}) {
    const std::string name = autolocate::decisionName(d);
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(name.find(' '), std::string::npos);
  }
}

TEST(SleepCardLocation, AutoLocateKeepsATighterWifiFixOfTheSamePlace) {
  // A ±30 m Locate Me fix; the new answer ±900 m, 400 m north of it: the same place, kept.
  const char* locateMe = "wifi 30 2026-09-28 47.6205,-122.3493";
  double lat = 47.6205 + 400.0 / 111195.0;
  double lon = -122.3493;
  uint32_t acc = 900;
  ASSERT_TRUE(autolocate::keepTighterFix(NEEDLE, locateMe, lat, lon, acc));
  EXPECT_DOUBLE_EQ(lat, 47.6205);
  EXPECT_DOUBLE_EQ(lon, -122.3493);
  EXPECT_EQ(acc, 30u);
  // An earlier automatic fix counts as measured too.
  lat = 47.6205;
  acc = 900;
  EXPECT_TRUE(autolocate::keepTighterFix(NEEDLE, "wifi-auto 200 2026-09-28 47.6205,-122.3493", lat, lon, acc));
  EXPECT_EQ(acc, 200u);

  // Replaced: the new fix is tighter, or elsewhere (5 km east), or the old one is not measured.
  const auto replaced = [](const char* record, const double newLat, const double newLon, const uint32_t newAcc) {
    double la = newLat, lo = newLon;
    uint32_t ac = newAcc;
    const bool kept = autolocate::keepTighterFix(NEEDLE, record, la, lo, ac);
    return !kept && la == newLat && lo == newLon && ac == newAcc;
  };
  EXPECT_TRUE(replaced(locateMe, 47.6205, -122.3493, 20));
  EXPECT_TRUE(replaced(locateMe, 47.6205,
                       -122.3493 + 5000.0 / (111195.0 * std::cos(47.6205 * 3.141592653589793 / 180.0)), 900));
  EXPECT_TRUE(replaced("typed 0 2026-09-28 47.6205,-122.3493", 47.6205, -122.3493, 900));
  EXPECT_TRUE(replaced("ip 25000 2026-09-28 47.6205,-122.3493", 47.6205, -122.3493, 900));
  EXPECT_TRUE(replaced("", 47.6205, -122.3493, 900));
  // A record for another place than the stored one reads as typed in.
  EXPECT_TRUE(replaced("wifi 30 2026-09-28 47.6097,-122.3422", 47.6205, -122.3493, 900));
  double la = 47.6205, lo = -122.3493;
  uint32_t ac = 900;
  EXPECT_FALSE(autolocate::keepTighterFix("", locateMe, la, lo, ac));
}
