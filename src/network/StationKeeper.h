#pragma once

// Live sleep's Wi-Fi (X4 Pro on external power, util/LiveSleepPolicy.h): holds the reader on a
// saved network while the sleep screen is live, the one time Wi-Fi stays up with nothing on the
// screen asking for it (on the charger, power does not matter). SleepActivity ticks it on the
// passes it does not redraw. A small state machine, one step per tick so the keys are read in
// between:
//   scan (RadioPower) -> the join order of network/WifiJoinOrder.h (never the book-sync peer's
//   or hub's hotspot: no internet behind them) -> join with the scan's channel and BSSID ->
//   online: the clock from NTP on the first join and every 6 h (whatever "synced once" says),
//   then Update Location When Syncing as after "Sync clock now", then the Weather card's forecast
//   (network/WeatherFetch: one request per tick, a cache over ~3 h old) -> watch the link. A lost link
//   or a round with nothing joined waits 2 min and scans again; after 3 such rounds in a row,
//   10 min.
// Every start goes through RadioPower; main.cpp turns the radio off (RadioPower::off()) when
// the live screen ends, by a key or in deep sleep.
namespace StationKeeper {

// The live screen is up: load the Wi-Fi list (the first tick scans).
void start();

// One step; returns at once when there is nothing to do.
void tick();

// The live screen ends (the caller turns the radio off).
void stop();

// For the bench STATE line: off | none (no saved network) | scan | join | up | wait.
const char* stateName();

}  // namespace StationKeeper
