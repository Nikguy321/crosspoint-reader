#pragma once

#include <cstdint>
#include <string>

// Reading-position sync with a peer's hotspot (a WiPhone) and a home KOSync
// server, carried by the stock KOReader Sync path. This header is pure data and
// decisions so it builds on the host; BookSyncStore persists it and
// src/util/BookSyncHooks holds the device glue.

// What started a KOReader sync. A close lands where the user was going (home or
// the library); every other sync returns to the reader.
enum class BookSyncTrigger : uint8_t { Manual = 0, Open = 1, CloseToHome = 2, CloseToLibrary = 3 };

namespace BookSync {

constexpr char DEFAULT_PEER_SSID[] = "WiPhone-Books";
constexpr char DEFAULT_PEER_URL[] = "http://192.168.4.1";
constexpr size_t MAX_SSID_LENGTH = 32;
constexpr size_t MAX_URL_LENGTH = 128;

// "Wait for Wi-Fi" choices in seconds, persisted by index (append-only). 0 keeps
// the stock behaviour: no saved network in range opens the network list.
constexpr uint16_t WINDOW_SECONDS[] = {0, 60, 120, 300, 600};
constexpr uint8_t WINDOW_COUNT = sizeof(WINDOW_SECONDS) / sizeof(WINDOW_SECONDS[0]);
constexpr uint8_t DEFAULT_WINDOW_INDEX = 3;

// An automatic sync never parks on the network list (it keeps the device awake
// with nobody looking), and a sync on book open never holds the reader longer
// than this.
constexpr uint32_t AUTO_TRIGGER_WINDOW_MS = 20000;

struct Config {
  std::string peerSsid = DEFAULT_PEER_SSID;
  std::string peerUrl = DEFAULT_PEER_URL;
  uint8_t windowIndex = DEFAULT_WINDOW_INDEX;
  bool pushOnClose = false;
  bool pullOnOpen = false;
};

uint8_t sanitizeWindowIndex(uint8_t index);
uint32_t windowMs(uint8_t windowIndex);

// How long a sync started by `trigger` waits for a saved network before giving
// up; 0 means the stock network list.
uint32_t patientWindowMs(BookSyncTrigger trigger, uint8_t windowIndex);

bool isCloseTrigger(BookSyncTrigger trigger);

// The server URL the base URL is composed from: the peer URL while the station
// is joined to the peer SSID, `configured` everywhere else (and whenever either
// peer setting is empty). SSIDs compare exactly, as 802.11 does.
std::string chooseServerUrl(const std::string& connectedSsid, const Config& config, const std::string& configured);

}  // namespace BookSync
