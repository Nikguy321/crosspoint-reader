#include "StationKeeper.h"

#include <Arduino.h>
#include <BookSyncStore.h>
#include <HalClock.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "activities/RenderLock.h"
#include "network/AutoLocate.h"
#include "network/RadioPower.h"
#include "network/WifiJoinOrder.h"

namespace StationKeeper {
namespace {

constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
constexpr uint32_t JOIN_TIMEOUT_MS = 10000;
// A link that reads down this long is lost (the driver's own reconnect gets the time first).
constexpr uint32_t LINK_GRACE_MS = 15000;
constexpr uint32_t SHORT_WAIT_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t LONG_WAIT_MS = 10UL * 60UL * 1000UL;
constexpr uint8_t ROUNDS_BEFORE_LONG_WAIT = 3;
constexpr uint32_t NTP_EVERY_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t NTP_RETRY_MS = 10UL * 60UL * 1000UL;
// Saved-network sightings kept from one scan (several access points of one network each count).
constexpr size_t MAX_SIGHTINGS = 16;

enum class State : uint8_t { Off, Start, NoNetworks, Scanning, Joining, Online, Waiting };

struct Sighting {
  char ssid[33] = "";
  int32_t rssi = 0;
  int32_t channel = 0;  // 0: join by name only (a hidden network)
  uint8_t bssid[6] = {};
};

// Static rather than heap: the keeper runs for hours beside the Wi-Fi driver's own buffers.
Sighting sightings[MAX_SIGHTINGS];
uint16_t joinOrder[wifi_join::MAX_CANDIDATES + 1];  // + a hidden last network
size_t candidateCount = 0;
size_t candidateNext = 0;
Sighting hiddenLast;

State state = State::Off;
uint32_t stateSince = 0;
uint32_t waitUntil = 0;
uint8_t failedRounds = 0;
uint32_t linkDownSince = 0;
bool linkDown = false;
bool ntpEver = false;
uint32_t nextNtpAt = 0;
bool ntpDue = false;
bool locateDue = false;
char joining[33] = "";

void enter(const State next) {
  state = next;
  stateSince = millis();
}

// As WifiSelectionActivity: routers show "CrossPoint-Reader-AABBCCDDEEFF".
void setHostname() {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return;
  char hostname[sizeof("CrossPoint-Reader-") + 12];
  snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
  WiFi.setHostname(hostname);
}

void wait(const char* why) {
  failedRounds++;
  const uint32_t ms = failedRounds >= ROUNDS_BEFORE_LONG_WAIT ? LONG_WAIT_MS : SHORT_WAIT_MS;
  LOG_INF("KEEP", "%s; scanning again in %lu s", why, static_cast<unsigned long>(ms / 1000));
  WiFi.disconnect();
  waitUntil = millis() + ms;
  enter(State::Waiting);
}

void startScan() {
  RadioPower::mode(WIFI_STA);
  WiFi.disconnect();
  RadioPower::scanNetworks(true, /*showHidden=*/true);
  enter(State::Scanning);
}

const Sighting& candidate(const size_t i) {
  return joinOrder[i] < MAX_SIGHTINGS ? sightings[joinOrder[i]] : hiddenLast;
}

// The next candidate of this round, or wait when none is left.
void joinNext() {
  while (candidateNext < candidateCount) {
    const Sighting& c = candidate(candidateNext++);
    const auto cred = WIFI_STORE.findCredential(c.ssid);
    if (!cred) continue;
    snprintf(joining, sizeof(joining), "%s", c.ssid);
    // The join sequence of WifiSelectionActivity::attemptConnection(), except that the driver
    // stays up: that activity always ends in a restart, while a live session can retry for days,
    // and a deinit/init per attempt would fragment the heap. RadioPower::off() ends it.
    WiFi.persistent(false);
    RadioPower::mode(WIFI_STA);
    WiFi.disconnect(false, true);
    delay(100);
    WiFi.setScanMethod(c.channel > 0 ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    setHostname();
    RadioPower::begin(c.ssid, cred->password.empty() ? nullptr : cred->password.c_str(), c.channel,
                      c.channel > 0 ? c.bssid : nullptr);
    enter(State::Joining);
    return;
  }
  wait("no saved network joined");
}

void takeScan() {
  const int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) {
    if (millis() - stateSince > SCAN_TIMEOUT_MS) {
      WiFi.scanDelete();
      wait("scan timed out");
    }
    return;
  }
  if (found < 0) {
    wait("scan failed");
    return;
  }

  // The saved networks in view (each sighting), and whether anything hid its name.
  size_t count = 0;
  bool hiddenSeen = false;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  bool lastSeen = false;
  for (int16_t i = 0; i < found && count < MAX_SIGHTINGS; i++) {
    Sighting& s = sightings[count];
    snprintf(s.ssid, sizeof(s.ssid), "%s", WiFi.SSID(i).c_str());
    if (s.ssid[0] == '\0') {
      hiddenSeen = true;
      continue;
    }
    if (!WIFI_STORE.hasSavedCredential(s.ssid)) continue;
    lastSeen = lastSeen || last == s.ssid;
    s.rssi = WiFi.RSSI(i);
    s.channel = WiFi.channel(i);
    if (const uint8_t* bssid = WiFi.BSSID(i)) memcpy(s.bssid, bssid, sizeof(s.bssid));
    count++;
  }
  WiFi.scanDelete();

  wifi_join::Seen seen[MAX_SIGHTINGS];
  for (size_t i = 0; i < count; i++) seen[i] = {sightings[i].ssid, sightings[i].rssi, true};
  // No internet behind the book-sync peer's or hub's hotspot (and the phone's is not to be held).
  const BookSync::Config sync = BOOKSYNC_STORE.getConfig();
  const std::string_view excluded[] = {sync.peerSsid, sync.hubSsid};
  candidateCount =
      wifi_join::order(seen, count, last, excluded, std::size(excluded), joinOrder, wifi_join::MAX_CANDIDATES);
  const bool lastExcluded = last == sync.peerSsid || last == sync.hubSsid;
  if (!lastExcluded &&
      wifi_join::tryHiddenLast(!last.empty() && WIFI_STORE.hasSavedCredential(last), lastSeen, hiddenSeen)) {
    snprintf(hiddenLast.ssid, sizeof(hiddenLast.ssid), "%s", last.c_str());
    hiddenLast.channel = 0;
    joinOrder[candidateCount++] = MAX_SIGHTINGS;
  }
  candidateNext = 0;
  LOG_INF("KEEP", "scan: %d networks, %u saved to try", static_cast<int>(found), static_cast<unsigned>(candidateCount));
  joinNext();
}

void pollJoin() {
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    failedRounds = 0;
    linkDown = false;
    LOG_INF("KEEP", "joined, %d dBm", static_cast<int>(WiFi.RSSI()));
    {
      // The SD card shares SPI with the panel.
      RenderLock lock;
      WIFI_STORE.setLastConnectedSsid(joining);
    }
    ntpDue = !ntpEver || static_cast<int32_t>(millis() - nextNtpAt) >= 0;
    enter(State::Online);
    return;
  }
  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL || millis() - stateSince > JOIN_TIMEOUT_MS) {
    LOG_INF("KEEP", "join failed (status %d)", static_cast<int>(status));
    WiFi.disconnect();
    joinNext();
  }
}

