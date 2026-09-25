#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <mutex>
#include <string>

#include "BookSyncConfig.h"

// Settings for syncing with a peer's hotspot and the automatic triggers, kept
// in their own file so a stock koreader.json is never touched. The username,
// password and home server stay in KOReaderCredentialStore. The peer hotspot's
// password is XOR-obfuscated with the device MAC, as KOReaderCredentialStore does.
class BookSyncStore : public PersistableStore<BookSyncStore> {
  BookSync::Config config;
  // The settings screen and the web API write while the reader and the sync
  // client read; the strings are copied out under this lock.
  mutable std::mutex configMutex;

  BookSyncStore() = default;
  ~BookSyncStore() = default;

  friend class PersistableStore<BookSyncStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/booksync.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  BookSync::Config getConfig() const;
  std::string getPeerSsid() const;
  std::string getPeerUrl() const;
  std::string getPeerPassword() const;
  uint8_t getWindowIndex() const;
  bool getPushOnClose() const;
  bool getPullOnOpen() const;

  void setPeerSsid(const std::string& ssid);
  void setPeerUrl(const std::string& url);
  void setPeerPassword(const std::string& password);
  void setWindowIndex(uint8_t index);
  void setPushOnClose(bool enabled);
  void setPullOnOpen(bool enabled);

  // KOReaderCredentialStore::getBaseUrl() composes from this: the peer URL while
  // the station is joined to the peer SSID, `configured` otherwise.
  std::string serverUrlForCurrentNetwork(const std::string& configured) const;
  // True while the station is joined to the peer SSID.
  bool onPeerNetwork() const;
};

#define BOOKSYNC_STORE BookSyncStore::getInstance()
