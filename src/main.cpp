#include <Arduino.h>
#include <BoardConfig.h>
#include <BookSyncStore.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <PowerManager.h>
#include <PowerPolicy.h>
#include <SPI.h>
#include <VectorFontSupport.h>
#include <WiFi.h>
#include <XteinkDetect.h>
#include <builtinFonts/all.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "LiveSleep.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/apps/CrosswordActivity.h"
#include "activities/apps/GuideApp.h"
#include "activities/apps/SudokuActivity.h"
#include "activities/apps/WordSearchActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/RadioPower.h"
#include "network/StationKeeper.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "util/AutoPowerOff.h"
#include "util/BenchConsole.h"
#include "util/BookSyncHooks.h"
#include "util/ButtonNavigator.h"
#include "util/LiveSleepPolicy.h"
#include "util/PowerButtonTiming.h"
#include "util/PowerLedger.h"
#include "util/ScreenshotUtil.h"
#include "util/SleepLedger.h"
#include "util/Timezones.h"

#if CROSSPOINT_VECTOR_FONTS
// Rendering (incl. FreeType TTF rasterization) runs on the Arduino loop task.
// The default 8 KB stack overflows inside FreeType's FT_Open_Face / variable-font
// parsing. This runtime override applies even with the prebuilt (dio_opi) core,
// where CONFIG_ARDUINO_LOOP_STACK_SIZE from sdkconfig is baked in and ignored.
// Vector-font boards only: without TTF the stock loop stack has always sufficed,
// and non-PSRAM boards need the 16KB back in DRAM.
SET_LOOP_TASK_STACK_SIZE(24 * 1024)
#endif

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
static unsigned long allowSleepAt = 0;
static unsigned long lastX4ProPowerClickAt = 0;

// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
RTC_NOINIT_ATTR uint32_t silentRebootPayload;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;
constexpr uint32_t SILENT_REBOOT_TARGET_SETTINGS = 2;
constexpr uint32_t SILENT_REBOOT_TARGET_SLEEP_CARDS = 3;
// The app in APP_STATE.lastSleepApp (a live sleep screen's wake inside an app).
constexpr uint32_t SILENT_REBOOT_TARGET_APP = 4;
constexpr uint32_t SILENT_REBOOT_TARGET_MAX = SILENT_REBOOT_TARGET_APP;
constexpr uint32_t SILENT_REBOOT_LIGHT_ON = 1U << 0;
// A key on the live sleep screen: the reopen is a wake, so Pull on Open runs as after deep sleep.
constexpr uint32_t SILENT_REBOOT_LIVE_WAKE = 1U << 1;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
  LiveSleep,       // auto power-off timer wake on the charger: the live sleep screen
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

// A silent restart is internal maintenance, so the light must come back exactly
// as the user left it. SETTINGS.frontlightOn is the saved preference and
// legitimately diverges from the live state (a wake with Restore Light on Wake
// off leaves the light off while the saved "was on" preference is kept), so
// carry the live state across the reboot instead of re-deriving it from
// settings. Cleared with the magic in setup().
static void armSilentReboot(const uint32_t target, const bool lightOn, const bool liveWake = false) {
  silentRebootTarget = target;
  silentRebootPayload = (lightOn ? SILENT_REBOOT_LIGHT_ON : 0) | (liveWake ? SILENT_REBOOT_LIVE_WAKE : 0);
  silentRebootMagic = SILENT_REBOOT_MAGIC;
}

