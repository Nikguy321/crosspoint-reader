#include "PhoneNmea.h"

#include <cmath>

namespace phonenmea {
namespace {

constexpr int64_t DAY_S = 86400;
// GGA and RMC carry at most 14 fields (RMC 13 + nav status); a few spare for longer variants.
constexpr size_t MAX_FIELDS = 20;

struct Field {
  const char* p = nullptr;
  size_t n = 0;
};

bool isDigit(const char c) { return c >= '0' && c <= '9'; }
bool isUpper(const char c) { return c >= 'A' && c <= 'Z'; }

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// A run of 1..maxDigits digits and nothing else.
bool parseUnsigned(const Field& f, const size_t maxDigits, uint32_t& out) {
  if (f.n == 0 || f.n > maxDigits) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < f.n; i++) {
    if (!isDigit(f.p[i])) return false;
    v = v * 10 + static_cast<uint32_t>(f.p[i] - '0');
  }
  out = v;
  return true;
}

// "hhmmss" with an optional ".s" (1..6 digits): whole seconds of the UTC day.
bool parseTime(const Field& f, uint32_t& out) {
  if (f.n < 6) return false;
  for (size_t i = 0; i < 6; i++) {
    if (!isDigit(f.p[i])) return false;
  }
  if (f.n > 6) {
    if (f.p[6] != '.' || f.n == 7 || f.n > 13) return false;
    for (size_t i = 7; i < f.n; i++) {
      if (!isDigit(f.p[i])) return false;
    }
  }
  const uint32_t hh = static_cast<uint32_t>((f.p[0] - '0') * 10 + (f.p[1] - '0'));
  const uint32_t mm = static_cast<uint32_t>((f.p[2] - '0') * 10 + (f.p[3] - '0'));
  const uint32_t ss = static_cast<uint32_t>((f.p[4] - '0') * 10 + (f.p[5] - '0'));
  if (hh > 23 || mm > 59 || ss > 60) return false;  // 60: a leap second
  out = hh * 3600 + mm * 60 + ss;
  return true;
}

// "ddmm.mmmm" (latitude) or "dddmm.mmmm" (longitude) in degrees, before the hemisphere: digits
// only, at least one degree digit, minutes under 60. Anything else (a sign, an exponent, a
// second point) is refused.
bool parseCoordinate(const Field& f, const bool latitude, double& out) {
  size_t i = 0;
  uint32_t whole = 0;
  while (i < f.n && isDigit(f.p[i])) {
    whole = whole * 10 + static_cast<uint32_t>(f.p[i] - '0');
    i++;
  }
  if (i < 3 || i > (latitude ? 4u : 5u)) return false;
  uint64_t fraction = 0;
  uint64_t scale = 1;
  if (i < f.n) {
    if (f.p[i] != '.') return false;
    i++;
    const size_t start = i;
    while (i < f.n && isDigit(f.p[i])) {
      fraction = fraction * 10 + static_cast<uint64_t>(f.p[i] - '0');
      scale *= 10;
      i++;
    }
    if (i == start || i - start > 10 || i != f.n) return false;
  }
  const double minutes = static_cast<double>(whole % 100) + static_cast<double>(fraction) / static_cast<double>(scale);
  if (minutes >= 60.0) return false;
  out = static_cast<double>(whole / 100) + minutes / 60.0;
  return out <= (latitude ? 90.0 : 180.0);
}

// A coordinate and its hemisphere letter: both present (have = true), both empty (have = false),
// or malformed (false).
bool parseSigned(const Field& value, const Field& hemi, const bool latitude, bool& have, double& out) {
  have = false;
  if (value.n == 0 && hemi.n == 0) return true;
  if (value.n == 0 || hemi.n != 1) return false;
  double v = 0;
  if (!parseCoordinate(value, latitude, v)) return false;
  const char h = hemi.p[0];
  if (latitude ? h == 'S' : h == 'W') {
    v = -v;
  } else if (h != (latitude ? 'N' : 'E')) {
    return false;
  }
  have = true;
  out = v;
  return true;
}

// HDOP to tenths, rounded up ("0.91" -> 10): never better than the sentence said.
bool parseHdopX10(const Field& f, uint16_t& out) {
  size_t i = 0;
  uint32_t whole = 0;
  while (i < f.n && isDigit(f.p[i])) {
    if (i >= 4) return false;
    whole = whole * 10 + static_cast<uint32_t>(f.p[i] - '0');
    i++;
  }
  if (i == 0) return false;
  uint32_t tenths = whole * 10;
  if (i < f.n) {
    if (f.p[i] != '.') return false;
    i++;
    const size_t start = i;
    bool rest = false;
    while (i < f.n && isDigit(f.p[i])) {
      if (i == start) {
        tenths += static_cast<uint32_t>(f.p[i] - '0');
      } else if (f.p[i] != '0') {
        rest = true;
      }
      i++;
    }
    if (i == start || i != f.n || i - start > 6) return false;
    if (rest) tenths++;
  }
  out = static_cast<uint16_t>(tenths > 0xFFFF ? 0xFFFF : tenths);
  return true;
}

bool qualityUsable(const uint8_t q) { return q == 1 || q == 2 || q == 4 || q == 5; }

// The RMC mode indicator (NMEA 2.3): A autonomous, D differential, F/R RTK, P precise. E
// (estimated), N (not valid), M (manual) and S (simulated) are not a measured fix.
bool modeUsable(const char m) { return m == '\0' || m == 'A' || m == 'D' || m == 'F' || m == 'R' || m == 'P'; }

Parse parseGga(const Field* f, const size_t count, Gga& g) {
  g = Gga{};
  if (count < 6) return Parse::Malformed;
  if (f[0].n > 0) {
    if (!parseTime(f[0], g.daySeconds)) return Parse::Malformed;
    g.haveTime = true;
  }
  bool haveLat = false;
  bool haveLon = false;
  if (!parseSigned(f[1], f[2], true, haveLat, g.lat) || !parseSigned(f[3], f[4], false, haveLon, g.lon)) {
    return Parse::Malformed;
  }
  if (haveLat != haveLon) return Parse::Malformed;
  // 0,0 is what a receiver without a fix tends to print.
  g.havePos = haveLat && !(g.lat == 0.0 && g.lon == 0.0);
  if (f[5].n > 0) {
    uint32_t q = 0;
    if (!parseUnsigned(f[5], 1, q)) return Parse::Malformed;
    g.quality = static_cast<uint8_t>(q);
  }
  if (count > 6 && f[6].n > 0) {
    uint32_t sats = 0;
    if (!parseUnsigned(f[6], 3, sats)) return Parse::Malformed;
    g.haveSats = true;
    g.sats = static_cast<uint8_t>(sats > 255 ? 255 : sats);
  }
  if (count > 7 && f[7].n > 0) {
    if (!parseHdopX10(f[7], g.hdopX10)) return Parse::Malformed;
    g.haveHdop = true;
  }
  return Parse::Gga;
}

Parse parseRmc(const Field* f, const size_t count, Rmc& r) {
  r = Rmc{};
  if (count < 2) return Parse::Malformed;
  if (f[0].n > 0) {
    if (!parseTime(f[0], r.daySeconds)) return Parse::Malformed;
    r.haveTime = true;
  }
  if (f[1].n > 1 || (f[1].n == 1 && f[1].p[0] != 'A' && f[1].p[0] != 'V')) return Parse::Malformed;
  r.active = f[1].n == 1 && f[1].p[0] == 'A';
  // Its coordinates are not used, but an unreadable one condemns the sentence.
  if (count > 5) {
    bool have = false;
    double ignored = 0;
    if (!parseSigned(f[2], f[3], true, have, ignored) || !parseSigned(f[4], f[5], false, have, ignored)) {
      return Parse::Malformed;
    }
  }
  if (count > 11 && f[11].n > 0) {
    if (f[11].n != 1 || !isUpper(f[11].p[0])) return Parse::Malformed;
    r.mode = f[11].p[0];
  }
  return Parse::Rmc;
}

}  // namespace

