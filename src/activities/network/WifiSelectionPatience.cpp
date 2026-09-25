// The patient half of WifiSelectionActivity's auto-connect (booksync fork): a
// sync event with no saved network in range rescans every few seconds under a
// countdown instead of opening the network list, which never sleeps and waits
// for a person. Back cancels, Confirm opens the list, and the window running out
// cancels the sync.
//
// A wrong peer password cannot be read from the join itself: arduino-esp32 maps
// the usual wrong-key disconnect (4-way handshake timeout) to a plain disconnect
// and retries, so the attempt just times out. The scan does say whether the
// hotspot is protected, which catches the case that matters: saved open, hotspot
// protected (or the reverse).
#include <BookSyncStore.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <optional>

#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void WifiSelectionActivity::setPatientWindowMs(const uint32_t windowMs) { patience.arm(windowMs, millis()); }

bool WifiSelectionActivity::patientWait() {
  if (!patience.armed()) return false;
  if (patience.nothingToJoin(millis())) {
    LOG_DBG("WIFI", "No saved network to join; rescanning in %lu ms, %lu s left",
            static_cast<unsigned long>(BookSyncPatience::RESCAN_INTERVAL_MS),
            static_cast<unsigned long>(patience.secondsLeft(millis())));

    const std::string peer = BOOKSYNC_STORE.getPeerSsid();
    const auto peerNetwork = std::find_if(networks.begin(), networks.end(), [&peer](const WifiNetworkInfo& network) {
      return !network.isHiddenPlaceholder && !peer.empty() && network.ssid == peer;
    });
    const bool peerVisible = peerNetwork != networks.end();
    const bool peerEncrypted = peerVisible && peerNetwork->isEncrypted;
    std::optional<std::string> savedPassword;
    if (const auto cred = WIFI_STORE.findCredential(peer)) savedPassword = cred->password;
    const bool mismatch = BookSync::peerPasswordMismatch(peerVisible, peerEncrypted, savedPassword);
    if (mismatch && !patience.showPeerPasswordHint()) {
      LOG_INF("WIFI", "Peer %s is %s but saved %s", peer.c_str(), peerEncrypted ? "protected" : "open",
              peerEncrypted ? "without a password" : "with a password");
    }
    patience.setPeerPasswordHint(mismatch);

    state = WifiSelectionState::SCANNING;
    requestUpdate();
    return true;
  }
  LOG_INF("WIFI", "No saved network appeared within the sync window");
  patience.disarm();
  onComplete(false);
  return true;
}

bool WifiSelectionActivity::patientLoop() {
  if (!patience.waiting()) return false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    patience.disarm();
    onComplete(false);
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    patience.disarm();
    showNetworkListFromAutoConnect();
    return true;
  }

  const uint32_t now = millis();
  if (patience.expired(now)) {
    LOG_INF("WIFI", "No saved network appeared within the sync window");
    patience.disarm();
    onComplete(false);
    return true;
  }
  if (patience.takeRescan(now)) {
    // The countdown stays on the panel while the radio scans; a network that
    // failed earlier (a hotspot still starting up) is tried again.
    autoAttemptedSsids.clear();
    autoConnecting = true;
    manualNetworkListRequested = false;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    WiFi.scanNetworks(true);
  }
  return true;
}

bool WifiSelectionActivity::renderPatientWait(const Rect* screen, const ThemeMetrics* metrics) const {
  if (!patience.waiting()) return false;

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int top = screen->y + (screen->height - lineHeight * 4) / 2;
  UITheme::drawCenteredText(renderer, *screen, UI_12_FONT_ID, top, tr(STR_BOOKSYNC_WAITING), true, EpdFontFamily::BOLD);

  const std::string peer = BOOKSYNC_STORE.getPeerSsid();
  if (patience.showPeerPasswordHint()) {
    UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + lineHeight + metrics->verticalSpacing * 2,
                              tr(STR_BOOKSYNC_CHECK_PEER_PASSWORD), true, EpdFontFamily::BOLD);
  } else if (!peer.empty()) {
    char lookingFor[96];
    snprintf(lookingFor, sizeof(lookingFor), tr(STR_BOOKSYNC_LOOKING_FOR), peer.c_str());
    const Rect textBounds{screen->x + metrics->contentSidePadding, top + lineHeight + metrics->verticalSpacing,
                          screen->width - metrics->contentSidePadding * 2, lineHeight * 2};
    UITheme::drawCenteredWrappedText(renderer, textBounds, UI_10_FONT_ID, lookingFor, 2);
  }

  const uint32_t secondsLeft = patience.secondsLeft(millis());
  char timeLeft[48];
  snprintf(timeLeft, sizeof(timeLeft), tr(STR_BOOKSYNC_TIME_LEFT), static_cast<unsigned>(secondsLeft / 60),
           static_cast<unsigned>(secondsLeft % 60));
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + lineHeight * 4 + metrics->verticalSpacing * 2,
                            timeLeft);

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SHOW_NETWORKS), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  return true;
}
