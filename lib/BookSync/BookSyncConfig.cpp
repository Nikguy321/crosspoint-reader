#include "BookSyncConfig.h"

namespace BookSync {

uint8_t sanitizeWindowIndex(const uint8_t index) { return index < WINDOW_COUNT ? index : DEFAULT_WINDOW_INDEX; }

uint32_t windowMs(const uint8_t windowIndex) {
  return static_cast<uint32_t>(WINDOW_SECONDS[sanitizeWindowIndex(windowIndex)]) * 1000U;
}

bool isCloseTrigger(const BookSyncTrigger trigger) {
  return trigger == BookSyncTrigger::CloseToHome || trigger == BookSyncTrigger::CloseToLibrary;
}

uint32_t patientWindowMs(const BookSyncTrigger trigger, const uint8_t windowIndex) {
  const uint32_t configured = windowMs(windowIndex);
  switch (trigger) {
    case BookSyncTrigger::Open:
      return (configured != 0 && configured < AUTO_TRIGGER_WINDOW_MS) ? configured : AUTO_TRIGGER_WINDOW_MS;
    case BookSyncTrigger::CloseToHome:
    case BookSyncTrigger::CloseToLibrary:
      return configured != 0 ? configured : AUTO_TRIGGER_WINDOW_MS;
    case BookSyncTrigger::Manual:
    default:
      return configured;
  }
}

namespace {
bool joinedTo(const std::string& connectedSsid, const std::string& ssid) {
  return !ssid.empty() && connectedSsid == ssid;
}
}  // namespace

std::string chooseServerUrl(const std::string& connectedSsid, const Config& config, const std::string& configured) {
  if (!config.peerUrl.empty() && joinedTo(connectedSsid, config.peerSsid)) return config.peerUrl;
  if (!config.hubUrl.empty() && joinedTo(connectedSsid, config.hubSsid)) return config.hubUrl;
  return configured;
}

bool onDeviceNetwork(const std::string& connectedSsid, const Config& config) {
  return joinedTo(connectedSsid, config.peerSsid) ||
         (!config.hubUrl.empty() && joinedTo(connectedSsid, config.hubSsid));
}

std::optional<std::string> peerCredentialToWrite(const Config& config, const std::optional<std::string>& saved,
                                                 const PeerWrite cause) {
  if (config.peerSsid.empty()) return std::nullopt;
  if (!saved.has_value()) return config.peerPassword;
  if (cause == PeerWrite::PasswordSaved && *saved != config.peerPassword) return config.peerPassword;
  return std::nullopt;
}

bool wifiListWritable(const bool loaded, const bool fileExists) { return loaded || !fileExists; }

bool joinPeerDirectly(const Config& config, const bool peerVisible, const bool peerInWifiList,
                      const bool alreadyTried) {
  return !config.peerSsid.empty() && peerVisible && !peerInWifiList && !alreadyTried;
}

bool peerPasswordMismatch(const bool peerVisible, const bool peerEncrypted, const std::optional<std::string>& saved) {
  return peerVisible && saved.has_value() && peerEncrypted == saved->empty();
}

}  // namespace BookSync
