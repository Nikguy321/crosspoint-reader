// EpubReaderActivity's booksync hooks: the automatic sync on book open and on
// Back, and the landing of a percentage-only record armed before the post-sync
// reboot. Kept apart from EpubReaderActivity.cpp so the fork touches that file
// only at its call sites.
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

bool EpubReaderActivity::bookSyncOnOpen() {
  // Once the first page is on the panel, so the local position is the real one.
  if (!bookSyncOpenPending || !section || lastRenderCompleteMs == 0 || RenderLock::peek()) return false;
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
  LOG_INF("BKS", "Sync on book close");
  if (!launchKOReaderSync(trigger)) {
    BookSyncHooks::leaveSync(trigger, bookPath);
  }
  return true;
}