// Returns instead of rebooting when sleep supersedes the reboot; callers keep
// running in that case. lightOn: the frontlight the restarted reader shows.
// liveWake: the restart is the live sleep screen's wake (SILENT_REBOOT_LIVE_WAKE).
static void silentRestartWithLight(const uint32_t target, const char* targetName, const bool lightOn,
                                   const bool liveWake = false) {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  armSilentReboot(target, lightOn, liveWake);
  LOG_DBG("MAIN", "Silent restart (target=%s)", targetName);
  // E-ink retains the previous frame until the target's first paint lands
  // (~2-3s). Without an overlay, users don't see the reboot and fire input
  // through to the new activity. On Home, Select on the default
  // selectorIndex=0 opens the most-recent book, looking like a trampoline back
  // to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

static void silentRestartTo(const uint32_t target, const char* targetName) {
  silentRestartWithLight(target, targetName, Frontlight.isOn());
}

void silentRestart() { silentRestartTo(SILENT_REBOOT_TARGET_HOME, "home"); }

bool deepSleepStarting() { return deepSleepInProgress; }

void silentRestartToReader() { silentRestartTo(SILENT_REBOOT_TARGET_READER, "reader"); }

void silentRestartToSettings() { silentRestartTo(SILENT_REBOOT_TARGET_SETTINGS, "settings"); }

void silentRestartToSleepCards() { silentRestartTo(SILENT_REBOOT_TARGET_SLEEP_CARDS, "sleep cards"); }

void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;  // sleeping supersedes the storage handoff reboot
  armSilentReboot(SILENT_REBOOT_TARGET_HOME, Frontlight.isOn());
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

void toggleFrontlight() {
  if (!Frontlight.present()) return;
  const bool lightOn = !Frontlight.isOn();
  Frontlight.setOn(lightOn);
  SETTINGS.frontlightOn = lightOn ? 1 : 0;
  SETTINGS.saveToFile();
  LOG_INF("LIGHT", "Frontlight toggled %s", lightOn ? "on" : "off");
}

bool handleX4ProFrontlightDoubleClick() {
  if (!BoardConfig::isX4Pro() || !SETTINGS.doubleClickPwrLight || !gpio.wasReleased(HalGPIO::BTN_POWER)) {
    return false;
  }

  const unsigned long now = millis();
  if (gpio.getPowerButtonHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS) {
    lastX4ProPowerClickAt = 0;
    return false;
  }

  if (lastX4ProPowerClickAt == 0 || now - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = now;
    return false;
  }

  lastX4ProPowerClickAt = 0;
  toggleFrontlight();
  return true;
}

static_assert(live_sleep::FIRST_CARD_MODE == CrossPointSettings::NOW_READING &&
                  live_sleep::LAST_CARD_MODE + 1 == CrossPointSettings::SLEEP_SCREEN_MODE_COUNT,
              "live_sleep's card modes must match CrossPointSettings::SLEEP_SCREEN_MODE");

// The live sleep screen (LiveSleep.h): one session from the sleep gesture (or a timer wake on the
// charger) to a key or the unplug.
struct LiveSession {
  bool active = false;
  bool lightWasOn = false;   // the frontlight before the screen went live, for the wake
  bool wakePending = false;  // a key went down: the wake waits until every key is up again
  uint32_t wakePressAt = 0;  // when that key went down (live_sleep::wakeNow)
  uint32_t rotatedAt = 0;    // the last /sleep.log rotation check (the pwr lines go on for days)
  live_sleep::UnplugDebounce unplug;
  unsigned socAtLoss = 0;  // the gauge's SOC when external power last vanished
  // The full-charge hold (live_sleep::holdEligible): the charger went idle at full, the screen
  // stays up quietly until power comes back or the battery drops.
  bool holding = false;
  unsigned holdStartSoc = 0;
  uint32_t holdSince = 0;
  uint32_t holdCheckedAt = 0;  // the last gauge read for the drop and the cap
};
static LiveSession liveSession;

// /sleep.log "live" x=: what put it to sleep. "sleep" x= of a live session's end.
constexpr int LIVE_FROM_BUTTON = 0;
constexpr int LIVE_FROM_TIMEOUT = 1;
constexpr int LIVE_FROM_TIMER_BOOT = 2;
constexpr int SLEEP_AFTER_UNPLUG = 2;
constexpr int SLEEP_AFTER_BENCH = 3;
// The hold's ends (live_sleep::holdEndReason): 4 the drop, 5 the cap, as "holdend" and "sleep" x=.
static_assert(live_sleep::holdEndReason(live_sleep::HoldExit::Drop) == 4 &&
                  live_sleep::holdEndReason(live_sleep::HoldExit::Cap) == 5 &&
                  live_sleep::holdEndReason(live_sleep::HoldExit::PowerBack) == 0,
              "/sleep.log's hold reasons (util/SleepLedger.h)");
constexpr unsigned long LIVE_LOOP_DELAY_MS = 20;
constexpr uint32_t LIVE_ROTATE_EVERY_MS = 60UL * 60UL * 1000UL;
// While holding, the gauge is read this often for the drop and the cap (power back is seen on
// every pass).
constexpr uint32_t HOLD_CHECK_EVERY_MS = 60UL * 1000UL;

bool liveSleepActive() { return liveSession.active; }

LiveHold liveHoldStatus() {
  LiveHold hold;
  if (!liveSession.active || !liveSession.holding) return hold;
  hold.holding = true;
  hold.startSoc = liveSession.holdStartSoc;
  hold.heldMs = millis() - liveSession.holdSince;
  return hold;
}

#if CROSSPOINT_BENCH_CONSOLE
static bool powerFakedAbsent = false;
void setExternalPowerFakedAbsent(const bool absent) { powerFakedAbsent = absent; }
bool externalPowerFakedAbsent() { return powerFakedAbsent; }
#endif

// A computer on the USB cable (SOF frames), sampled once per loop pass. The
// bench console tracks it in dev builds; release builds read the same monitor.
static bool usbHostAttached() {
#if CROSSPOINT_BENCH_CONSOLE
  return BenchConsole::hostPresent();
#elif ARDUINO_USB_MODE && SOC_USB_SERIAL_JTAG_SUPPORTED
  static power_policy::HostSeen seen;
  const uint32_t now = millis();
  seen.update(now, HWCDC::isPlugged());
  return seen.present(now);
#else
  return false;
#endif
}

// External power as live sleep reads it: the charger line or a computer, unless the bench's
// POWER fake absent hides both.
static bool externalPowerFrom(const bool stat, const bool usbHost) {
#if CROSSPOINT_BENCH_CONSOLE
  if (powerFakedAbsent) return false;
#endif
  return live_sleep::externalPower(stat, usbHost);
}

bool externalPowerPresent() { return externalPowerFrom(gpio.isUsbConnected(), usbHostAttached()); }

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

// Timer wake to arm beside the power button: the auto power-off timeout, or 0
// (Never, or a board whose rail cannot be cut).
static uint64_t autoPowerOffMicros() {
  return auto_power_off::timerMicros(
      SETTINGS.autoPowerOff, auto_power_off::canCutRail(BoardConfig::isX4Pro(), BoardConfig::ACTIVE.power.latch0));
}

#if FREEINK_DEVICE_X4PRO
// Set by powerOffAfterTimer() so the wake after a rail cut knows the panel was
// power-cycled: RTC memory survives the button-only sleep (USB power kept the
// SoC up) and is garbage on the cold boot a battery cut ends in, which takes
// the splash path anyway.
RTC_NOINIT_ATTR uint32_t railCutMagic;
constexpr uint32_t RAIL_CUT_MAGIC = 0x52414943;  // 'RAIC'

// Append one line to the sleep ledger on the card (format: util/SleepLedger.h).
// Best effort: a card that is not mounted or refuses the write costs nothing,
// and it must never keep the device awake.
static void appendLedgerLine(const char* line, const size_t len) {
  if (len == 0 || !Storage.ready()) return;
  HalFile f = Storage.open(sleep_ledger::PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (f) f.write(line, len);
}

static void sleepLedger(const char* event, const int extra) {
  // The boot's wake cause, taken on the first (boot) line: idle light sleeps
  // overwrite esp_sleep_get_wakeup_cause() later on.
  static const int bootWakeCause = static_cast<int>(esp_sleep_get_wakeup_cause());
  if (!Storage.ready()) return;
  sleep_ledger::Entry e;
  time_t now = 0;
  if (halClock.utcEpoch(now)) e.unixTime = static_cast<uint32_t>(now);
  e.event = event;
  e.uptimeS = static_cast<uint32_t>(millis() / 1000UL);
  e.resetReason = static_cast<int>(esp_reset_reason());
  e.wakeCause = bootWakeCause;
  e.extra = extra;
  e.socPercent = powerManager.getBatteryPercentage();
  e.millivolts = powerManager.getBatteryMillivolts();
  char line[sleep_ledger::LINE_CAP];
  appendLedgerLine(line, sleep_ledger::formatLine(line, sizeof line, e));
}

// Every power_ledger::WINDOW_MS while awake (not in USB Drive): one "pwr" line
// with what the window cost and what kept the chip awake (util/PowerLedger.h).
static void powerLedgerTick(const bool usbHost) {
  static uint32_t windowStartMs = millis();
  static uint32_t rendersAtStart = 0;
  static uint32_t sleepsAtStart = 0;
  static uint64_t sleptUsAtStart = 0;
  static power_ledger::WindowLatch latch;
  latch.note((Frontlight.present() && Frontlight.isOn()) ? Frontlight.brightness() : 0, powerManager.radioActive(),
             gpio.usbConnectedAtUpdate(), usbHost, liveSession.active, liveSession.active && liveSession.holding);
  const uint32_t now = millis();
  const uint32_t windowMs = now - windowStartMs;
  if (windowMs < power_ledger::WINDOW_MS) return;

  power_ledger::Window w;
  time_t unixNow = 0;
  if (halClock.utcEpoch(unixNow)) w.unixTime = static_cast<uint32_t>(unixNow);
  w.uptimeS = now / 1000UL;
  w.socPercent = powerManager.getBatteryPercentage();
  w.millivolts = powerManager.getBatteryMillivolts();
  w.cpuMhz = getCpuFrequencyMhz();
  latch.applyTo(w);
  w.renders = activityManager.renderCount() - rendersAtStart;
  w.lightSleeps = powerManager.lightSleepCount() - sleepsAtStart;
  w.lightSleepPermille = power_ledger::permille(powerManager.lightSleepMicros() - sleptUsAtStart,
                                                static_cast<uint64_t>(windowMs) * 1000ULL);
  w.lightSleepEnabled = powerManager.lightSleepEnabled();
  const auto heap = HalMemory::getInternalHeap();
  w.heapFree = static_cast<uint32_t>(heap.freeBytes);
  w.heapLargest = static_cast<uint32_t>(heap.largestBlockBytes);
  char line[power_ledger::LINE_CAP];
  appendLedgerLine(line, power_ledger::formatLine(line, sizeof line, w));

  windowStartMs = now;
  latch = power_ledger::WindowLatch{};
  rendersAtStart = activityManager.renderCount();
  sleepsAtStart = powerManager.lightSleepCount();
  sleptUsAtStart = powerManager.lightSleepMicros();
}

// At boot, at live sleep entry and hourly while live (the sleep and cut paths
// stay tiny): roll the ledger over once it passes its cap, keeping one previous file.
static void rotateSleepLedger() {
  if (!Storage.ready()) return;
  size_t bytes = 0;
  {
    HalFile f = Storage.open(sleep_ledger::PATH);
    if (!f) return;
    bytes = f.size();
  }
  if (!sleep_ledger::shouldRotate(bytes)) return;
  Storage.remove(sleep_ledger::ROTATED_PATH);
  Storage.rename(sleep_ledger::PATH, sleep_ledger::ROTATED_PATH);
}

// Second half of auto power-off: the timer armed by the last sleep has expired.
// The rail is still held up from that sleep, so the card can take the ledger
// line first (the RTC and gauge share the I2C bus halClock.begin() starts).
// Then the rail drops and the device sleeps on the power button alone.
// storageReady: setup() already mounted the card (a charging timer wake whose
// sleep screen turned out not to be live-capable).
[[noreturn]] static void powerOffAfterTimer(const bool storageReady = false) {
  if (!storageReady) halClock.begin();
  if (storageReady || Storage.begin()) {
    sleepLedger("cut", 0);
    Storage.prepareForDeepSleep();
  }
  railCutMagic = RAIL_CUT_MAGIC;
  powerManager.cutRailAndSleep();
}

// The charger STAT line at the top of setup(), before BatteryMonitor configures
// it: input with no pull, like BatteryMonitor (STAT is push-pull).
static bool chargerStatAtBoot() {
  const int8_t pin = BoardConfig::ACTIVE.batteryChargeStatus;
  if (pin < 0) return false;
  pinMode(pin, INPUT);
  return digitalRead(pin) == (BoardConfig::ACTIVE.batteryChargeStatusActiveHigh ? HIGH : LOW);
}
#else
// The ledger is an X4 Pro diagnostic; no other board writes it.
static void sleepLedger(const char*, int) {}
static void rotateSleepLedger() {}
static void powerLedgerTick(bool) {}
#endif

// The second half of a sleep: the sleep screen is on the panel; tear down and
// deep sleep (the auto power-off timer armed). ledgerExtra: the "sleep" line's x.
static void commitDeepSleep(const bool isQuickResumeSleep, const int ledgerExtra) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  deepSleepInProgress = true;
  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  } else if (Storage.exists(SLEEP_FRAME_FILE)) {
    // A stale Quick Resume frame must not replace the selected sleep screen during wake.
    Storage.remove(SLEEP_FRAME_FILE);
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  RadioPower::off();

  halTiltSensor.deepSleep();
  display.deepSleep();
  sleepLedger("sleep", ledgerExtra);
  Storage.prepareForDeepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio, autoPowerOffMicros());
}

