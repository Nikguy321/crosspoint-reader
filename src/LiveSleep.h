#pragma once

// Live sleep (X4 Pro on external power, util/LiveSleepPolicy.h): the sleep screen stays up and is
// redrawn on the minute (SleepActivity), Wi-Fi is held by network/StationKeeper, and a key wakes
// it with a restart to where the reader was; unplugged, it ends in today's deep sleep. main.cpp
// owns the session.

// The charger STAT line (charging) or a computer on USB (SOF frames).
bool externalPowerPresent();

// The live sleep screen is up.
bool liveSleepActive();
