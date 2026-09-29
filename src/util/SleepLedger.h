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
//          sleep    x: 1 = inactivity timeout, 0 = button / click
//          resleep  x: 1 = unverified (ghost) button wake, slept again
//          cut      the auto power-off timer fired and the rail was dropped
//   rst    5 = deep-sleep wake, 1 = power-on (cold boot);  wake 7 = EXT1 button,
//          4 = timer, 0 = none.  unix is 0 while the RTC is unset.
//   sleep -> cut gap = the auto power-off timer;  cut -> boot with rst=1 = a real
//   power-off (battery);  rst=5 wake=7 x=2 instead = USB kept the SoC alive.
//   A sleep -> boot pair with no cut line = the timer never fired (Never, or the
//   card did not mount in the cut path, which then cannot write its line).
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
  const char* event = "";   // boot | sleep | resleep | cut
  uint32_t uptimeS = 0;     // seconds since this boot
  int resetReason = 0;      // esp_reset_reason(); 5 = deep-sleep wake
  int wakeCause = 0;        // esp_sleep_get_wakeup_cause(); 7 = EXT1 button, 4 = timer
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