// The live sleep screen instead of deep sleep (LiveSleep.h). The light goes off
// without touching the saved setting; the wake brings it back. Silent restarts
// stay possible afterwards (the wake is one), so the deep-sleep latch is held
// only while goToSleep() runs the outgoing activity's onExit().
static void enterLiveSleep(const int ledgerExtra, const bool fromTimeout, const bool popup) {
  liveSession = LiveSession{};
  liveSession.active = true;
  liveSession.unplug.start(millis());
  liveSession.rotatedAt = millis();
  liveSession.lightWasOn = Frontlight.present() && Frontlight.isOn();
  if (Frontlight.present()) Frontlight.setOn(false);
  rotateSleepLedger();
  sleepLedger("live", ledgerExtra);
  LOG_INF("SLP", "Live sleep: on external power, the sleep screen stays up");
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout, /*live=*/true, popup);
  deepSleepInProgress = false;
  // A radio the outgoing activity left up (or only stopped: RadioPower::stop() cannot be started
  // again) goes off, so the station keeper starts the driver afresh. The keeper starts only now:
  // its first scan would otherwise be torn down here (goToSleep() already ran one loop pass).
  RadioPower::off();
  StationKeeper::start();
}

// A key on the live screen: back to where the reader was, like a deep-sleep
// wake (a restart, which also clears the heap the Wi-Fi session fragmented).
static void wakeFromLiveSleep() {
  HalPowerManager::Lock powerLock;
  LOG_INF("SLP", "Live sleep: woken by a key");
  sleepLedger("livewake", 0);
  StationKeeper::stop();
  RadioPower::off();
  liveSession.active = false;
  // Restore Light on Wake, of the light as it was before the screen went live.
  const bool lightOn = liveSession.lightWasOn && SETTINGS.frontlightRestoreOnWake != 0;
  if (APP_STATE.lastSleepFromReader && !APP_STATE.openEpubPath.empty()) {
    silentRestartWithLight(SILENT_REBOOT_TARGET_READER, "reader", lightOn, /*liveWake=*/true);
  }
  if (APP_STATE.lastSleepApp != 0) silentRestartWithLight(SILENT_REBOOT_TARGET_APP, "app", lightOn, /*liveWake=*/true);
  silentRestartWithLight(SILENT_REBOOT_TARGET_HOME, "home", lightOn, /*liveWake=*/true);
}