Parse parseSentence(const char* text, const size_t len, Gga& gga, Rmc& rmc) {
  if (text == nullptr || len < 4 || text[0] != '$') return Parse::BadChecksum;
  // "*hh" ends the sentence; the checksum is the XOR of everything between '$' and '*'.
  const size_t star = len - 3;
  if (text[star] != '*') return Parse::BadChecksum;
  const int hi = hexValue(text[star + 1]);
  const int lo = hexValue(text[star + 2]);
  if (hi < 0 || lo < 0) return Parse::BadChecksum;
  uint8_t sum = 0;
  for (size_t i = 1; i < star; i++) sum = static_cast<uint8_t>(sum ^ static_cast<uint8_t>(text[i]));
  if (sum != static_cast<uint8_t>(hi * 16 + lo)) return Parse::BadChecksum;
  for (size_t i = 1; i < star; i++) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c < 0x20 || c > 0x7E || c == '$' || c == '*') return Parse::Malformed;
  }

  // "$ttSSS,": a two-letter talker (GP, GN, GL, GA, BD ...) and the sentence type.
  if (star < 7 || text[6] != ',' || !isUpper(text[1]) || !isUpper(text[2])) return Parse::Other;
  const bool isGga = text[3] == 'G' && text[4] == 'G' && text[5] == 'A';
  const bool isRmc = text[3] == 'R' && text[4] == 'M' && text[5] == 'C';
  if (!isGga && !isRmc) return Parse::Other;

  Field fields[MAX_FIELDS];
  size_t count = 0;
  size_t start = 7;
  for (size_t i = 7; i <= star; i++) {
    if (i < star && text[i] != ',') continue;
    if (count == MAX_FIELDS) return Parse::Malformed;
    fields[count].p = text + start;
    fields[count].n = i - start;
    count++;
    start = i + 1;
  }
  return isGga ? parseGga(fields, count, gga) : parseRmc(fields, count, rmc);
}

