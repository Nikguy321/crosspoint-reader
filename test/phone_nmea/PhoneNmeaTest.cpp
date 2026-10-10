// Locate Me's phone source, the pure half (src/network/PhoneNmea.cpp): NMEA sentences from a phone
// on its own hotspot, and the gates a GGA must pass before its position is taken - the checksum,
// the replayed queue at the connect, the fix quality, satellites and HDOP, the clock and its
// drift since the last sync, for iPhone apps a matching RMC, and a second GGA that agrees.
//
// Positions are public landmarks (the Eiffel Tower, the Sydney Opera House) and the sample
// sentences from the gpsd NMEA documentation; nothing here is a real person's fix.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "network/PhoneNmea.h"

using namespace phonenmea;

namespace {

// 2026-10-10 00:00:00 UTC.
constexpr int64_t MIDNIGHT = 1791590400;
constexpr int64_t NO_CLOCK = -1;
// Comfortably past the settle window.
constexpr uint32_t LATE = SETTLE_MS + 500;

// "$<body>*hh" with the right checksum.
std::string nmea(const std::string& body) {
  unsigned sum = 0;
  for (const char c : body) sum ^= static_cast<unsigned char>(c);
  char tail[8];
  std::snprintf(tail, sizeof(tail), "*%02X", sum);
  return "$" + body + tail;
}

std::string crlf(const std::string& sentence) { return sentence + "\r\n"; }

// "hhmmss.00" for a second of the day.
std::string hms(const int64_t daySeconds) {
  char buf[16];
  const int64_t s = ((daySeconds % 86400) + 86400) % 86400;
  std::snprintf(buf, sizeof(buf), "%02d%02d%02d.00", static_cast<int>(s / 3600), static_cast<int>(s / 60 % 60),
                static_cast<int>(s % 60));
  return buf;
}

// The Eiffel Tower, 48.8584 N 2.2945 E.
constexpr const char* EIFFEL_LAT = "4851.5040,N";
constexpr const char* EIFFEL_LON = "00217.6700,E";
constexpr double EIFFEL_LAT_DEG = 48.8584;
constexpr double EIFFEL_LON_DEG = 2.2945;

// An Android receiver's own sentences as gpsdRelay relays them (raw: real satellites and HDOP).
std::string rawGga(const int64_t day, const std::string& quality = "1", const std::string& sats = "09",
                   const std::string& hdop = "0.9") {
  return nmea("GNGGA," + hms(day) + "," + EIFFEL_LAT + "," + EIFFEL_LON + "," + quality + "," + sats + "," + hdop +
              ",35.0,M,44.9,M,,");
}
std::string rawRmc(const int64_t day, const char* status = "A", const char* mode = "A") {
  return nmea("GNRMC," + hms(day) + "," + status + "," + EIFFEL_LAT + "," + EIFFEL_LON + ",0.0,,101026,,," + mode);
}
// The confirmation a fix needs: two GGAs a second apart, the later at `day`.
std::string rawPair(const int64_t day, const std::string& quality = "1", const std::string& sats = "09",
                    const std::string& hdop = "0.9") {
  return crlf(rawGga(day - 1, quality, sats, hdop)) + crlf(rawGga(day, quality, sats, hdop));
}
// A GGA at another position: latitude and longitude as NMEA fields ("4851.5040,N").
std::string ggaAt(const int64_t day, const std::string& lat, const std::string& lon) {
  return crlf(nmea("GNGGA," + hms(day) + "," + lat + "," + lon + ",1,09,0.9,35.0,M,44.9,M,,"));
}

Reader fed(const std::string& text, const uint32_t ms = LATE, const int64_t now = NO_CLOCK) {
  Reader r;
  r.feed(text.data(), text.size(), ms, now);
  return r;
}

}  // namespace

// ---- one sentence ------------------------------------------------------------------------------

TEST(PhoneNmeaParse, ReadsTheGpsdSampleSentences) {
  Gga g;
  Rmc r;
  const std::string gga = "$GNGGA,001043.00,4404.14036,N,12118.85961,W,1,12,0.98,1113.0,M,-21.3,M*47";
  ASSERT_EQ(parseSentence(gga.data(), gga.size(), g, r), Parse::Gga);
  EXPECT_TRUE(g.haveTime);
  EXPECT_EQ(g.daySeconds, 10u * 60 + 43);
  EXPECT_TRUE(g.havePos);
  EXPECT_NEAR(g.lat, 44 + 4.14036 / 60, 1e-9);
  EXPECT_NEAR(g.lon, -(121 + 18.85961 / 60), 1e-9);
  EXPECT_EQ(g.quality, 1);
  EXPECT_TRUE(g.haveSats);
  EXPECT_EQ(g.sats, 12);
  EXPECT_TRUE(g.haveHdop);
  EXPECT_EQ(g.hdopX10, 10);  // 0.98 rounded up to tenths: never better than it said

  const std::string rmc = "$GNRMC,001031.00,A,4404.13993,N,12118.86023,W,0.146,,100117,,,A*7B";
  ASSERT_EQ(parseSentence(rmc.data(), rmc.size(), g, r), Parse::Rmc);
  EXPECT_TRUE(r.active);
  EXPECT_EQ(r.mode, 'A');
  EXPECT_EQ(r.daySeconds, 10u * 60 + 31);
}

