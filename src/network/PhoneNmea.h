#pragma once

#include <cstddef>
#include <cstdint>

// Locate Me's phone source (X4 Pro): the pure half, host-tested in test/phone_nmea. A phone whose
// hotspot the reader has joined can serve its own GPS as NMEA 0183 over TCP at the hotspot's
// gateway address (gpsdRelay or Share GPS on Android at port 10110, GPS 2 IP on iPhone at 11123).
// The device half (network/PhoneGps) connects and feeds the bytes here; this assembles them into
// sentences and takes a fix's figures from ONE GGA sentence that shows it is current and real:
//   - its checksum is right (a sentence without one is refused), and it completed at least
//     SETTLE_MS after the connect: gpsdRelay replays a shared queue of old lines to a new client;
//   - fix quality 1, 2, 4 or 5 (refused: 0 none, 3 PPS, 6 dead reckoning, 7 manual, 8 simulated);
//   - at least MIN_SATS satellites and HDOP at most MAX_HDOP_X10 / 10, each checked when the
//     sentence carries it (gpsdRelay's generated GGA puts the accuracy in metres where HDOP goes,
//     and the same limit holds);
//   - when it carries neither (iPhone apps), an RMC with status A, a mode other than E/N/M/S and
//     the same hhmmss;
//   - when the reader's clock is set from the internet, its UTC time of day within the reader's
//     allowance of now (across midnight too): MAX_SKEW_S just after a sync, widening with the
//     clock chip's drift since (skewAllowanceS);
//   - and a GGA of another second before it that passed the same gates agrees with it (within
//     AGREE_MIN_M or the accuracies, plus AGREE_SPEED_MPS for each second between them).
//     gpsdRelay's generated coordinates go through Kotlin's Double.toString and are then cut to six
//     characters, so near a whole degree "6.0000001E-5" minutes print as "06.000000": a well-formed
//     sentence ~11 km off. The fix that moves through such a strip jumps; a stationary one inside
//     it does not, which is why the setup notes say to relay the receiver's own NMEA.
// Coordinates are read strictly: ddmm.mmmm / dddmm.mmmm digits and one hemisphere letter. A sign,
// an exponent (gpsdRelay's generated sentences can print one) or any other character refuses the
// sentence.
namespace phonenmea {

constexpr uint16_t PORT_NMEA = 10110;    // the IANA NMEA-0183 port (gpsdRelay, Share GPS)
constexpr uint16_t PORT_GPS2IP = 11123;  // GPS 2 IP's default (iPhone)
constexpr uint32_t CONNECT_TIMEOUT_MS = 1500;
constexpr uint32_t READ_WINDOW_MS = 8000;
constexpr uint32_t SETTLE_MS = 2000;
// The sentence time against the reader's clock: MAX_SKEW_S right after an NTP sync, plus one
// second for every SKEW_DRIFT_DIVISOR seconds since (100 ppm: the RTC crystal's drift, cold
// included), at most MAX_SKEW_CAP_S (also used when the time of the last sync is not known).
constexpr int64_t MAX_SKEW_S = 5;
constexpr int64_t SKEW_DRIFT_DIVISOR = 10000;
constexpr int64_t MAX_SKEW_CAP_S = 900;
// Two GGAs agree when they lie within max(AGREE_MIN_M, their accuracies) plus AGREE_SPEED_MPS for
// each second between them, at most AGREE_GAP_S apart.
constexpr uint32_t AGREE_MIN_M = 30;
constexpr uint32_t AGREE_SPEED_MPS = 50;
constexpr uint32_t AGREE_GAP_S = 8;
constexpr uint8_t MIN_SATS = 4;
constexpr uint16_t MAX_HDOP_X10 = 100;
// NMEA allows 82 characters; a longer line (some chips' GSV) is dropped whole.
constexpr size_t LINE_CAP = 128;

struct Gga {
  bool haveTime = false;
  uint32_t daySeconds = 0;  // UTC time of day, whole seconds
  bool havePos = false;     // both coordinates present and not 0,0
  double lat = 0;
  double lon = 0;
  uint8_t quality = 0;  // an empty field reads 0
  bool haveSats = false;
  uint8_t sats = 0;
  bool haveHdop = false;
  uint16_t hdopX10 = 0;  // rounded up: 0.91 -> 10
};

struct Rmc {
  bool haveTime = false;
  uint32_t daySeconds = 0;
  bool active = false;  // status A
  char mode = '\0';     // the NMEA 2.3 mode indicator, '\0' when absent
};

enum class Parse : uint8_t { Gga, Rmc, Other, BadChecksum, Malformed };

// One sentence without its line ending ("$GNGGA,...*hh"). Gga and Rmc fill the matching struct;
// Other is a well-formed, checksummed sentence of another type.
Parse parseSentence(const char* text, size_t len, Gga& gga, Rmc& rmc);

// How far a sentence's time may be from the reader's clock, sinceSyncS seconds after the clock was
// last set from the internet (< 0: not known).
int64_t skewAllowanceS(int64_t sinceSyncS);

// A UTC time of day against the reader's clock, within maxSkewS (nowUtcS < 0: no trusted clock,
// always true).
bool timeMatches(uint32_t daySeconds, int64_t nowUtcS, int64_t maxSkewS = MAX_SKEW_S);

struct Fix {
  double lat = 0;
  double lon = 0;
  uint32_t accuracyM = 0;  // HDOP x 5 m, at least 5 m (a rough figure); 0 = not known
  uint8_t sats = 0;        // 0 = not given
  uint16_t hdopX10 = 0;    // 0 = not given
};

struct Stats {
  uint16_t lines = 0;        // complete sentences, any type
  uint16_t badChecksum = 0;  // ... with a missing or wrong checksum
  uint16_t malformed = 0;    // ... checksummed but unreadable (a bad coordinate, time or field)
  uint16_t settling = 0;     // GGA/RMC in the first SETTLE_MS (the replayed queue)
  uint16_t stale = 0;        // GGA/RMC whose time is not now
  uint16_t noFix = 0;        // GGA without a usable fix (quality, or no position)
  uint16_t weak = 0;         // GGA with too few satellites or too high an HDOP
  uint16_t gga = 0;          // GGA past the gates above
  uint16_t rmc = 0;          // RMC past the settle and time gates
  uint16_t disagree = 0;     // GGA past every gate that did not agree with the one before it
};

class Reader {
 public:
  // maxSkewS: the clock allowance (skewAllowanceS) for every sentence this reader takes.
  explicit Reader(int64_t maxSkewS = MAX_SKEW_S) : maxSkewS_(maxSkewS) {}

