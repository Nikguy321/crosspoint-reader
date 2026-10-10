#include "PhoneGps.h"

#include <BoardConfig.h>

#include <cstdio>

#if FREEINK_DEVICE_X4PRO
#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <unistd.h>

#include "util/TaskWatchdog.h"
#endif

namespace PhoneGps {
namespace {

void formatAddress(const uint32_t address, char* out, const size_t cap) {
  // Network byte order: the first octet is the lowest byte in memory.
  const auto* b = reinterpret_cast<const uint8_t*>(&address);
  std::snprintf(out, cap, "%u.%u.%u.%u", static_cast<unsigned>(b[0]), static_cast<unsigned>(b[1]),
                static_cast<unsigned>(b[2]), static_cast<unsigned>(b[3]));
}

}  // namespace

void describeTarget(const Target& target, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return;
  if (target.address == 0) {
    std::snprintf(out, cap, "off");
    return;
  }
  char ip[16];
  formatAddress(target.address, ip, sizeof(ip));
  if (target.port != 0) {
    std::snprintf(out, cap, "%s:%u", ip, static_cast<unsigned>(target.port));
  } else {
    std::snprintf(out, cap, "%s ports=%u,%u", ip, static_cast<unsigned>(phonenmea::PORT_NMEA),
                  static_cast<unsigned>(phonenmea::PORT_GPS2IP));
  }
}

#if FREEINK_DEVICE_X4PRO
namespace {

// A connected TCP socket to address:port within timeoutMs, or -1. Non-blocking from here on (the
// same socket calls as the framework's NetworkClient: select and close go through the VFS).
int connectWithin(const uint32_t address, const uint16_t port, const uint32_t timeoutMs) {
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return -1;
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  to.sin_addr.s_addr = address;
  int r = connect(fd, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  if (r < 0 && errno != EINPROGRESS) {
    close(fd);
    return -1;
  }
  if (r != 0) {
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval tv = {};
    tv.tv_sec = static_cast<time_t>(timeoutMs / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000);
    r = select(fd + 1, nullptr, &writable, nullptr, &tv);
    int error = 0;
    socklen_t len = sizeof(error);
    if (r <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
      close(fd);
      return -1;
    }
  }
  return fd;
}

// Reads one connection into reader until a fix, the peer closing, or windowMs ending. Returns the
// time it took.
unsigned long readStream(const int fd, phonenmea::Reader& reader, const int64_t nowUtcS, const unsigned long windowMs) {
  const unsigned long connected = millis();
  char buf[128];
  for (;;) {
    const unsigned long elapsed = millis() - connected;
    if (elapsed >= windowMs || reader.located()) return elapsed;
    const int n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0) {
      // The clock read at the start, moved on by the time since (whole seconds are enough).
      const int64_t now = nowUtcS < 0 ? -1 : nowUtcS + static_cast<int64_t>(millis() - connected) / 1000;
      reader.feed(buf, static_cast<size_t>(n), static_cast<uint32_t>(millis() - connected), now);
      continue;
    }
    if (n == 0) return millis() - connected;  // the phone closed the connection
    if (errno != EAGAIN && errno != EWOULDBLOCK) return millis() - connected;
    resetTaskWatchdogIfSubscribed();
    delay(20);
  }
}

}  // namespace

Result read(const int64_t nowUtcS, const int64_t maxSkewS, const Target& target) {
  Result result;
  if (WiFi.status() != WL_CONNECTED) {
    result.why = "no-station";
    return result;
  }
  const uint32_t address = target.address != 0 ? target.address : static_cast<uint32_t>(WiFi.gatewayIP());
  if (address == 0) return result;
  const bool onePort = target.address != 0 && target.port != 0;
  const uint16_t ports[2] = {onePort ? target.port : phonenmea::PORT_NMEA,
                             onePort ? static_cast<uint16_t>(0) : phonenmea::PORT_GPS2IP};
  const unsigned long started = millis();
  // One read window across both ports.
  unsigned long readMs = 0;
  for (const uint16_t port : ports) {
    if (port == 0 || readMs >= phonenmea::READ_WINDOW_MS) continue;
    const int fd = connectWithin(address, port, phonenmea::CONNECT_TIMEOUT_MS);
    resetTaskWatchdogIfSubscribed();
    if (fd < 0) continue;
    result.accepted = true;
    phonenmea::Reader reader(maxSkewS);
    const int64_t now = nowUtcS < 0 ? -1 : nowUtcS + static_cast<int64_t>(millis() - started) / 1000;
    readMs += readStream(fd, reader, now, phonenmea::READ_WINDOW_MS - readMs);
    close(fd);
    result.port = port;
    result.heard = reader.heard();
    result.located = reader.located();
    result.fix = reader.fix();
    result.stats = reader.stats();
    result.why = reader.why();
    // Something spoke NMEA here: this is the phone, whether or not it had a fix. A port that
    // accepted and closed without NMEA lets the next one have what is left of the window.
    if (result.heard) break;
  }
  LOG_INF("GEO",
          "phone: port %u, %s, %u lines (%u bad, %u settling, %u stale, %u no-fix, %u weak, %u disagree), skew %ld s, "
          "%lu ms",
          static_cast<unsigned>(result.port), result.why, static_cast<unsigned>(result.stats.lines),
          static_cast<unsigned>(result.stats.badChecksum), static_cast<unsigned>(result.stats.settling),
          static_cast<unsigned>(result.stats.stale), static_cast<unsigned>(result.stats.noFix),
          static_cast<unsigned>(result.stats.weak), static_cast<unsigned>(result.stats.disagree),
          nowUtcS < 0 ? -1L : static_cast<long>(maxSkewS), millis() - started);
  return result;
}

#else

// X4 Pro only (Locate Me and its network live there).
Result read(int64_t, int64_t, const Target&) {
  Result result;
  result.why = "board";
  return result;
}

#endif

}  // namespace PhoneGps
