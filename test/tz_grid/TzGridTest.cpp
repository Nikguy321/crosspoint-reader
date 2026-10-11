// lib/TzGrid - the WiPhone repository's tz_grid.{h,cpp} and tz_grid_data.h, copied unchanged -
// against that repository's own test vectors (vectors_tzgrid.json, copied here unchanged; COVEY
// is tested against the same file). Passing means this copy answers exactly what the WiPhone and
// COVEY answer:
//   * the content hash is recomputed from the arrays this build compiles, as gen_tz_grid.py
//     defines it, and must equal the header's and the vectors' (the data is the same release);
//   * every zone, named place, random and near-line point, offset and next transition in the
//     vectors. The offsets and transitions come from zoneinfo (tzdata 2026d), not from a rule model.
// Failure lines print coordinates to two decimals only.

#include <gtest/gtest.h>
#include <tz_grid.h>
#include <tz_grid_data.h>  // the arrays tz_grid.cpp compiles, for the hash

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "TzVectors.h"

namespace {

// ---- SHA-256 (FIPS 180-4), for the content hash ---------------------------------------------

class Sha256 {
 public:
  void update(const void* data, size_t n) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++) {
      block[used++] = p[i];
      if (used == 64) {
        compress();
        used = 0;
      }
    }
    bits += static_cast<uint64_t>(n) * 8;
  }

  std::string hex() {
    const uint64_t total = bits;
    const uint8_t one = 0x80;
    update(&one, 1);
    const uint8_t zero = 0;
    while (used != 56) update(&zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; i++) length[i] = static_cast<uint8_t>(total >> (56 - 8 * i));
    update(length, 8);
    std::string out;
    char byte[3];
    for (const uint32_t word : h) {
      for (int i = 0; i < 4; i++) {
        snprintf(byte, sizeof(byte), "%02x", static_cast<unsigned>((word >> (24 - 8 * i)) & 0xFF));
        out += byte;
      }
    }
    return out;
  }

 private:
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64] = {};
  size_t used = 0;
  uint64_t bits = 0;

  static uint32_t rotr(const uint32_t x, const int n) { return (x >> n) | (x << (32 - n)); }

  void compress() {
    static constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) | (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
             (static_cast<uint32_t>(block[4 * i + 2]) << 8) | block[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      k = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
  }
};

void putLe(Sha256& sha, const uint64_t value, const int bytes) {
  uint8_t b[8];
  for (int i = 0; i < bytes; i++) b[i] = static_cast<uint8_t>(value >> (8 * i));
  sha.update(b, static_cast<size_t>(bytes));
}

// The grid's content hash exactly as the WiPhone's tools/gen_tz_grid.py defines it.
std::string contentHash() {
  Sha256 sha;
  sha.update("TZGRID1\n", 8);
  putLe(sha, static_cast<uint32_t>(static_cast<int32_t>(TZGRID_LAT0_E7)), 4);
  putLe(sha, static_cast<uint32_t>(static_cast<int32_t>(TZGRID_LON0_E7)), 4);
  putLe(sha, static_cast<uint32_t>(static_cast<int32_t>(TZGRID_RES_E7)), 4);
  putLe(sha, TZGRID_ROWS, 2);
  putLe(sha, TZGRID_COLS, 2);
  for (int i = 0; i < TZGRID_NZONES; i++) {
    const TzZone& z = TZGRID_ZONES[i];
    char line[256];
    const int n = snprintf(line, sizeof(line), "%s\t%s\t%d\t%d\t%lld\t%s\t%s\n", z.iana, z.display, z.stdMinutes,
                           z.rule, static_cast<long long>(z.sinceUtc), z.abbrStd, z.abbrDst);
    sha.update(line, static_cast<size_t>(n));
  }
  sha.update("\n", 1);
  for (int i = 0; i <= TZGRID_ROWS; i++) putLe(sha, TZGRID_ROW_START[i], 4);
  for (int i = 0; i < TZGRID_NRUNS; i++) putLe(sha, TZGRID_RUNS[i], 2);
  return sha.hex();
}

const char* whyName(const int why) {
  static const char* NAMES[] = {"ZONE", "BORDER", "WATER", "OUTSIDE", "UNKNOWN_RULE", "BAD_INPUT"};
  return why >= 0 && why < 6 ? NAMES[why] : "?";
}

class TzGrid : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_EQ(tzv::vectors().kind, tzv::Value::Kind::Object) << "vectors not loaded"; }
  const tzv::Value& v = tzv::vectors();
};

}  // namespace