  // Bytes received msSinceConnect after the connect; nowUtcS is the reader's UTC clock when it is
  // set from the internet, else -1. Stops taking sentences once a fix is accepted.
  void feed(const char* data, size_t len, uint32_t msSinceConnect, int64_t nowUtcS);

  bool located() const { return located_; }
  const Fix& fix() const { return fix_; }
  const Stats& stats() const { return stats_; }
  // A checksummed sentence arrived: something speaks NMEA on this port.
  bool heard() const { return stats_.lines > stats_.badChecksum; }
  // Why there is no fix yet, as one log word: "ok", "silent", "bad-checksum", "disagree" (two
  // good GGAs that did not agree), "unconfirmed" (one good GGA, none after it), "unmatched" (an
  // iPhone-style GGA with no matching RMC), "weak", "no-fix", "stale", "malformed", "rmc-only"
  // (gpsdRelay generating RMC alone), "only-old", "no-gga".
  const char* why() const;

 private:
  void line(uint32_t msSinceConnect, int64_t nowUtcS);
  void takeGga(const Gga& g, uint32_t msSinceConnect, int64_t nowUtcS);
  void takeRmc(const Rmc& r, uint32_t msSinceConnect, int64_t nowUtcS);
  void confirm(const Gga& g);
  void accept(const Gga& g);

  int64_t maxSkewS_;

  char buf_[LINE_CAP] = {};
  size_t len_ = 0;
  bool inLine_ = false;
  bool overflow_ = false;

  bool located_ = false;
  Fix fix_;
  Stats stats_;
  // The latest GGA that passed every gate but carries neither satellites nor HDOP: it waits for
  // an RMC of the same second.
  bool pending_ = false;
  Gga pendingGga_;
  // The second of the latest RMC that passed its gates (-1 = none).
  int32_t rmcSecond_ = -1;
  // The latest GGA that passed every gate, waiting for one of another second to agree with it.
  bool haveCandidate_ = false;
  Gga candidate_;
};

}  // namespace phonenmea
