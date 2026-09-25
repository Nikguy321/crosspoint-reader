#pragma once

#include <cstdint>
#include <optional>
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
// A WPA2 passphrase is 8-63 characters, a raw PSK 64 hex digits (the Wi-Fi store's limit).
constexpr size_t MAX_PASSWORD_LENGTH = 64;

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
  std::string peerPassword;  // the peer hotspot's WPA2 password; empty = an open hotspot
  // A second fixed server by SSID, empty = off: a hub's own hotspot, e.g. COVEY's
  // game hotspot "COVEY" at http://192.168.89.1:8088. Joined from the Wi-Fi list.
  std::string hubSsid;
  std::string hubUrl;
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
// is joined to the peer SSID, the hub URL while joined to the hub SSID (the peer
// wins if both name the same network), `configured` everywhere else (and
// whenever a pair has an empty half). SSIDs compare exactly, as 802.11 does.
std::string chooseServerUrl(const std::string& connectedSsid, const Config& config, const std::string& configured);

// The plaintext Basic header stays off the air: on the peer's hotspot (open
// unless given a password), and while the hub's server is in use (it
// authenticates on x-auth-key alone, as the peer's does).
bool onDeviceNetwork(const std::string& connectedSsid, const Config& config);

// What wrote the peer network to the Wi-Fi list: a sync starting, or the user
// saving the Peer Wi-Fi Name or Peer Wi-Fi Password row.
enum class PeerWrite : uint8_t { SyncStart, NameSaved, PasswordSaved };

// The password to write to the Wi-Fi store for the peer SSID, or nullopt when
// nothing needs writing. `saved` is what the store holds for that SSID (nullopt
// when it holds nothing). A sync start or a saved peer name only adds a missing
// entry, so a password saved from the Wi-Fi list survives pointing the peer at
// that network; only saving the peer password overwrites the entry with it.
// Empty means an open network.
std::optional<std::string> peerCredentialToWrite(const Config& config, const std::optional<std::string>& saved,
                                                 PeerWrite cause);

// Whether the in-memory Wi-Fi list may be written back: it loaded, or there was
// no file to load. Saving after a failed read would replace every saved network.
bool wifiListWritable(bool loaded, bool fileExists);

// The patient connect joins the peer with the configured password when it is in
// range but not in the Wi-Fi list (the list holds at most 8 networks), once per
// scan.
bool joinPeerDirectly(const Config& config, bool peerVisible, bool peerInWifiList, bool alreadyTried);

// The peer hotspot was in the last scan, but the saved entry cannot join it: a
// protected hotspot saved without a password, or an open one saved with a
// password (a password sets a WPA2 minimum, so an open network is never joined).
bool peerPasswordMismatch(bool peerVisible, bool peerEncrypted, const std::optional<std::string>& saved);

}  // namespace BookSync
