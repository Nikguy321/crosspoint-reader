// Time Zone From Location (src/util/ClockZone.cpp) over the real clock table
// (src/util/TimezoneTable.cpp) and the real time zone map (lib/TzGrid):
//   * the table: its first 104 entries are pinned (APPEND ONLY), every IANA name it carries is a
//     zone of the map keeping the entry's clock, and every zone the map answers has an entry -
//     the same IANA name, else the same POSIX text - keeping the zone's clock, checked through
//     this computer's own C library at every DST change of 2026-2035 (the rules newlib is given);
//   * the decisions at the vectors' public named places: set, same clock (Detroit / Windsor),
//     near a line (the map's own band and the 10 km margin), at sea, outside, unmodelled, no entry,
//     off, Daylight Saving set by hand, not measured;
//   * the owner's rule: wherever a decision sets or keeps a zone, its clock is the clock zoneinfo
//     keeps in the polygon there.
// Fixtures are the vectors' public places, looked up by name; failures print no coordinates.

#include <gtest/gtest.h>
#include <tz_grid.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "TzVectors.h"
#include "util/ClockZone.h"
#include "util/Timezones.h"

namespace {

constexpr size_t PINNED_ENTRIES = 104;
// FNV-1a 64 of "name\tposix\toffsetQ\n" over the entries before Time Zone From Location.
constexpr uint64_t PINNED_FINGERPRINT = 0x4c815db10ad7fef3ULL;

int entryNamed(const char* name) {
  for (size_t i = 0; i < timezones::count(); i++) {
    if (strcmp(timezones::table()[i].name, name) == 0) return static_cast<int>(i);
  }
  return -1;
}

bool modelled(const int zone) {
  const TzZone* z = tzGridZone(zone);
  return z != nullptr && (z->rule == TZ_RULE_NONE || z->rule == TZ_RULE_USCAN);
}

// The UTC offset in minutes this computer's C library gives a POSIX TZ rule at an instant.
int hostOffsetMinutes(const char* posixTz, const int64_t utc) {
  setenv("TZ", posixTz, 1);
  tzset();
  const auto t = static_cast<time_t>(utc);
  struct tm local{};
  localtime_r(&t, &local);
  return static_cast<int>(local.tm_gmtoff / 60);
}

int64_t utcAt(const int year, const int month, const int day) {
  struct tm when{};
  when.tm_year = year - 1900;
  when.tm_mon = month - 1;
  when.tm_mday = day;
  when.tm_hour = 12;
  return static_cast<int64_t>(timegm(&when));
}

geolocate::Fix fixAt(const tzv::Value& place, const geolocate::FixSource source = geolocate::FixSource::Phone,
                     const uint32_t accuracyM = 15) {
  geolocate::Fix fix;
  fix.source = source;
  fix.lat = static_cast<double>(place["lat_e7"].asInt()) / 1e7;
  fix.lon = static_cast<double>(place["lon_e7"].asInt()) / 1e7;
  fix.accuracyM = accuracyM;
  return fix;
}

clockzone::Settings inUse(const char* entry) {
  clockzone::Settings settings;
  settings.current = static_cast<uint8_t>(entryNamed(entry));
  return settings;
}

class ClockZone : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_EQ(tzv::vectors().kind, tzv::Value::Kind::Object) << "vectors not loaded"; }

  // The decision at a named place of the vectors, with the given zone in use.
  static clockzone::Decision at(const char* place, const clockzone::Settings& settings,
                                const geolocate::FixSource source = geolocate::FixSource::Phone) {
    const tzv::Value& p = tzv::named(place);
    EXPECT_FALSE(p.isNull()) << "no named place " << place;
    return clockzone::decide(fixAt(p, source), settings);
  }
};

}  // namespace

// ---- the table -------------------------------------------------------------------------------