// The live screen ends in today's deep sleep (unplugged, or the bench's SLEEP
// deep): one last redraw so the card's time and battery are those of this
// moment, then the unchanged commit. Auto Power Off counts from here.
static void endLiveSleep(const int ledgerExtra, const char* why) {
  HalPowerManager::Lock powerLock;
  LOG_INF("SLP", "Live sleep ends (%s): deep sleep", why);
  SleepActivity::finalLiveRedraw();
  StationKeeper::stop();
  liveSession.active = false;
  commitDeepSleep(false, ledgerExtra);
}

// The charger went idle at full (live_sleep::holdEligible): assume the cable is still in and
// stay up quietly. Wi-Fi goes off through RadioPower::off() - the driver torn down, never
// RadioPower::stop(), so StationKeeper::start() can bring it up again in this boot when power
// comes back, exactly as at live entry - which also lets the idle loop light-sleep between the
// redraws (SleepActivity: every max(Charging Updates, 15 min)).
static void enterHold(const uint32_t now) {
  HalPowerManager::Lock powerLock;
  liveSession.holding = true;
  liveSession.holdStartSoc = liveSession.socAtLoss;
  liveSession.holdSince = now;
  liveSession.holdCheckedAt = now;
  LOG_INF("SLP", "Live sleep: charger idle at %u %%, holding (Wi-Fi off) until power is back or it drops %u %%",
          liveSession.holdStartSoc, live_sleep::HOLD_DROP_PCT);
  sleepLedger("hold", static_cast<int>(liveSession.holdStartSoc));
  StationKeeper::stop();
  RadioPower::off();
  SleepActivity::setLiveHold(true);
}

// Power back while holding: fully live again (the keeper rejoins, the normal interval, the bolt).
static void leaveHoldPowerBack() {
  liveSession.holding = false;
  LOG_INF("SLP", "Live sleep: external power back, live again");
  sleepLedger("holdend", live_sleep::holdEndReason(live_sleep::HoldExit::PowerBack));
  SleepActivity::setLiveHold(false);
  StationKeeper::start();
}

// One hold pass: power back is seen at once; the gauge, every HOLD_CHECK_EVERY_MS. Returns true
// when the session ended (a drop or the cap: deep sleep).
static bool holdPass(const bool power, const uint32_t now) {
  if (power) {
    leaveHoldPowerBack();
    return false;
  }
  if (now - liveSession.holdCheckedAt < HOLD_CHECK_EVERY_MS) return false;
  liveSession.holdCheckedAt = now;
  const unsigned soc = powerManager.getBatteryPercentage();
  const auto end = live_sleep::holdExit(false, soc, liveSession.holdStartSoc, now - liveSession.holdSince);
  if (end == live_sleep::HoldExit::Stay) return false;
  const bool dropped = end == live_sleep::HoldExit::Drop;
  const int reason = live_sleep::holdEndReason(end);
  LOG_INF("SLP", "Live sleep: hold ends (%s, %u %% from %u %%)", dropped ? "dropped" : "cap", soc,
          liveSession.holdStartSoc);
  sleepLedger("holdend", reason);
  endLiveSleep(reason, dropped ? "unplugged at full" : "hold cap");
  return true;
}

// The idle end of a hold pass: one light-sleep slice, as the main loop's idle (the radio is off;
// HalPowerManager refuses under every guard, a computer on the cable included).
static void holdIdle(const bool usbHost) {
  bool slept = false;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock()) {
      HalPowerManager::LightSleepContext ctx;
      ctx.usbHost = usbHost;
      ctx.activityBusy = activityManager.preventAutoSleep() || activityManager.skipLoopDelay();
      ctx.renderQueued = activityManager.renderQueued();
      slept = powerManager.lightSleep(gpio, ctx);
    }
  }
  if (!slept) delay(LIVE_LOOP_DELAY_MS);
}

static bool anyKeyDown() {
  for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; button++) {
    if (gpio.isPressed(button)) return true;
  }
  return false;
}

