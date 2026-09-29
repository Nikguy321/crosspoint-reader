#include "BenchConsole.h"

#if CROSSPOINT_BENCH_CONSOLE

#ifndef ENABLE_SERIAL_LOG
#error "CROSSPOINT_BENCH_CONSOLE needs ENABLE_SERIAL_LOG: the console talks over logSerial"
#endif

#include <Arduino.h>
#include <BenchInjection.h>
#include <BenchProtocol.h>
#include <BoardConfig.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <Memory.h>
#include <PowerPolicy.h>

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "activities/Activity.h"  // ActivityManager and RenderLock, with Activity complete
#include "activities/boot_sleep/SleepCardPreviewActivity.h"
#include "sleepcards/SleepCard.h"
#include "util/HomeButtonInput.h"
#include "util/PowerButtonTiming.h"
#include "util/TaskWatchdog.h"

extern GfxRenderer renderer;                   // defined in main.cpp
extern MappedInputManager mappedInputManager;  // defined in main.cpp

namespace BenchConsole {
namespace {

constexpr size_t LINE_CAP = 512;
constexpr size_t REPLY_CAP = 400;
constexpr unsigned long PUT_IDLE_TIMEOUT_MS = 5000;
constexpr unsigned long PUT_PAYLOAD_TIMEOUT_MS = 2000;
constexpr unsigned long DRAIN_QUIET_MS = 100;
constexpr unsigned long DRAIN_MAX_MS = 3000;
constexpr unsigned long SHOT_SETTLE_TIMEOUT_MS = 5000;
// A line the host does not take within this long marks the link stalled (the
// host stopped reading while USB stays up): later lines are dropped at once
// until the host sends something, so the main loop never waits on it.
constexpr unsigned long LINE_TX_BUDGET_MS = 100;
constexpr unsigned long WDT_FEED_MS = 250;
constexpr uint16_t OPEN_MAX_PASSES = 200;
// LS: a FAT long name is up to 255 UTF-16 units, 765 bytes of UTF-8.
constexpr size_t LS_NAME_CAP = 800;
constexpr size_t LS_LINE_CAP = LS_NAME_CAP + 48;
constexpr uint32_t TAP_HOLD_MS = 80;
constexpr uint32_t LONGTAP_HOLD_MS = 800;
constexpr uint32_t SWIPE_MS = 250;

// Static rather than stack: the console runs only on the loop task, and these
// would otherwise put ~1.5 KB on it per call.
char lineBuf[LINE_CAP];
size_t lineLen = 0;
bool lineOverflow = false;
char replyBuf[REPLY_CAP];
// PUT's part-file path; OPEN's path while its switch is pending (never both).
char pathBuf[bench::MAX_PATH_LEN + sizeof(bench::PART_SUFFIX) + 1];
char nameBuf[256];
bool txStalled = false;

bench::HostPresence host;

enum class Pending : uint8_t { None, Input, Shot, Open, Card };
struct PendingState {
  Pending kind = Pending::None;
  char verb[12] = {};
  char detail[48] = {};
  unsigned long deadline = 0;
  uint16_t passes = 0;
  uint8_t idleStreak = 0;
  bench::InputWaiter waiter;
};
PendingState pend;

const char* orientationName(const int o) {
  switch (o) {
    case GfxRenderer::Portrait:
      return "portrait";
    case GfxRenderer::LandscapeClockwise:
      return "landscape_cw";
    case GfxRenderer::PortraitInverted:
      return "portrait_inv";
    default:
      return "landscape_ccw";
  }
}

// One write per line so another task's log output cannot split it. A write
// that the 1 ms TX timeout cuts short is continued within LINE_TX_BUDGET_MS;
// past that the link counts as stalled. Returns false when the line was not
// (completely) sent.
bool writeAll(const char* data, size_t len) {
  if (txStalled) return false;
  const unsigned long start = millis();
  while (len > 0) {
    const size_t n = logSerial.write(reinterpret_cast<const uint8_t*>(data), len);
    if (n == 0 || n > len) {
      if (millis() - start > LINE_TX_BUDGET_MS) {
        txStalled = true;
        return false;
      }
      delay(1);
      continue;
    }
    data += n;
    len -= n;
  }
  return true;
}

// Formats "@B <fmt>\n" into buf (truncating the payload to fit) and writes it.
bool vreplyInto(char* buf, const size_t cap, const char* fmt, va_list args) {
  memcpy(buf, "@B ", 3);
  const int n = vsnprintf(buf + 3, cap - 4, fmt, args);
  if (n < 0) return false;
  size_t len = 3 + std::min(static_cast<size_t>(n), cap - 5);
  buf[len++] = '\n';
  return writeAll(buf, len);
}

bool reply(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const bool sent = vreplyInto(replyBuf, REPLY_CAP, fmt, args);
  va_end(args);
  return sent;
}

bool replyInto(char* buf, const size_t cap, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const bool sent = vreplyInto(buf, cap, fmt, args);
  va_end(args);
  return sent;
}

// A short wait inside the console's blocking loops (PUT, drain) that keeps
// the task watchdog fed however long the wait runs.
void idleWait(unsigned long& lastFeed) {
  delay(1);
  if (millis() - lastFeed >= WDT_FEED_MS) {
    resetTaskWatchdogIfSubscribed();
    lastFeed = millis();
  }
}

bool storageBusy(const bool exclusive) { return exclusive || activityManager.requiresExclusiveStorageLoop(); }

// Common guard for file verbs. Replies ERR and returns false when refused.
bool fileVerbAllowed(const char* verb, const bool exclusive) {
  if (storageBusy(exclusive)) {
    reply("ERR %s busy", verb);
    return false;
  }
  if (!Storage.ready()) {
    reply("ERR %s nosd", verb);
    return false;
  }
  return true;
}

bool isDirectory(const char* path) {
  HalFile f = Storage.open(path);
  return f && f.isDirectory();
}

// A PSRAM buffer when available (the X4 Pro has 8 MB), else internal heap.
struct ScratchBuffer {
  HalMemory::PsramBuffer psram;
  std::unique_ptr<uint8_t[]> internal;
  uint8_t* data = nullptr;
  explicit ScratchBuffer(const size_t bytes) : psram(HalMemory::allocatePsram(bytes)) {
    if (psram) {
      data = psram.get();
      return;
    }
    internal = makeUniqueNoThrow<uint8_t[]>(bytes);
    data = internal.get();
  }
};

bool md5File(const char* path, char hexOut[33], uint64_t& sizeOut) {
  HalFile file;
  if (!Storage.openFileForRead("BENCH", path, file)) return false;
  ScratchBuffer buf(bench::PUT_CHUNK_MAX);
  if (!buf.data) {
    LOG_ERR("BENCH", "OOM: md5 buffer");
    return false;
  }
  MD5Builder md5;
  md5.begin();
  uint64_t total = 0;
  unsigned chunks = 0;
  while (true) {
    const int n = file.read(buf.data, bench::PUT_CHUNK_MAX);
    if (n < 0) return false;
    if (n == 0) break;
    md5.add(buf.data, static_cast<size_t>(n));
    total += static_cast<uint64_t>(n);
    if (++chunks % 16 == 0) {
      yield();
      resetTaskWatchdogIfSubscribed();
    }
  }
  md5.calculate();
  md5.getChars(hexOut);
  sizeOut = total;
  return true;
}

// --- Raw reads used inside PUT ------------------------------------------------

enum class ReadResult : uint8_t { Ok, Timeout, Overflow };

ReadResult readLineUntil(char* out, const size_t cap, const unsigned long deadline) {
  size_t n = 0;
  bool overflow = false;
  unsigned long lastFeed = millis();
  while (true) {
    const int c = logSerial.read();
    if (c < 0) {
      if (static_cast<long>(millis() - deadline) >= 0) return ReadResult::Timeout;
      idleWait(lastFeed);
      continue;
    }
    if (c == '\r') continue;
    if (c == '\n') break;
    if (n + 1 < cap) {
      out[n++] = static_cast<char>(c);
    } else {
      overflow = true;
    }
  }
  out[n] = '\0';
  return overflow ? ReadResult::Overflow : ReadResult::Ok;
}

// Reads up to len bytes; returns how many arrived before the timeout.
size_t readUpTo(uint8_t* out, const size_t len, const unsigned long timeoutMs) {
  size_t got = 0;
  const unsigned long start = millis();
  unsigned long lastFeed = start;
  while (got < len) {
    const size_t n = logSerial.read(out + got, len - got);
    if (n > 0 && n <= len - got) {
      got += n;
      continue;
    }
    if (millis() - start >= timeoutMs) break;
    idleWait(lastFeed);
  }
  return got;
}

// Discard input until the line has been quiet for DRAIN_QUIET_MS, so a resend
// starts on a clean header after a lost or corrupted chunk.
void drainInput() {
  const unsigned long start = millis();
  unsigned long lastByte = millis();
  unsigned long lastFeed = start;
  while (millis() - lastByte < DRAIN_QUIET_MS && millis() - start < DRAIN_MAX_MS) {
    if (logSerial.read() >= 0) {
      lastByte = millis();
    } else {
      idleWait(lastFeed);
    }
  }
}

// --- Verbs -------------------------------------------------------------------

// Appends an activity name with spaces and commas replaced, so it never
// breaks the space-separated k=v form or the comma-separated stack list.
size_t appendName(size_t len, const char* name) {
  if (*name == '\0') name = "-";
  for (; *name != '\0' && len + 1 < sizeof(nameBuf); ++name) {
    nameBuf[len++] = (*name == ' ' || *name == ',') ? '_' : *name;
  }
  nameBuf[len] = '\0';
  return len;
}

void cmdState(const bool exclusive, const unsigned long lastActivityMs) {
  // Activity and stack (bottom first).
  const size_t depth = activityManager.benchStackDepth();
  size_t len = static_cast<size_t>(snprintf(nameBuf, sizeof(nameBuf), "STATE act="));
  len = appendName(len, activityManager.benchCurrentName());
  len += static_cast<size_t>(
      snprintf(nameBuf + len, sizeof(nameBuf) - len, " depth=%u stack=", static_cast<unsigned>(depth)));
  len = std::min(len, sizeof(nameBuf) - 1);
  if (depth == 0) len = appendName(len, "");
  for (size_t i = 0; i < depth && len + 2 < sizeof(nameBuf); ++i) {
    if (i > 0) nameBuf[len++] = ',';
    len = appendName(len, activityManager.benchStackName(i));
  }
  reply("%s", nameBuf);

  const auto heap = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  reply("STATE up=%lu heap=%u heapmin=%u heapmax=%u psram=%u", millis(), static_cast<unsigned>(heap.freeBytes),
        static_cast<unsigned>(heap.minFreeBytes), static_cast<unsigned>(heap.largestBlockBytes),
        static_cast<unsigned>(psram.freeBytes));

  // usb= is HalGPIO's USB reading; on the X4 Pro that is the charger status
  // line (0 again at full charge). host= (USB SOF frames) is the real "a
  // computer is attached" signal. Free space is DF's job: it can scan the FAT.
  const bool sdReady = !storageBusy(exclusive) && Storage.ready();
  const long long sdTotal = sdReady ? static_cast<long long>(Storage.sdTotalBytes()) : -1;
  // mv= is the gauge's VCELL (~0.3 mV/LSB); 0 on I2C failure
  reply("STATE bat=%u mv=%u usb=%d host=%d sd=%d sdtotal=%lld pwrshort=%u dblclick=%u",
        powerManager.getBatteryPercentage(), powerManager.getBatteryMillivolts(), gpio.isUsbConnected() ? 1 : 0,
        host.present() ? 1 : 0, sdReady ? 1 : 0, sdTotal, static_cast<unsigned>(SETTINGS.shortPwrBtn),
        static_cast<unsigned>(SETTINGS.doubleClickPwrLight));

  const unsigned long timeoutMs = SETTINGS.getSleepTimeoutMs();
  long left = -1;
  const char* held = "none";
  if (exclusive) {
    held = "usbdrive";
  } else if (timeoutMs == 0) {
    held = "never";
  } else {
    const unsigned long idle = millis() - lastActivityMs;
    left = idle >= timeoutMs ? 0 : static_cast<long>(timeoutMs - idle);
    if (host.present()) {
      held = "host";
    } else if (activityManager.preventAutoSleep()) {
      held = "activity";
    }
  }
  reply("STATE sleep_ms=%lu sleep_left=%ld held=%s orient=%s w=%d h=%d inv=%d exclusive=%d", timeoutMs, left, held,
        orientationName(renderer.getOrientation()), renderer.getScreenWidth(), renderer.getScreenHeight(),
        display.isInverted() ? 1 : 0, exclusive ? 1 : 0);
  // Idle light sleep since boot: ls = enabled (LS on|off), lsn = sleeps,
  // lsw = sleeps ended by a key or STAT (the rest end on the 50 ms timer),
  // lsms = time asleep, lsblk = why the last idle pass did not sleep (always
  // "host" while this console is attached), mhz = CPU clock now, radio = the
  // radio lock or Wi-Fi is up, fls = a lit frontlight keeps napping (its PWM
  // survives light sleep), xtal = the IDF's light-sleep XTAL request count (1
  // only while the light is lit at a nonzero duty), flrun = lit naps of at
  // least 45 ms whose PWM was measured running / all such naps.
  reply("STATE ls=%d lsn=%lu lsw=%lu lsms=%llu lsblk=%s mhz=%lu radio=%d fls=%d xtal=%ld flrun=%lu/%lu",
        powerManager.lightSleepEnabled() ? 1 : 0, static_cast<unsigned long>(powerManager.lightSleepCount()),
        static_cast<unsigned long>(powerManager.lightSleepGpioWakes()),
        static_cast<unsigned long long>(powerManager.lightSleepMicros() / 1000ULL), powerManager.lightSleepBlockName(),
        static_cast<unsigned long>(getCpuFrequencyMhz()), powerManager.radioActive() ? 1 : 0,
        Frontlight.survivesLightSleep() ? 1 : 0, static_cast<long>(Frontlight.sleepClockRequests()),
        static_cast<unsigned long>(Frontlight.napProbeRan()), static_cast<unsigned long>(Frontlight.napProbeChecked()));
  reply("OK STATE");
}

void startInput(const char* verb, const bool needsKeyEdge, const uint32_t holdMs, const uint32_t settleMs) {
  pend.kind = Pending::Input;
  snprintf(pend.verb, sizeof(pend.verb), "%s", verb);
  pend.waiter.start(static_cast<uint32_t>(millis()), holdMs, settleMs, needsKeyEdge);
}

// Whether a power press of this shape puts the reader to sleep, following
// main.cpp's power-button handling.
bool powerPressWouldSleep(const uint32_t hold, const uint8_t presses) {
  const bool doubleClickLight = BoardConfig::isX4Pro() && SETTINGS.doubleClickPwrLight;
  if (presses >= 2 && doubleClickLight && hold <= X4PRO_POWER_CLICK_MAX_HOLD_MS) return false;  // frontlight toggle
  return hold > SETTINGS.getPowerButtonDuration();
}

// KEY <name> [holdMs] [force]
void cmdKey(char* args) {
  char* nameTok = nullptr;
  char* tok = nullptr;
  if (!bench::nextToken(args, nameTok)) {
    reply("ERR KEY usage");
    return;
  }
  bench::KeyDef key{};
  if (!bench::lookupKey(nameTok, key)) {
    reply("ERR KEY badkey");
    return;
  }
  uint32_t hold = key.defaultHoldMs;
  bool holdGiven = false;
  bool force = false;
  while (bench::nextToken(args, tok)) {
    if (strcmp(tok, "force") == 0 && !force) {
      force = true;
    } else if (!holdGiven && bench::parseU32(tok, hold)) {
      holdGiven = true;
    } else {
      reply("ERR KEY usage");
      return;
    }
  }
  hold = bench::clampHoldMs(hold);
  if (gpio.benchInjectionBusy()) {
    reply("ERR KEY busy");
    return;
  }

  uint32_t settle = 0;
  if (key.kind == bench::KeyKind::Home) {
    if (!gpio.hasHomeKey()) {
      reply("ERR KEY nohome");
      return;
    }
    gpio.benchPressHomeKey(hold, key.presses, bench::DOUBLE_GAP_MS);
    // A single tap is dispatched only after the double-tap window when a
    // double-tap action is configured; wait for it so the host sees the result.
    if (key.presses == 1 && hold < bench::TouchInjector::HOME_LONG_PRESS_MS &&
        static_cast<HomeButtonAction>(SETTINGS.homeButtonDoubleTapAction) != HomeButtonAction::Ignore) {
      settle = HomeButtonInput::DOUBLE_TAP_MS + 60;
    }
  } else {
    if (key.buttonIndex == HalGPIO::BTN_POWER && !force && powerPressWouldSleep(hold, key.presses)) {
      // Deep sleep powers USB down: nothing on the computer can wake it again.
      reply("ERR KEY wouldsleep (add force, or use SLEEP)");
      return;
    }
    gpio.benchPressButton(key.buttonIndex, hold, key.presses, bench::DOUBLE_GAP_MS);
    // X4 Pro defers a single short power click by its frontlight double-click window.
    if (key.buttonIndex == HalGPIO::BTN_POWER && key.presses == 1 && BoardConfig::isX4Pro() &&
        SETTINGS.doubleClickPwrLight && hold <= X4PRO_POWER_CLICK_MAX_HOLD_MS) {
      settle = X4PRO_POWER_DOUBLE_CLICK_MS + 60;
    }
  }
  const uint32_t total = key.presses > 1 ? key.presses * hold + (key.presses - 1) * bench::DOUBLE_GAP_MS : hold;
  startInput("KEY", key.kind == bench::KeyKind::Button, total, settle);
  snprintf(pend.detail, sizeof(pend.detail), "%s req=%lu", key.name, static_cast<unsigned long>(hold));
}

// TAP x y [ms] / LONGTAP x y [ms] / SWIPE x1 y1 x2 y2 [ms], in SHOT pixel space.
void cmdTouch(const char* verb, char* args, const int points, const uint32_t defaultHold) {
  if (!gpio.hasTouch()) {
    reply("ERR %s notouch", verb);
    return;
  }
  if (gpio.benchInjectionBusy()) {
    reply("ERR %s busy", verb);
    return;
  }
  int32_t v[5] = {};
  char* tok = nullptr;
  int count = 0;
  while (count < 5 && bench::nextToken(args, tok)) {
    if (!bench::parseI32(tok, v[count])) {
      reply("ERR %s badnum", verb);
      return;
    }
    ++count;
  }
  if (bench::nextToken(args, tok) || (count != points * 2 && count != points * 2 + 1)) {
    reply("ERR %s usage", verb);
    return;
  }
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  for (int i = 0; i < points * 2; i += 2) {
    if (v[i] < 0 || v[i] >= w || v[i + 1] < 0 || v[i + 1] >= h) {
      reply("ERR %s range %dx%d", verb, w, h);
      return;
    }
  }
  const uint32_t hold = bench::clampHoldMs(
      count == points * 2 + 1 ? static_cast<uint32_t>(std::max<int32_t>(v[count - 1], 0)) : defaultHold);
  const int o = renderer.getOrientation();
  const int pw = renderer.getDisplayWidth();
  const int ph = renderer.getDisplayHeight();
  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  bench::logicalToPanel(o, v[0], v[1], pw, ph, x1, y1);
  if (points == 2) {
    bench::logicalToPanel(o, v[2], v[3], pw, ph, x2, y2);
  } else {
    x2 = x1;
    y2 = y1;
  }
  gpio.benchTouchContact(x1, y1, x2, y2, hold);
  startInput(verb, false, hold, 0);
  if (points == 2) {
    snprintf(pend.detail, sizeof(pend.detail), "%d %d %d %d req=%lu", static_cast<int>(v[0]), static_cast<int>(v[1]),
             static_cast<int>(v[2]), static_cast<int>(v[3]), static_cast<unsigned long>(hold));
  } else {
    snprintf(pend.detail, sizeof(pend.detail), "%d %d req=%lu", static_cast<int>(v[0]), static_cast<int>(v[1]),
             static_cast<unsigned long>(hold));
  }
}

void emitShot(const bool settled) {
  const size_t size = renderer.getBufferSize();
  ScratchBuffer copy(size);
  const uint8_t* data = nullptr;
  {
    // The render task draws under this lock: a copy taken here is never half a frame.
    RenderLock lock;
    const int orientation = renderer.getOrientation();
    const int w = renderer.getScreenWidth();
    const int h = renderer.getScreenHeight();
    const bool inverted = display.isInverted();
    const uint8_t* fb = renderer.getFrameBuffer();
    if (!fb) {
      reply("ERR SHOT nofb");
      return;
    }
    if (copy.data) {
      memcpy(copy.data, fb, size);
      data = copy.data;
    } else {
      data = fb;  // no scratch memory: stream straight from the locked buffer
    }
    const uint32_t crc = bench::crc32Update(0, data, size);
    if (copy.data) lock.unlock();
    bool sent = reply("SHOT w=%d h=%d bytes=%u crc=%08lx pw=%u ph=%u rot=%d orient=%s inv=%d bit1=white settled=%d", w,
                      h, static_cast<unsigned>(size), static_cast<unsigned long>(crc), renderer.getDisplayWidth(),
                      renderer.getDisplayHeight(), orientation, orientationName(orientation), inverted ? 1 : 0,
                      settled ? 1 : 0);
    char b64[bench::base64Length(bench::SHOT_BYTES_PER_LINE) + 1];
    for (size_t off = 0; sent && off < size; off += bench::SHOT_BYTES_PER_LINE) {
      const size_t n = std::min(bench::SHOT_BYTES_PER_LINE, size - off);
      bench::base64Encode(data + off, n, b64, sizeof(b64));
      sent = reply("D %s", b64);
      if ((off / bench::SHOT_BYTES_PER_LINE) % 64 == 63) resetTaskWatchdogIfSubscribed();
    }
    if (!sent) {
      // The host stopped reading: give the loop back instead of waiting on it.
      LOG_ERR("BENCH", "SHOT abandoned: host not reading");
      return;
    }
  }
  reply("OK SHOT");
}

// LS on|off: idle light sleep for A/B runs, in RAM until the next boot (a
// deep-sleep wake is a boot: it comes back on). Any other LS argument is a path.
bool cmdLightSleep(const char* args) {
  bool on = false;
  if (strcmp(args, "on") == 0) {
    on = true;
  } else if (strcmp(args, "off") != 0) {
    return false;
  }
  powerManager.setLightSleepEnabled(on);
  LOG_INF("BENCH", "Idle light sleep %s", on ? "on" : "off");
  reply("OK LS lightsleep=%s", on ? "on" : "off");
  return true;
}

// The window plus its awake tail ends before the shortest auto-sleep can fire
// (every accepted command resets the activity timer).
static_assert(power_policy::BENCH_FORCE_MAX_S * 1000UL + power_policy::BENCH_FORCE_TAIL_MS <
                  CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES * 60000UL,
              "LSFORCE must end before the shortest auto-sleep");

// LSFORCE <1..45>: naps for that many seconds even with this cable attached or
// a charger in, to count them without unplugging. The USB link drops for the
// window and comes back once the chip stays awake after it; reconnect then.
void cmdLightSleepForce(const char* args) {
  uint32_t seconds = 0;
  if (!bench::parseU32(args, seconds) || seconds < 1 || seconds > power_policy::BENCH_FORCE_MAX_S) {
    reply("ERR LSFORCE badarg");
    return;
  }
  powerManager.forceLightSleepFor(seconds);
  reply("OK LSFORCE s=%lu tailms=%lu", static_cast<unsigned long>(seconds),
        static_cast<unsigned long>(power_policy::BENCH_FORCE_TAIL_MS));
}

void cmdLs(char* args, const bool exclusive) {
  if (cmdLightSleep(args)) return;
  static char root[] = "/";
  char* path = *args ? args : root;
  bench::trimTrailingSlashes(path);
  if (!bench::isValidPath(path, true)) {
    reply("ERR LS badpath");
    return;
  }
  if (!fileVerbAllowed("LS", exclusive)) return;
  HalFile dir = Storage.open(path);
  if (!dir) {
    reply("ERR LS notfound");
    return;
  }
  if (!dir.isDirectory()) {
    reply("ERR LS notdir");
    return;
  }
  ScratchBuffer scratch(LS_NAME_CAP + LS_LINE_CAP);
  if (!scratch.data) {
    LOG_ERR("BENCH", "OOM: LS buffers");
    reply("ERR LS oom");
    return;
  }
  char* name = reinterpret_cast<char*>(scratch.data);
  char* line = name + LS_NAME_CAP;
  unsigned count = 0;
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    name[0] = '\0';
    entry.getName(name, LS_NAME_CAP);
    const bool sent = entry.isDirectory() ? replyInto(line, LS_LINE_CAP, "DIR %s", name)
                                          : replyInto(line, LS_LINE_CAP, "F %llu %s",
                                                      static_cast<unsigned long long>(entry.fileSize64()), name);
    if (!sent) {
      LOG_ERR("BENCH", "LS abandoned: host not reading");
      return;
    }
    if (++count % 32 == 0) {
      yield();
      resetTaskWatchdogIfSubscribed();
    }
  }
  reply("OK LS %u", count);
}

void cmdMd5(const char* path, const bool exclusive) {
  if (!bench::isValidPath(path, false)) {
    reply("ERR MD5 badpath");
    return;
  }
  if (!fileVerbAllowed("MD5", exclusive)) return;
  if (!Storage.exists(path)) {
    reply("ERR MD5 notfound");
    return;
  }
  if (isDirectory(path)) {
    reply("ERR MD5 isdir");
    return;
  }
  char hex[33];
  uint64_t size = 0;
  if (!md5File(path, hex, size)) {
    reply("ERR MD5 read");
    return;
  }
  reply("OK MD5 %s %llu", hex, static_cast<unsigned long long>(size));
}

// CAT [<tailBytes>] <path>: the tail of a text file as "L <line>" replies (the
// sleep ledger, a log). Control characters become '?' so a line can never be
// taken for console framing; lines past CAT_LINE_CAP are cut on the wire.
void cmdCat(char* args, const bool exclusive) {
  uint32_t tail = 0;
  char* path = nullptr;
  if (!bench::parseCatArgs(args, tail, path) || !bench::isValidPath(path, false)) {
    reply("ERR CAT badpath");
    return;
  }
  if (!fileVerbAllowed("CAT", exclusive)) return;
  if (!Storage.exists(path)) {
    reply("ERR CAT notfound");
    return;
  }
  if (isDirectory(path)) {
    reply("ERR CAT isdir");
    return;
  }
  HalFile file;
  if (!Storage.openFileForRead("BENCH", path, file)) {
    reply("ERR CAT read");
    return;
  }
  const size_t size = file.size();
  const size_t start = size > tail ? size - tail : 0;
  if (start > 0 && !file.seek(start)) {
    reply("ERR CAT read");
    return;
  }
  constexpr size_t LINE_REPLY_CAP = bench::CAT_LINE_CAP + 16;
  ScratchBuffer scratch(tail + bench::CAT_LINE_CAP + 1 + LINE_REPLY_CAP);
  if (!scratch.data) {
    LOG_ERR("BENCH", "OOM: CAT buffers");
    reply("ERR CAT oom");
    return;
  }
  char* buf = reinterpret_cast<char*>(scratch.data);  // the file window
  char* line = buf + tail;                            // one sanitized line
  char* out = line + bench::CAT_LINE_CAP + 1;         // its "@B L ..." reply
  const int got = file.read(buf, tail);
  if (got < 0) {
    reply("ERR CAT read");
    return;
  }
  const size_t n = static_cast<size_t>(got);
  size_t pos = 0;
  if (start > 0) {  // the window opened mid-line: drop that fragment
    while (pos < n && buf[pos] != '\n') ++pos;
    if (pos < n) ++pos;
  }
  unsigned lines = 0;
  size_t bytes = 0;
  while (pos < n) {
    size_t end = pos;
    while (end < n && buf[end] != '\n') ++end;
    size_t len = end - pos;
    if (len > 0 && buf[pos + len - 1] == '\r') --len;
    const size_t shown = std::min(len, bench::CAT_LINE_CAP);
    for (size_t i = 0; i < shown; ++i) {
      const char c = buf[pos + i];
      line[i] = ((c >= 0 && c < 0x20 && c != '\t') || c == 0x7f) ? '?' : c;
    }
    line[shown] = '\0';
    if (!replyInto(out, LINE_REPLY_CAP, "L %s", line)) {
      LOG_ERR("BENCH", "CAT abandoned: host not reading");
      return;
    }
    ++lines;
    bytes += (end < n ? end + 1 : end) - pos;
    pos = end + 1;
    if (lines % 32 == 0) {
      yield();
      resetTaskWatchdogIfSubscribed();
    }
  }
  reply("OK CAT lines=%u bytes=%lu size=%lu", lines, static_cast<unsigned long>(bytes),
        static_cast<unsigned long>(size));
}

// Free space may scan the whole FAT (SdFat keeps no free-cluster count here)
// and is cached by the SDK for 20 s, so it can lag a PUT by that long.
void cmdDf(const bool exclusive) {
  if (!fileVerbAllowed("DF", exclusive)) return;
  const uint64_t total = Storage.sdTotalBytes();
  const uint64_t used = Storage.sdUsedBytes();
  reply("OK DF total=%llu free=%llu used=%llu", static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(total > used ? total - used : 0), static_cast<unsigned long long>(used));
}

void cmdMkdir(char* path, const bool exclusive) {
  bench::trimTrailingSlashes(path);
  if (!bench::isValidPath(path, false)) {
    reply("ERR MKDIR badpath");
    return;
  }
  if (!fileVerbAllowed("MKDIR", exclusive)) return;
  if (Storage.exists(path)) {
    if (isDirectory(path)) {
      reply("OK MKDIR exists %s", path);
    } else {
      reply("ERR MKDIR notdir");
    }
    return;
  }
  if (!Storage.mkdir(path, true)) {
    reply("ERR MKDIR failed");
    return;
  }
  reply("OK MKDIR created %s", path);
}

// PUT <size> <md5hex> <path...>: add-only upload through "<path>.bench-part".
void cmdPut(char* args, const bool exclusive) {
  char* sizeTok = nullptr;
  char* md5Tok = nullptr;
  if (!bench::nextToken(args, sizeTok) || !bench::nextToken(args, md5Tok) || *args == '\0') {
    reply("ERR PUT usage");
    return;
  }
  char* path = args;
  uint32_t size = 0;
  if (!bench::parseU32(sizeTok, size)) {
    reply("ERR PUT badsize");
    return;
  }
  if (!bench::normalizeMd5Hex(md5Tok)) {
    reply("ERR PUT badmd5");
    return;
  }
  if (!bench::isValidPath(path, false) || bench::isPartPath(path)) {
    reply("ERR PUT badpath");
    return;
  }
  if (!fileVerbAllowed("PUT", exclusive)) return;
  if (Storage.exists(path)) {
    reply("ERR PUT exists");
    return;
  }

  // Parent directories are created as needed (never removed).
  char* slash = strrchr(path, '/');
  if (slash != path) {
    *slash = '\0';
    const bool parentOk = Storage.exists(path) ? isDirectory(path) : Storage.mkdir(path, true);
    *slash = '/';
    if (!parentOk) {
      reply("ERR PUT noparent");
      return;
    }
  }

  snprintf(pathBuf, sizeof(pathBuf), "%s%s", path, bench::PART_SUFFIX);
  if (Storage.exists(pathBuf)) Storage.remove(pathBuf);  // a stale part file of ours

  ScratchBuffer buf(bench::PUT_CHUNK_MAX);
  if (!buf.data) {
    LOG_ERR("BENCH", "OOM: %u byte chunk buffer", static_cast<unsigned>(bench::PUT_CHUNK_MAX));
    reply("ERR PUT oom");
    return;
  }
  HalFile file;
  if (!Storage.openFileForWrite("BENCH", pathBuf, file)) {
    LOG_ERR("BENCH", "PUT %s: cannot create %s", path, pathBuf);
    reply("ERR PUT open");
    return;
  }

  const auto failPut = [&](const char* reason) {
    if (file) file.close();  // close before remove
    Storage.remove(pathBuf);
    LOG_ERR("BENCH", "PUT %s failed: %s", path, reason);
  };

  powerManager.setPowerSaving(false);
  txStalled = false;
  reply("READY %u", static_cast<unsigned>(bench::PUT_CHUNK_MAX));

  bench::PutSession session(size);
  unsigned long lastChunkAt = millis();
  char header[48];
  while (!session.complete()) {
    resetTaskWatchdogIfSubscribed();
    const ReadResult r = readLineUntil(header, sizeof(header), lastChunkAt + PUT_IDLE_TIMEOUT_MS);
    if (r == ReadResult::Timeout) {
      failPut("timeout");
      reply("ERR PUT timeout");
      return;
    }
    txStalled = false;  // the host is talking to us again
    auto action = session.onHeader(header, r == ReadResult::Ok);
    if (action == bench::PutSession::Action::ReadPayload) {
      const size_t got = readUpTo(buf.data, session.chunkLen(), PUT_PAYLOAD_TIMEOUT_MS);
      action = session.onPayload(buf.data, got);
    }
    if (action == bench::PutSession::Action::Abort) {
      // After a bad chunk the rest of it is still in the queue: never let it
      // reach the command parser.
      if (strcmp(session.reason(), "interrupted") != 0) drainInput();
      failPut(session.reason());
      reply("ERR PUT %s", session.reason());
      return;
    }
    if (action == bench::PutSession::Action::Nak) {
      drainInput();
      lastChunkAt = millis();
      reply("NAK %lu %s", static_cast<unsigned long>(session.total()), session.reason());
      continue;
    }
    if (file.write(buf.data, session.chunkLen()) != session.chunkLen()) {
      failPut("write");
      reply("ERR PUT write");
      return;
    }
    session.committed();
    lastChunkAt = millis();
    reply("ACK %lu", static_cast<unsigned long>(session.total()));
  }
  file.flush();
  file.close();  // close before re-opening the same path for the read-back

  // Verify what the card returns, not what was sent.
  char hex[33];
  uint64_t readBack = 0;
  if (!md5File(pathBuf, hex, readBack)) {
    failPut("readback");
    reply("ERR PUT readback");
    return;
  }
  if (readBack != size || strcmp(hex, md5Tok) != 0) {
    failPut("md5");
    reply("ERR PUT md5 %s %llu", hex, static_cast<unsigned long long>(readBack));
    return;
  }
  // SdFat's rename refuses an existing destination, so a file that appeared
  // meanwhile is never replaced.
  if (!Storage.rename(pathBuf, path)) {
    failPut("rename");
    reply("ERR PUT rename");
    return;
  }
  LOG_INF("BENCH", "PUT %s (%lu bytes)", path, static_cast<unsigned long>(size));
  reply("OK PUT %s %s", hex, path);
}

bool isOpenable(const char* path) {
  const std::string_view name{path};
  return FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
         FsHelpers::hasMarkdownExtension(name) || FsHelpers::hasBmpExtension(name) || FsHelpers::hasPngExtension(name);
}

void cmdOpen(const char* path, const bool exclusive) {
  if (!bench::isValidPath(path, false)) {
    reply("ERR OPEN badpath");
    return;
  }
  if (!isOpenable(path)) {
    reply("ERR OPEN type");  // the same types the file browser lists
    return;
  }
  if (!fileVerbAllowed("OPEN", exclusive)) return;
  if (!Storage.exists(path)) {
    reply("ERR OPEN notfound");
    return;
  }
  if (isDirectory(path)) {
    reply("ERR OPEN isdir");
    return;
  }
  // The same entry point the library and recents use. The switch runs in this
  // pass's activityManager.loop(); the reply waits until it has happened.
  snprintf(pathBuf, sizeof(pathBuf), "%s", path);
  activityManager.goToReader(std::string(path));
  pend.kind = Pending::Open;
  pend.passes = 0;
}

// CARD <name|default>: a sleep-screen card drawn exactly as the sleep screen
// would, shown without sleeping (SleepCardPreviewActivity; the next key or tap
// returns). The reply waits until it is on the panel. A CARD while a preview is
// up redraws that preview rather than stacking another one.
void cmdCard(char* args, const bool exclusive) {
  char* name = args ? args : const_cast<char*>("");
  while (*name == ' ') ++name;
  size_t len = strlen(name);
  while (len > 0 && name[len - 1] == ' ') name[--len] = '\0';
  if (!BoardConfig::isX4Pro()) {
    reply("ERR CARD board");
    return;
  }
  const bool isDefault = strcmp(name, "default") == 0;
  const sleepcards::CardId id = isDefault ? sleepcards::CardId::None : sleepcards::cardByName(name);
  if (!isDefault && id == sleepcards::CardId::None) {
    reply("ERR CARD unknown");
    return;
  }
  // The card reads the SD card: not while USB (mass storage) or a transfer owns it.
  if (activityManager.benchSwitchPending() || storageBusy(exclusive)) {
    reply("ERR CARD busy");
    return;
  }
  snprintf(pathBuf, sizeof(pathBuf), "%s", name);
  if (strcmp(activityManager.benchCurrentName(), SleepCardPreviewActivity::NAME) == 0) {
    static_cast<SleepCardPreviewActivity*>(activityManager.benchCurrentActivity())->show(id);
    pend.kind = Pending::Card;
    pend.passes = 0;
    return;
  }
  auto preview = makeUniqueNoThrow<SleepCardPreviewActivity>(renderer, mappedInputManager, id);
  if (!preview) {
    LOG_ERR("BENCH", "OOM: SleepCardPreviewActivity");
    reply("ERR CARD oom");
    return;
  }
  activityManager.pushActivity(std::move(preview));
  pend.kind = Pending::Card;
  pend.passes = 0;
}

void cmdLegacyScreenshot() {
  const uint32_t bufferSize = display.getBufferSize();
  logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
  uint8_t* buf = display.getFrameBuffer();
  logSerial.write(buf, bufferSize);
  logSerial.printf("SCREENSHOT_END\n");
}

uint8_t dispatch(char* line, const bool exclusive, const unsigned long lastActivityMs) {
  char* verb = nullptr;
  char* args = nullptr;
  if (!bench::splitCommand(line, verb, args)) return NONE;
  if (*verb == '\0') {
    reply("ERR ? empty");
    return NONE;
  }

  if (strcmp(verb, "PING") == 0) {
    // An optional nonce is echoed: the host uses it to skip stale output.
    if (bench::isValidNonce(args)) {
      reply("OK PING %s proto=%d %s", CROSSPOINT_VERSION, bench::PROTOCOL_VERSION, args);
    } else {
      reply("OK PING %s proto=%d", CROSSPOINT_VERSION, bench::PROTOCOL_VERSION);
    }
  } else if (strcmp(verb, "STATE") == 0) {
    cmdState(exclusive, lastActivityMs);
  } else if (strcmp(verb, "AWAKE") == 0) {
    reply("OK AWAKE");
  } else if (strcmp(verb, "KEY") == 0) {
    cmdKey(args);
  } else if (strcmp(verb, "TAP") == 0) {
    cmdTouch("TAP", args, 1, TAP_HOLD_MS);
  } else if (strcmp(verb, "LONGTAP") == 0) {
    cmdTouch("LONGTAP", args, 1, LONGTAP_HOLD_MS);
  } else if (strcmp(verb, "SWIPE") == 0) {
    cmdTouch("SWIPE", args, 2, SWIPE_MS);
  } else if (strcmp(verb, "SHOT") == 0) {
    pend.kind = Pending::Shot;
    pend.idleStreak = 0;
    pend.deadline = millis() + SHOT_SETTLE_TIMEOUT_MS;
  } else if (strcmp(verb, "LS") == 0) {
    cmdLs(args, exclusive);
  } else if (strcmp(verb, "LSFORCE") == 0) {
    cmdLightSleepForce(args);
  } else if (strcmp(verb, "MD5") == 0) {
    cmdMd5(args, exclusive);
  } else if (strcmp(verb, "CAT") == 0) {
    cmdCat(args, exclusive);
  } else if (strcmp(verb, "DF") == 0) {
    cmdDf(exclusive);
  } else if (strcmp(verb, "MKDIR") == 0) {
    cmdMkdir(args, exclusive);
  } else if (strcmp(verb, "PUT") == 0) {
    cmdPut(args, exclusive);
  } else if (strcmp(verb, "OPEN") == 0) {
    cmdOpen(args, exclusive);
  } else if (strcmp(verb, "CARD") == 0) {
    cmdCard(args, exclusive);
  } else if (strcmp(verb, "SLEEP") == 0) {
    if (exclusive) {
      reply("ERR SLEEP busy");
      return USER_ACTIVITY;
    }
    // Same rule as the auto-sleep: never cut an upload, sync or OTA short.
    if (activityManager.preventAutoSleep()) {
      reply("ERR SLEEP activity");
      return USER_ACTIVITY;
    }
    reply("OK SLEEP");
    return USER_ACTIVITY | REQUEST_SLEEP;
  } else if (strcmp(verb, "SCREENSHOT") == 0) {
    cmdLegacyScreenshot();
  } else {
    reply("ERR %s unknown", verb);
    return NONE;
  }
  return USER_ACTIVITY;
}

void servicePending() {
  const auto now = static_cast<uint32_t>(millis());
  if (pend.kind == Pending::Shot) {
    pend.idleStreak = activityManager.benchRenderIdle() ? pend.idleStreak + 1 : 0;
    const bool settled = pend.idleStreak >= 2;
    if (settled || static_cast<long>(now - pend.deadline) >= 0) {
      pend.kind = Pending::None;
      emitShot(settled);
    }
    return;
  }
  if (pend.kind == Pending::Open) {
    if (activityManager.benchSwitchPending()) {
      if (++pend.passes < OPEN_MAX_PASSES) return;
      pend.kind = Pending::None;
      reply("ERR OPEN timeout");
      return;
    }
    pend.kind = Pending::None;
    // A book that fails to load finishes straight back to where it came from.
    if (activityManager.benchCurrentIsReader() || strcmp(activityManager.benchCurrentName(), "BmpViewer") == 0) {
      reply("OK OPEN act=%s %s", activityManager.benchCurrentName(), pathBuf);
    } else {
      reply("ERR OPEN failed act=%s", activityManager.benchCurrentName());
    }
    return;
  }
  if (pend.kind == Pending::Card) {
    if (activityManager.benchSwitchPending()) {
      if (++pend.passes < OPEN_MAX_PASSES) return;
      pend.kind = Pending::None;
      reply("ERR CARD timeout");
      return;
    }
    pend.kind = Pending::None;
    const auto result = SleepCardPreviewActivity::lastResult();
    if (!result.valid || strcmp(activityManager.benchCurrentName(), SleepCardPreviewActivity::NAME) != 0) {
      reply("ERR CARD failed act=%s", activityManager.benchCurrentName());
      return;
    }
    reply("OK CARD %s shown=%s outcome=%s ms=%lu", pathBuf, sleepcards::cardName(result.shown), result.outcome,
          result.ms);
    return;
  }
  if (pend.kind != Pending::Input) return;

  switch (pend.waiter.poll(now, gpio.benchInjectionDone(), gpio.benchKeyPressDelivered())) {
    case bench::InputWaiter::Verdict::Wait:
      return;
    case bench::InputWaiter::Verdict::ErrNoEdge:
      gpio.benchClearInjection();
      pend.kind = Pending::None;
      reply("ERR %s noedge", pend.verb);
      return;
    case bench::InputWaiter::Verdict::ErrTimeout:
      // Never cut a gesture off mid-contact: it ends with a real release on
      // the next update, and a new KEY/TAP waits for that (ERR busy).
      gpio.benchFinishInjection();
      pend.kind = Pending::None;
      reply("ERR %s timeout", pend.verb);
      return;
    case bench::InputWaiter::Verdict::Ok:
      break;
  }
  const uint32_t held = gpio.benchDeliveredHoldMs();
  gpio.benchClearInjection();
  pend.kind = Pending::None;
  reply("OK %s %s held=%lu%s", pend.verb, pend.detail, static_cast<unsigned long>(held),
        pend.waiter.late() ? " late=1" : "");
}

}  // namespace

