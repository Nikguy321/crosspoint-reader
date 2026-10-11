#pragma once

#include <cstddef>
#include <cstdint>

// Apps > AI's wire (X4 Pro): one HTTPS request on a station already joined, for Gemini's answer and
// the Wikipedia lookup before it (network/AiRun). It never starts the radio.
//
// esp_http_client, like GeolocateClient, but each host's chain is checked against its own PINNED
// roots (cert_pem; the host name is checked too), never the whole bundle: Gemini's request carries
// the owner's key, and only Google's roots may see it. The roots, verified 2026-10-11 against the
// live chains with openssl (only these PEMs trusted: "Verify return code: 0"), are in AiHttps.cpp.
//
// Every wait - the name lookup, Gemini's up to 150 s before its first byte (free-tier Flash takes
// 30-90 s), the body - is cut into SLICE_MS slices; tick() runs between them, so the loop task can
// keep the watchdog fed, read the Back key and repaint, and a false from it (a Cancel) ends the
// request at once. The TCP connect and the TLS handshake are one blocking step (connectMs).
// Failures come back in gemini.h's words (GEM_NET_*), which the ladder's rules read.
namespace AiHttps {

constexpr uint32_t SLICE_MS = 200;

enum class Roots : uint8_t {
  Google,     // GTS Root R1 + R4 (generativelanguage.googleapis.com)
  Wikipedia,  // ISRG Root X1 + X2 + Root YE (en.wikipedia.org)
};

struct Request {
  const char* url = nullptr;
  const char* host = nullptr;  // the URL's host, for the bounded name lookup
  Roots roots = Roots::Google;
  const char* body = nullptr;  // POST with this JSON body; nullptr: GET
  size_t bodyLength = 0;
  // ⚠ The x-goog-api-key header's value, or nullptr. A header, never the URL; never logged.
  const char* apiKey = nullptr;
  const char* userAgent = nullptr;
  uint32_t dnsMs = 10000;         // the name lookup
  uint32_t connectMs = 10000;     // TCP connect and the TLS handshake
  uint32_t firstByteMs = 150000;  // after the request went: the wait for the response headers
  uint32_t bodyIdleMs = 30000;    // a body that stops this long: a timeout
  uint32_t totalMs = 0;           // > 0: the whole request, the name lookup included
  // The body lands here (PSRAM), NUL-terminated; a longer one is read to its end and flagged.
  char* sink = nullptr;
  size_t cap = 0;
  // Between slices (at least every SLICE_MS while waiting): false = cancelled.
  bool (*tick)(void* ctx) = nullptr;
  void* ctx = nullptr;
};

struct Response {
  int status = 0;  // the HTTP status; 0 = no answer (net says why)
  int net = 0;     // GEM_NET_*: GEM_NET_OK with a status; GEM_NET_TOO_BIG when the body passed cap
  bool cancelled = false;
  size_t length = 0;    // body bytes in the sink
  uint32_t ms = 0;      // the whole request
  uint32_t waitMs = 0;  // from the request sent to the response headers
};

// One request. Returns status (> 0) or 0 with net set.
int request(const Request& request, Response& response);

}  // namespace AiHttps