int64_t skewAllowanceS(const int64_t sinceSyncS) {
  if (sinceSyncS < 0 || sinceSyncS / SKEW_DRIFT_DIVISOR >= MAX_SKEW_CAP_S - MAX_SKEW_S) return MAX_SKEW_CAP_S;
  return MAX_SKEW_S + sinceSyncS / SKEW_DRIFT_DIVISOR;
}

bool timeMatches(const uint32_t daySeconds, const int64_t nowUtcS, const int64_t maxSkewS) {
  if (nowUtcS < 0) return true;
  int64_t diff = static_cast<int64_t>(daySeconds % DAY_S) - nowUtcS % DAY_S;
  if (diff < 0) diff = -diff;
  if (diff > DAY_S / 2) diff = DAY_S - diff;  // across midnight
  return diff <= maxSkewS;
}

namespace {

// Seconds from a to b on the UTC day, -DAY_S/2 < result <= DAY_S/2 (across midnight too).
int32_t secondsBetween(const uint32_t a, const uint32_t b) {
  int32_t d = static_cast<int32_t>(b % DAY_S) - static_cast<int32_t>(a % DAY_S);
  if (d > DAY_S / 2) d -= static_cast<int32_t>(DAY_S);
  if (d <= -DAY_S / 2) d += static_cast<int32_t>(DAY_S);
  return d;
}

// Metres between two nearby points (equirectangular: exact enough over a few kilometres).
double nearDistanceM(const double lat1, const double lon1, const double lat2, const double lon2) {
  constexpr double M_PER_DEG = 111195.0;
  constexpr double RAD_PER_DEG = 0.017453292519943295;
  double dLon = lon2 - lon1;
  if (dLon > 180.0) dLon -= 360.0;
  if (dLon < -180.0) dLon += 360.0;
  const double x = dLon * std::cos((lat1 + lat2) / 2.0 * RAD_PER_DEG) * M_PER_DEG;
  const double y = (lat2 - lat1) * M_PER_DEG;
  return std::sqrt(x * x + y * y);
}

// HDOP x 5 m, rounded up, at least 5 m; 0 = no HDOP.
uint32_t accuracyOf(const Gga& g) {
  if (!g.haveHdop) return 0;
  const uint32_t metres = (static_cast<uint32_t>(g.hdopX10) * 5 + 9) / 10;
  return metres < 5 ? 5 : metres;
}

}  // namespace

void Reader::feed(const char* data, const size_t len, const uint32_t msSinceConnect, const int64_t nowUtcS) {
  if (data == nullptr) return;
  for (size_t i = 0; i < len && !located_; i++) {
    const char c = data[i];
    if (c == '$') {
      // A new sentence: whatever was half-read is dropped.
      inLine_ = true;
      overflow_ = false;
      len_ = 0;
      buf_[len_++] = c;
    } else if (c == '\r' || c == '\n') {
      if (inLine_ && !overflow_) line(msSinceConnect, nowUtcS);
      inLine_ = false;
      len_ = 0;
    } else if (inLine_) {
      if (len_ < LINE_CAP) {
        buf_[len_++] = c;
      } else {
        overflow_ = true;
      }
    }
  }
}