TEST(ClockZoneTable, TheEntriesBeforeItArePinned) {
  ASSERT_GE(timezones::count(), PINNED_ENTRIES);
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < PINNED_ENTRIES; i++) {
    const TimezoneInfo& e = timezones::table()[i];
    char line[128];
    snprintf(line, sizeof(line), "%s\t%s\t%d\n", e.name, e.posixTz, e.stdOffsetQ);
    for (const char* c = line; *c != '\0'; c++) {
      hash ^= static_cast<uint8_t>(*c);
      hash *= 0x100000001b3ULL;
    }
  }
  EXPECT_EQ(hash, PINNED_FINGERPRINT) << "an existing entry moved or changed: the table is APPEND ONLY";
  EXPECT_EQ(timezones::utcIndex(), 17);
  EXPECT_STREQ(timezones::table()[timezones::utcIndex()].name, "UTC");
}

TEST(ClockZoneTable, TheAppendedEntries) {
  ASSERT_EQ(timezones::count(), PINNED_ENTRIES + 4);
  const TimezoneInfo* t = timezones::table();
  EXPECT_STREQ(t[104].iana, "America/Adak");
  EXPECT_STREQ(t[105].iana, "America/Cancun");
  EXPECT_STREQ(t[106].iana, "America/Puerto_Rico");
  EXPECT_STREQ(t[107].iana, "America/Miquelon");
  // Each is spelled the way the map's own zone is, so the zones that share the rule find it.
  for (size_t i = PINNED_ENTRIES; i < timezones::count(); i++) {
    char rule[64];
    ASSERT_TRUE(clockzone::zonePosix(tzGridFindZone(t[i].iana), rule, sizeof(rule))) << t[i].iana;
    EXPECT_STREQ(t[i].posixTz, rule) << t[i].iana;
    EXPECT_EQ(t[i].stdOffsetQ * 15, tzGridZone(tzGridFindZone(t[i].iana))->stdMinutes) << t[i].iana;
  }
}

TEST(ClockZoneTable, EveryIanaNameIsAZoneOfTheMapWithTheEntrysClock) {
  int named = 0;
  for (size_t i = 0; i < timezones::count(); i++) {
    const TimezoneInfo& e = timezones::table()[i];
    if (e.iana == nullptr) continue;
    named++;
    const int zone = tzGridFindZone(e.iana);
    ASSERT_GE(zone, 0) << e.iana;
    ASSERT_TRUE(modelled(zone)) << e.iana;
    char rule[64];
    ASSERT_TRUE(clockzone::zonePosix(zone, rule, sizeof(rule))) << e.iana;
    EXPECT_TRUE(clockzone::sameClock(e.posixTz, rule)) << e.name << ": " << e.posixTz << " vs " << rule;
    EXPECT_EQ(e.stdOffsetQ * 15, tzGridZone(zone)->stdMinutes) << e.name;
    EXPECT_EQ(clockzone::entryForZone(zone), static_cast<int>(i)) << e.iana;
  }
  EXPECT_EQ(named, 17);
}

TEST(ClockZoneTable, EveryZoneOfTheMapHasAnEntryWithItsClock) {
  int mapped = 0;
  for (int zone = 0; zone < tzGridZoneCount(); zone++) {
    const TzZone* z = tzGridZone(zone);
    const int entry = clockzone::entryForZone(zone);
    if (!modelled(zone)) {
      EXPECT_EQ(entry, -1) << z->iana;
      continue;
    }
    // Outside North America: no entry is appended for them, and nothing is set there.
    if (strcmp(z->iana, "America/Guyana") == 0 || strcmp(z->iana, "Asia/Anadyr") == 0 ||
        strcmp(z->iana, "Asia/Kamchatka") == 0) {
      EXPECT_EQ(entry, -1) << z->iana;
      continue;
    }
    ASSERT_GE(entry, 0) << z->iana;
    mapped++;
    const TimezoneInfo& e = timezones::table()[entry];
    char rule[64];
    ASSERT_TRUE(clockzone::zonePosix(zone, rule, sizeof(rule)));
    // Found by its own IANA name, or else by the same POSIX text.
    if (e.iana == nullptr || strcmp(e.iana, z->iana) != 0) EXPECT_STREQ(e.posixTz, rule) << z->iana;
    EXPECT_TRUE(clockzone::sameClock(e.posixTz, rule)) << z->iana << " -> " << e.name;
  }
  // All but Havana and Nuuk (rules not modelled) and the three above.
  EXPECT_EQ(mapped, tzGridZoneCount() - 2 - 3);
}