void syncClock() {
  ntpDue = false;
  if (!halClock.isAvailable()) return;
  if (!halClock.syncFromNTP()) {
    LOG_INF("KEEP", "clock sync failed");
    nextNtpAt = millis() + NTP_RETRY_MS;
    return;
  }
  ntpEver = true;
  nextNtpAt = millis() + NTP_EVERY_MS;
  LOG_INF("KEEP", "clock synced");
  if (!SETTINGS.clockHasBeenSynced) {
    SETTINGS.clockHasBeenSynced = 1;
    SETTINGS.saveToFile();
  }
  // Update Location When Syncing treats this like "Sync clock now" (its own setting, gates and
  // once-a-day rule decide).
  AutoLocate::rearm();
  locateDue = true;
}

void watchLink() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (!linkDown) {
      linkDown = true;
      linkDownSince = now;
    } else if (now - linkDownSince > LINK_GRACE_MS) {
      linkDown = false;
      wait("link lost");
    }
    return;
  }
  linkDown = false;
  if (ntpDue) {
    syncClock();
    return;
  }
  if (locateDue) {
    locateDue = false;
    if (AutoLocate::due(true)) AutoLocate::run();
    return;
  }
  if (static_cast<int32_t>(now - nextNtpAt) >= 0) ntpDue = true;
}

}  // namespace

void start() {
  {
    RenderLock lock;
    WIFI_STORE.loadFromFile();
  }
  failedRounds = 0;
  linkDown = false;
  locateDue = false;
  if (WIFI_STORE.getCredentialCount() == 0) {
    LOG_INF("KEEP", "no saved Wi-Fi network: the radio stays off");
    enter(State::NoNetworks);
    return;
  }
  enter(State::Start);
}

void tick() {
  switch (state) {
    case State::Start:
      startScan();
      return;
    case State::Scanning:
      takeScan();
      return;
    case State::Joining:
      pollJoin();
      return;
    case State::Online:
      watchLink();
      return;
    case State::Waiting:
      if (static_cast<int32_t>(millis() - waitUntil) >= 0) startScan();
      return;
    case State::Off:
    case State::NoNetworks:
      return;
  }
}

void stop() { enter(State::Off); }

const char* stateName() {
  switch (state) {
    case State::NoNetworks:
      return "none";
    case State::Start:
    case State::Scanning:
      return "scan";
    case State::Joining:
      return "join";
    case State::Online:
      return "up";
    case State::Waiting:
      return "wait";
    case State::Off:
    default:
      return "off";
  }
}

}  // namespace StationKeeper
