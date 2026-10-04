#pragma once

#include <cstdint>

// Live sleep (X4 Pro on external power, util/LiveSleepPolicy.h): the sleep screen stays up and is
// redrawn on the minute (SleepActivity), Wi-Fi is held by network/StationKeeper, and a key wakes
// it with a restart to where the reader was; unplugged, it ends in today's deep sleep. At full
// charge a lost charger line starts the full-charge hold instead (quiet, Wi-Fi off) until the
// charger starts again or the battery drops. main.cpp owns the session.

// The charger STAT line (charging) or a computer on USB (SOF frames).
bool externalPowerPresent();

// The live sleep screen is up.
bool liveSleepActive();

// The full-charge hold, for the bench STATE line.
struct LiveHold {
  bool holding = false;
  unsigned startSoc = 0;  // the SOC when power vanished (0 when not holding)
  uint32_t heldMs = 0;
};
LiveHold liveHoldStatus();

#if CROSSPOINT_BENCH_CONSOLE
// Bench POWER fake absent|real (dev builds): while set, external power reads absent whatever the
// charger line and the bench cable say, so the unplug and the full-charge hold can be tried on the
// bench. RAM only: a reboot clears it.
void setExternalPowerFakedAbsent(bool absent);
bool externalPowerFakedAbsent();
#endif