TEST(PhoneNmeaParse, TheChecksumIsRequiredAndChecked) {
  Gga g;
  Rmc r;
  const std::string good = "$GNGGA,001043.00,4404.14036,N,12118.85961,W,1,12,0.98,1113.0,M,-21.3,M*47";
  std::string wrong = good;
  wrong[wrong.size() - 1] = '8';
  EXPECT_EQ(parseSentence(wrong.data(), wrong.size(), g, r), Parse::BadChecksum);
  const std::string none = good.substr(0, good.size() - 3);
  EXPECT_EQ(parseSentence(none.data(), none.size(), g, r), Parse::BadChecksum);
  std::string flipped = good;
  flipped[20] = '5';  // one digit of the latitude changed in transit
  EXPECT_EQ(parseSentence(flipped.data(), flipped.size(), g, r), Parse::BadChecksum);
  const std::string notHex = good.substr(0, good.size() - 2) + "4G";
  EXPECT_EQ(parseSentence(notHex.data(), notHex.size(), g, r), Parse::BadChecksum);
  // Lower-case hex is the same checksum.
  const std::string sample = nmea("GPGGA,120000.00,4851.5040,N,00217.6700,E,1,08,1.2,35.0,M,,M,,");
  std::string lower = sample;
  for (size_t i = lower.size() - 2; i < lower.size(); i++) {
    if (lower[i] >= 'A' && lower[i] <= 'F') lower[i] = static_cast<char>(lower[i] + 32);
  }
  EXPECT_EQ(parseSentence(lower.data(), lower.size(), g, r), Parse::Gga);
  EXPECT_EQ(parseSentence(nullptr, 10, g, r), Parse::BadChecksum);
}

TEST(PhoneNmeaParse, CoordinatesAreReadStrictly) {
  Gga g;
  Rmc r;
  const auto parse = [&](const std::string& lat, const std::string& lon) {
    const std::string s = nmea("GPGGA,120000.00," + lat + "," + lon + ",1,08,1.2,35.0,M,,M,,");
    return parseSentence(s.data(), s.size(), g, r);
  };
  ASSERT_EQ(parse(EIFFEL_LAT, EIFFEL_LON), Parse::Gga);
  EXPECT_NEAR(g.lat, EIFFEL_LAT_DEG, 1e-9);
  EXPECT_NEAR(g.lon, EIFFEL_LON_DEG, 1e-9);
  // The Sydney Opera House: south and east.
  ASSERT_EQ(parse("3351.4080,S", "15112.9180,E"), Parse::Gga);
  EXPECT_NEAR(g.lat, -33.8568, 1e-9);
  EXPECT_NEAR(g.lon, 151.2153, 1e-9);
  // gpsdRelay's generated sentences print tiny minutes with an exponent (Double.toString).
  EXPECT_EQ(parse("4851.0E-400,N", EIFFEL_LON), Parse::Malformed);
  EXPECT_EQ(parse(EIFFEL_LAT, "00217.6700e1,E"), Parse::Malformed);
  EXPECT_EQ(parse("+4851.5040,N", EIFFEL_LON), Parse::Malformed);
  EXPECT_EQ(parse("-4851.5040,N", EIFFEL_LON), Parse::Malformed);
  EXPECT_EQ(parse("48a1.5040,N", EIFFEL_LON), Parse::Malformed);
  EXPECT_EQ(parse("4851.50.40,N", EIFFEL_LON), Parse::Malformed);
  EXPECT_EQ(parse("4851.,N", EIFFEL_LON), Parse::Malformed);
  EXPECT_EQ(parse("4860.0000,N", EIFFEL_LON), Parse::Malformed);  // 60 minutes
  EXPECT_EQ(parse("9100.0000,N", EIFFEL_LON), Parse::Malformed);  // beyond the pole
  EXPECT_EQ(parse("48515.040,N", EIFFEL_LON), Parse::Malformed);  // five degree-and-minute digits
  EXPECT_EQ(parse("51.5040,N", EIFFEL_LON), Parse::Malformed);    // no degree digit
  EXPECT_EQ(parse(EIFFEL_LAT, "18100.0000,E"), Parse::Malformed);
  EXPECT_EQ(parse("4851.5040,X", EIFFEL_LON), Parse::Malformed);
  EXPECT_EQ(parse("4851.5040,", EIFFEL_LON), Parse::Malformed);  // no hemisphere
  EXPECT_EQ(parse("4851.5040,NN", EIFFEL_LON), Parse::Malformed);
  EXPECT_EQ(parse(EIFFEL_LAT, "00217.6700,N"), Parse::Malformed);  // a latitude letter
  EXPECT_EQ(parse(EIFFEL_LAT, ","), Parse::Malformed);             // half a position
  // No position at all reads as none (a receiver without a fix), and 0,0 too.
  ASSERT_EQ(parse(",", ","), Parse::Gga);
  EXPECT_FALSE(g.havePos);
  ASSERT_EQ(parse("0000.0000,N", "00000.0000,E"), Parse::Gga);
  EXPECT_FALSE(g.havePos);
  // But when the exponent is cut off with the rest, what is left is well formed: gpsdRelay prints
  // 40.000001 N (6.000000001904482E-5 minutes) as "4006.000000", six minutes (~11 km) north. Only
  // the second GGA a fix needs catches it (PhoneNmeaReader.AGeneratedGlitchNeedsASecondOpinion).
  ASSERT_EQ(parse("4006.000000,N", "10000.000000,W"), Parse::Gga);
  EXPECT_NEAR(g.lat, 40.1, 1e-9);
}

