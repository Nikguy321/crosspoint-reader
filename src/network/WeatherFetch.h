#pragma once

#include <cstddef>
#include <cstdint>

// The Weather card's fetch (Display > Sleep Screen Cards > Weather, X4 Pro; off by default): the
// Open-Meteo forecast and, for points in the US, the NWS alerts for the saved location, parsed
// (network/WeatherProtocol) and saved as the card's cache (sleepcards/WeatherCache), written to
// weather.tmp and renamed over /.crosspoint/sleepcards/weather.dat under the render lock.
//
// Never starts the radio: it runs only on a station already connected, beside a job that needed
// Wi-Fi for its own work - the live sleep screen's station keeper on the charger (every ~3 h, one
// request per keeper tick), "Sync clock now" and a book sync (when the cache is over an hour old),
// and the bench console's WEATHER fetch. Each request goes through GeolocateClient's
// esp_http_client with the certificate bundle (chain and host name verified), with AutoLocate's
// budget: DNS bounded to DNS_MS, the whole request to BUDGET_MS. Skipped without Weather on, on
// the book-sync peer's or hub's hotspot, without a set clock or a location, or within the retry
// wait after the last attempt (stamped before its request; kept across the silent reboot that ends
// every sync and across deep sleep). Sends the location rounded to 0.01 degree only, and logs one
// line per fetch with no URL or place in it:
//   weather: ok 200/200 1.2 s     forecast / alerts status ("-" = not asked: outside the US)
//   weather: failed unreachable   (the forecast; the old cache stays)
//   weather: skipped retry-wait
namespace WeatherFetch {

constexpr uint32_t BUDGET_MS = 14000;  // one request, DNS included
constexpr uint32_t DNS_MS = 3000;

// A sync piggyback (a job's own request reached its server: jobOnline): refresh when the cache is
// over an hour old (or for another place), at most once per 10 minutes. True = call run() now, with
// the Wi-Fi still up. Logs the skip line when not due (nothing when Weather is off); answers once
// per boot.
bool due(bool jobOnline);
// The forecast, then the alerts, now. Blocks the calling (loop) task for up to about 2 x BUDGET_MS.
void run();

// The live keeper (network/StationKeeper), one HTTPS request per call so keys and the unplug check
// run in between: refreshes a cache over ~3 h old, at most once per 30 min. cacheChanged is set
// when the cache file now holds something new (a Weather card on screen should redraw).
enum class KeeperStep : uint8_t {
  Nothing,  // not due (fresh, the retry wait, Weather off, no clock ...): no request made
  More,     // the forecast is saved; the alerts request is next (call again on a later tick)
  Done,     // a fetch finished (the forecast saved; the alerts answered or not)
  Failed,   // the forecast request failed: the old cache stays
};
KeeperStep keeperStep(bool& cacheChanged);

// The bench console's WEATHER verb.
// fetch: the forecast and alerts now, whatever the cache's age and the retry wait (the opt-in, a
// station up, a set clock and a location still required). Returns the outcome line (no place).
void benchFetch(char* out, size_t cap);
// show: one-line summaries of the settings, the last attempt and the cache into the callback, one
// call per line (no coordinates: the cache's distance from the saved location instead).
void benchShow(void (*line)(void* ctx, const char* text), void* ctx);
// clear: the cache file and the retry stamp. False when the file could not be removed.
bool benchClear();

}  // namespace WeatherFetch
