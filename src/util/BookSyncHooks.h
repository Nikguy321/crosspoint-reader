#pragma once

#include <BookSyncConfig.h>

#include <cstdint>
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
// name or password: write that network over any existing entry.
void savePeerNetwork();
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

}  // namespace BookSyncHooks
