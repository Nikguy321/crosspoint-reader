#include "BookSyncSettingsActivity.h"

#include <BookSyncStore.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <memory>
#include <utility>

#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/BookSyncHooks.h"

namespace fui = freeink::ui;

namespace {
enum Row : uint8_t {
  PEER_SSID,
  PEER_PASSWORD,
  PEER_URL,
  HUB_SSID,
  HUB_URL,
  OTHER_SERVER,
  WINDOW,
  PUSH_ON_CLOSE,
  PULL_ON_OPEN
};

const StrId menuNames[BookSyncSettingsActivity::MENU_ITEMS] = {
    StrId::STR_BOOKSYNC_PEER_SSID, StrId::STR_BOOKSYNC_PEER_PASSWORD, StrId::STR_BOOKSYNC_PEER_URL,
    StrId::STR_BOOKSYNC_HUB_SSID,  StrId::STR_BOOKSYNC_HUB_URL,       StrId::STR_BOOKSYNC_OTHER_SERVER,
    StrId::STR_BOOKSYNC_WINDOW,    StrId::STR_BOOKSYNC_PUSH_ON_CLOSE, StrId::STR_BOOKSYNC_PULL_ON_OPEN};

// The user saved a peer row: the Wi-Fi list follows it (a new name adds a
// missing entry, a new password is written over the entry).
void savePeerNetwork(const BookSync::PeerWrite cause) {
  RenderLock lock;
  BookSyncHooks::savePeerNetwork(cause);
}

// The keyboard's bare scheme prefix means no URL.
std::string urlFromKeyboard(const std::string& text) { return (text == "https://" || text == "http://") ? "" : text; }

// "Wait for Wi-Fi" label for a BookSync::WINDOW_SECONDS index.
StrId windowLabel(const uint8_t windowIndex) {
  switch (BookSync::sanitizeWindowIndex(windowIndex)) {
    case 1:
      return StrId::STR_BOOKSYNC_WINDOW_1_MIN;
    case 2:
      return StrId::STR_BOOKSYNC_WINDOW_2_MIN;
    case 3:
      return StrId::STR_BOOKSYNC_WINDOW_5_MIN;
    case 4:
      return StrId::STR_BOOKSYNC_WINDOW_10_MIN;
    case 0:
    default:
      return StrId::STR_STATE_OFF;
  }
}
}  // namespace