TEST(ClockZoneTable, TheHostCLibraryAgreesWithTheMapAtEveryChange) {
  // The entry's POSIX rule, read by this computer's C library, against the map's own offsets: one
  // second before, at and after each DST change of 2026-2035, and mid-January and mid-July.
  int checked = 0;
  for (int zone = 0; zone < tzGridZoneCount(); zone++) {
    const int entry = clockzone::entryForZone(zone);
    if (entry < 0) continue;
    const TzZone* z = tzGridZone(zone);
    const char* rule = timezones::table()[entry].posixTz;
    for (int year = 2026; year <= 2035; year++) {
      int64_t instants[] = {utcAt(year, 1, 15),
                            utcAt(year, 7, 15),
                            tzUsCanDstStart(year, z->stdMinutes) - 1,
                            tzUsCanDstStart(year, z->stdMinutes),
                            tzUsCanDstEnd(year, z->stdMinutes) - 1,
                            tzUsCanDstEnd(year, z->stdMinutes)};
      for (const int64_t t : instants) {
        const int want = tzUtcOffsetMinutes(zone, t);
        if (want == TZ_NO_OFFSET) continue;  // before the zone's 2026 change
        EXPECT_EQ(hostOffsetMinutes(rule, t), want) << z->iana << " as " << rule << " at " << t;
        checked++;
      }
    }
  }
  EXPECT_GT(checked, 5000);
  unsetenv("TZ");
  tzset();
}

// ---- the POSIX helpers -------------------------------------------------------------------------

TEST(ClockZonePosix, ZonePosixSpellsTheMapsZones) {
  char rule[64];
  ASSERT_TRUE(clockzone::zonePosix(tzGridFindZone("America/New_York"), rule, sizeof(rule)));
  EXPECT_STREQ(rule, "EST5EDT,M3.2.0,M11.1.0");
  ASSERT_TRUE(clockzone::zonePosix(tzGridFindZone("America/St_Johns"), rule, sizeof(rule)));
  EXPECT_STREQ(rule, "NST3:30NDT,M3.2.0,M11.1.0");
  ASSERT_TRUE(clockzone::zonePosix(tzGridFindZone("America/Bogota"), rule, sizeof(rule)));
  EXPECT_STREQ(rule, "<-05>5");
  ASSERT_TRUE(clockzone::zonePosix(tzGridFindZone("America/Miquelon"), rule, sizeof(rule)));
  EXPECT_STREQ(rule, "<-03>3<-02>,M3.2.0,M11.1.0");
  ASSERT_TRUE(clockzone::zonePosix(tzGridFindZone("Asia/Kamchatka"), rule, sizeof(rule)));
  EXPECT_STREQ(rule, "<+12>-12");
  ASSERT_TRUE(clockzone::zonePosix(tzGridFindZone("America/Vancouver"), rule, sizeof(rule)));
  EXPECT_STREQ(rule, "MST7");
  EXPECT_FALSE(clockzone::zonePosix(tzGridFindZone("America/Havana"), rule, sizeof(rule)));
  EXPECT_STREQ(rule, "");
  EXPECT_FALSE(clockzone::zonePosix(-1, rule, sizeof(rule)));
  EXPECT_FALSE(clockzone::zonePosix(tzGridFindZone("America/New_York"), rule, 8));  // too short
  EXPECT_FALSE(clockzone::zonePosix(tzGridFindZone("America/New_York"), nullptr, 64));
}