TEST(PhoneNmeaParse, TimesAndSmallFields) {
  Gga g;
  Rmc r;
  const auto parse = [&](const std::string& time, const std::string& rest) {
    const std::string s = nmea("GPGGA," + time + "," + EIFFEL_LAT + "," + EIFFEL_LON + "," + rest);
    return parseSentence(s.data(), s.size(), g, r);
  };
  ASSERT_EQ(parse("235959", "1,08,1.2"), Parse::Gga);  // no fraction, trailing fields missing
  EXPECT_EQ(g.daySeconds, 86399u);
  ASSERT_EQ(parse("235960.5", "1,08,1.2"), Parse::Gga);  // a leap second
  EXPECT_EQ(parse("246000", "1,08,1.2"), Parse::Malformed);
  EXPECT_EQ(parse("12000", "1,08,1.2"), Parse::Malformed);
  EXPECT_EQ(parse("120000.", "1,08,1.2"), Parse::Malformed);
  EXPECT_EQ(parse("12:00:00", "1,08,1.2"), Parse::Malformed);
  ASSERT_EQ(parse("", "1,08,1.2"), Parse::Gga);
  EXPECT_FALSE(g.haveTime);
  EXPECT_EQ(parse("120000", "1x,08,1.2"), Parse::Malformed);
  EXPECT_EQ(parse("120000", "1,8.5,1.2"), Parse::Malformed);
  EXPECT_EQ(parse("120000", "1,08,1.2.3"), Parse::Malformed);
  EXPECT_EQ(parse("120000", "1,08,-1.2"), Parse::Malformed);
  ASSERT_EQ(parse("120000", "1,08,10.01"), Parse::Gga);
  EXPECT_EQ(g.hdopX10, 101);
  ASSERT_EQ(parse("120000", "1,08,10.00"), Parse::Gga);
  EXPECT_EQ(g.hdopX10, 100);
  ASSERT_EQ(parse("120000", ",,"), Parse::Gga);
  EXPECT_EQ(g.quality, 0);
  EXPECT_FALSE(g.haveSats);
  EXPECT_FALSE(g.haveHdop);
  EXPECT_EQ(parse("120000", ""), Parse::Gga);  // the quality field, empty
  const std::string tooShort = nmea("GPGGA,120000,4851.5040,N,00217.6700");
  EXPECT_EQ(parseSentence(tooShort.data(), tooShort.size(), g, r), Parse::Malformed);
}

TEST(PhoneNmeaParse, OtherSentencesAreLeftAlone) {
  Gga g;
  Rmc r;
  for (const std::string& s : {nmea("GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00"),
                               nmea("GNGSA,A,3,04,05,,09,12,,,,,,,,2.5,1.3,2.1"), nmea("PSRF103,00,00,01,01"),
                               nmea("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K")}) {
    EXPECT_EQ(parseSentence(s.data(), s.size(), g, r), Parse::Other) << s;
  }
  // A control character inside a checksummed sentence is not NMEA.
  const std::string tab = nmea("GPGGA,120000.00,4851.5040,N,\t00217.6700,E,1,08,1.2,,,,,,");
  EXPECT_EQ(parseSentence(tab.data(), tab.size(), g, r), Parse::Malformed);
}

// ---- the clock ---------------------------------------------------------------------------------

