#include "GeolocateClient.h"

#include <BoardConfig.h>
#include <Logging.h>

#if FREEINK_DEVICE_X4PRO
#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>

#include <cstring>
#endif

namespace GeolocateClient {

#if FREEINK_DEVICE_X4PRO
namespace {

// beaconDB asks every client to identify itself.
constexpr const char* USER_AGENT =
    "CrossPoint-X4Pro/" CROSSPOINT_VERSION " (+https://github.com/Nikguy321/crosspoint-reader)";

// What is left of the request's budget, at least 1 ms (0 = spent).
int remainingMs(const unsigned long started) {
  const unsigned long elapsed = millis() - started;
  return elapsed >= REQUEST_TIMEOUT_MS ? 0 : static_cast<int>(REQUEST_TIMEOUT_MS - elapsed);
}

struct ClientGuard {
  esp_http_client_handle_t client;
  ~ClientGuard() {
    if (client) esp_http_client_cleanup(client);  // closes the connection too
  }
};

}  // namespace

Result request(const char* url, const char* jsonBody, char* out, const size_t cap, size_t& length, int& status) {
  length = 0;
  status = 0;
  if (url == nullptr || out == nullptr || cap < 2) return Result::Transport;
  out[0] = '\0';

  const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (freeInternal < MIN_INTERNAL_FREE || largestInternal < MIN_INTERNAL_BLOCK) {
    LOG_ERR("GEO", "Not enough internal heap for TLS: %u free, %u largest", static_cast<unsigned>(freeInternal),
            static_cast<unsigned>(largestInternal));
    return Result::NoMemory;
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.method = jsonBody ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  config.timeout_ms = static_cast<int>(CONNECT_TIMEOUT_MS);
  // Verify the chain against the bundled roots; esp-tls checks the host name against the URL's.
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.user_agent = USER_AGENT;
  config.disable_auto_redirect = true;  // both endpoints answer directly; a redirect is a failure
  config.keep_alive_enable = false;
  config.buffer_size = 1024;  // response headers
  config.buffer_size_tx = 1024;

  ClientGuard guard{esp_http_client_init(&config)};
  if (!guard.client) {
    LOG_ERR("GEO", "esp_http_client_init failed");
    return Result::NoMemory;
  }
  esp_http_client_handle_t client = guard.client;
  esp_http_client_set_header(client, "Accept", "application/json");
  const size_t bodyLength = jsonBody ? std::strlen(jsonBody) : 0;
  if (jsonBody) esp_http_client_set_header(client, "Content-Type", "application/json");

  const unsigned long started = millis();
  esp_err_t err = esp_http_client_open(client, static_cast<int>(bodyLength));
  if (err != ESP_OK) {
    // Which step failed (DNS, TCP, TLS or the certificate) and after how long; no host or address.
    int tlsCode = 0;
    int certFlags = 0;
    const esp_err_t tlsErr = esp_http_client_get_and_clear_last_tls_error(client, &tlsCode, &certFlags);
    LOG_ERR("GEO", "Connect failed after %lu ms: %s, tls %s (mbedtls -0x%x, cert flags 0x%x), errno %d",
            millis() - started, esp_err_to_name(err), esp_err_to_name(tlsErr), static_cast<unsigned>(-tlsCode),
            static_cast<unsigned>(certFlags), esp_http_client_get_errno(client));
    return Result::Transport;
  }

  size_t sent = 0;
  while (sent < bodyLength) {
    const int left = remainingMs(started);
    if (left == 0) {
      LOG_ERR("GEO", "Request timed out while sending");
      return Result::Transport;
    }
    esp_http_client_set_timeout_ms(client, left);
    const int n = esp_http_client_write(client, jsonBody + sent, static_cast<int>(bodyLength - sent));
    if (n <= 0) {
      LOG_ERR("GEO", "Send failed");
      return Result::Transport;
    }
    sent += static_cast<size_t>(n);
  }

  int left = remainingMs(started);
  if (left == 0) return Result::Transport;
  esp_http_client_set_timeout_ms(client, left);
  const int64_t contentLength = esp_http_client_fetch_headers(client);
  if (contentLength < 0) {
    LOG_ERR("GEO", "No response headers");
    return Result::Transport;
  }
  status = esp_http_client_get_status_code(client);
  if (contentLength > static_cast<int64_t>(cap - 1)) {
    LOG_ERR("GEO", "Response too large: %lld bytes", static_cast<long long>(contentLength));
    return Result::TooLarge;
  }

  for (;;) {
    left = remainingMs(started);
    if (left == 0) {
      LOG_ERR("GEO", "Request timed out while reading");
      return Result::Transport;
    }
    esp_http_client_set_timeout_ms(client, left);
    if (length >= cap - 1) {
      // Full: one more byte means the body does not fit.
      char probe;
      const int extra = esp_http_client_read(client, &probe, 1);
      if (extra > 0) {
        LOG_ERR("GEO", "Response larger than %u bytes", static_cast<unsigned>(cap - 1));
        return Result::TooLarge;
      }
      if (extra < 0) return Result::Transport;
      break;
    }
    const int n = esp_http_client_read(client, out + length, static_cast<int>(cap - 1 - length));
    if (n < 0) {
      LOG_ERR("GEO", "Read failed");
      return Result::Transport;
    }
    if (n == 0) break;
    length += static_cast<size_t>(n);
  }
  out[length] = '\0';
  if (!esp_http_client_is_complete_data_received(client)) {
    LOG_ERR("GEO", "Response cut off after %u bytes", static_cast<unsigned>(length));
    return Result::Transport;
  }
  LOG_DBG("GEO", "HTTP %d, %u bytes in %lu ms", status, static_cast<unsigned>(length), millis() - started);
  return Result::Ok;
}

#else

// Locate Me is X4 Pro only: other boards link no TLS stack or certificate bundle for it.
Result request(const char*, const char*, char* out, const size_t cap, size_t& length, int& status) {
  length = 0;
  status = 0;
  if (out != nullptr && cap > 0) out[0] = '\0';
  LOG_ERR("GEO", "Location lookup is X4 Pro only");
  return Result::Transport;
}

#endif

}  // namespace GeolocateClient