void Reader::line(const uint32_t msSinceConnect, const int64_t nowUtcS) {
  stats_.lines++;
  Gga gga;
  Rmc rmc;
  switch (parseSentence(buf_, len_, gga, rmc)) {
    case Parse::BadChecksum:
      stats_.badChecksum++;
      return;
    case Parse::Malformed:
      stats_.malformed++;
      return;
    case Parse::Other:
      return;
    case Parse::Gga:
      takeGga(gga, msSinceConnect, nowUtcS);
      return;
    case Parse::Rmc:
      takeRmc(rmc, msSinceConnect, nowUtcS);
      return;
  }
}

void Reader::takeGga(const Gga& g, const uint32_t msSinceConnect, const int64_t nowUtcS) {
  if (msSinceConnect < SETTLE_MS) {
    stats_.settling++;
    return;
  }
  if (!qualityUsable(g.quality) || !g.havePos) {
    stats_.noFix++;
    return;
  }
  // A fix must say when it was taken.
  if (!g.haveTime) {
    stats_.malformed++;
    return;
  }
  if (!timeMatches(g.daySeconds, nowUtcS, maxSkewS_)) {
    stats_.stale++;
    return;
  }
  if ((g.haveSats && g.sats < MIN_SATS) || (g.haveHdop && g.hdopX10 > MAX_HDOP_X10)) {
    stats_.weak++;
    return;
  }
  stats_.gga++;
  if (g.haveSats || g.haveHdop) {
    confirm(g);
    return;
  }
  // Neither figure: only an RMC of the same second vouches for it.
  if (rmcSecond_ >= 0 && static_cast<uint32_t>(rmcSecond_) == g.daySeconds % DAY_S) {
    confirm(g);
    return;
  }
  pending_ = true;
  pendingGga_ = g;
}

void Reader::takeRmc(const Rmc& r, const uint32_t msSinceConnect, const int64_t nowUtcS) {
  if (msSinceConnect < SETTLE_MS) {
    stats_.settling++;
    return;
  }
  if (!r.haveTime) return;
  if (!timeMatches(r.daySeconds, nowUtcS, maxSkewS_)) {
    stats_.stale++;
    return;
  }
  stats_.rmc++;
  if (!r.active || !modeUsable(r.mode)) return;
  rmcSecond_ = static_cast<int32_t>(r.daySeconds % DAY_S);
  if (pending_ && pendingGga_.daySeconds % DAY_S == static_cast<uint32_t>(rmcSecond_)) {
    pending_ = false;
    confirm(pendingGga_);
  }
}

void Reader::confirm(const Gga& g) {
  if (haveCandidate_) {
    const int32_t dt = secondsBetween(candidate_.daySeconds, g.daySeconds);
    // Another talker's copy of the same epoch confirms nothing.
    if (dt == 0) return;
    const uint32_t gap = static_cast<uint32_t>(dt < 0 ? -dt : dt);
    uint32_t reach = AGREE_MIN_M;
    if (accuracyOf(candidate_) > reach) reach = accuracyOf(candidate_);
    if (accuracyOf(g) > reach) reach = accuracyOf(g);
    if (gap <= AGREE_GAP_S && nearDistanceM(candidate_.lat, candidate_.lon, g.lat, g.lon) <=
                                  static_cast<double>(reach) + static_cast<double>(AGREE_SPEED_MPS) * gap) {
      accept(g);
      return;
    }
    stats_.disagree++;
  }
  // The newer one waits for the next.
  haveCandidate_ = true;
  candidate_ = g;
}

void Reader::accept(const Gga& g) {
  located_ = true;
  pending_ = false;
  fix_ = Fix{};
  fix_.lat = g.lat;
  fix_.lon = g.lon;
  fix_.sats = g.haveSats ? g.sats : 0;
  fix_.hdopX10 = g.haveHdop ? g.hdopX10 : 0;
  // HDOP times a typical 5 m range error; gpsdRelay's generated metres read larger.
  fix_.accuracyM = accuracyOf(g);
}

const char* Reader::why() const {
  if (located_) return "ok";
  if (stats_.lines == 0) return "silent";
  if (!heard()) return "bad-checksum";
  if (stats_.disagree > 0) return "disagree";
  if (haveCandidate_) return "unconfirmed";
  if (pending_) return "unmatched";
  if (stats_.weak > 0) return "weak";
  if (stats_.noFix > 0) return "no-fix";
  if (stats_.stale > 0) return "stale";
  if (stats_.malformed > 0) return "malformed";
  if (stats_.rmc > 0 && stats_.gga == 0) return "rmc-only";
  if (stats_.settling > 0) return "only-old";
  return "no-gga";
}

}  // namespace phonenmea