// One main-loop pass while the live screen is up: no inactivity or power-hold
// sleep, no naps (the charger and the radio block them anyway) except in the
// full-charge hold. SleepActivity's loop() redraws and ticks the station keeper.
static void liveSleepLoop() {
  const uint32_t now = millis();
#if CROSSPOINT_BENCH_CONSOLE
  const uint8_t bench = BenchConsole::poll(/*exclusiveStorage=*/false, now);
  if (bench & BenchConsole::REQUEST_DEEP_SLEEP) {
    endLiveSleep(SLEEP_AFTER_BENCH, "bench");
    return;
  }
  if (bench & (BenchConsole::REQUEST_SLEEP | BenchConsole::REQUEST_REDRAW)) SleepActivity::requestLiveRedraw();
#endif
  // The unplug first, on every pass: nothing (not even a key held down) keeps the screen live
  // on the battery.
  const bool usbHost = usbHostAttached();
  const bool power = externalPowerFrom(gpio.usbConnectedAtUpdate(), usbHost);
  const bool wasAbsent = liveSession.unplug.absent();
  liveSession.unplug.note(power, now);
  // The SOC at the moment power vanished decides the hold (the gauge read once, not every pass).
  if (!wasAbsent && liveSession.unplug.absent()) liveSession.socAtLoss = powerManager.getBatteryPercentage();
  if (liveSession.holding) {
    if (holdPass(power, now)) return;
  } else if (liveSession.unplug.unplugged(now)) {
    LOG_INF("SLP", "Live sleep: no external power for %lu s (charger line low, no USB host), %u %%",
            static_cast<unsigned long>(live_sleep::UNPLUG_DEBOUNCE_MS / 1000), liveSession.socAtLoss);
    if (!live_sleep::holdEligible(liveSession.socAtLoss)) {
      endLiveSleep(SLEEP_AFTER_UNPLUG, "unplugged");
      return;
    }
    enterHold(now);
  }

  // Any physical key wakes it (not touch). The wake waits until every key is up
  // so the press cannot also act on what comes back (page turns fire on release),
  // but not for a key that stays down (live_sleep::wakeNow).
  if (!liveSession.wakePending && gpio.wasAnyPressed()) {
    liveSession.wakePending = true;
    liveSession.wakePressAt = now;
  }
  if (liveSession.wakePending) {
    if (live_sleep::wakeNow(anyKeyDown(), now - liveSession.wakePressAt)) wakeFromLiveSleep();
    delay(10);
    return;
  }

  if (now - liveSession.rotatedAt >= LIVE_ROTATE_EVERY_MS) {
    liveSession.rotatedAt = now;
    rotateSleepLedger();
  }
  powerLedgerTick(usbHost);

  activityManager.loop();
  powerManager.setPowerSaving(true);  // refused while a radio is up
  if (liveSession.holding) {
    holdIdle(usbHost);
    return;
  }
  delay(LIVE_LOOP_DELAY_MS);
}

// Show the sleep screen and deep sleep, or keep it live on external power
// (LiveSleep.h). forceDeep: the bench's SLEEP deep.
void enterDeepSleep(bool fromTimeout = false, bool forceDeep = false) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  const bool live = !forceDeep && live_sleep::liveEligible(BoardConfig::isX4Pro(), externalPowerPresent(),
                                                           SETTINGS.sleepScreen, SETTINGS.cardCycleWhenCharging != 0);
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();
  // Before the save below: the app's own onExit runs too late to record it.
  APP_STATE.lastSleepApp = APP_STATE.lastSleepFromReader ? 0 : activityManager.resumeApp();

  // Live wins over Quick Resume after timeout: its screen is redrawn.
  const bool isQuickResumeSleep =
      !live && (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
                (fromTimeout && SETTINGS.quickResumeSleepScreen ==
                                    CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT));
  // Every sleep mode leaves a complete retained frame on the e-ink panel. Keep
  // it visible until the first useful reader or home paint replaces it.
  APP_STATE.showBootScreen = false;

  APP_STATE.saveToFile();

  if (live) {
    enterLiveSleep(fromTimeout ? LIVE_FROM_TIMEOUT : LIVE_FROM_BUTTON, fromTimeout, /*popup=*/true);
    return;
  }

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);
  commitDeepSleep(isQuickResumeSleep, fromTimeout ? 1 : 0);
}

// Reopens the app the last sleep left (APP_STATE.lastSleepApp). One-shot, cleared and saved first:
// an app that crashes on load can never boot-loop (the X4 Pro has no Back key to hold). A single
// replace: a push after BootActivity would drop the pending replace.
static void clearLastSleepApp() {
  if (APP_STATE.lastSleepApp == 0) return;
  APP_STATE.lastSleepApp = 0;
  APP_STATE.saveToFile();
}

static void resumeLastApp() {
  const uint8_t app = APP_STATE.lastSleepApp;
  clearLastSleepApp();
  if (app == WordSearchActivity::APP_ID) {
    activityManager.goToWordSearch();
  } else if (app == CrosswordActivity::APP_ID) {
    activityManager.goToCrossword();
  } else if (app == SudokuActivity::APP_ID) {
    activityManager.goToSudoku();
  } else if (app == GuideScreen::APP_ID) {
    activityManager.goToGuide(/*resume=*/true);
  } else {
    activityManager.goHome();
  }
}