TEST(PhoneNmeaClock, WithinFiveSecondsAcrossMidnight) {
  EXPECT_TRUE(timeMatches(12 * 3600, MIDNIGHT + 12 * 3600));
  EXPECT_TRUE(timeMatches(12 * 3600 + 5, MIDNIGHT + 12 * 3600));
  EXPECT_TRUE(timeMatches(12 * 3600 - 5, MIDNIGHT + 12 * 3600));
  EXPECT_FALSE(timeMatches(12 * 3600 + 6, MIDNIGHT + 12 * 3600));
  EXPECT_FALSE(timeMatches(12 * 3600 - 6, MIDNIGHT + 12 * 3600));
  // 23:59:58 by the phone, 00:00:02 by the reader (and the other way round).
  EXPECT_TRUE(timeMatches(86398, MIDNIGHT + 2));
  EXPECT_TRUE(timeMatches(2, MIDNIGHT - 2));
  EXPECT_FALSE(timeMatches(86390, MIDNIGHT + 2));
  EXPECT_FALSE(timeMatches(10, MIDNIGHT - 2));
  // A whole day off reads as the same time of day: only the time is in the sentence.
  EXPECT_TRUE(timeMatches(100, MIDNIGHT + 86400 + 100));
  EXPECT_TRUE(timeMatches(7, NO_CLOCK));
  // A wider allowance, across midnight too.
  EXPECT_TRUE(timeMatches(12 * 3600 - 30, MIDNIGHT + 12 * 3600, 30));
  EXPECT_FALSE(timeMatches(12 * 3600 - 31, MIDNIGHT + 12 * 3600, 30));
  EXPECT_TRUE(timeMatches(86390, MIDNIGHT + 15, 25));
}

TEST(PhoneNmeaClock, TheAllowanceWidensWithTheTimeSinceTheSync) {
  EXPECT_EQ(skewAllowanceS(0), MAX_SKEW_S);
  EXPECT_EQ(skewAllowanceS(3600), MAX_SKEW_S);       // an hour: under a second of drift
  EXPECT_EQ(skewAllowanceS(86400), MAX_SKEW_S + 8);  // a day: 100 ppm is 8.6 s
  EXPECT_EQ(skewAllowanceS(3 * 86400), MAX_SKEW_S + 25);
  EXPECT_EQ(skewAllowanceS(365 * 86400), MAX_SKEW_CAP_S);
  EXPECT_EQ(skewAllowanceS(INT64_MAX), MAX_SKEW_CAP_S);
  // Not known (no sync since the power came on): the widest.
  EXPECT_EQ(skewAllowanceS(-1), MAX_SKEW_CAP_S);
}

// ---- the reader ---------------------------------------------------------------------------------

TEST(PhoneNmeaReader, TakesAGpsdRelayRawFix) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  Reader r = fed(crlf(rawRmc(12 * 3600 - 1)) + rawPair(12 * 3600), LATE, now);
  ASSERT_TRUE(r.located());
  EXPECT_STREQ(r.why(), "ok");
  EXPECT_NEAR(r.fix().lat, EIFFEL_LAT_DEG, 1e-9);
  EXPECT_NEAR(r.fix().lon, EIFFEL_LON_DEG, 1e-9);
  EXPECT_EQ(r.fix().sats, 9);
  EXPECT_EQ(r.fix().hdopX10, 9);
  EXPECT_EQ(r.fix().accuracyM, 5u);  // HDOP 0.9 x 5 m, at least 5 m
  // Raw GGA alone is enough: it carries its own satellites and HDOP.
  r = fed(rawPair(12 * 3600, "2", "14", "3.1"), LATE, now);
  ASSERT_TRUE(r.located());
  EXPECT_EQ(r.fix().accuracyM, 16u);  // 3.1 x 5 = 15.5, rounded up
}

TEST(PhoneNmeaReader, OneGgaWaitsForAnotherSecond) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  Reader r = fed(crlf(rawGga(12 * 3600)), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "unconfirmed");
  // Another talker's copy of the same epoch confirms nothing.
  const std::string gp =
      crlf(nmea("GPGGA," + hms(12 * 3600) + "," + EIFFEL_LAT + "," + EIFFEL_LON + ",1,09,0.9,35.0,M,44.9,M,,"));
  r.feed(gp.data(), gp.size(), LATE + 10, now);
  EXPECT_FALSE(r.located());
  const std::string next = crlf(rawGga(12 * 3600 + 1));
  r.feed(next.data(), next.size(), LATE + 1000, now);
  EXPECT_TRUE(r.located());
  // Two good GGAs further apart than AGREE_GAP_S do not vouch for each other.
  r = fed(crlf(rawGga(12 * 3600 - 9)) + crlf(rawGga(12 * 3600)), LATE, NO_CLOCK);
  EXPECT_FALSE(r.located());
  r = fed(crlf(rawGga(12 * 3600 - 8)) + crlf(rawGga(12 * 3600)), LATE, NO_CLOCK);
  EXPECT_TRUE(r.located());
}

