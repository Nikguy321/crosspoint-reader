#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// The awake half of /sleep.log (X4 Pro): one "pwr" line every WINDOW_MS while
// the reader is awake and not in USB Drive, appended beside SleepLedger.h's
// boot/sleep/cut lines. Pure formatting so a host test can pin the layout.
//
//   <unix> pwr up=<s> soc=<%> mv=<mV> mhz=<n> fl=<n> wifi=<0|1> usb=<0|1>
//          host=<0|1> rnd=<n> ls=<permille> lsn=<n> lse=<0|1>
//   mhz   CPU clock at the instant the line was written (a snapshot, and
//         nearly always 80: the write follows an idle pass)
//   fl    the highest frontlight brightness % seen in the window, 0 = dark all
//         window
//   wifi  a radio was initialised at any time in the window
//   usb   charger STAT (charging) at any time in the window; host = a computer
//         on USB (SOF frames) at any time in the window
//   rnd   renders in the window;  ls = per-mille of the window in light sleep
//   lsn   light sleeps in the window;  lse = light sleep enabled (bench LS on|off)
// Reading current off the gauge: over windows with fl=0 wifi=0 usb=0 within one
// boot, mA ~= 11 x (sum of soc drops) / (sum of hours); the mv slope agrees.
namespace power_ledger {

constexpr uint32_t WINDOW_MS = 5UL * 60UL * 1000UL;
constexpr size_t LINE_CAP = 160;

struct Window {
  uint32_t unixTime = 0;  // 0 while the RTC is unset
  uint32_t uptimeS = 0;
  unsigned socPercent = 0;
  unsigned millivolts = 0;
  unsigned cpuMhz = 0;
  unsigned frontlight = 0;
  bool wifi = false;
  bool usb = false;
  bool host = false;
  uint32_t renders = 0;
  unsigned lightSleepPermille = 0;
  uint32_t lightSleeps = 0;
  bool lightSleepEnabled = false;
};

// The window's conditions, noted on every loop pass: a window counts as dark,
// radio-free or unplugged only if it was so all the way through.
struct WindowLatch {
  unsigned frontlightMax = 0;
  bool wifi = false;
  bool usb = false;
  bool host = false;

  void note(const unsigned frontlight, const bool wifiOn, const bool usbOn, const bool hostOn) {
    if (frontlight > frontlightMax) frontlightMax = frontlight;
    wifi = wifi || wifiOn;
    usb = usb || usbOn;
    host = host || hostOn;
  }
  void applyTo(Window& w) const {
    w.frontlight = frontlightMax;
    w.wifi = wifi;
    w.usb = usb;
    w.host = host;
  }
};

// Share of windowUs spent asleep, in per-mille, clamped to 0..1000.
constexpr unsigned permille(const uint64_t sleptUs, const uint64_t windowUs) {
  if (windowUs == 0) return 0;
  if (sleptUs >= windowUs) return 1000;
  return static_cast<unsigned>((sleptUs * 1000ULL + windowUs / 2) / windowUs);
}

// Returns the length written (excluding the terminator), 0 if it does not fit.
inline size_t formatLine(char* buf, const size_t cap, const Window& w) {
  const int n = snprintf(buf, cap,
                         "%lu pwr up=%lu soc=%u mv=%u mhz=%u fl=%u wifi=%d usb=%d host=%d rnd=%lu ls=%u lsn=%lu "
                         "lse=%d\n",
                         static_cast<unsigned long>(w.unixTime), static_cast<unsigned long>(w.uptimeS), w.socPercent,
                         w.millivolts, w.cpuMhz, w.frontlight, w.wifi ? 1 : 0, w.usb ? 1 : 0, w.host ? 1 : 0,
                         static_cast<unsigned long>(w.renders), w.lightSleepPermille,
                         static_cast<unsigned long>(w.lightSleeps), w.lightSleepEnabled ? 1 : 0);
  if (n <= 0 || static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}

}  // namespace power_ledger