TEST(TzGridSha256, KnownAnswers) {
  Sha256 empty;
  EXPECT_EQ(empty.hex(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  Sha256 abc;
  abc.update("abc", 3);
  EXPECT_EQ(abc.hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  Sha256 two;  // two blocks
  two.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56);
  EXPECT_EQ(two.hex(), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_F(TzGrid, ContentHashIsTheWiPhonesAndCoveys) {
  const std::string hash = contentHash();
  EXPECT_EQ(hash, TZGRID_SHA256) << "the compiled arrays are not the data the header names";
  EXPECT_EQ(hash, v["sha256"].text) << "the data is not the vectors' (= the WiPhone's and COVEY's)";
  EXPECT_STREQ(tzGridInfo()->sha256, TZGRID_SHA256);
}

TEST_F(TzGrid, InfoAndAttribution) {
  const TzGridInfo* info = tzGridInfo();
  EXPECT_EQ(std::string(info->release), v["release"].text);
  EXPECT_EQ(std::string(info->tzdata), v["tzdata"].text);
  EXPECT_EQ(std::string(info->attribution), v["attribution"].text);
  EXPECT_NE(strstr(info->attribution, "OpenStreetMap contributors"), nullptr);
  EXPECT_NE(strstr(info->attribution, "Open Database License (ODbL) 1.0"), nullptr);
  const tzv::Value& grid = v["grid"];
  EXPECT_EQ(info->lat0E7, grid["lat0_e7"].asInt());
  EXPECT_EQ(info->lon0E7, grid["lon0_e7"].asInt());
  EXPECT_EQ(info->resE7, grid["res_e7"].asInt());
  EXPECT_EQ(info->rows, grid["rows"].asInt());
  EXPECT_EQ(info->cols, grid["cols"].asInt());
  EXPECT_EQ(static_cast<int64_t>(info->runs), grid["nruns"].asInt());
  EXPECT_EQ(info->validFrom, grid["valid_from"].asInt());
  EXPECT_EQ(info->validTo, grid["valid_to"].asInt());
}

TEST_F(TzGrid, EveryRowIsWhole) {
  ASSERT_EQ(TZGRID_ROW_START[0], 0u);
  ASSERT_EQ(TZGRID_ROW_START[TZGRID_ROWS], static_cast<uint32_t>(TZGRID_NRUNS));
  for (int r = 0; r < TZGRID_ROWS; r++) {
    ASSERT_LT(TZGRID_ROW_START[r], TZGRID_ROW_START[r + 1]) << "row " << r;
    long cols = 0;
    for (uint32_t k = TZGRID_ROW_START[r]; k < TZGRID_ROW_START[r + 1]; k++) {
      const int value = TZGRID_RUNS[k] >> 9;
      ASSERT_TRUE(value < TZGRID_NZONES || value == TZGRID_CODE_WATER || value == TZGRID_CODE_BORDER) << "row " << r;
      cols += (TZGRID_RUNS[k] & 511) + 1;
    }
    ASSERT_EQ(cols, TZGRID_COLS) << "row " << r;
  }
}

TEST_F(TzGrid, ZonesMatchTheVectors) {
  const tzv::Value& zones = v["zones"];
  ASSERT_EQ(static_cast<int>(zones.size()), tzGridZoneCount());
  for (int i = 0; i < tzGridZoneCount(); i++) {
    const TzZone* z = tzGridZone(i);
    const tzv::Value& want = zones[static_cast<size_t>(i)];
    ASSERT_NE(z, nullptr);
    EXPECT_EQ(std::string(z->iana), want["iana"].text) << i;
    EXPECT_EQ(std::string(z->display), want["display"].text) << i;
    EXPECT_EQ(z->stdMinutes, want["std"].asInt()) << want["iana"].text;
    EXPECT_EQ(z->rule, want["rule"].asInt()) << want["iana"].text;
    EXPECT_EQ(z->sinceUtc, want["since"].asInt()) << want["iana"].text;
    EXPECT_EQ(std::string(z->abbrStd), want["abbr_std"].text) << want["iana"].text;
    EXPECT_EQ(std::string(z->abbrDst), want["abbr_dst"].text) << want["iana"].text;
    EXPECT_EQ(tzGridFindZone(z->iana), i);
  }
  EXPECT_EQ(tzGridZone(-1), nullptr);
  EXPECT_EQ(tzGridZone(tzGridZoneCount()), nullptr);
  EXPECT_EQ(tzGridFindZone("Europe/London"), -1);
  EXPECT_EQ(tzGridFindZone(nullptr), -1);
}

TEST_F(TzGrid, NamedPlaces) {
  const int64_t jan27 = 1800014400LL;  // 2027-01-15 12:00 UTC
  const int64_t jul27 = 1815652800LL;  // 2027-07-15 12:00 UTC
  const tzv::Value& places = v["named"];
  ASSERT_GT(places.size(), 100u);
  for (const auto& p : places.items) {
    const auto lat = static_cast<int32_t>(p["lat_e7"].asInt());
    const auto lon = static_cast<int32_t>(p["lon_e7"].asInt());
    uint8_t why = 99;
    const int answer = tzGridLookup(lat, lon, &why);
    EXPECT_EQ(answer, p["answer"].asInt()) << p["name"].text;
    EXPECT_EQ(why, p["why"].asInt()) << p["name"].text << ": " << whyName(why);
    EXPECT_EQ(tzGridLookup(lat, lon, nullptr), answer) << p["name"].text;
    if (answer < 0) continue;
    EXPECT_EQ(std::string(tzGridZone(answer)->iana), p["iana"].text) << p["name"].text;
    // Where the map answers a zone, its clock is the clock zoneinfo keeps in the polygon there.
    if (!p["jan"].isNull()) {
      EXPECT_EQ(tzUtcOffsetMinutes(answer, jan27), p["jan"].asInt()) << p["name"].text;
      EXPECT_EQ(tzUtcOffsetMinutes(answer, jul27), p["jul"].asInt()) << p["name"].text;
    }
  }
}

TEST_F(TzGrid, PointsAnswerAsTheGeneratorsGrid) {
  const tzv::Value& points = v["points"];
  ASSERT_GT(points.size(), 1000u);
  int zones = 0;
  int borders = 0;
  int water = 0;
  for (const auto& p : points.items) {
    const auto lat = static_cast<int32_t>(p[0].asInt());
    const auto lon = static_cast<int32_t>(p[1].asInt());
    uint8_t why = 99;
    const int answer = tzGridLookup(lat, lon, &why);
    ASSERT_EQ(answer, p[2].asInt()) << "point " << (lat / 1e7) << ", " << (lon / 1e7);
    ASSERT_EQ(why, p[3].asInt()) << "point " << (lat / 1e7) << ", " << (lon / 1e7);
    zones += why == TZ_WHY_ZONE;
    borders += why == TZ_WHY_BORDER;
    water += why == TZ_WHY_WATER;
  }
  EXPECT_GT(zones, 1000);
  EXPECT_GT(borders, 1000);
  EXPECT_GT(water, 500);
}

TEST_F(TzGrid, OffsetsAtEveryChangeAreZoneinfos) {
  const tzv::Value& offsets = v["offsets"];
  ASSERT_GT(offsets.size(), 1000u);
  for (const auto& o : offsets.items) {
    const int zone = tzGridFindZone(o[0].str());
    ASSERT_GE(zone, 0) << o[0].text;
    // null: before the zone's 2026 change, which tz_grid refuses (TZ_NO_OFFSET).
    const int64_t want = o[2].isNull() ? TZ_NO_OFFSET : o[2].asInt();
    EXPECT_EQ(tzUtcOffsetMinutes(zone, o[1].asInt()), want) << o[0].text << " at " << o[1].asInt();
  }
}

TEST_F(TzGrid, NextTransitionsAreZoneinfos) {
  const tzv::Value& rows = v["next"];
  ASSERT_GT(rows.size(), 1000u);
  for (const auto& row : rows.items) {
    const int zone = tzGridFindZone(row[0].str());
    int after = 12345;
    const int64_t next = tzNextTransition(zone, row[1].asInt(), &after);
    EXPECT_EQ(next, row[2].asInt()) << row[0].text << " after " << row[1].asInt();
    if (row[2].asInt() != 0) EXPECT_EQ(after, row[3].asInt()) << row[0].text << " after " << row[1].asInt();
    EXPECT_EQ(tzNextTransition(zone, row[1].asInt(), nullptr), next) << row[0].text;
  }
}

TEST_F(TzGrid, TheAntimeridianIsOneMeridian) {
  for (int32_t lat = 70000000; lat < 720000000; lat += 1234567) {
    uint8_t east = 0;
    uint8_t west = 0;
    EXPECT_EQ(tzGridLookup(lat, 1800000000, &east), tzGridLookup(lat, -1800000000, &west)) << lat / 1e7;
    EXPECT_EQ(east, west) << lat / 1e7;
  }
  uint8_t why = 0;
  EXPECT_EQ(tzGridLookup(520000000, 1719999999, &why), TZ_NOT_COVERED);
  EXPECT_EQ(why, TZ_WHY_OUTSIDE);
}
