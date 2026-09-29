// Where the sleep-card location came from: the stored record (sleepCardLocationFix), what it
// refuses, which location it applies to, and the "source, accuracy, date" line under the Location
// row. Public Seattle landmarks only.
#include <gtest/gtest.h>

#include <cstring>
#include <string>

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
