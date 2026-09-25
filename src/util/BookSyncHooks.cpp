#include "BookSyncHooks.h"

#include <BookSyncLanding.h>
#include <BookSyncPush.h>
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
constexpr char SYNCED_FILE[] = "/booksync_synced.bin";
// The cache dir EpubReaderActivity and KOReaderSyncActivity open books with.
constexpr char BOOK_CACHE_DIR[] = "/.crosspoint";

// RTC_NOINIT survives the post-sync ESP.restart() but holds garbage after a cold
// boot, hence a 32-bit value rather than a flag.
constexpr uint32_t LIBRARY_LANDING_MARKER = 0xB0C5118AU;
RTC_NOINIT_ATTR uint32_t libraryLandingMarker;

bool bootLibraryLanding = false;
bool skipNextOpenPull = false;

// The Wi-Fi store is loaded on demand (WifiSelectionActivity), so load it before
// writing or the save would drop every other network; a file that exists but
// cannot be read is left alone for the same reason.
void writePeerNetwork(const BookSync::PeerWrite cause) {
  const BookSync::Config config = BOOKSYNC_STORE.getConfig();
  if (config.peerSsid.empty()) return;
  const bool loaded = WIFI_STORE.loadFromFile();
  if (!BookSync::wifiListWritable(loaded, Storage.exists(WifiCredentialStore::getFilePath()))) {
    LOG_ERR("BKS", "Could not read %s; not saving peer network %s", WifiCredentialStore::getFilePath(),
            config.peerSsid.c_str());
    return;
  }
  std::optional<std::string> saved;
  if (const auto cred = WIFI_STORE.findCredential(config.peerSsid)) saved = cred->password;
  const auto password = BookSync::peerCredentialToWrite(config, saved, cause);
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

std::string syncedPathFor(const std::string& cachePath) { return cachePath + SYNCED_FILE; }

std::string syncedPathForBook(const std::string& epubPath) {
  return syncedPathFor(Epub(epubPath, BOOK_CACHE_DIR).getCachePath());
}

BookSync::SyncedRecord readSynced(const std::string& path) {
  // One spare byte so an over-long file reads as the wrong length.
  uint8_t record[BookSync::SYNCED_RECORD_SIZE + 1];
  int length = -1;
  if (Storage.exists(path.c_str())) {
    HalFile file;
    if (Storage.openFileForRead("BKS", path, file)) length = file.read(record, sizeof(record));
  }
  return length > 0 ? BookSync::decodeSynced(record, static_cast<size_t>(length)) : BookSync::SyncedRecord{};
}

void writeSynced(const std::string& path, const BookSync::SyncedRecord& synced) {
  uint8_t record[BookSync::SYNCED_RECORD_SIZE];
  BookSync::encodeSynced(synced, record);
  HalFile file;
  if (!Storage.openFileForWrite("BKS", path, file) || file.write(record, sizeof(record)) != sizeof(record)) {
    LOG_ERR("BKS", "Could not write %s", path.c_str());
  }
}
}  // namespace

namespace BookSyncHooks {

void onBoot(const bool silentReboot, const bool silentRebootToReader) {
  bootLibraryLanding = silentReboot && libraryLandingMarker == LIBRARY_LANDING_MARKER;
  libraryLandingMarker = 0;
  skipNextOpenPull = silentReboot && silentRebootToReader;
}

bool bootToLibrary() { return bootLibraryLanding; }

void ensurePeerNetworkSaved() { writePeerNetwork(BookSync::PeerWrite::SyncStart); }

void savePeerNetwork(const BookSync::PeerWrite cause) { writePeerNetwork(cause); }

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

void takeOpenBaseline(const Epub& epub, const float openPercentage) {
  if (!KOREADER_STORE.hasCredentials()) return;
  const std::string path = syncedPathFor(epub.getCachePath());
  const BookSync::SyncedRecord synced = readSynced(path);
  if (synced.kind != BookSync::SyncedRecord::Kind::Rebase) return;
  writeSynced(path, BookSync::rebased(synced, openPercentage));
  LOG_DBG("BKS", "Applied record %.6f landed at %.6f", synced.remote, openPercentage);
}

bool closeSyncWanted(const Epub& epub, const bool atEndOfBook, const std::optional<float> openPercentage,
                     const float nowPercentage) {
  const BookSync::SyncedRecord synced =
      atEndOfBook ? BookSync::SyncedRecord{} : readSynced(syncedPathFor(epub.getCachePath()));
  const bool wanted = BookSync::closeSyncWanted(atEndOfBook, openPercentage, nowPercentage, synced);
  if (!wanted) {
    LOG_INF("BKS", "No sync on close: %s", atEndOfBook ? "end of book" : "not moved since opened or last synced");
  }
  return wanted;
}

void recordSynced(const std::string& epubPath, const float localPercentage, const float remotePercentage) {
  BookSync::SyncedRecord synced;
  synced.kind = BookSync::SyncedRecord::Kind::At;
  synced.local = localPercentage;
  synced.remote = remotePercentage;
  writeSynced(syncedPathForBook(epubPath), synced);
}

void recordApplied(const std::string& epubPath, const float remotePercentage) {
  BookSync::SyncedRecord synced;
  synced.kind = BookSync::SyncedRecord::Kind::Rebase;
  synced.remote = remotePercentage;
  writeSynced(syncedPathForBook(epubPath), synced);
}

BookSync::SmartOverride smartOverride(const std::string& epubPath, const float localPercentage,
                                      const float remotePercentage, const std::string& remoteDeviceId,
                                      const std::string& remoteDevice) {
  const BookSync::SmartOverride choice =
      BookSync::smartOverride(readSynced(syncedPathForBook(epubPath)), localPercentage, remotePercentage,
                              BookSync::fromAnotherDevice(remoteDeviceId, remoteDevice));
  switch (choice) {
    case BookSync::SmartOverride::AlreadySynced:
      LOG_INF("BKS", "Server still holds the %.6f this book synced with; nothing to send", remotePercentage);
      break;
    case BookSync::SmartOverride::Upload:
      LOG_INF("BKS", "Server still holds the %.6f this book synced with and the book moved since; uploading %.6f",
              remotePercentage, localPercentage);
      break;
    case BookSync::SmartOverride::Offer:
      LOG_INF("BKS", "%s is at %.6f, this book at %.6f; offering it on the compare screen", remoteDevice.c_str(),
              remotePercentage, localPercentage);
      break;
    case BookSync::SmartOverride::None:
    default:
      break;
  }
  return choice;
}

}  // namespace BookSyncHooks
