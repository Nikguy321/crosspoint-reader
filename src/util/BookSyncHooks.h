#pragma once

#include <BookSyncConfig.h>
#include <BookSyncPush.h>

#include <cstdint>
#include <optional>
#include <string>

class Epub;

// Device glue behind the booksync fork's hooks in upstream files. Each function
// names the one hook site that calls it.
namespace BookSyncHooks {

// main.cpp setup(), beside the silent-reboot flag: read and clear the post-sync
// landing marker. Every silent reboot into the reader ends a sync, so the open
// that follows it is that sync's continuation, not a new open.
void onBoot(bool silentReboot, bool silentRebootToReader);
// main.cpp routing: a close that was headed for the library lands there.
bool bootToLibrary();

// KOReaderSyncActivity::onEnter (under a RenderLock): save the peer's hotspot
// (with the Peer Wi-Fi Password, empty = open) so auto-connect finds it, never
// over an entry already in the Wi-Fi list.
void ensurePeerNetworkSaved();
// BookSyncSettingsActivity (under a RenderLock), after the user saves the peer
// name or password row: BookSync::peerCredentialToWrite decides what that row
// may change in the Wi-Fi list.
void savePeerNetwork(BookSync::PeerWrite cause);
// How long the Wi-Fi step waits for a saved network (0 = the stock network list).
uint32_t patientWindowMs(BookSyncTrigger trigger);
// KOReaderSyncActivity::returnToReader: where a finished sync goes.
void leaveSync(BookSyncTrigger trigger, const std::string& epubPath);
// KOReaderSyncActivity::onExit: the heap-defrag reboot, toward where leaveSync()
// went. Returns only when deep sleep superseded the reboot.
void restartAfterSync(BookSyncTrigger trigger);
// KOReaderSyncActivity::saveProgressAndReturn: a percentage-only record lands on
// the target chapter's own fraction after the reboot; spineIndex/pageNumber
// become what progress.bin should hold. Leaves them alone when nothing was armed.
void armPercentLanding(const Epub& epub, float percentage, int& spineIndex, int& pageNumber);

// EpubReaderActivity::onBookSyncLoad: consume a landing armed for this book.
bool takePendingLanding(const Epub& epub, int& spineIndex, float& within);
// Whether this book open should run a sync (clears the post-sync skip).
bool pullOnOpenWanted();
// Whether leaving a book with Back should run a sync first.
bool pushOnCloseWanted();

// The book's last agreement with a server (BookSyncPush.h), in its cache dir.
// EpubReaderActivity::bookSyncOnOpen, once the first page is on the panel: a
// record applied by the last sync is synced at the page it landed on.
void takeOpenBaseline(const Epub& epub, float openPercentage);
// EpubReaderActivity::bookSyncOnBack: whether this close pushes (BookSync::closeSyncWanted).
bool closeSyncWanted(const Epub& epub, bool atEndOfBook, std::optional<float> openPercentage, float nowPercentage);
// KOReaderSyncActivity: the book and the server now agree (an upload, or already
// synchronized), or the server's record was applied.
void recordSynced(const std::string& epubPath, float localPercentage, float remotePercentage);
void recordApplied(const std::string& epubPath, float remotePercentage);
// KOReaderSyncActivity's smart decision: BookSync::smartOverride for this book.
BookSync::SmartOverride smartOverride(const std::string& epubPath, float localPercentage, float remotePercentage,
                                      const std::string& remoteDeviceId, const std::string& remoteDevice);

}  // namespace BookSyncHooks
