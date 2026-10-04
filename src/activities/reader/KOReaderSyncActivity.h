#pragma once
#include <BookSyncConfig.h>
#include <Epub.h>

#include <functional>
#include <memory>
#include <optional>

#include "KOReaderSyncClient.h"
#include "ProgressMapper.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

/**
 * Activity for syncing reading progress with KOReader sync server.
 *
 * Flow:
 * 1. Connect to WiFi (if not connected)
 * 2. Calculate document hash
 * 3. Fetch remote progress
 * 4. Show comparison and options (Apply/Upload)
 * 5. Apply or upload progress
 */
class KOReaderSyncActivity final : public Activity, private UiAppHost {
 public:
  explicit KOReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& epubPath,
                                CrossPointPosition localPosition, SavedProgressPosition localKoPos,
                                std::string localChapterName, BookSyncTrigger trigger = BookSyncTrigger::Manual);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING || state == UPLOADING; }

 private:
  enum State {
    WIFI_SELECTION,
    CONNECTING,
    SYNCING,
    SHOWING_RESULT,
    UPLOADING,
    UPLOAD_COMPLETE,
    SYNC_COMPLETE,
    NO_REMOTE_PROGRESS,
    SYNC_FAILED,
    NO_CREDENTIALS
  };

  std::shared_ptr<Epub> epub;  // null until lazy-loaded after TLS in performSync()
  std::string epubPath;
  std::string localChapterName;
  CrossPointPosition localPosition;
  BookSyncTrigger trigger;

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string documentHash;

  // Remote progress data
  bool hasRemoteProgress = false;
  KOReaderProgress remoteProgress;
  CrossPointPosition remotePosition;

  // Local progress as KOReader format (pre-computed before Epub was released)
  SavedProgressPosition localProgress;

  // Selection in result screen (0=Apply, 1=Upload)
  int selectedOption = 0;

  // Timed return for successful smart-sync terminal states.
  unsigned long autoReturnAt = 0;
  static constexpr unsigned long AUTO_RETURN_DELAY_MS = 1200;

  // Tracks whether this session activated WiFi. Set in onEnter past the credentials
  // check; checked in onExit to decide whether to silent-reboot. Can't rely on
  // WiFi.getMode() alone: performUpload() stops the RF (RadioPower::stop()) on the way out, and
  // whether the mode still reads on after that is the Arduino core's business, not this rule's.
  bool wifiActivated = false;

  // The last KOSync request got an HTTP answer: the network works, so "Update location when
  // syncing" may ride it (AutoLocate); after a network failure it does not add its own wait.
  bool reachedServer = false;

  void onWifiSelectionComplete(bool success);
  void performSync();
  void performUpload();
  bool smartSyncEnabled() const;
  void markAutoReturn();
  void completeAlreadySynced();
  // A due AutoLocate run, then a due weather fetch, with the current result on screen; true when
  // either ran (and drew it). Loop task only, with no render lock held (never from onExit).
  bool refreshLocationWhileShowing();
  void ensureEpubLoaded();
  void saveProgressAndReturn(int spineIndex, int page);
  void returnToReader();

  // The UiAppHost app hosts the interactive states (SHOWING_RESULT compare
  // rows and the NO_REMOTE_PROGRESS upload prompt) so they get themed
  // rows/buttons and tap-flash; the header stays on GUI.drawHeader for the
  // battery indicator and the purely-informational states keep their raw
  // centered text.
  static void resultScreen(UiScreen& screen, void* user);
  static void onResultRow(const freeink::ui::ActionEvent& event, void* user);
  void buildResultScreen(UiScreen& screen);
  void chooseResultOption();
  void startUpload();
};