TEST(PhoneNmeaReader, TwoGgasMustAgree) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  // The Eiffel Tower, then the Sydney Opera House a second later: neither is taken...
  const std::string sydneyLat = "3351.4080,S";
  const std::string sydneyLon = "15112.9180,E";
  Reader r = fed(crlf(rawGga(12 * 3600 - 2)) + ggaAt(12 * 3600 - 1, sydneyLat, sydneyLon), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_EQ(r.stats().disagree, 1);
  EXPECT_STREQ(r.why(), "disagree");
  // ... until the next one agrees with the later of them.
  const std::string again = ggaAt(12 * 3600, sydneyLat, sydneyLon);
  r.feed(again.data(), again.size(), LATE + 1000, now);
  ASSERT_TRUE(r.located());
  EXPECT_NEAR(r.fix().lat, -33.8568, 1e-9);
  // Moving: 40 m in a second is within reach (30 m plus 50 m/s), 120 m is not.
  r = fed(ggaAt(12 * 3600 - 1, "4851.5040,N", EIFFEL_LON) + ggaAt(12 * 3600, "4851.5256,N", EIFFEL_LON), LATE, now);
  EXPECT_TRUE(r.located());
  r = fed(ggaAt(12 * 3600 - 1, "4851.5040,N", EIFFEL_LON) + ggaAt(12 * 3600, "4851.5688,N", EIFFEL_LON), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "disagree");
  // A rough fix reaches as far as its accuracy: HDOP 9 is 45 m.
  r = fed(crlf(nmea("GNGGA," + hms(12 * 3600 - 1) + ",4851.5040,N," + EIFFEL_LON + ",1,09,9.0,35.0,M,,M,,")) +
              crlf(nmea("GNGGA," + hms(12 * 3600) + ",4851.5470,N," + EIFFEL_LON + ",1,09,9.0,35.0,M,,M,,")),
          LATE, now);
  EXPECT_TRUE(r.located());  // 80 m apart: within 45 m + 50 m
}

TEST(PhoneNmeaReader, TakesAGpsdRelayGeneratedFix) {
  // gpsdRelay's own GGA (mock locations): 12 satellites always, the accuracy in metres as HDOP,
  // six decimals of minutes, an empty age and station.
  const int64_t now = MIDNIGHT + 12 * 3600;
  const auto generated = [](const int64_t day, const char* acc) {
    return crlf(nmea("GNGGA," + hms(day) + ",4851.504000,N,00217.670000,E,1,12," + acc + ",35.0,M,0,M,,"));
  };
  Reader r = fed(generated(12 * 3600 - 1, "4.0") + generated(12 * 3600, "4.0"), LATE, now);
  ASSERT_TRUE(r.located());
  EXPECT_NEAR(r.fix().lat, EIFFEL_LAT_DEG, 1e-9);
  EXPECT_EQ(r.fix().accuracyM, 20u);
  // Its accuracy past 10 m reads as a weak fix, as an HDOP over 10 would.
  r = fed(generated(12 * 3600 - 1, "35.0") + generated(12 * 3600, "35.0"), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "weak");
  // Its generated RMC alone (no GGA) is never a fix.
  const std::string rmc = nmea("GPRMC," + hms(12 * 3600) + ",A,4851.504000,N,00217.670000,E,0.0,0.0,101026,,,A,V");
  r = fed(crlf(rmc) + crlf(rmc), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "rmc-only");
  // A coordinate printed with an exponent condemns the sentence.
  const std::string sci = nmea("GNGGA," + hms(12 * 3600) + ",4851.0E-600,N,00217.670000,E,1,12,4.0,35.0,M,0,M,,");
  r = fed(crlf(sci), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "malformed");
}

TEST(PhoneNmeaReader, AGeneratedGlitchNeedsASecondOpinion) {
  // Dummy coordinates on a whole degree (40 N 100 W). gpsdRelay's generated GGA of 40.000001 N
  // reads "4006.000000" (six minutes north, ~11 km); a second later the phone has moved 1.85 m
  // north to 40.0000167 N, printed "4000.001000". Neither agrees with the other.
  const int64_t now = MIDNIGHT + 12 * 3600;
  const auto generated = [](const int64_t day, const char* lat) {
    return crlf(nmea("GNGGA," + hms(day) + "," + lat + ",N,10000.000000,W,1,12,3.9,250.0,M,0,M,,"));
  };
  Reader r = fed(generated(12 * 3600 - 1, "4006.000000") + generated(12 * 3600, "4000.001000"), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "disagree");
  // The next good one agrees with the one before it: the glitch cost two seconds, not the place.
  const std::string next = generated(12 * 3600 + 1, "4000.001200");
  r.feed(next.data(), next.size(), LATE + 1000, now);
  ASSERT_TRUE(r.located());
  EXPECT_NEAR(r.fix().lat, 40.0, 0.001);
  EXPECT_NEAR(r.fix().lon, -100.0, 1e-9);
  // The glitch after a good one is refused the same way.
  r = fed(generated(12 * 3600 - 1, "4000.001000") + generated(12 * 3600, "4006.000000"), LATE, now);
  EXPECT_FALSE(r.located());
}