TEST(ClockZonePosix, SameClockSetsTheAbbreviationsAside) {
  using clockzone::sameClock;
  EXPECT_TRUE(sameClock("EST5EDT,M3.2.0,M11.1.0", "EST5EDT,M3.2.0,M11.1.0"));
  EXPECT_TRUE(sameClock("COT5", "<-05>5"));
  EXPECT_TRUE(sameClock("UTC5", "EST5"));
  EXPECT_TRUE(sameClock("MST7", "UTC7:00"));
  EXPECT_TRUE(sameClock("EST5EDT,M3.2.0,M11.1.0", "XST+5XDT4,M3.2.0/2,M11.1.0/02:00"));
  EXPECT_TRUE(sameClock("<-03>3<-02>,M3.2.0,M11.1.0", "PMST3PMDT,M3.2.0,M11.1.0"));
  EXPECT_FALSE(sameClock("EST5", "EST5EDT,M3.2.0,M11.1.0"));
  EXPECT_FALSE(sameClock("MST7", "MST7MDT,M3.2.0,M11.1.0"));
  EXPECT_FALSE(sameClock("CST6CDT,M3.2.0,M11.1.0", "EST5EDT,M3.2.0,M11.1.0"));
  EXPECT_FALSE(sameClock("NST3:30NDT,M3.2.0,M11.1.0", "UTC3:30"));
  EXPECT_FALSE(sameClock("GMT0BST,M3.5.0/1,M10.5.0", "WET0WEST,M3.5.0/1,M10.5.0/3"));
  EXPECT_FALSE(sameClock("UTC-5:30", "UTC5:30"));
  EXPECT_FALSE(sameClock(nullptr, "UTC0"));
  EXPECT_FALSE(sameClock("UTC0", nullptr));
  EXPECT_FALSE(sameClock("U0", "U0"));    // a name needs three letters
  EXPECT_FALSE(sameClock("EST", "EST"));  // and an offset
  EXPECT_FALSE(sameClock("EST5EDT,M3.2.0,", "EST5EDT,M3.2.0,"));
}

// ---- the margin lookup -----------------------------------------------------------------------

TEST(ClockZoneLookup, NoMarginIsTheMapsOwnAnswer) {
  for (const auto& p : tzv::vectors()["points"].items) {
    const auto lat = static_cast<int32_t>(p[0].asInt());
    const auto lon = static_cast<int32_t>(p[1].asInt());
    uint8_t plainWhy = 99;
    uint8_t why = 99;
    ASSERT_EQ(clockzone::lookupClear(lat, lon, 0, &why), tzGridLookup(lat, lon, &plainWhy));
    ASSERT_EQ(why, plainWhy);
  }
}

TEST(ClockZoneLookup, TheMarginOnlyEverRefuses) {
  int refused = 0;
  int answered = 0;
  for (const auto& p : tzv::vectors()["points"].items) {
    const auto lat = static_cast<int32_t>(p[0].asInt());
    const auto lon = static_cast<int32_t>(p[1].asInt());
    uint8_t plainWhy = 99;
    const int plain = tzGridLookup(lat, lon, &plainWhy);
    uint8_t why = 99;
    const int clear = clockzone::lookupClear(lat, lon, clockzone::MARGIN_M, &why);
    if (plain < 0) {
      ASSERT_EQ(clear, plain);
      ASSERT_EQ(why, plainWhy);
    } else if (clear < 0) {
      ASSERT_EQ(clear, TZ_BORDER);
      ASSERT_EQ(why, TZ_WHY_BORDER);
      refused++;
    } else {
      ASSERT_EQ(clear, plain);
      ASSERT_EQ(why, TZ_WHY_ZONE);
      answered++;
    }
  }
  EXPECT_GT(refused, 100);
  EXPECT_GT(answered, 100);
}

// ---- the decisions -----------------------------------------------------------------------------

