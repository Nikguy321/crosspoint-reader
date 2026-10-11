#include "AiHttps.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <gemini.h>

#if FREEINK_DEVICE_X4PRO
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_tls_errors.h>

#include <algorithm>
#include <climits>

#include "AiRoots.h"
#include "GeolocateClient.h"
#endif

namespace AiHttps {

#if FREEINK_DEVICE_X4PRO
namespace {

// Response headers (Google's run to ~1 KB) and the request's (the key header and the user agent).
constexpr int RX_BUFFER = 2048;
constexpr int TX_BUFFER = 1024;
constexpr size_t READ_CHUNK = 4096;

// The caller's tick, remembered once it says stop.
struct Ticker {
  const Request* q;
  bool cancelled;
};

bool tick(void* p) {
  auto* t = static_cast<Ticker*>(p);
  if (!t->cancelled && t->q->tick != nullptr && !t->q->tick(t->q->ctx)) t->cancelled = true;
  return !t->cancelled;
}

// What is left of the request's totalMs (UINT32_MAX without one; 0 = spent).
uint32_t left(const Request& q, const unsigned long t0) {
  if (q.totalMs == 0) return UINT32_MAX;
  const unsigned long used = millis() - t0;
  return used >= q.totalMs ? 0 : static_cast<uint32_t>(q.totalMs - used);
}

// Why the connect or the handshake failed, in the ladder's words.
int netOfOpen(esp_http_client_handle_t client, const esp_err_t err) {
  int tlsCode = 0;
  int certFlags = 0;
  const esp_err_t tlsErr = esp_http_client_get_and_clear_last_tls_error(client, &tlsCode, &certFlags);
  LOG_ERR("AI", "Connect failed: %s, tls %s (mbedtls -0x%x, cert flags 0x%x)", esp_err_to_name(err),
          esp_err_to_name(tlsErr), static_cast<unsigned>(-tlsCode), static_cast<unsigned>(certFlags));
  if (certFlags != 0 || tlsCode == -0x2700) return GEM_NET_TLS;  // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
  if (tlsErr == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) return GEM_NET_DNS;
  if (err == ESP_ERR_NO_MEM || tlsErr == ESP_ERR_NO_MEM || tlsCode == -0x7F00)
    return GEM_NET_NOMEM;  // SSL_ALLOC_FAILED
  if (tlsCode != 0) return GEM_NET_TLS_OTHER;
  return GEM_NET_CONNECT;
}

struct ClientGuard {
  esp_http_client_handle_t client;
  ~ClientGuard() {
    if (client) esp_http_client_cleanup(client);  // closes the connection too
  }
};

}  // namespace

int request(const Request& q, Response& r) {
  r = Response{};
  const unsigned long t0 = millis();
  const char* what = q.roots == Roots::Google ? "gemini" : "wikipedia";
  Ticker ticker{&q, false};
  const auto done = [&](const int status, const int net) {
    r.status = status;
    r.net = net;
    r.cancelled = ticker.cancelled;
    r.ms = millis() - t0;
    if (status > 0) {
      LOG_INF("AI", "%s: HTTP %d, %u bytes, first byte after %lu ms, %lu ms in all", what, status,
              static_cast<unsigned>(r.length), static_cast<unsigned long>(r.waitMs), static_cast<unsigned long>(r.ms));
    } else {
      LOG_INF("AI", "%s: no answer (net %d%s) after %lu ms", what, net, ticker.cancelled ? ", cancelled" : "",
              static_cast<unsigned long>(r.ms));
    }
    return status;
  };
  if (q.url == nullptr || q.host == nullptr || q.sink == nullptr || q.cap < 2) return done(0, GEM_NET_NOMEM);
  q.sink[0] = '\0';
  // The TLS session's internal heap first (GeolocateClient's bar): a handshake started under it fails
  // as a generic TLS error.
  if (!GeolocateClient::enoughHeap()) {
    LOG_ERR("AI", "Not enough internal heap for TLS: %u free, %u largest",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return done(0, GEM_NET_NOMEM);
  }

  // The name, bounded (the resolver alone retries for many seconds on a network with no internet).
  const uint32_t dnsMs = std::min(q.dnsMs, left(q, t0));
  if (!GeolocateClient::resolveWithin(q.host, dnsMs, &tick, &ticker)) {
    return done(0, ticker.cancelled ? GEM_NET_LOST : (left(q, t0) == 0 ? GEM_NET_TIMEOUT : GEM_NET_DNS));
  }

  esp_http_client_config_t config = {};
  config.url = q.url;
  config.method = q.body ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  config.timeout_ms = static_cast<int>(std::max<uint32_t>(1, std::min(q.connectMs, left(q, t0))));
  config.cert_pem = q.roots == Roots::Google ? GOOGLE_ROOTS : WIKIPEDIA_ROOTS;
  config.user_agent = q.userAgent;
  config.disable_auto_redirect = true;  // both hosts answer directly; a redirect is a failure
  config.keep_alive_enable = false;
  config.buffer_size = RX_BUFFER;
  config.buffer_size_tx = TX_BUFFER;
  ClientGuard guard{esp_http_client_init(&config)};
  if (!guard.client) {
    LOG_ERR("AI", "esp_http_client_init failed");
    return done(0, GEM_NET_NOMEM);
  }
  esp_http_client_handle_t client = guard.client;
  esp_http_client_set_header(client, "Accept", "application/json");
  if (q.body) esp_http_client_set_header(client, "Content-Type", "application/json");
  if (q.apiKey) esp_http_client_set_header(client, "x-goog-api-key", q.apiKey);

  const esp_err_t err = esp_http_client_open(client, q.body ? static_cast<int>(q.bodyLength) : 0);
  if (err != ESP_OK) return done(0, netOfOpen(client, err));
  if (!tick(&ticker)) return done(0, GEM_NET_LOST);

  size_t sent = 0;
  while (q.body && sent < q.bodyLength) {
    const uint32_t rest = left(q, t0);
    if (rest == 0) return done(0, GEM_NET_TIMEOUT);
    esp_http_client_set_timeout_ms(client, static_cast<int>(std::min(q.connectMs, rest)));
    const int n = esp_http_client_write(client, q.body + sent, static_cast<int>(q.bodyLength - sent));
    if (n <= 0) return done(0, GEM_NET_LOST);
    sent += static_cast<size_t>(n);
  }

  // The answer's headers: a non-streamed answer's come only once the whole answer is written.
  esp_http_client_set_timeout_ms(client, static_cast<int>(SLICE_MS));
  const unsigned long sentAt = millis();
  int64_t contentLength = 0;
  for (;;) {
    contentLength = esp_http_client_fetch_headers(client);
    if (contentLength != -ESP_ERR_HTTP_EAGAIN) break;
    if (!tick(&ticker)) return done(0, GEM_NET_LOST);
    if (millis() - sentAt >= q.firstByteMs || left(q, t0) == 0) return done(0, GEM_NET_TIMEOUT);
  }
  r.waitMs = millis() - sentAt;
  const int status = esp_http_client_get_status_code(client);
  if (contentLength < 0 || status <= 0) return done(0, GEM_NET_LOST);

  // The body, to its end: what passes the sink is read and dropped (and the answer is "too big").
  size_t length = 0;
  bool overflow = false;
  char drain[256];
  unsigned long lastData = millis();
  for (;;) {
    if (!tick(&ticker)) return done(0, GEM_NET_LOST);
    if (left(q, t0) == 0) return done(0, GEM_NET_TIMEOUT);
    const bool room = length < q.cap - 1;
    char* to = room ? q.sink + length : drain;
    const size_t want = room ? std::min(q.cap - 1 - length, READ_CHUNK) : sizeof(drain);
    const int n = esp_http_client_read(client, to, static_cast<int>(want));
    if (n == -ESP_ERR_HTTP_EAGAIN) {
      if (millis() - lastData > q.bodyIdleMs) return done(0, GEM_NET_TIMEOUT);
      continue;
    }
    if (n < 0) return done(0, GEM_NET_LOST);
    if (n == 0) break;
    lastData = millis();
    if (room) {
      length += static_cast<size_t>(n);
    } else {
      overflow = true;
    }
  }
  q.sink[length] = '\0';
  r.length = length;
  if (!esp_http_client_is_complete_data_received(client)) return done(0, GEM_NET_LOST);
  if (overflow) return done(0, GEM_NET_TOO_BIG);
  return done(status, GEM_NET_OK);
}

#else

// Apps > AI is X4 Pro only: other boards link no TLS stack for it.
int request(const Request&, Response& r) {
  r = Response{};
  r.net = GEM_NET_CONNECT;
  return 0;
}

#endif

}  // namespace AiHttps