BookSyncSettingsActivity::BookSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("BookSyncSettings", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int BookSyncSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* BookSyncSettingsActivity::headerTitle() const { return tr(STR_BOOKSYNC_SETTINGS); }

void BookSyncSettingsActivity::activateIndex(const int index) {
  // Activation opens a keyboard or repaints a new value; a lingering flash would
  // gray an unrelated row.
  app.clearTapFlash();
  switch (index) {
    case PEER_SSID: {
      // Empty turns peer sync off: no hotspot is saved and the configured server is always used.
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOKSYNC_PEER_SSID),
                                                               BOOKSYNC_STORE.getPeerSsid(), BookSync::MAX_SSID_LENGTH);
      if (!keyboard) {
        LOG_ERR("BKS", "OOM: KeyboardEntryActivity");
        break;
      }
      startActivityForResult(std::move(keyboard), [](const ActivityResult& result) {
        if (result.isCancelled) return;
        BOOKSYNC_STORE.setPeerSsid(std::get<KeyboardResult>(result.data).text);
        BOOKSYNC_STORE.saveToFile();
        savePeerNetwork(BookSync::PeerWrite::NameSaved);
      });
      break;
    }
    case PEER_PASSWORD: {
      // Empty means the peer's hotspot is open.
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOKSYNC_PEER_PASSWORD),
                                                               BOOKSYNC_STORE.getPeerPassword(),
                                                               BookSync::MAX_PASSWORD_LENGTH, InputType::Text);
      if (!keyboard) {
        LOG_ERR("BKS", "OOM: KeyboardEntryActivity");
        break;
      }
      startActivityForResult(std::move(keyboard), [](const ActivityResult& result) {
        if (result.isCancelled) return;
        BOOKSYNC_STORE.setPeerPassword(std::get<KeyboardResult>(result.data).text);
        BOOKSYNC_STORE.saveToFile();
        savePeerNetwork(BookSync::PeerWrite::PasswordSaved);
      });
      break;
    }
    case PEER_URL: {
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOKSYNC_PEER_URL),
                                                               BOOKSYNC_STORE.getPeerUrl(), BookSync::MAX_URL_LENGTH,
                                                               InputType::Url);
      if (!keyboard) {
        LOG_ERR("BKS", "OOM: KeyboardEntryActivity");
        break;
      }
      startActivityForResult(std::move(keyboard), [](const ActivityResult& result) {
        if (result.isCancelled) return;
        BOOKSYNC_STORE.setPeerUrl(urlFromKeyboard(std::get<KeyboardResult>(result.data).text));
        BOOKSYNC_STORE.saveToFile();
      });
      break;
    }
    case HUB_SSID: {
      // Empty turns the hub off. The hub's network is joined from the Wi-Fi list, with its own password.
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOKSYNC_HUB_SSID),
                                                               BOOKSYNC_STORE.getHubSsid(), BookSync::MAX_SSID_LENGTH);
      if (!keyboard) {
        LOG_ERR("BKS", "OOM: KeyboardEntryActivity");
        break;
      }
      startActivityForResult(std::move(keyboard), [](const ActivityResult& result) {
        if (result.isCancelled) return;
        BOOKSYNC_STORE.setHubSsid(std::get<KeyboardResult>(result.data).text);
        BOOKSYNC_STORE.saveToFile();
      });
      break;
    }
    case HUB_URL: {
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOKSYNC_HUB_URL),
                                                               BOOKSYNC_STORE.getHubUrl(), BookSync::MAX_URL_LENGTH,
                                                               InputType::Url);
      if (!keyboard) {
        LOG_ERR("BKS", "OOM: KeyboardEntryActivity");
        break;
      }
      startActivityForResult(std::move(keyboard), [](const ActivityResult& result) {
        if (result.isCancelled) return;
        BOOKSYNC_STORE.setHubUrl(urlFromKeyboard(std::get<KeyboardResult>(result.data).text));
        BOOKSYNC_STORE.saveToFile();
      });
      break;
    }
    case OTHER_SERVER:
      // Shown only; it is KOReader Sync's Sync Server URL.
      break;
    case WINDOW: {
      const int next = (BOOKSYNC_STORE.getWindowIndex() + 1) % BookSync::WINDOW_COUNT;
      BOOKSYNC_STORE.setWindowIndex(static_cast<uint8_t>(next));
      BOOKSYNC_STORE.saveToFile();
      requestUpdate();
      break;
    }
    case PUSH_ON_CLOSE:
      BOOKSYNC_STORE.setPushOnClose(!BOOKSYNC_STORE.getPushOnClose());
      BOOKSYNC_STORE.saveToFile();
      requestUpdate();
      break;
    case PULL_ON_OPEN:
      BOOKSYNC_STORE.setPullOnOpen(!BOOKSYNC_STORE.getPullOnOpen());
      BOOKSYNC_STORE.saveToFile();
      requestUpdate();
      break;
    default:
      break;
  }
}

void BookSyncSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const BookSync::Config config = BOOKSYNC_STORE.getConfig();
  rowValues_[PEER_SSID] = config.peerSsid.empty() ? tr(STR_STATE_OFF) : config.peerSsid;
  rowValues_[PEER_PASSWORD] = config.peerPassword.empty() ? tr(STR_NOT_SET) : "******";
  rowValues_[PEER_URL] = config.peerUrl.empty() ? tr(STR_NOT_SET) : config.peerUrl;
  rowValues_[HUB_SSID] = config.hubSsid.empty() ? tr(STR_STATE_OFF) : config.hubSsid;
  rowValues_[HUB_URL] = config.hubUrl.empty() ? tr(STR_NOT_SET) : config.hubUrl;
  // Where a sync goes on any other network; an empty Sync Server URL is the public default.
  rowValues_[OTHER_SERVER] = KOREADER_STORE.getBaseUrl(false);
  rowValues_[WINDOW] = I18N.get(windowLabel(config.windowIndex));
  rowValues_[PUSH_ON_CLOSE] = config.pushOnClose ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rowValues_[PULL_ON_OPEN] = config.pullOnOpen ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].value = rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