void setupDisplayAndFonts(bool seamless = false) {
#if !FREEINK_MCU_C3
  // C3 resolves its controller in HalGPIO::begin() before SPI claims the
  // display pins. X4 Pro skips that C3-only path, so probe here before
  // display.begin() selects and initializes its panel driver.
  static bool controllerResolved = false;
  if (!controllerResolved) {
    controllerResolved = true;
    if (freeink::applyXteinkDisplayController()) {
      LOG_DBG("MAIN", "Panel controller: UltraChip UC81xx variant detected");
    }
  }
#endif

  display.begin(seamless);
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
#if FREEINK_DEVICE_X4PRO
  // Auto power-off: a timer wake can only come from startDeepSleep(gpio,
  // powerOffAfterUs). Decided before holdPowerRails() re-asserts the rail
  // this is about to cut. The bitmap form sees a button press that landed in
  // the timer's instant (the single-cause call reports the timer first). A
  // reader plugged in while it slept is not cut: it boots into the live sleep
  // screen below (once the settings say its sleep screen can be live).
  bool chargingTimerWake = false;
  {
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    const bool timerWake = (causes & (1u << ESP_SLEEP_WAKEUP_TIMER)) != 0;
    const bool buttonWake = (causes & ((1u << ESP_SLEEP_WAKEUP_EXT1) | (1u << ESP_SLEEP_WAKEUP_GPIO))) != 0;
    const bool railCuttable = auto_power_off::canCutRail(BoardConfig::isX4Pro(), BoardConfig::ACTIVE.power.latch0);
    const bool charging = timerWake && chargerStatAtBoot();
    if (auto_power_off::shouldCutRailOnWake(timerWake, buttonWake, railCuttable, charging)) {
      powerOffAfterTimer();
    }
    chargingTimerWake = timerWake && !buttonWake && railCuttable && charging;
  }
  const bool railWasCut = railCutMagic == RAIL_CUT_MAGIC;
  railCutMagic = 0;
#else
  constexpr bool railWasCut = false;
#endif
  BoardConfig::holdPowerRails();

#ifdef ENABLE_SERIAL_LOG
#ifdef CROSSPOINT_WAIT_FOR_USB_SERIAL
  // Development builds preserve reliable early CDC logs; release builds let
  // enumeration proceed asynchronously so users do not pay this startup cost.
  delay(250);
#endif
#if CROSSPOINT_BENCH_CONSOLE
  // Must precede begin(), which otherwise creates the default 256-byte queue.
  logSerial.setRxBufferSize(BenchConsole::RX_BUFFER_BYTES);
#endif
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  // checkPanic() clears the watchdog capture marker after a successful SD
  // dump, so retain the boot classification for the later activity route.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_MAX) ? silentRebootTarget : 0;
  const bool silentRebootLightOn = isSilentReboot && (silentRebootPayload & SILENT_REBOOT_LIGHT_ON) != 0;
  const bool silentRebootLiveWake = isSilentReboot && (silentRebootPayload & SILENT_REBOOT_LIVE_WAKE) != 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;
  silentRebootPayload = 0;
  // The live sleep screen's wake reopens the book like a deep-sleep wake: a fresh open (Pull on
  // Open runs), not a sync's continuation.
  BookSyncHooks::onBoot(isSilentReboot, snapshotTarget == SILENT_REBOOT_TARGET_READER && !silentRebootLiveWake);

  gpio.begin();
  powerManager.begin();

  const auto wakeupReason = gpio.getWakeupReason();
  // Sample the wake hold now — a click wake is released within milliseconds of
  // boot — but defer the sleep-or-boot decision until SETTINGS is loaded below:
  // click-to-wake is a setting, and an X4 battery power-off cuts all power, so
  // only SD state survives to the next boot.
  const bool wakeHoldVerified = wakeupReason != HalGPIO::WakeupReason::PowerButton || gpio.verifyPowerButtonWakeup();

  // X4 Pro and X4 Classic both map BTN_UP to GPIO0 — an ESP32-S3 boot strap — so
  // gate recovery on the non-strap Down key (GPIO7) to avoid a stuck-in-recovery loop.
  const auto recoveryButton = (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? MappedInputManager::Button::Down
                                                                                     : MappedInputManager::Button::Up;
  const bool recoveryFirmwareMode = wakeupReason == HalGPIO::WakeupReason::PowerButton && !BoardConfig::isPaperMono() &&
                                    mappedInputManager.isPressed(recoveryButton);

  halTiltSensor.begin();
  halClock.begin();

#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");
#else
  LOG_INF("MAIN", "Device: %s", BoardConfig::ACTIVE.name);
#endif

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();

  {
    // The SDK's record of an esp_deep_sleep_start() that returned: that
    // "sleep" never happened (boot line x bit 1).
    const auto aborted = freeink::PowerManager::takeAbortedSleepInfo();
    if (aborted.aborted) {
      // cause= is whatever the wake-cause register held at the abort: after an
      // idle light sleep that is the last nap's cause, not the abort's.
      LOG_ERR("PWR", "Previous sleep entry aborted: cause=%d powerPin=%d", aborted.wakeupCause, aborted.wakePinLevel);
    }
    rotateSleepLedger();
    sleepLedger("boot", (aborted.aborted ? 1 : 0) | (railWasCut ? 2 : 0));
  }

  APP_STATE.loadFromFile();
  const bool isSleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  // A rail cut power-cycled the panel, so its retained frame is gone: that wake
  // takes the splash path (full clear + resync) like the cold boot it mimics.
  const bool isPersistedSleepWake = isSleepWake && !APP_STATE.showBootScreen && !railWasCut;

  if (recoveryFirmwareMode) {
    LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)",
            (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? "DOWN" : "UP");
  }

  // Touch boards default the reader menu to the toolbar overlay instead of the
  // full-screen list. Seeded before the load: fromJson() falls back to the
  // in-memory value only when the file carries no readerMenuStyle key, so a
  // user's saved choice (either style) still wins.
  if (gpio.hasTouch()) {
    SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  }
  SETTINGS.loadFromFile();
  // Push the saved timezone's POSIX rule into the clock (migrating the legacy
  // UTC-offset setting on first boot after the update).
  timezones::applyToClock();
  RECENT_BOOKS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  BOOKSYNC_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  // The auto power-off timer fired on the charger: straight back into the live
  // sleep screen, or the cut it was armed for when the screen cannot be live.
  bool liveBoot = false;
#if FREEINK_DEVICE_X4PRO
  if (chargingTimerWake) {
    liveBoot = live_sleep::liveEligible(BoardConfig::isX4Pro(), externalPowerPresent(), SETTINGS.sleepScreen,
                                        SETTINGS.cardCycleWhenCharging != 0);
    if (!liveBoot) powerOffAfterTimer(/*storageReady=*/true);
  }
#endif

  // Brightness and warmth are always restored. A normal wake starts with the
  // light off unless Restore Light on Wake is enabled; silent maintenance
  // reboots replay the live state captured at restart, so they neither go dark
  // nor light up against the user's wake preference.
  const bool restoreLightOn =
      !liveBoot &&
      (isSilentReboot ? silentRebootLightOn : (SETTINGS.frontlightOn != 0 && SETTINGS.frontlightRestoreOnWake != 0));
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth, restoreLightOn);

  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      // With Short Power Button Press = Sleep, a single click wakes on any
      // device; otherwise the button must still be held (ghost-wake debounce).
      if (!wakeHoldVerified && SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::SLEEP) {
        LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
        sleepLedger("resleep", 1);
        Storage.prepareForDeepSleep();
        // Re-armed from scratch: a ghost wake must not cancel the auto power-off.
        powerManager.startDeepSleep(gpio, autoPowerOffMicros());
      }
      wakePowerReleasePending = true;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // Most devices return to sleep after a USB-powered cold boot.
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_X4CLASSIC || FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_EEGO_A4
      // X4 Pro must stay awake so USB Serial/JTAG remains available after leaving
      // USB Drive and reconnecting the cable. Paper Mono has no armable GPIO wake
      // (its button is behind the PMIC). EEGO A4's post-flash reset reads as
      // POWERON (native-USB), so a flash would otherwise be misclassified as a
      // USB-power cold boot and sleep. Sleeping any of these here would strand
      // the device in a USB-replug boot loop (or sleep right after a flash).
      break;