TEST_F(ClockZone, AnotherClockIsSet) {
  const auto d = at("Atlanta GA", inUse("Los Angeles / Vancouver"));
  EXPECT_EQ(d.verdict, clockzone::Verdict::Set);
  EXPECT_EQ(d.entry, entryNamed("New York / Toronto"));
  EXPECT_STREQ(clockzone::zoneName(d.gridZone), "Eastern");
  // A two-half Wi-Fi fix counts the same.
  EXPECT_EQ(at("Atlanta GA", inUse("Los Angeles / Vancouver"), geolocate::FixSource::Wifi).verdict,
            clockzone::Verdict::Set);
  // A zone found by its POSIX rule: Vancouver keeps Phoenix's clock since 2026.
  const auto v = at("Vancouver BC (UTC-7 all year since 2026-03-08)", inUse("Los Angeles / Vancouver"));
  EXPECT_EQ(v.verdict, clockzone::Verdict::Set);
  EXPECT_EQ(v.entry, entryNamed("Phoenix"));
  EXPECT_STREQ(clockzone::zoneName(v.gridZone), "Mountain, no DST");
  EXPECT_EQ(at("Calgary AB", inUse("Denver / Edmonton")).entry, entryNamed("Mexico City"));
  EXPECT_EQ(at("Regina SK (no DST)", inUse("Chicago / Winnipeg")).entry, entryNamed("Mexico City"));
  EXPECT_EQ(at("Hamilton BM", inUse("UTC")).entry, entryNamed("Halifax"));
  EXPECT_EQ(at("St. John's NL", inUse("UTC")).entry, entryNamed("St. John's"));
  EXPECT_EQ(at("Honolulu HI", inUse("UTC")).entry, entryNamed("Honolulu"));
  // The appended entries: by IANA name, then by the same rule.
  EXPECT_EQ(at("Cancun (no DST)", inUse("UTC")).entry, entryNamed("Cancun / Panama / Jamaica"));
  EXPECT_EQ(at("Kingston JM", inUse("UTC")).entry, entryNamed("Cancun / Panama / Jamaica"));
  EXPECT_EQ(at("Panama City", inUse("UTC")).entry, entryNamed("Cancun / Panama / Jamaica"));
  EXPECT_EQ(at("San Juan PR", inUse("UTC")).entry, entryNamed("Puerto Rico / Caribbean"));
  EXPECT_EQ(at("Santo Domingo DO", inUse("UTC")).entry, entryNamed("Puerto Rico / Caribbean"));
  // A zone in use that is not valid (never chosen, or from a newer firmware) is replaced.
  clockzone::Settings unset;
  unset.current = 254;
  EXPECT_EQ(clockzone::decide(fixAt(tzv::named("Atlanta GA")), unset).verdict, clockzone::Verdict::Set);
}

TEST_F(ClockZone, TheSameClockChangesNothing) {
  EXPECT_EQ(at("Sacramento CA", inUse("Los Angeles / Vancouver")).verdict, clockzone::Verdict::Same);
  // Detroit and Windsor: one clock, whichever name the map gives either side of the river.
  for (const char* place : {"Detroit MI", "Windsor ON (across the river from Detroit)", "Lansing MI", "Toronto ON"}) {
    const auto d = at(place, inUse("New York / Toronto"));
    EXPECT_EQ(d.verdict, clockzone::Verdict::Same) << place;
    EXPECT_EQ(d.entry, entryNamed("New York / Toronto")) << place;
  }
  // Another entry with the same clock is not a change either: Bogota's "COT5" or a fixed UTC-5
  // against Panama's "EST5".
  EXPECT_EQ(at("Panama City", inUse("Bogota / Lima")).verdict, clockzone::Verdict::Same);
  EXPECT_EQ(at("Kingston JM", inUse("UTC-05:00")).verdict, clockzone::Verdict::Same);
  // ... but the same offset with another rule is: Phoenix against Denver.
  EXPECT_EQ(at("Phoenix AZ", inUse("Denver / Edmonton")).verdict, clockzone::Verdict::Set);
}

TEST_F(ClockZone, NearALineNothingChanges) {
  const auto settings = inUse("UTC");
  // The map's own one-cell band.
  EXPECT_EQ(at("Pierre SD (on the Missouri: Fort Pierre across it is Mountain)", settings).verdict,
            clockzone::Verdict::NearLine);
  EXPECT_EQ(at("Vincennes IN (on the Wabash: Illinois across it is Central)", settings).verdict,
            clockzone::Verdict::NearLine);
  EXPECT_EQ(at("Tuba City AZ (Navajo; Hopi Moenkopi next door)", settings).verdict, clockzone::Verdict::NearLine);
  // The margin: the map answers these, but another clock is within 10 km.
  for (const char* place : {"Knox IN (Central)", "Kykotsmovi AZ (Hopi)", "Victoria BC", "Atikokan ON (no DST)",
                            "Dawson Creek BC (no DST)"}) {
    const tzv::Value& p = tzv::named(place);
    ASSERT_GE(
        tzGridLookup(static_cast<int32_t>(p["lat_e7"].asInt()), static_cast<int32_t>(p["lon_e7"].asInt()), nullptr), 0)
        << place;
    EXPECT_EQ(at(place, settings).verdict, clockzone::Verdict::NearLine) << place;
  }
  // ... while coastal places keep their answer (the edge of the territorial waters counts as a line).
  for (const char* place : {"Honolulu HI", "Hilo HI", "San Juan PR", "Hamilton BM", "Nome AK", "Mazatlan (no DST)"}) {
    EXPECT_EQ(at(place, settings).verdict, clockzone::Verdict::Set) << place;
  }
}