TEST(PhoneNmeaReader, AnIphoneGgaNeedsTheRmcOfItsSecond) {
  // GPS 2 IP style: quality 1 but no satellites and no HDOP.
  const int64_t now = MIDNIGHT + 12 * 3600;
  const auto ios = [](const int64_t day) {
    return crlf(nmea("GPGGA," + hms(day) + "," + EIFFEL_LAT + "," + EIFFEL_LON + ",1,,,35.0,M,,M,,"));
  };
  const auto vouched = [&](const int64_t day) { return ios(day) + crlf(rawRmc(day)); };
  Reader r = fed(ios(12 * 3600), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "unmatched");
  // The RMC of the same second makes it a good GGA; the next such pair confirms it.
  r = fed(vouched(12 * 3600 - 1), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "unconfirmed");
  r = fed(vouched(12 * 3600 - 1) + vouched(12 * 3600), LATE, now);
  ASSERT_TRUE(r.located());
  EXPECT_EQ(r.fix().accuracyM, 0u);  // not known
  EXPECT_EQ(r.fix().sats, 0);
  // The RMC before its GGA works too.
  r = fed(crlf(rawRmc(12 * 3600 - 1)) + ios(12 * 3600 - 1) + crlf(rawRmc(12 * 3600)) + ios(12 * 3600), LATE, now);
  EXPECT_TRUE(r.located());
  // Another second, a void status or an estimated mode does not vouch for it.
  r = fed(vouched(12 * 3600 - 1) + ios(12 * 3600) + crlf(rawRmc(12 * 3600 - 2)), LATE, now);
  EXPECT_FALSE(r.located());
  r = fed(vouched(12 * 3600 - 1) + ios(12 * 3600) + crlf(rawRmc(12 * 3600, "V")), LATE, now);
  EXPECT_FALSE(r.located());
  for (const char* mode : {"E", "N", "M", "S"}) {
    r = fed(vouched(12 * 3600 - 1) + ios(12 * 3600) + crlf(rawRmc(12 * 3600, "A", mode)), LATE, now);
    EXPECT_FALSE(r.located()) << mode;
  }
  r = fed(vouched(12 * 3600 - 1) + ios(12 * 3600) + crlf(rawRmc(12 * 3600, "A", "D")), LATE, now);
  EXPECT_TRUE(r.located());
  // An unmatched GGA is replaced by the next one, whose own RMC vouches for it.
  r = fed(vouched(12 * 3600 - 2) + ios(12 * 3600 - 1) + crlf(rawRmc(12 * 3600 - 3)) + vouched(12 * 3600), LATE, now);
  EXPECT_TRUE(r.located());
  // Only one of the two figures: that one is checked and the RMC is not needed.
  const auto satsOnly = [](const int64_t day, const char* sats) {
    return crlf(nmea("GPGGA," + hms(day) + "," + EIFFEL_LAT + "," + EIFFEL_LON + ",1," + sats + ",,35.0,M,,M,,"));
  };
  r = fed(satsOnly(12 * 3600 - 1, "07") + satsOnly(12 * 3600, "07"), LATE, now);
  EXPECT_TRUE(r.located());
  r = fed(satsOnly(12 * 3600 - 1, "03") + satsOnly(12 * 3600, "03"), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "weak");
}

TEST(PhoneNmeaReader, TheReplayedQueueIsIgnored) {
  // gpsdRelay's shared queue hands a new client its old lines at once: all inside the settle window.
  const int64_t now = MIDNIGHT + 12 * 3600;
  std::string burst;
  for (int i = 30; i > 0; i--) burst += crlf(rawGga(12 * 3600 - i));
  Reader r;
  r.feed(burst.data(), burst.size(), 40, now);
  EXPECT_FALSE(r.located());
  EXPECT_EQ(r.stats().settling, 30);
  EXPECT_STREQ(r.why(), "only-old");
  // A good line just inside the window is still not taken; past it, it counts, and the next
  // second's confirms it. Nothing from the replay vouched for either.
  const std::string fresh = crlf(rawGga(12 * 3600));
  r.feed(fresh.data(), fresh.size(), SETTLE_MS - 1, now);
  EXPECT_FALSE(r.located());
  r.feed(fresh.data(), fresh.size(), SETTLE_MS, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "unconfirmed");
  const std::string next = crlf(rawGga(12 * 3600 + 1));
  r.feed(next.data(), next.size(), SETTLE_MS + 1000, now);
  EXPECT_TRUE(r.located());
}