#else
      Storage.prepareForDeepSleep();
      powerManager.startDeepSleep(gpio);
      break;
#endif
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Timer:
      // Only armed where setup() already cut the rail above; boot otherwise.
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  // Only a verified deep-sleep wake may use the one-shot persisted flag.
  // Otherwise a stale flag could suppress the splash on a cold boot.
  const BootResume resume = isSilentReboot         ? BootResume::Silent
                            : isPersistedSleepWake ? BootResume::SplashlessWake
                            : liveBoot             ? BootResume::LiveSleep
                                                   : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;
  bool needsWakeRefresh = false;

  setupDisplayAndFonts(resume != BootResume::Splash);

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::SplashlessWake:
      // One-shot flag: re-arm the splash for the next ordinary boot. Save
      // before any painting so a hang in the blocking paint path can't strand
      // us in a splashless-with-no-frame loop on the next boot.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      if (Storage.exists(SLEEP_FRAME_FILE) && loadSleepFrameBuffer()) {
        if (gpio.deviceIsX3()) {
          // begin() clears the X3 controller RAM, so restore the saved frame as
          // the baseline for the first reader paint without refreshing the panel.
          renderer.cleanupGrayscaleWithFrameBuffer();
          allowFastInitialReaderRefresh = true;
        }
      } else {
        // Clean the retained sleep image as part of the first Home paint.
        needsWakeRefresh = true;
      }
      break;
    case BootResume::Splash:
      activityManager.goToBoot();
      break;
    case BootResume::LiveSleep:
      // The panel keeps the card it slept with until the live screen's first draw.
      break;
  }

  // Output polarity is resolved per render by ActivityManager (night mode
  // inverts only the reading surfaces), so nothing to restore here.

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (rebootedFromPanic) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::LiveSleep) {
    // As it went to sleep: lastSleepFromReader and the open book are as saved then.
    enterLiveSleep(LIVE_FROM_TIMER_BOOT, /*fromTimeout=*/false, /*popup=*/false);
    liveSession.lightWasOn = SETTINGS.frontlightOn != 0;
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_SETTINGS) {
    // Back out of the WiFi rows and the user is where they left off, not on Home.
    activityManager.goToSettings();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_SLEEP_CARDS) {
    // Back from Locate Me: the Location row shows what was saved.
    activityManager.goToSleepCardSettings();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_APP &&
             APP_STATE.lastSleepApp != 0) {
    resumeLastApp();
  } else if (resume == BootResume::Silent && BookSyncHooks::bootToLibrary()) {
    // A sync on closing a book with the library-bound Back finishes there.
    activityManager.goToFileBrowser(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    clearLastSleepApp();
    activityManager.goHome();
  } else if (APP_STATE.lastSleepApp != 0 && !mappedInputManager.isPressed(MappedInputManager::Button::Back)) {
    // Asleep inside an app (deep wake, a wake after Auto Power Off cut the rail, a cold boot or a
    // flash): back into it, as the reader reopens its book.
    resumeLastApp();
  } else if (APP_STATE.lastSleepApp != 0) {
    // Back held skips the app, and for good: the next boot must not reopen it either.
    clearLastSleepApp();
    activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path, allowFastInitialReaderRefresh);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  allowSleepAt = millis() + 2000;
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  mappedInputManager.update();

  if (activityManager.requiresExclusiveStorageLoop()) {
    // USB Drive handed the raw SD card to the host. Do not run screenshots,
    // sleep, shortcuts, or normal navigation while its filesystem is detached.
#if CROSSPOINT_BENCH_CONSOLE
    // Dev builds: the bench console still serves SHOT, STATE and injected input
    // here (they touch no files); its file verbs and SLEEP refuse.
    BenchConsole::poll(/*exclusiveStorage=*/true, 0);
#endif
    activityManager.loop();
    if (activityManager.preventAutoSleep()) {
      powerManager.setPowerSaving(false);
      delay(10);
    } else {
      // No host is active, so a slower loop is safe. The activity itself times
      // out the raw-storage handoff rather than entering deep sleep detached.
      powerManager.setPowerSaving(true);
      delay(50);
    }
    return;
  }

  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

  renderer.setFadingFix(SETTINGS.fadingFix);

  // The ROM console does not depend on Arduino USB CDC's connection state.
  if ((Serial || FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF) && millis() - lastMemPrint >= 10000) {
    const auto heap = HalMemory::getInternalHeap();
    LOG_INF("MEM", "Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes", heap.freeBytes,
            heap.totalBytes, heap.minFreeBytes, heap.largestBlockBytes);
#ifdef BOARD_HAS_PSRAM
    const auto psram = HalMemory::getPsramHeap();
    LOG_INF("MEM", "PSRAM: Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes",
            psram.freeBytes, psram.totalBytes, psram.minFreeBytes, psram.largestBlockBytes);
#endif
    lastMemPrint = millis();
  }

  if (liveSession.active) {
    liveSleepLoop();
    return;
  }

#if CROSSPOINT_BENCH_CONSOLE
  // The bench console owns the serial input (it also serves CMD:SCREENSHOT).
  static unsigned long lastActivityTime = millis();
  const uint8_t benchResult = BenchConsole::poll(/*exclusiveStorage=*/false, lastActivityTime);
  if (benchResult & BenchConsole::REQUEST_SLEEP) {
    // The auto-sleep path; SLEEP deep skips the live screen.
    enterDeepSleep(true, (benchResult & BenchConsole::REQUEST_DEEP_SLEEP) != 0);
    return;
  }
  const bool benchActivity = (benchResult & BenchConsole::USER_ACTIVITY) != 0;
#else
  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  constexpr bool benchActivity = false;
#endif
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep() || benchActivity) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  // Let wake continue as soon as its hold has been verified. The release can
  // arrive after setup, so consume that one input frame rather than making it
  // a page turn, refresh, or other short power-button action.
  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  // Consume the second X4 Pro power-button release so it does not also run a
  // configured short-power action after toggling the frontlight.
  if (handleX4ProFrontlightDoubleClick()) {
    return;
  }

  const bool x4ProDoubleClickPwrLight = BoardConfig::isX4Pro() && SETTINGS.doubleClickPwrLight;

