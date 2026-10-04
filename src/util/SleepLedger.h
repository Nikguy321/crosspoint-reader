#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// One line per boot, sleep entry and rail cut, appended to /sleep.log on the
// card (X4 Pro only). Counting lines answers "was it asleep all night?"; the
// gauge fields say what the sleep cost. Pure formatting so a host test can pin
// the layout.
//
// Reading a night:
//   <unix> <event> up=<s> rst=<n> wake=<n> x=<n> soc=<%> mv=<mV>
//   event  boot     x: bit 1 = the previous sleep entry aborted (never slept),
//                      bit 2 = the SoC survived a rail cut (USB power kept it up)
//          sleep    x: 1 = inactivity timeout, 0 = button / click; a live sleep
//                      screen ending: 2 = unplugged, 3 = the bench's SLEEP deep,
//                      4 = the full-charge hold saw the battery drop (unplugged),
//                      5 = the hold's 24 h cap
//          resleep  x: 1 = unverified (ghost) button wake, slept again
//          cut      the auto power-off timer fired and the rail was dropped
//          live     the live sleep screen on external power instead of deep sleep
//                   (util/LiveSleepPolicy.h); x: 0 button, 1 timeout, 2 an auto
//                   power-off timer wake on the charger (booted straight into it)
//          livewake a key woke the live sleep screen (a restart follows)
//          hold     the live screen's full-charge hold began (the charger went idle
//                   at full: Wi-Fi off, quiet redraws); x = the SOC when power vanished
//          holdend  the hold ended; x: 0 = power back (fully live again), 4 = the
//                   battery dropped 3 % (a "sleep" x=4 follows), 5 = the 24 h cap
//                   (a "sleep" x=5 follows); a key (livewake) or the bench's SLEEP
//                   deep (sleep x=3) also ends a hold, with no holdend line
//   rst    5 = deep-sleep wake, 1 = power-on (cold boot);  wake 7 = EXT1 button,
//          4 = timer, 0 = none.  unix is 0 while the RTC is unset.
//   sleep -> cut gap = the auto power-off timer;  cut -> boot with rst=1 = a real
//   power-off (battery);  rst=5 wake=7 x=2 instead = USB kept the SoC alive.
//   A sleep -> boot pair with no cut line = the timer never fired (Never, or the
//   card did not mount in the cut path, which then cannot write its line).
//   The same file carries a "pwr" line every 5 min while awake: util/PowerLedger.h.
namespace sleep_ledger {

constexpr const char* PATH = "/sleep.log";
constexpr const char* ROTATED_PATH = "/sleep.log.1";
constexpr size_t LINE_CAP = 128;
// A ghost-waking button could add ~170 KB a day; at boot the file is rolled
// over to ROTATED_PATH past this size so it never grows without bound.
constexpr size_t ROTATE_BYTES = 64 * 1024;

constexpr bool shouldRotate(const size_t fileBytes) { return fileBytes > ROTATE_BYTES; }

struct Entry {
  uint32_t unixTime = 0;    // 0 when the RTC is absent
  const char* event = "";   // boot | sleep | resleep | cut | live | livewake | hold | holdend
  uint32_t uptimeS = 0;     // seconds since this boot
  int resetReason = 0;      // esp_reset_reason(); 5 = deep-sleep wake
  int wakeCause = 0;        // the boot's wake cause; 3 = EXT1 button, 4 = timer, 0 = none
  int extra = 0;            // per-event, see above
  unsigned socPercent = 0;  // gauge SoC, 1 % resolution
  unsigned millivolts = 0;  // gauge VCELL, ~0.3 mV resolution; 0 on I2C failure
};

// Formats "<unix> <event> up=<s> rst=<n> wake=<n> x=<n> soc=<n> mv=<n>\n".
// Returns the length written (excluding the terminator), 0 if it does not fit.
inline size_t formatLine(char* buf, const size_t cap, const Entry& e) {
  const int n = snprintf(buf, cap, "%lu %s up=%lu rst=%d wake=%d x=%d soc=%u mv=%u\n",
                         static_cast<unsigned long>(e.unixTime), e.event, static_cast<unsigned long>(e.uptimeS),
                         e.resetReason, e.wakeCause, e.extra, e.socPercent, e.millivolts);
  if (n <= 0 || static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}

}  // namespace sleep_ledger