uint8_t poll(const bool exclusiveStorage, const unsigned long lastActivityMs) {
  host.update(static_cast<uint32_t>(millis()), HWCDC::isPlugged());

  uint8_t result = NONE;
  if (pend.kind != Pending::None) {
    servicePending();
    // A command in flight keeps the loop at full cadence so its timing holds.
    if (pend.kind != Pending::None) return USER_ACTIVITY;
    result |= USER_ACTIVITY;
  }

  while (pend.kind == Pending::None) {
    const int c = logSerial.read();
    if (c < 0) break;
    txStalled = false;  // the host is talking to us again
    if (c == '\r') continue;
    if (c != '\n') {
      if (lineLen + 1 < LINE_CAP) {
        lineBuf[lineLen++] = static_cast<char>(c);
      } else {
        lineOverflow = true;
      }
      continue;
    }
    lineBuf[lineLen] = '\0';
    const bool overflow = lineOverflow;
    lineLen = 0;
    lineOverflow = false;
    if (overflow) {
      if (strncmp(lineBuf, "CMD:", 4) == 0) reply("ERR ? toolong");
      continue;
    }
    result |= dispatch(lineBuf, exclusiveStorage, lastActivityMs);
    if (result & REQUEST_SLEEP) break;
  }
  return result;
}

bool hostPresent() { return host.present(); }

}  // namespace BenchConsole

#endif  // CROSSPOINT_BENCH_CONSOLE
