#include "RadioPower.h"

#include <HalPowerManager.h>
#include <Logging.h>
#include <esp_wifi.h>

namespace RadioPower {
namespace {
bool ran = false;

// Before any call that can bring a radio up.
void lockForStart() {
  powerManager.acquireRadioLock();
  ran = true;
}

// After any call that may have left the radio down (WIFI_OFF, or a start that
// failed before the driver came up): release only once WiFi reads off. The one
// place the radio lock is released.
void releaseIfOff() {
  if (WiFi.getMode() == WIFI_MODE_NULL) powerManager.releaseRadioLock();
}
}  // namespace

bool mode(const wifi_mode_t m) {
  if (m == WIFI_MODE_NULL) {
    const bool ok = WiFi.mode(WIFI_OFF);
    releaseIfOff();
    return ok;
  }
  lockForStart();
  const bool ok = WiFi.mode(m);
  releaseIfOff();
  return ok;
}

wl_status_t begin(const char* ssid, const char* passphrase, const int32_t channel, const uint8_t* bssid) {
  lockForStart();
  const wl_status_t status = WiFi.begin(ssid, passphrase, channel, bssid);
  releaseIfOff();
  return status;
}

bool softAP(const char* ssid, const char* passphrase, const int channel, const bool hidden, const int maxConnections) {
  lockForStart();
  const bool ok = WiFi.softAP(ssid, passphrase, channel, hidden, maxConnections);
  releaseIfOff();
  return ok;
}

int16_t scanNetworks(const bool async, const bool showHidden) {
  lockForStart();
  const int16_t result = WiFi.scanNetworks(async, showHidden);
  releaseIfOff();
  return result;
}

void stop() {
  // RF off only: the driver stays initialised (Arduino still reads the mode as
  // on), so the radio lock stays held until WIFI_OFF, off() or the restart.
  const esp_err_t err = esp_wifi_stop();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
    LOG_ERR("RADIO", "esp_wifi_stop failed: %d", static_cast<int>(err));
  }
}

void off() {
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  releaseIfOff();
}

bool ranThisBoot() { return ran; }

}  // namespace RadioPower
