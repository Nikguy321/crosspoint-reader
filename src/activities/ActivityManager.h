#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "util/ScreenshotInfo.h"

class Activity;    // forward declaration
class RenderLock;  // forward declaration

enum class HomeMenuItem { NONE, FILE_BROWSER, LIBRARY, OPDS_BROWSER, FILE_TRANSFER, APPS, SETTINGS_MENU };

/**
 * ActivityManager
 *
 * This mirrors the same concept of Activity in Android, where an activity represents a single screen of the UI. The
 * manager is responsible for launching activities, and ensuring that only one activity is active at a time.
 *
 * It also provides a stack mechanism to allow activities to launch sub-activities and get back the results when the
 * sub-activity is done. For example, the WebServer activity can launch a WifiSelect activity to let the user choose a
 * wifi network, and get back the selected network when the user is done.
 *
 * Main differences from Android's ActivityManager:
 * - No onPause/onResume, since we don't have a concept of background activities
 * - onActivityResult is implemented via a callback instead of a separate method, for simplicity
 */
class ActivityManager {
  friend class RenderLock;

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;

  void exitActivity(const RenderLock& lock);

  // Pending activity to be launched on next loop iteration
  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;

  // Task to render and display the activity
  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();

  // Set by requestUpdateAndWait(); read and cleared by the render task after render completes.
  // Note: only one waiting task is supported at a time
  TaskHandle_t waitingTaskHandle = nullptr;

  // Mutex to protect rendering operations from race conditions
  // Must only be used via RenderLock
  SemaphoreHandle_t renderingMutex = nullptr;

  // Whether to trigger a render after the current loop()
  // This variable must only be set by the main loop, to avoid race conditions
  std::atomic<bool> requestedUpdate{false};

  // Renders run since boot (the power ledger's rnd=).
  std::atomic<uint32_t> renders{0};
  // The render task took a notification and has not finished that render.
  std::atomic<bool> renderTaken{false};

 public:
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
    assert(renderingMutex != nullptr && "Failed to create rendering mutex");
    stackActivities.reserve(10);
  }
  ~ActivityManager() { assert(false); /* should never be called */ };

  void begin();
  void loop();

  // Will replace currentActivity and drop all activities on stack
  void replaceActivity(std::unique_ptr<Activity>&& newActivity);

  // goTo... functions are convenient wrapper for replaceActivity()
  void goToFileTransfer();
  void goToUsbDrive();
  void goToSettings();
  // Settings with Sleep Screen Cards open over it (X4 Pro).
  void goToSleepCardSettings();
  void goToFileBrowser(std::string path = {});
  void goToLibrary();
  void goToBrowser();
  // The Apps list, and its games (replaces, so the way back survives sleep: see resumeApp()).
  void goToApps();
  void goToWordSearch();
  void goToCrossword();
  void goToReader(std::string path, bool allowFastInitialRefresh = false);
  // live: the live sleep screen (charging, X4 Pro), which stays up and is redrawn; popup = show
  // "Entering sleep" first (not at a boot straight into the live screen).
  void goToSleep(bool fromTimeout = false, bool live = false, bool popup = true);
  void goToBoot();
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
  void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE, bool cleanInitialRefresh = false);

  // This will move current activity to stack instead of deleting it
  void pushActivity(std::unique_ptr<Activity>&& activity);

  // Remove the currentActivity, returning the last one on stack
  // Note: if popActivity() on last activity on the stack, we will goHome()
  void popActivity();

  bool preventAutoSleep() const;
  bool requiresExclusiveStorageLoop() const;
  bool isReaderActivity() const;
  // The app to reopen after sleep (Activity::resumeApp) on the stack or current; 0 = none.
  uint8_t resumeApp() const;
  bool handleForcedRefresh();
  bool skipLoopDelay() const;
  ScreenshotInfo getScreenshotInfo() const;
  uint32_t renderCount() const { return renders.load(); }
  // A render is requested, queued for or in the hands of the render task, or a
  // switch is pending (the idle loop must not light-sleep on top of it).
  bool renderQueued() const;

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  void requestUpdate(bool immediate = false);

  // Trigger a render and block until it completes.
  // Must NOT be called from the render task or while holding a RenderLock.
  void requestUpdateAndWait();

#if CROSSPOINT_BENCH_CONSOLE
  // Bench console introspection. Names are the activities' internal names;
  // index 0 of the stack is the bottom.
  const char* benchCurrentName() const;
  Activity* benchCurrentActivity() const { return currentActivity.get(); }
  size_t benchStackDepth() const { return stackActivities.size(); }
  const char* benchStackName(size_t index) const;
  // No render requested, queued, or in progress.
  bool benchRenderIdle() const;
  // An activity switch is queued for the next loop().
  bool benchSwitchPending() const { return pendingActivity != nullptr; }
  // The current (top) activity is a book reader.
  bool benchCurrentIsReader() const;
#endif
};

extern ActivityManager activityManager;  // singleton, to be defined in main.cpp
