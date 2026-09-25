// EpubReaderActivity's booksync hooks: the automatic sync on book open and on
// Back, the landing of a percentage-only record armed before the post-sync
// reboot, and the position the book opened at (a close pushes only a moved
// position). Kept apart from EpubReaderActivity.cpp so the fork touches that
// file only at its call sites.
#include <Logging.h>

#include "CrossPointSettings.h"
#include "EpubReaderActivity.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "util/BookSyncHooks.h"

void EpubReaderActivity::onBookSyncLoad() {
  int spine = 0;
  float within = 0.0f;
  if (BookSyncHooks::takePendingLanding(*epub, spine, within)) {
    // The state jumpToPercent() sets: the first section load takes the full-build
    // path and lands on page within * pageCount of the target chapter.
    clearDeferredReposition();
    currentSpineIndex = spine;
    nextPageNumber = 0;
    pendingSpineProgress = within;
    pendingPercentJump = true;
  }
  bookSyncOpenPending = BookSyncHooks::pullOnOpenWanted();
}

float EpubReaderActivity::bookSyncPercentage() const {
  // The percentage ProgressMapper::toSavedProgress sends for this position.
  const CrossPointPosition pos = getCurrentPosition();
  const float intra =
      pos.totalPages > 1 ? static_cast<float>(pos.pageNumber) / static_cast<float>(pos.totalPages - 1) : 0.0f;
  return epub->calculateProgress(pos.spineIndex, intra);
}

bool EpubReaderActivity::bookSyncOnOpen() {
  if (bookSyncOpenPercentage && !bookSyncOpenPending) return false;
  // Once the first page is on the panel, so the local position is the real one.
  if (!section || lastRenderCompleteMs == 0 || RenderLock::peek()) return false;
  if (!bookSyncOpenPercentage) {
    RenderLock lock;
    bookSyncOpenPercentage = bookSyncPercentage();
    BookSyncHooks::takeOpenBaseline(*epub, *bookSyncOpenPercentage);
  }
  if (!bookSyncOpenPending) return false;
  bookSyncOpenPending = false;
  LOG_INF("BKS", "Sync on book open");
  return launchKOReaderSync(BookSyncTrigger::Open);
}

bool EpubReaderActivity::bookSyncOnBack() {
  // Inside a footnote the stock exit saves the footnote's origin; a sync would record the footnote page.
  if (footnoteDepth > 0 || mappedInput.wasBackGesture()) return false;
  if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    return false;
  }
  if (!BookSyncHooks::pushOnCloseWanted()) return false;

  // The Back rule of ReaderUtils::handleBackNavigation, with a sync before leaving.
  const bool backTriggered =
      mappedInput.wasLongPressed(MappedInputManager::Button::Back, ReaderUtils::GO_BACK_OR_HOME_MS) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back);
  if (!backTriggered) return false;
  const bool longPress = mappedInput.getHeldTime() >= ReaderUtils::GO_BACK_OR_HOME_MS;
  const BookSyncTrigger trigger =
      longPress != SETTINGS.backShortToFileBrowser ? BookSyncTrigger::CloseToLibrary : BookSyncTrigger::CloseToHome;

  // An unmoved book never goes over a newer record, and the end-of-book screen
  // leaves the stock way (it saves nothing and moves a finished book to /Read).
  bool wanted;
  {
    RenderLock lock;
    wanted = BookSyncHooks::closeSyncWanted(*epub, isAtEndOfBook(), bookSyncOpenPercentage, bookSyncPercentage());
  }
  if (!wanted) {
    BookSyncHooks::leaveSync(trigger, bookPath);
    return true;
  }

  LOG_INF("BKS", "Sync on book close");
  pendingSyncSaveError = false;
  if (!launchKOReaderSync(trigger) || pendingSyncSaveError) {
    // No sync, or the position could not be saved: Back still leaves the book.
    pendingSyncSaveError = false;
    BookSyncHooks::leaveSync(trigger, bookPath);
  }
  return true;
}
