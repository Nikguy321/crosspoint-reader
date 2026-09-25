#include "BookSyncHooks.h"

#include <BookSyncLanding.h>
#include <BookSyncStore.h>
#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_attr.h>

#include <optional>

#include "KOReaderCredentialStore.h"
#include "SilentRestart.h"
#include "WifiCredentialStore.h"
#include "activities/Activity.h"

namespace {
constexpr char LANDING_FILE[] = "/booksync_landing.bin";

// RTC_NOINIT survives the post-sync ESP.restart() but holds garbage after a cold
// boot, hence a 32-bit value rather than a flag.
constexpr uint32_t LIBRARY_LANDING_MARKER = 0xB0C5118AU;
RTC_NOINIT_ATTR uint32_t libraryLandingMarker;

bool bootLibraryLanding = false;
bool skipNextOpenPull = false;

// The Wi-Fi store is loaded on demand (WifiSelectionActivity), so load it before
// writing or the save would drop every other network.
void writePeerNetwork(const bool userSaved) {
  const BookSync::Config config = BOOKSYNC_STORE.getConfig();
  if (config.peerSsid.empty()) return;
  WIFI_STORE.loadFromFile();
  std::optional<std::string> saved;
  if (const auto cred = WIFI_STORE.findCredential(config.peerSsid)) saved = cred->password;
  const auto password = BookSync::peerCredentialToWrite(config, saved, userSaved);
  if (!password) return;
  const char* kind = password->empty() ? "open" : "with password";
  if (WIFI_STORE.addCredential(config.peerSsid, *password)) {
    LOG_INF("BKS", "Saved peer network %s (%s)", config.peerSsid.c_str(), kind);
  } else {
    LOG_ERR("BKS", "Could not save peer network %s (%s)", config.peerSsid.c_str(), kind);
  }
}

size_t cumulativeSize(const void* ctx, const int index) {
  return static_cast<const Epub*>(ctx)->getCumulativeSpineItemSize(index);
}
}  // namespace

namespace BookSyncHooks {

void onBoot(const bool silentReboot, const bool silentRebootToReader) {
  bootLibraryLanding = silentReboot && libraryLandingMarker == LIBRARY_LANDING_MARKER;
  libraryLandingMarker = 0;
  skipNextOpenPull = silentReboot && silentRebootToReader;
}

bool bootToLibrary() { return bootLibraryLanding; }

void ensurePeerNetworkSaved() { writePeerNetwork(false); }

void savePeerNetwork() { writePeerNetwork(true); }

uint32_t patientWindowMs(const BookSyncTrigger trigger) {
  return BookSync::patientWindowMs(trigger, BOOKSYNC_STORE.getWindowIndex());
}

void leaveSync(const BookSyncTrigger trigger, const std::string& epubPath) {
  switch (trigger) {
    case BookSyncTrigger::CloseToHome:
      activityManager.goHome();
      return;
    case BookSyncTrigger::CloseToLibrary:
      activityManager.goToFileBrowser(epubPath);
      return;
    case BookSyncTrigger::Manual:
    case BookSyncTrigger::Open:
    default:
      activityManager.goToReader(epubPath);
      return;
  }
}

void restartAfterSync(const BookSyncTrigger trigger) {
  if (!BookSync::isCloseTrigger(trigger)) {
    silentRestartToReader();
    return;
  }
  if (trigger == BookSyncTrigger::CloseToLibrary) libraryLandingMarker = LIBRARY_LANDING_MARKER;
  silentRestart();
}

void armPercentLanding(const Epub& epub, const float percentage, int& spineIndex, int& pageNumber) {
  const int spineCount = epub.getSpineItemsCount();
  const auto located = BookSync::locateByteWeighted(percentage, spineCount, cumulativeSize, &epub);
  if (!located) {
    LOG_ERR("BKS", "Book has no spine bytes; keeping the stock landing");
    return;
  }
  const BookSync::SpinePoint point = BookSync::snapToChapterStart(*located, spineCount, cumulativeSize, &epub);

  uint8_t record[BookSync::LANDING_RECORD_SIZE];
  BookSync::encodeLanding(point, spineCount, record);
  const std::string path = epub.getCachePath() + LANDING_FILE;
  {
    HalFile file;
    if (!Storage.openFileForWrite("BKS", path, file) || file.write(record, sizeof(record)) != sizeof(record)) {
      LOG_ERR("BKS", "Could not write %s; keeping the stock landing", path.c_str());
      return;
    }
  }
  LOG_INF("BKS", "Landing %.6f at spine %d within %.4f (stock estimate: spine %d page %d)", percentage,
          point.spineIndex, point.within, spineIndex, pageNumber);
  if (point.spineIndex != spineIndex) pageNumber = 0;
  spineIndex = point.spineIndex;
}

bool takePendingLanding(const Epub& epub, int& spineIndex, float& within) {
  const std::string path = epub.getCachePath() + LANDING_FILE;
  if (!Storage.exists(path.c_str())) return false;

  // One spare byte so an over-long file reads as the wrong length.
  uint8_t record[BookSync::LANDING_RECORD_SIZE + 1];
  int length = -1;
  {
    HalFile file;
    if (Storage.openFileForRead("BKS", path, file)) length = file.read(record, sizeof(record));
  }
  Storage.remove(path.c_str());

  std::optional<BookSync::SpinePoint> point;
  if (length > 0) point = BookSync::decodeLanding(record, static_cast<size_t>(length), epub.getSpineItemsCount());
  if (!point) {
    LOG_ERR("BKS", "Discarded an unusable landing record (%d bytes)", length);
    return false;
  }
  spineIndex = point->spineIndex;
  within = static_cast<float>(point->within);
  LOG_INF("BKS", "Landing on spine %d within %.4f", spineIndex, point->within);
  return true;
}

bool pullOnOpenWanted() {
  const bool skip = skipNextOpenPull;
  skipNextOpenPull = false;
  return !skip && BOOKSYNC_STORE.getPullOnOpen() && KOREADER_STORE.hasCredentials();
}

bool pushOnCloseWanted() { return BOOKSYNC_STORE.getPushOnClose() && KOREADER_STORE.hasCredentials(); }

}  // namespace BookSyncHooks