TEST_F(ClockZone, TheMarginGrowsWithTheFixsAccuracy) {
  // A fix exactly MARGIN_M rough is still measured; one metre more is not.
  const tzv::Value& p = tzv::named("Atlanta GA");
  const auto settings = inUse("UTC");
  EXPECT_EQ(clockzone::decide(fixAt(p, geolocate::FixSource::Wifi, clockzone::MARGIN_M), settings).verdict,
            clockzone::Verdict::Set);
  EXPECT_EQ(clockzone::decide(fixAt(p, geolocate::FixSource::Wifi, clockzone::MARGIN_M + 1), settings).verdict,
            clockzone::Verdict::NotMeasured);
}

TEST_F(ClockZone, OutsideWhatTheMapAnswersNothingChanges) {
  const auto settings = inUse("UTC");
  EXPECT_EQ(at("Havana CU (Cuba's own DST rule)", settings).verdict, clockzone::Verdict::Unmodelled);
  EXPECT_EQ(at("Nuuk GL (EU-style DST)", settings).verdict, clockzone::Verdict::Unmodelled);
  EXPECT_EQ(at("Pacific, 40 N 140 W", settings).verdict, clockzone::Verdict::AtSea);
  EXPECT_EQ(at("Gulf of Mexico, 25 N 90 W", settings).verdict, clockzone::Verdict::AtSea);
  EXPECT_EQ(at("London", settings).verdict, clockzone::Verdict::Outside);
  EXPECT_EQ(at("Resolute NU (north of 72 N)", settings).verdict, clockzone::Verdict::Outside);
  EXPECT_EQ(at("Bogota (south of 7 N)", settings).verdict, clockzone::Verdict::Outside);
  const auto anadyr = at("Anadyr, Chukotka (UTC+12)", settings);
  EXPECT_EQ(anadyr.verdict, clockzone::Verdict::NoEntry);
  EXPECT_GE(anadyr.gridZone, 0);
  EXPECT_EQ(anadyr.entry, -1);
}

TEST_F(ClockZone, TheSettingsComeFirst) {
  // Off: nothing changes, but what it would have set is still known (the bench prints it).
  auto off = inUse("Los Angeles / Vancouver");
  off.enabled = false;
  const auto d = at("Atlanta GA", off);
  EXPECT_EQ(d.verdict, clockzone::Verdict::Off);
  EXPECT_EQ(d.entry, entryNamed("New York / Toronto"));
  EXPECT_EQ(at("Pacific, 40 N 140 W", off).verdict, clockzone::Verdict::Off);
  // Daylight Saving forced on or off: the owner's override of the zone's rule stands.
  auto manual = inUse("Los Angeles / Vancouver");
  manual.dstAuto = false;
  EXPECT_EQ(at("Atlanta GA", manual).verdict, clockzone::Verdict::ManualDst);
  EXPECT_EQ(at("Sacramento CA", manual).verdict, clockzone::Verdict::ManualDst);
  EXPECT_EQ(at("Pierre SD (on the Missouri: Fort Pierre across it is Mountain)", manual).verdict,
            clockzone::Verdict::NearLine);
}

