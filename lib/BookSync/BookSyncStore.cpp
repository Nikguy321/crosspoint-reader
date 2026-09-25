#include "BookSyncStore.h"

#include <Logging.h>
#include <WiFi.h>

namespace {
std::string connectedSsid() {
  if (WiFi.status() != WL_CONNECTED) return {};
  return WiFi.SSID().c_str();
}
}  // namespace

void BookSyncStore::toJson(JsonDocument& doc) const {
  const BookSync::Config c = getConfig();
  doc["peerSsid"] = c.peerSsid;
  doc["peerUrl"] = c.peerUrl;
  doc["windowIndex"] = c.windowIndex;
  doc["pushOnClose"] = c.pushOnClose;
  doc["pullOnOpen"] = c.pullOnOpen;
}

bool BookSyncStore::fromJson(JsonVariantConst doc) {
  // Missing keys keep the defaults; an empty peer SSID is a saved choice (peer sync off).
  const BookSync::Config defaults;
  std::lock_guard<std::mutex> lock(configMutex);
  config.peerSsid = doc["peerSsid"] | defaults.peerSsid.c_str();
  config.peerUrl = doc["peerUrl"] | defaults.peerUrl.c_str();
  config.windowIndex = BookSync::sanitizeWindowIndex(doc["windowIndex"] | defaults.windowIndex);
  config.pushOnClose = doc["pushOnClose"] | defaults.pushOnClose;
  config.pullOnOpen = doc["pullOnOpen"] | defaults.pullOnOpen;
  return true;
}

BookSync::Config BookSyncStore::getConfig() const {
  std::lock_guard<std::mutex> lock(configMutex);
  return config;
}

std::string BookSyncStore::getPeerSsid() const {
  std::lock_guard<std::mutex> lock(configMutex);
  return config.peerSsid;
}

std::string BookSyncStore::getPeerUrl() const {
  std::lock_guard<std::mutex> lock(configMutex);
  return config.peerUrl;
}

uint8_t BookSyncStore::getWindowIndex() const {
  std::lock_guard<std::mutex> lock(configMutex);
  return config.windowIndex;
}

bool BookSyncStore::getPushOnClose() const {
  std::lock_guard<std::mutex> lock(configMutex);
  return config.pushOnClose;
}

bool BookSyncStore::getPullOnOpen() const {
  std::lock_guard<std::mutex> lock(configMutex);
  return config.pullOnOpen;
}

void BookSyncStore::setPeerSsid(const std::string& ssid) {
  std::lock_guard<std::mutex> lock(configMutex);
  config.peerSsid = ssid.substr(0, BookSync::MAX_SSID_LENGTH);
}

void BookSyncStore::setPeerUrl(const std::string& url) {
  std::lock_guard<std::mutex> lock(configMutex);
  config.peerUrl = url.substr(0, BookSync::MAX_URL_LENGTH);
}

void BookSyncStore::setWindowIndex(const uint8_t index) {
  std::lock_guard<std::mutex> lock(configMutex);
  config.windowIndex = BookSync::sanitizeWindowIndex(index);
}

void BookSyncStore::setPushOnClose(const bool enabled) {
  std::lock_guard<std::mutex> lock(configMutex);
  config.pushOnClose = enabled;
}

void BookSyncStore::setPullOnOpen(const bool enabled) {
  std::lock_guard<std::mutex> lock(configMutex);
  config.pullOnOpen = enabled;
}

std::string BookSyncStore::serverUrlForCurrentNetwork(const std::string& configured) const {
  const std::string ssid = connectedSsid();
  if (ssid.empty()) return configured;
  const std::string chosen = BookSync::chooseServerUrl(ssid, getConfig(), configured);
  if (chosen != configured) {
    LOG_DBG("BKS", "On peer network %s, using %s", ssid.c_str(), chosen.c_str());
  }
  return chosen;
}

bool BookSyncStore::onPeerNetwork() const {
  const std::string peer = getPeerSsid();
  return !peer.empty() && connectedSsid() == peer;
}