TEST(PhoneNmeaReader, StaleLinesAreRefusedWhenTheClockIsSet) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  // An old line that arrives late (a slow link): 30 s behind the reader's clock.
  Reader r = fed(crlf(rawGga(12 * 3600 - 30)), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "stale");
  r = fed(rawPair(12 * 3600 - 4), LATE, now);  // 5 and 4 s behind
  EXPECT_TRUE(r.located());
  r = fed(rawPair(12 * 3600 + 6), LATE, now);
  EXPECT_FALSE(r.located());
  // Without a clock set from the internet the time cannot be checked; the settle window still holds.
  r = fed(rawPair(12 * 3600 - 30), LATE, NO_CLOCK);
  EXPECT_TRUE(r.located());
  r = fed(rawPair(12 * 3600 - 30), 100, NO_CLOCK);
  EXPECT_FALSE(r.located());
}

TEST(PhoneNmeaReader, AClockSyncedLongAgoGetsItsDriftAllowed) {
  // The reader's clock last synced three days ago has drifted 20 s: the phone's current sentences
  // read 20 s off. Five seconds would refuse them all; the allowance for three days takes them.
  const int64_t now = MIDNIGHT + 12 * 3600;
  const std::string pair = rawPair(12 * 3600 - 20);
  Reader strict;
  strict.feed(pair.data(), pair.size(), LATE, now);
  EXPECT_FALSE(strict.located());
  EXPECT_STREQ(strict.why(), "stale");
  Reader drifted(skewAllowanceS(3 * 86400));
  drifted.feed(pair.data(), pair.size(), LATE, now);
  EXPECT_TRUE(drifted.located());
  // A sentence an hour old is still refused, whatever the allowance.
  const std::string old = rawPair(12 * 3600 - 3600);
  Reader widest(skewAllowanceS(-1));
  widest.feed(old.data(), old.size(), LATE, now);
  EXPECT_FALSE(widest.located());
  EXPECT_STREQ(widest.why(), "stale");
}

TEST(PhoneNmeaReader, AFixAcrossMidnight) {
  // The phone says 23:59:58 and 23:59:59, the reader's clock 00:00:02 the next day.
  Reader r = fed(rawPair(86399), LATE, MIDNIGHT + 2);
  EXPECT_TRUE(r.located());
  // The pair itself across midnight.
  r = fed(crlf(rawGga(86399)) + crlf(rawGga(0)), LATE, MIDNIGHT + 1);
  EXPECT_TRUE(r.located());
  r = fed(rawPair(3), LATE, MIDNIGHT - 1);
  EXPECT_TRUE(r.located());
  r = fed(rawPair(86390), LATE, MIDNIGHT + 2);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "stale");
}

TEST(PhoneNmeaReader, OnlyRealFixQualitiesCount) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  for (const char* q : {"1", "2", "4", "5"}) {
    EXPECT_TRUE(fed(rawPair(12 * 3600, q), LATE, now).located()) << q;
  }
  for (const char* q : {"0", "3", "6", "7", "8", ""}) {
    Reader r = fed(rawPair(12 * 3600, q), LATE, now);
    EXPECT_FALSE(r.located()) << q;
    EXPECT_STREQ(r.why(), "no-fix") << q;
  }
  // A receiver without a fix often prints an empty sentence.
  Reader r = fed(crlf(nmea("GPGGA,,,,,,0,,,,,,,,")), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "no-fix");
  // Dead reckoning between two real fixes does not break the pair, nor stand in for one.
  r = fed(crlf(rawGga(12 * 3600 - 2)) + crlf(rawGga(12 * 3600 - 1, "6")), LATE, now);
  EXPECT_FALSE(r.located());
  const std::string next = crlf(rawGga(12 * 3600));
  r.feed(next.data(), next.size(), LATE + 1000, now);
  EXPECT_TRUE(r.located());
}

TEST(PhoneNmeaReader, SatellitesAndHdopLimits) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  EXPECT_TRUE(fed(rawPair(12 * 3600, "1", "04", "10.0"), LATE, now).located());
  Reader r = fed(rawPair(12 * 3600, "1", "03", "1.0"), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "weak");
  r = fed(rawPair(12 * 3600, "1", "12", "10.1"), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "weak");
  r = fed(rawPair(12 * 3600, "1", "12", "10.0"), LATE, now);
  ASSERT_TRUE(r.located());
  EXPECT_EQ(r.fix().accuracyM, 50u);
}

