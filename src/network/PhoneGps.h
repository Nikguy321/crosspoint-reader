#pragma once

#include <cstddef>
#include <cstdint>

#include "PhoneNmea.h"

// Locate Me's phone source, the socket half (X4 Pro): on a station already joined (it never starts
// the radio), connects to the network's gateway - on a phone's hotspot, the phone - at port 10110
// (gpsdRelay, Share GPS), then 11123 (GPS 2 IP), each connect bounded to
// phonenmea::CONNECT_TIMEOUT_MS, and reads NMEA for up to phonenmea::READ_WINDOW_MS into a
// phonenmea::Reader, which decides whether a sentence is a fix. Nothing is sent to the phone. A
// plain TCP socket (lwIP), closed before returning; no task, no heap. Logs no position.
//
// The bench console's LOCTEST can read another address instead (LOCPHONE: a computer on the same
// network stands in for the phone). Locate Me and AutoLocate always read the gateway.
namespace PhoneGps {

// Where to read from: address 0 = the network's gateway, ports 10110 then 11123; otherwise this
// IPv4 address (network byte order) at `port` (0 = the two default ports in turn).
struct Target {
  uint32_t address = 0;
  uint16_t port = 0;
};

struct Result {
  uint16_t port = 0;      // the port that delivered NMEA, else the last that accepted (0 = none did)
  bool accepted = false;  // some port accepted the connection: something listens where a phone would
  bool heard = false;     // a checksummed NMEA sentence arrived: a phone source answered
  bool located = false;   // a sentence passed every gate
  phonenmea::Fix fix;
  phonenmea::Stats stats;
  const char* why = "not-found";  // "ok", "not-found", "no-station", or phonenmea::Reader::why()
};

// Blocks the calling (loop) task for at most two connect timeouts plus one read window (~11 s).
// nowUtcS: the reader's UTC clock at the call when it is set from the internet, else -1;
// maxSkewS: how far a sentence's time may be from it (phonenmea::skewAllowanceS).
Result read(int64_t nowUtcS, int64_t maxSkewS, const Target& target = Target{});

// "192.0.2.10:10110", "192.0.2.10 ports=10110,11123" or "off" (address 0), for the bench console.
void describeTarget(const Target& target, char* out, size_t cap);

}  // namespace PhoneGps