TEST_F(ClockZone, OnlyAMeasuredFixCounts) {
  const auto settings = inUse("UTC");
  const tzv::Value& p = tzv::named("Atlanta GA");
  EXPECT_EQ(clockzone::decide(fixAt(p, geolocate::FixSource::None), settings).verdict, clockzone::Verdict::NotMeasured);
  geolocate::Fix bad = fixAt(p);
  bad.lat = std::nan("");
  EXPECT_EQ(clockzone::decide(bad, settings).verdict, clockzone::Verdict::NotMeasured);
  bad = fixAt(p);
  bad.lon = 181.0;
  EXPECT_EQ(clockzone::decide(bad, settings).verdict, clockzone::Verdict::NotMeasured);
  EXPECT_EQ(clockzone::decide(geolocate::Fix{}, settings).gridZone, -1);
}

TEST_F(ClockZone, WhereAZoneIsSetItIsTheClockThere) {
  // Every named place: a set or kept zone keeps the clock zoneinfo keeps in the polygon there.
  const int64_t jan27 = utcAt(2027, 1, 15);
  const int64_t jul27 = utcAt(2027, 7, 15);
  int decided = 0;
  for (const auto& place : tzv::vectors()["named"].items) {
    const auto d = clockzone::decide(fixAt(place), inUse("UTC"));
    if (d.verdict != clockzone::Verdict::Set && d.verdict != clockzone::Verdict::Same) continue;
    ASSERT_FALSE(place["jan"].isNull()) << place["name"].text;
    const char* rule = timezones::table()[d.entry].posixTz;
    EXPECT_EQ(hostOffsetMinutes(rule, jan27), place["jan"].asInt()) << place["name"].text;
    EXPECT_EQ(hostOffsetMinutes(rule, jul27), place["jul"].asInt()) << place["name"].text;
    decided++;
  }
  unsetenv("TZ");
  tzset();
  EXPECT_GT(decided, 120);
}

TEST(ClockZoneWords, VerdictNames) {
  EXPECT_STREQ(clockzone::verdictName(clockzone::Verdict::Set), "set");
  EXPECT_STREQ(clockzone::verdictName(clockzone::Verdict::NearLine), "near-line");
  EXPECT_STREQ(clockzone::verdictName(clockzone::Verdict::NoEntry), "no-entry");
  EXPECT_STREQ(clockzone::verdictName(static_cast<clockzone::Verdict>(99)), "?");
  EXPECT_STREQ(clockzone::zoneName(-1), "");
}

// The screen names a zone by its region word in the reader's language (tr()), never the map's
// English text: every zone a decision can name has one, except the UTC-offset zones.
TEST(ClockZoneWords, Regions) {
  bool noDst = true;
  EXPECT_EQ(clockzone::zoneRegion(tzGridFindZone("America/New_York"), &noDst), clockzone::Region::Eastern);
  EXPECT_FALSE(noDst);
  EXPECT_EQ(clockzone::zoneRegion(tzGridFindZone("America/Phoenix"), &noDst), clockzone::Region::Mountain);
  EXPECT_TRUE(noDst);
  EXPECT_EQ(clockzone::zoneRegion(tzGridFindZone("Pacific/Honolulu"), &noDst), clockzone::Region::Hawaii);
  EXPECT_FALSE(noDst);
  EXPECT_EQ(clockzone::zoneRegion(tzGridFindZone("America/Miquelon"), nullptr), clockzone::Region::StPierre);
  EXPECT_EQ(clockzone::zoneRegion(tzGridFindZone("America/St_Johns"), nullptr), clockzone::Region::Newfoundland);
  EXPECT_EQ(clockzone::zoneRegion(tzGridFindZone("Asia/Anadyr"), &noDst), clockzone::Region::None);
  EXPECT_EQ(clockzone::zoneRegion(tzGridFindZone("America/Havana"), &noDst), clockzone::Region::None);
  EXPECT_EQ(clockzone::zoneRegion(-1, &noDst), clockzone::Region::None);
  EXPECT_FALSE(noDst);
  int named = 0;
  for (int z = 0; z < tzGridZoneCount(); z++) {
    const TzZone* zone = tzGridZone(z);
    ASSERT_NE(zone, nullptr);
    if (clockzone::entryForZone(z) < 0 || strncmp(zone->display, "UTC", 3) == 0) continue;
    EXPECT_NE(clockzone::zoneRegion(z, nullptr), clockzone::Region::None) << zone->iana;
    named++;
  }
  EXPECT_GT(named, 90);
}