TEST(PhoneNmeaReader, BadChecksumsAreNeverUsed) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  std::string bad = rawGga(12 * 3600);
  bad[bad.size() - 1] = bad[bad.size() - 1] == '0' ? '1' : '0';
  Reader r = fed(crlf(bad) + crlf(bad), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_EQ(r.stats().badChecksum, 2);
  EXPECT_FALSE(r.heard());
  EXPECT_STREQ(r.why(), "bad-checksum");
  // Without its checksum at all.
  const std::string bare = rawGga(12 * 3600);
  r = fed(crlf(bare.substr(0, bare.size() - 3)), LATE, now);
  EXPECT_FALSE(r.located());
  EXPECT_STREQ(r.why(), "bad-checksum");
  // A bad one never vouches for a good one; good ones after them are taken.
  r = fed(crlf(bad) + crlf(rawGga(12 * 3600 + 1)), LATE, now);
  EXPECT_FALSE(r.located());
  r = fed(crlf(bad) + rawPair(12 * 3600 + 1), LATE, now);
  EXPECT_TRUE(r.located());
  EXPECT_TRUE(r.heard());
}

TEST(PhoneNmeaReader, LinesAcrossReadsAndLineEndings) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  // Each case: a good GGA a second earlier, then the one under test.
  const std::string first = crlf(rawGga(12 * 3600 - 1));
  const std::string s = rawGga(12 * 3600);
  Reader r;
  r.feed(first.data(), first.size(), LATE, now);
  // Split anywhere across two reads, LF only.
  r.feed(s.data(), 17, LATE, now);
  EXPECT_FALSE(r.located());
  const std::string rest = s.substr(17) + "\n";
  r.feed(rest.data(), rest.size(), LATE, now);
  EXPECT_TRUE(r.located());
  // CR only.
  EXPECT_TRUE(fed(first + s + "\r", LATE, now).located());
  // No line ending yet: not a sentence.
  EXPECT_FALSE(fed(first + s, LATE, now).located());
  EXPECT_STREQ(fed("", LATE, now).why(), "silent");
  // A '$' in the middle starts over: the cut-off line before it is dropped.
  EXPECT_TRUE(fed(first + "$GNGGA,1200" + crlf(s), LATE, now).located());
  // Bytes outside a sentence are ignored.
  EXPECT_TRUE(fed(first + "garbage\r\n" + crlf(s), LATE, now).located());
  // A line longer than any NMEA sentence is dropped whole.
  const std::string longLine = "$GPGSV," + std::string(LINE_CAP, '1') + "\r\n";
  r = fed(first + longLine + crlf(s), LATE, now);
  EXPECT_TRUE(r.located());
  EXPECT_EQ(r.stats().lines, 2);
}

TEST(PhoneNmeaReader, TheFirstGoodFixIsKept) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  const std::string sydney = ggaAt(12 * 3600 + 1, "3351.4080,S", "15112.9180,E");
  Reader r = fed(rawPair(12 * 3600) + sydney, LATE, now);
  ASSERT_TRUE(r.located());
  EXPECT_NEAR(r.fix().lat, EIFFEL_LAT_DEG, 1e-9);
  r.feed(sydney.data(), sydney.size(), LATE + 1000, now);
  EXPECT_NEAR(r.fix().lat, EIFFEL_LAT_DEG, 1e-9);
}

TEST(PhoneNmeaReader, WhyNamesTheMostTellingReason) {
  const int64_t now = MIDNIGHT + 12 * 3600;
  EXPECT_STREQ(Reader().why(), "silent");
  EXPECT_STREQ(fed(crlf(nmea("GPGSV,1,1,00"))).why(), "no-gga");
  // Weak beats no-fix: the phone has satellites but not enough yet.
  EXPECT_STREQ(fed(crlf(rawGga(12 * 3600, "0")) + crlf(rawGga(12 * 3600, "1", "03")), LATE, now).why(), "weak");
  EXPECT_STREQ(fed(crlf(rawGga(12 * 3600 - 60)) + crlf(rawGga(12 * 3600, "0")), LATE, now).why(), "no-fix");
  // One good GGA beats them all; two that disagree beat that.
  EXPECT_STREQ(fed(crlf(rawGga(12 * 3600, "0")) + crlf(rawGga(12 * 3600)), LATE, now).why(), "unconfirmed");
  EXPECT_STREQ(fed(crlf(rawGga(12 * 3600 - 1)) + ggaAt(12 * 3600, "3351.4080,S", "15112.9180,E"), LATE, now).why(),
               "disagree");
}
