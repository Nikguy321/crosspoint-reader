#pragma once

#include <WiFi.h>

#include <cstdint>

// The one owner of every radio start (scripts/check_radio_power.py fails the
// build check on a start anywhere else). Each start takes HalPowerManager's
// radio lock BEFORE the radio comes up, so the CPU is at full clock and the idle
// loop cannot light-sleep for as long as a radio is initialised; the lock is
// released only once WiFi.getMode() reads WIFI_MODE_NULL (WIFI_OFF, off(), or a
// start that failed before the driver came up). A reboot ends it too, which is
// how every network activity leaves.
namespace RadioPower {

// WiFi.mode(). WIFI_OFF (WIFI_MODE_NULL) stops the radio, frees the driver and
// releases the lock.
bool mode(wifi_mode_t mode);

// WiFi.begin(): joins as a station (starting STA mode if needed).
wl_status_t begin(const char* ssid, const char* passphrase = nullptr);

// WiFi.softAP(): starts the access point (AP mode if needed).
bool softAP(const char* ssid, const char* passphrase, int channel, bool hidden, int maxConnections);

// WiFi.scanNetworks(): scans as a station (STA mode if needed).
int16_t scanNetworks(bool async);

// esp_wifi_stop(): RF off while a result screen is read. The driver stays
// initialised and the radio lock stays held (full clock, no light sleep) until
// the activity's teardown restart. Arduino still reads the mode as on, so a
// later start in the same boot would not restart the RF: stop() only on a path
// that ends in the restart.
void stop();

// Disconnect and WIFI_OFF (the deep-sleep path).
void off();

// A radio was started since boot: a network activity's exit restarts to clear
// the heap the driver fragmented, even after stop() turned the radio off.
bool ranThisBoot();

}  // namespace RadioPower
