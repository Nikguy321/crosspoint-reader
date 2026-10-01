#pragma once

#include <cstdint>

// "Update location when syncing" (Display > Sleep Screen Cards, X4 Pro; off by default): while a
// job that needed Wi-Fi for its own work still has it up (KOReaderSyncActivity, ClockSyncActivity
// "Sync clock now", the live sleep screen's clock sync on the charger: network/StationKeeper),
// refresh a sleep-card location that was not saved today from nearby Wi-Fi networks. Typed-in locations are refreshed
// too; a stored Wi-Fi fix at least as tight as the new answer and agreeing with it is kept (re-dated) instead.
//
// Never starts the radio: it runs only on a station already connected, and its scan goes through
// RadioPower like every other. Asks beaconDB only (the MLS geolocate API, up to 20 access points,
// no names, the Locate Me filters) and never the internet-address lookup; saves only a Wi-Fi fix
// within 1,000 m, recorded as "wifi-auto". Skipped when the job's own request never reached its
// server, on the book-sync peer's or hub's hotspot, with fewer than two usable access points,
// without a set clock; tried at most once a day (the day of the last try survives the reboot that
// ends every sync, and deep sleep). The decisions are sleepcards/AutoLocatePolicy (host-tested).
// Logs one line, "autolocate: skipped|failed <reason>" or "autolocate: saved", and nothing about
// the place.
namespace AutoLocate {

// The most one run adds to the job that called it: the scan, a name lookup capped at DNS_MS, then
// the request cut to what is left. Past the budget only the TLS handshake can run on (each of its
// blocking reads may wait up to the connect timeout, at most 4 s), on a server that answered the
// connect and then stalls.
constexpr uint32_t BUDGET_MS = 14000;
constexpr uint32_t DNS_MS = 3000;

// The decision, for a job whose own request did (jobOnline) or did not get an answer from its
// server: true = call run() now, with the Wi-Fi still up. Logs the skip line when it is not due
// (nothing when the setting is off), and answers once per boot: later calls return false quietly,
// so one sync logs one line.
bool due(bool jobOnline);

// The lookup, after due() said so. Blocks the calling (loop) task for up to about BUDGET_MS.
void run();

// Lets due() answer once more this boot: live sleep's station keeper asks after each clock sync of
// a charging session that may last days (the once-a-day rule still holds).
void rearm();

}  // namespace AutoLocate
