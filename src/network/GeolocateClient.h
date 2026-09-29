#pragma once

#include <cstddef>
#include <cstdint>

#include "GeolocateProtocol.h"

// The HTTPS half of "Locate me": one bounded request to a location service. X4 Pro only; on other
// boards request() refuses (Transport) and no TLS code is linked for it.
//
// Runs on esp_http_client with the ESP-IDF certificate bundle (esp_crt_bundle_attach, the
// Mozilla root set compiled into the framework): the server's chain AND its host name are
// verified. The fork's wolfSSL client (SecureHttpClient) is not used here: it has no CA bundle
// wired up and checks no host name, so on it a request is either unverified (setInsecure) or
// pinned to a root that any site under that CA would pass. The two services speak TLS 1.2, which
// the framework's mbedTLS supports.
namespace GeolocateClient {

// esp_http_client's per-operation timeout: the TCP connect and each blocking TLS read during the
// handshake.
constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
// From the connect onwards: each later socket operation gets what is left of it.
//
// Neither is a hard ceiling on one request. DNS runs before either (lwIP's own retries, several
// seconds per server when the resolver does not answer) and the handshake can wait up to the
// connect timeout per read, so a network that does not reach the internet can hold one request
// for ~30 s and the two lookups for about a minute. It blocks the loop task meanwhile; the screen
// says so.
constexpr uint32_t REQUEST_TIMEOUT_MS = 15000;
// mbedTLS allocates from internal RAM here (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC): two ~16.7 KB
// record buffers plus the handshake. Below this the request is not started.
constexpr uint32_t MIN_INTERNAL_FREE = 56000;
constexpr uint32_t MIN_INTERNAL_BLOCK = 20000;

enum class Result : uint8_t {
  Ok,         // a complete response (any HTTP status) is in out
  NoMemory,   // not enough internal heap for a TLS session
  Transport,  // DNS, connect, TLS (incl. certificate), timeout or a cut-off body
  TooLarge,   // the body is longer than the buffer
};

struct Options {
  uint32_t connectTimeoutMs = CONNECT_TIMEOUT_MS;
  uint32_t requestTimeoutMs = REQUEST_TIMEOUT_MS;
  // No error lines: a caller in the background (AutoLocate) logs its own one-line outcome.
  bool quiet = false;
};

// A blocking station scan on the joined radio (through RadioPower, which never starts it here):
// up to cap access points into out; returns how many, found = how many the scan saw. The scan's
// own results are freed before it returns.
size_t scanAccessPoints(geolocate::AccessPoint* out, size_t cap, int16_t& found);

// Enough internal heap for one request (the check request() starts with): a caller can test it
// before the work that leads up to a request (AutoLocate's scan).
bool enoughHeap();

// POST jsonBody (when not nullptr) or GET url. The body lands in out, NUL-terminated (cap bytes
// including the terminator); status is the HTTP status code.
Result request(const char* url, const char* jsonBody, char* out, size_t cap, size_t& length, int& status,
               const Options& options = Options{});

}  // namespace GeolocateClient
