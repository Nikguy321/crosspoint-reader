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

std::string chooseServerUrl(const std::string& connectedSsid, const Config& config, const std::string& configured) {
  if (config.peerSsid.empty() || config.peerUrl.empty() || connectedSsid != config.peerSsid) {
    return configured;
  }
  return config.peerUrl;
}

}  // namespace BookSync