#if FREEINK_CAP_TOUCH
  // A single X4 Pro power click becomes Confirm only after the frontlight
  // double-click window expires without a second click.
  mappedInputManager.setPowerConfirmClickFrame(false);
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM && x4ProDoubleClickPwrLight) {
    if (lastX4ProPowerClickAt != 0 && millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
      lastX4ProPowerClickAt = 0;
      mappedInputManager.setPowerConfirmClickFrame(true);
    }
    // A release held too long to be a double-click candidate (but still within
    // the normal Confirm press duration) never reaches handleX4ProFrontlightDoubleClick's
    // click tracking above, so it needs its own Confirm check here.
    if (mappedInputManager.wasReleased(MappedInputManager::Button::Power) &&
        gpio.getPowerButtonHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS &&
        gpio.getPowerButtonHeldTime() <= SETTINGS.getPowerButtonDuration()) {
      mappedInputManager.setPowerConfirmClickFrame(true);
    }
  }
#endif

  // Same deferral for SLEEP: getPowerButtonDuration() drops to 10ms so a quick
  // tap sleeps the device, which otherwise fires on button-down and never lets
  // a second click land. Sleep only once the double-click window has passed.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP && x4ProDoubleClickPwrLight &&
      lastX4ProPowerClickAt != 0 && millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = 0;
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
#if CROSSPOINT_BENCH_CONSOLE
  // A computer on the USB cable cannot wake a deep-sleeping reader (USB powers
  // down with the chip), so the inactivity sleep waits while a host is attached.
  const bool benchHoldsAwake = BenchConsole::hostPresent();
#else
  constexpr bool benchHoldsAwake = false;
#endif
  if (sleepTimeoutMs > 0 && !benchHoldsAwake && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // A hold that woke the device must be released before it can count as a new
  // in-app long press. Otherwise a user who keeps holding after wake would put
  // the device straight back to sleep once allowSleepAt expires.
  static bool powerReleasedSinceWake = false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER)) powerReleasedSinceWake = true;

  // On X4 Pro with SLEEP, a press still within the click window is a
  // double-click candidate — let it be released and evaluated above instead
  // of sleeping on button-down.
  const bool x4ProAwaitingClickWindow = x4ProDoubleClickPwrLight &&
                                        SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP &&
                                        gpio.getPowerButtonHeldTime() <= X4PRO_POWER_CLICK_MAX_HOLD_MS;

  if (!x4ProAwaitingClickWindow && powerReleasedSinceWake && millis() >= allowSleepAt &&
      gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    LOG_DBG("MAIN", "Power button held %lums, sleeping", gpio.getPowerButtonHeldTime());
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

#if FREEINK_DEVICE_PAPERMONO
  // Paper Mono reports the PMIC power button as a one-tick click, so the held
  // path above cannot fire. With the default Ignore action, retain the normal
  // power-button meaning and shut down; explicit alternate bindings still win.
  if ((SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP ||
       SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::IGNORE) &&
      millis() >= allowSleepAt && mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    enterDeepSleep();
    return;
  }
#endif

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (mappedInputManager.homeButtonAction() == HomeButtonAction::ToggleFrontlight) {
    toggleFrontlight();
  }
  if (mappedInputManager.homeButtonAction() == HomeButtonAction::Refresh ||
      (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
       mappedInputManager.wasReleased(MappedInputManager::Button::Power))) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  // Not while reading: there a repaint is a full page re-render (visible
  // flash, the AA pass re-running, and a frontlight dip under the refresh
  // load); the reader's status bar picks the charging state up on the next
  // page turn instead.
  if (gpio.wasUsbStateChanged() && !activityManager.isReaderActivity()) {
    activityManager.requestUpdate();
  }

  const bool usbHost = usbHostAttached();
  powerLedgerTick(usbHost);

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  bool skipLoopDelay = false;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) {
      // Let rendering advance without treating lock contention as idle.
      delay(10);
      return;
    }
    skipLoopDelay = activityManager.skipLoopDelay();
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (skipLoopDelay) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::idlePowerSavingMs()) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      // X4 Pro: one light-sleep slice, holding the RenderLock so no render,
      // SPI or SD transfer is frozen mid-flight. lightSleep() refuses
      // under every guard in power_policy (radio, USB, light, input, ...).
      bool slept = false;
      {
        RenderLock lock(RenderLock::Mode::Try);
        if (lock.ownsLock()) {
          HalPowerManager::LightSleepContext ctx;
          ctx.usbHost = usbHost;
          ctx.activityBusy = activityManager.preventAutoSleep() || activityManager.skipLoopDelay();
          ctx.renderQueued = activityManager.renderQueued();
          slept = powerManager.lightSleep(gpio, ctx);
        }
      }
      if (!slept) {
        // Sleep in short slices and wake the poll as soon as a button contact closes.
        // InputManager commits a press only when two consecutive polls agree, so a
        // press shorter than one 50 ms sleep could land in a single sample and be lost.
        const unsigned long idleStart = millis();
        while (millis() - idleStart < 50) {
          delay(10);
          if (gpio.rawInputActive()) break;
        }
      }
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
