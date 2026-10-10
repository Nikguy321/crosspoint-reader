#pragma once

#include <cstdint>

// "Update location when syncing" (Display > Sleep Screen Cards, X4 Pro; off by default): while a
// job that needed Wi-Fi for its own work still has it up (KOReaderSyncActivity, ClockSyncActivity
// "Sync clock now", the live sleep screen's clock sync on the charger: network/StationKeeper),
// refresh a sleep-card location that was not saved today. Typed-in locations are refreshed too; a
// stored measured fix at least as tight as a new Wi-Fi answer and agreeing with it is kept instead
// (a Wi-Fi one re-dated, a phone one left as it is).
//
// Never starts the radio: it runs only on a station already connected, and its scan goes through
// RadioPower like every other. The lookup is network/LocateRun, as Locate Me's: the phone's GPS
// over its hotspot (saved as "phone"), else nearby Wi-Fi when two beaconDB lookups of separate
// halves of the access points agree (saved as "wifi-auto"); never an internet-address lookup.
// Skipped when the job's own request never reached its server, on the book-sync peer's or hub's
// hotspot, without a set clock; tried at most once a day (the day of the last try survives the
// reboot that ends every sync, and deep sleep). A failure keeps the location as it was. The
// decisions are sleepcards/AutoLocatePolicy (host-tested). Logs one line, "autolocate: skipped|
// failed <reason>" or "autolocate: saved phone|wifi" (or "kept phone"), and nothing about the place.
namespace AutoLocate {

// The most the Wi-Fi half adds to the job that called it: the scan, a name lookup capped at 3 s,
// then the two requests cut to what is left. Past the budget only a TLS handshake can run on (each
// of its blocking reads may wait up to the connect timeout, at most 4 s), on a server that
// answered the connect and then stalls. The phone comes first and is bounded on its own (two 1.5 s
// connects and an 8 s read at most; a refused connect on a home router takes milliseconds).
constexpr uint32_t BUDGET_MS = 18000;

// The decision, for a job whose own request did (jobOnline) or did not get an answer from its
// server: true = call run() now, with the Wi-Fi still up. Logs the skip line when it is not due
// (nothing when the setting is off), and answers once per boot: later calls return false quietly,
// so one sync logs one line.
bool due(bool jobOnline);

// The lookup, after due() said so. Blocks the calling (loop) task for up to about 11 s + BUDGET_MS.
void run();

// Lets due() answer once more this boot: live sleep's station keeper asks after each clock sync of
// a charging session that may last days (the once-a-day rule still holds).
void rearm();

}  // namespace AutoLocate
