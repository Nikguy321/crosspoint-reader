# Sleep screen cards (X4 Pro)

On the X4 Pro the Sleep Screen setting offers, besides the classic screens, a set of
cards: **Now Reading**, **Day Card**, **Month Calendar**, **Quote**, **Owner Card**,
**Tonight's Sky**, **Weather** and **Shuffle** (a different ticked card each sleep; it may also
pick the picture frame, which is the existing Custom screen). Their options live in
Settings > Display > **Sleep Screen Cards**. The Dark and Light screens, the boot screen
and every card's fallback show the X4 Pro mark. Other boards keep the CrossPoint screens
and do not list the cards.

The e-ink keeps the last picture with no power, also after Auto Power Off cuts the rail,
so a card's footer says when it was drawn ("Screen updated 2:17 PM, Wed Oct 1", the 24-hour
form with the Clock setting's), never a clock that pretends to be live. The footer measures
the line beside the battery and shortens it until it fits between the margins: "Screen updated
2:17 PM, Oct 1", then "Updated 2:17 PM, Oct 1", then "Updated 2:17 PM". On external power the
battery carries a lightning bolt. On the charger the sleep screen stays live and is redrawn
(below), so the time and battery stay current.

## Settings

| Row | Stored as | Notes |
|---|---|---|
| Location | `sleepCardLocation` | decimal degrees, latitude first: `51.4779, -0.0015`, `51.48 N 0.00 W`. Empty = not set; sun and moon times then read "Set location in Settings" (the moon's phase needs no place). |
| (under Location) | `sleepCardLocationFix` | where the location came from, shown as the Location row's second line ("From Wi-Fi, ±80 m, Sep 29", "From Wi-Fi (auto), ±80 m, Sep 29", "From phone GPS, Oct 10", "Typed in, Sep 29"). Stored as `wifi 80 2026-09-29 47.6205,-122.3493` (source `typed` / `wifi` / `wifi-auto` / `phone`, accuracy in m, date saved, the location it describes): a location changed anywhere else reads as typed in, and one confirmed unchanged on the keyboard keeps its record. Older firmware could also store `ip` (an internet-address lookup): such a record is still read, but the location it names counts as **not set** everywhere (the cards, the weather, Update Location When Syncing), the Location row reads "Not set" with the line "Not used: found from an internet address, which can be far off", the web settings page shows the Location empty, and typing that same location back in on the reader records it as typed. An internal key: saved in `settings.json` but not shown on the web settings page. |
| Locate Me | - | finds the location from the phone's GPS over its hotspot, else from nearby Wi-Fi when two beaconDB lookups agree, else not at all (never from the internet address); see below |
| Update Location When Syncing | `autoLocateOnSync` | Off by default. While a book sync or Sync clock now already has Wi-Fi up, refreshes a location not saved today as Locate Me finds one: the phone's GPS over its hotspot (a connection to the network's gateway on ports 10110 and 11123), else nearby Wi-Fi when two beaconDB lookups agree (never an internet-address lookup; replaces a typed location), at most once a day; see below |
| Weather | `weatherEnabled` | Off by default: the opt-in for the Weather card. Sends the location, rounded to about 1 km, to Open-Meteo and, for US points, the National Weather Service, only while Wi-Fi is already up for something else; see Weather below |
| Weather Units | `weatherUnits` | Metric (°C, km/h, hPa) or US (°F, mph, inHg); the cache is metric, so a change needs no new fetch |
| Hunting Season | `huntingSeason` | Off / On / Between Dates |
| Season Start / End | `huntStartMonth` `huntStartDay` `huntEndMonth` `huntEndDay` | typed as month-day (`10-01`); a season may run past New Year |
| Legal Light | `legalLightRule` | sunrise - 30 min .. sunset + 30 min, or civil twilight; always rounded inward to the minute |
| Owner Name, Owner Contact 1/2 | `ownerName` `ownerContact1` `ownerContact2` | typed on the reader; nothing is preset |
| Quote Source | `quoteSources` | which categories feed the Quote card: All (default), Built-in + My Quotes, My Quotes + Bookmarks, Built-in + Bookmarks, Built-in Only, My Quotes Only, Bookmarks Only (stored by that index). Replaces the old `quoteSource` key, migrated once on load: quotes file -> My Quotes Only, bookmarks -> Bookmarks Only, both -> All |
| Dark Cards | `darkCards` | Off by default. The cards and the logo a card falls back to, white on black; pictures and covers follow the Sleep Screen Cover Filter instead. See "White on black" below |
| Shuffle: ... | `shuffleNowReading` ... `shuffleWeather`, `shufflePictures` | default on: Now Reading, Day, Calendar, Quote, Sky (Weather off: it needs the opt-in) |
| Card Cycle When Charging | `cardCycleWhenCharging` | Off by default. On the charger the live sleep screen deals a new Shuffle card at each update, whatever the Sleep Screen setting; see Live sleep on the charger |
| Charging Updates | `chargingUpdateInterval` | how often the live sleep screen redraws: Every 1 / 2 (default) / 5 / 10 / 15 min (stored by that index) |

All of them are ordinary `settings.json` keys and, apart from `sleepCardLocationFix`, appear
on the web settings page under "Sleep Screen Cards".

**White on black.** **Dark Cards** (`darkCards`) turns every card, and the logo screen a card
falls back to, white on black. The moon and the Now Reading cover keep their real tones: the lit
part of the moon stays white inside a white ring (a new moon is a dark disc, never the white disc
of a full moon), and the cover is never a negative. Pictures and book covers follow the Sleep
Screen Cover Filter instead, so they are never negatives because the cards are dark. (Dark cards
were the filter's **Inverted** until 2026-10-01; a reader with a card as its Sleep Screen and the
filter on Inverted is moved over once, to Dark Cards on and the filter back to None. Anywhere else
Inverted was chosen for pictures or covers, and it stays.)

### Locate Me

A screen says what will be asked and sent where (the phone's GPS over its hotspot, then
beaconDB) and that the reader restarts afterwards, before anything leaves the reader; nothing is
sent without the Locate tap, and nothing runs in the background (the one opt-in exception is
Update Location When Syncing, below, which rides a sync's own Wi-Fi). There is **no
internet-address lookup**: on a phone hotspot a carrier's exit address sits in a city hundreds of
km away, and a wrong location is worse than none (`scripts/check_radio_power.py` fails if any file
names an IP-geolocation service).

The order is the phone's GPS, then Wi-Fi only when it is genuinely local, else nothing
(`network/LocateRun`, shared with Update Location When Syncing and the bench's `LOCTEST`):

1. **Join** a saved Wi-Fi network through `WifiSelectionActivity`'s auto-connect (a scan first,
   then the last network when it is in view, else the strongest other saved one in view:
   `network/WifiJoinOrder.h`; the network list only when none is). Every radio start
   goes through `RadioPower` (full clock, no light sleep while it is up).
2. **Phone GPS** (`network/PhoneGps`, the pure gates in `network/PhoneNmea`, host-tested in
   `test/phone_nmea`): a TCP connection to the network's gateway - on a phone's hotspot, the
   phone - at port 10110 (gpsdRelay, Share GPS), then 11123 (GPS 2 IP's default on iPhone), each
   connect bounded to 1.5 s, then up to 8 s of NMEA. Nothing is sent to the phone; no Bluetooth.
   A fix's figures come from **one GGA sentence** that:
   - has a correct checksum and arrived at least 2 s after the connect (gpsdRelay replays a
     shared queue of old lines to every new client);
   - says fix quality 1, 2, 4 or 5 (not 0 none, 3 PPS, 6 dead reckoning, 7 manual, 8 simulated);
   - has 4 or more satellites and an HDOP of 10 or less, each checked when the sentence carries
     it (gpsdRelay's own generated GGA puts the accuracy in metres in the HDOP field; the same
     limit applies). With neither (iPhone apps), an RMC with status A, a mode other than
     E/N/M/S and the same hhmmss must vouch for it;
   - when the clock has been set from the internet, is within 5 s of the clock's UTC time of day
     (across midnight too), widened by 1 s for every 10,000 s since the last NTP sync (100 ppm:
     the RTC crystal drifts a second or two a day, more in the cold), at most 15 minutes; when
     the time of the last sync is not known (it is kept in RTC memory, lost with the power), 15
     minutes;
   - spells its coordinates strictly (`ddmm.mmmm` digits and a hemisphere letter: a sign or an
     exponent refuses the sentence);
   - agrees with a GGA of another second before it that passed the same gates: within the larger
     of 30 m and their accuracies, plus 50 m for each second between them, at most 8 s apart.
     gpsdRelay's generated coordinates go through Kotlin's `Double.toString` and are cut to six
     characters, so within ~1.85 m of a whole degree "6.0000001E-5" minutes print as
     `06.000000`: a well-formed sentence ~11 km off. A position moving through such a strip
     jumps and is refused; one standing still inside it is not, which is why the setup notes say
     to relay the receiver's own NMEA rather than generated sentences.
   Its accuracy is shown as HDOP x 5 m (at least 5 m), or not at all without an HDOP.
3. **Wi-Fi** only when the phone gave nothing: a blocking station scan on the joined radio, then
   the usable access points at -85 dBm or stronger - never hidden networks, SSIDs ending in
   `_nomap` or `_optout`, locally administered (randomised / hotspot) or group BSSIDs, VRRP
   virtual routers (`00:00:5e:00:01:xx`, `00:00:5e:00:02:xx`), or the joined access point (and
   its other addresses) when anything accepted a connection on the phone ports of this network.
   One router broadcasts several BSSIDs (2.4 and 5 GHz, guest networks) from neighbouring
   addresses and beaconDB counts BSSIDs, so they are grouped into devices first (the same OUI
   and less than 256 apart, `geolocate::sameDevice`): **at least four devices**, ranked by their
   strongest access point and dealt alternately into two halves, every address of a device in
   the same half. Each half goes to [beaconDB](https://beacondb.net) on its own (`POST
   https://api.beacondb.net/v1/geolocate`, the MLS / Ichnaea geolocate API, `considerIp:false`
   and `fallbacks: {ipf:false, lacf:false}`, `Content-Type: application/json`, a
   `CrossPoint-X4Pro/<version>` user agent as beaconDB asks). beaconDB averages whichever access
   points it knows and reports how far they are usually heard, not whether they agree, so one
   that has moved can drag its answer kilometres while it still reads "50 m": the location
   counts only when **both** halves answer HTTP 200 with a location (no `fallback` key), each
   within 1-100 m, and the two lie within the larger of their accuracies of each other. The
   location is their midpoint, shown with an accuracy of the largest of 50 m, both accuracies
   and their distance apart. When the first half fails the second is not asked. beaconDB keeps
   no record of the access points queried or the location returned ([privacy
   notice](https://beacondb.net/privacy/)); its web logs (with the IP address) go after 28 days.
4. **Result**: "Near 47.62, -122.35" and how sure it is ("About 15 m, from phone GPS", "From
   phone GPS" without an HDOP, "About 60 m, from Wi-Fi"). **Save** stores the location in the
   usual `sleepCardLocation` form (the same check a typed entry passes) and the source record
   above (`phone` or `wifi`); **Cancel** changes nothing.
5. **Failures**, one plain line from what each source said: "No location: phone GPS not found
   and not enough known Wi-Fi nearby." (or "phone GPS has no fix", "nearby Wi-Fi gave answers
   that disagree", "nearby Wi-Fi is too rough to trust", "the Wi-Fi location service is
   unreachable"), with a hint: run gpsdRelay, Share GPS or GPS 2 IP on the phone; try outdoors;
   or, when beaconDB was never heard from (the network may not reach the internet, e.g. a sync
   peer's or a hub's hotspot), **Choose Wi-Fi Network** opens the list and tries again on the
   one picked. Also "No saved Wi-Fi in range" (the network list was left without joining) and
   "Not enough memory".
6. After the lookups the RF is stopped (`RadioPower::stop()`) for the result screen, or the radio
   is turned fully off (`RadioPower::off()`) on a failure. Leaving reboots back to Sleep Screen
   Cards (over Settings) like every network activity, and the Location row shows the new line.
   The lookup logs only `located: phone|wifi, accuracy N m`, the phone's sentence counts and
   verdict (`phone: port 10110, ok, 12 lines ...`), the Wi-Fi verdict with its counts, HTTP
   statuses, accuracies and distance apart, and on a failed connect the error class (DNS, TCP,
   TLS, certificate flags). Never a position. The dev build's Wi-Fi join
   (`WifiSelectionActivity`, every network activity) additionally logs the joined network's name
   and BSSID at debug level, and the last lines of the log are kept in `/crash_report.txt` after
   a panic; the release build (`LOG_LEVEL=1`) logs neither.

The beaconDB requests use `esp_http_client` with the framework's certificate bundle
(`esp_crt_bundle_attach`): the chain and the host name are verified. The fork's wolfSSL client
is not used here because it has no CA bundle and checks no host name. The client and the phone
socket are compiled for the X4 Pro only (`FREEINK_DEVICE_X4PRO`), so the other boards link no
TLS stack or bundle for them. Limits: 10 s for the connect and for each blocking handshake read,
15 s from the connect for the rest of a request, a 4 KB response cap, and no request starts
below 56 KB of free internal RAM (mbedTLS's record buffers live there). DNS comes before those
timeouts, so on a network that does not reach the internet one request can take ~30 s; with the
phone's 11 s the buttons can wait about a minute, and the screen says so.

**Bench** (dev builds, `scripts/x4bench.py`): `locphone <a.b.c.d>[:port]` makes `loctest`'s phone
source read NMEA from a computer on the same network instead of the gateway (RAM only; `locphone
off`; Locate Me and Update Location When Syncing never use it, so a stand-in is never saved), and
`loctest` runs the whole pipeline on the station already up (the live sleep screen on the
cable) and prints the verdict - `phone ok ±5m`, `wifi agree ±62m` or `nothing: phone not-found,
wifi too-few-aps` - with the counts behind it, saving nothing (`loctest coords` adds the position
at two decimals).

### Update Location When Syncing

Off unless turned on (Sleep Screen Cards, under Locate Me; the row's second line says what it
sends). It never starts the radio and adds no network job of its own: `network/AutoLocate`
runs only while a job that needed Wi-Fi for its own work still has it up, just before that job
takes the radio down, with the job's result already on screen, or right after the live sleep
screen's own clock sync on the charger:

- `KOReaderSyncActivity::performUpload()` after the upload, once its result is drawn and before
  `RadioPower::stop()` (an upload, including the smart sync's and the sync on close);
- `KOReaderSyncActivity::completeAlreadySynced()` with "already synced" on screen, before the
  timed return;
- `KOReaderSyncActivity::onExit()` when the station is still joined (the compare screen, no
  remote progress, a remote place applied; sync on open included), before the disconnect and
  the reboot;
- `ClockSyncActivity::loop()` after a successful Settings > Clock > Sync clock now, with the
  result on screen. The automatic clock sync on a Wi-Fi join (`WifiSelectionActivity`) does not
  run it;
- `network/StationKeeper` after each successful clock sync while the live sleep screen is up on
  the charger (the first join, then every 6 hours), treated like Sync clock now. `due()` answers
  once per boot, so the keeper re-arms it before asking; the once-a-day rule below still holds
  over a charging session that lasts days.

It then runs only when all of these hold (`sleepcards/AutoLocatePolicy`, host-tested in
`test/sleep_card_location`): the setting is on; the job's own request got an answer from its
server (a KOSync request with an HTTP status, or the clock synced) - after a network failure it
adds no wait of its own; the station is joined; the network is not the book-sync peer's or hub's
hotspot (`BookSync` Peer / Hub Wi-Fi Name: no internet behind them); the clock is set; it has not
been tried today (the day of the last try is kept in RTC memory across the reboot that ends every
sync and across deep sleep; a cold boot forgets it); and the location was not saved today (its
record's date, a typed location's too; an unset or undated location counts as due). "Older than
a day" is "not saved today", because the record keeps the date only. A try counts from the phone's
connect on, whatever comes of it, so an area beaconDB does not cover costs one scan and one or
two requests a day, not one per sync - except when the Wi-Fi half could not start for want of
TLS heap (nothing scanned or sent): a later sync that day may try again. Deep sleep that has
already started skips it.

The lookup is Locate Me's (`network/LocateRun`): the phone's GPS over its hotspot first (it needs
no TLS, so it is asked however short the heap is), then the two-half Wi-Fi check - the TLS heap
is checked first, then a scan through `RadioPower`, then two beaconDB requests of separate halves
of the usable access points (their hardware addresses and signal strengths, never their names;
the same filters, the -85 dBm floor and the grouping by device). Fewer than four usable devices:
skipped. There is no internet-address lookup (`scripts/check_radio_power.py`
fails if `LocateRun.cpp` passes anything but `BEACONDB_URL`, if `AutoLocate.cpp` makes a request
of its own, or if any file names an IP-geolocation host), and beaconDB's own IP/cell "fallback"
answers are refused by the parser. A phone fix is saved as `phone`; an agreed Wi-Fi location as
`wifi-auto` with today's date (it is never vaguer than 100 m; the 1,000 m limit is kept as a
backstop). It replaces a typed location. A stored Wi-Fi fix (Locate Me or an earlier refresh) at
least as tight as the Wi-Fi answer and within the two accuracies of it is kept instead - its place
and accuracy, re-dated today as `wifi-auto` - and a stored phone fix that the Wi-Fi answer agrees
with is left exactly as it is (only the phone writes a phone fix).

The phone half is bounded on its own (two 1.5 s connects and one 8 s read at most; on a home
router the connects are refused in milliseconds). The Wi-Fi half is bounded by
`AutoLocate::BUDGET_MS` (18 s): the scan, a name lookup of `api.beacondb.net` capped at 3 s
(lwIP's `dns_gethostbyname` under the core lock; the answer sits in lwIP's cache for the
requests), then the two requests cut to what is left with a 4 s connect timeout each. Only a
server that answers the TCP connect and then stalls mid-handshake can hold it a few seconds past
the budget (each blocking handshake read may wait the connect timeout). Any failure leaves the
settings as they were and the calling job carries on. The log gets one line per sync and nothing
about the place: `autolocate: saved phone|wifi`, `autolocate: kept phone`, `autolocate: skipped
<reason>` (`job-offline`, `not-connected`, `device-network`, `no-clock`, `tried-today`, `fresh`,
`sleeping`, `too-few-aps`) or `autolocate: failed <reason>` (`no-memory`, `dns`, `unreachable`,
`no-fix`, `too-vague`, `disagree`, `save`), after the lookup's own `phone:` and `locate:` lines.
ESP-IDF's own error lines (esp-tls, HTTP_CLIENT) can add a line on a connect failure; they name
the host, never a place or a network.

## Weather

The **Weather** card (Sleep Screen > Weather, or Shuffle: Weather) shows the forecast the reader
last fetched for the saved Location: NWS alerts first, then the conditions now, the next 24
hours, the next days, and when and for where the forecast was fetched. It needs **Weather** on
(off by default), a set clock and a Location.

**What is sent, and to whom.** Two GETs, each with the location rounded to 0.01 degree (about
1 km): Open-Meteo's forecast (`api.open-meteo.com/v1/forecast`: current conditions, 48 hours of
temperature, rain chance, weather code and wind, 7 days; free, no key, CC BY 4.0 - the card says
"Weather: Open-Meteo.com (CC BY 4.0)") and, for a point inside a rough US box, the National
Weather Service's active alerts (`api.weather.gov/alerts/active?point=...&status=actual`, public
domain). The requests carry the reader's User-Agent with this repository's URL and no personal
detail; no elevation is sent (the reader has no ground elevation for the place). Open-Meteo keeps
request coordinates in its server logs for up to 90 days. Both go through `GeolocateClient`
(esp_http_client with the ESP-IDF certificate bundle: the chain and the host name are verified).
Both servers send a Let's Encrypt chain that reaches ISRG Root X1 through a cross-signed "Root
YR"; the bundle has X1 but not Root YR, so the day a server drops the cross-sign the fetch fails
(it says `failed unreachable`) until Root YR is added to the bundle.

**When it fetches.** Never from a card render, and never by starting the radio
(`network/WeatherFetch`, held to that by `scripts/check_radio_power.py`'s WEATHER contract): only
while Wi-Fi is already up for another job, and never on the book-sync peer's or hub's hotspot.

- The live sleep screen on the charger: `StationKeeper`, after its clock sync and Update Location
  When Syncing, refreshes a cache over ~3 h old, one HTTPS request per keeper step (the forecast
  on one, the alerts on the next) so keys and the unplug are read in between; 30 min after a
  failed try. A new forecast (with its alerts, once both requests are done) redraws a Weather
  card on screen (or the logo it fell back to) with one clean HALF refresh. With Card Cycle When
  Charging on it does not: the next deal reads the new cache.
- Sync clock now, and a book sync that reached its server: after the location refresh, when the
  cache is over an hour old or for a place more than ~5 km from the Location.
- The bench console's `WEATHER fetch` (below).

Every attempt is stamped before its request in RTC memory, which survives the silent reboot after
each sync and deep sleep, so no automatic attempt follows another within 10 minutes (30 on the
charger). Each request is bounded like Update Location When Syncing (DNS 3 s, 14 s in all). One
log line per fetch and nothing about the place: `weather: ok 200/200 1.2 s` (the forecast's and
the alerts' HTTP status; `-` = not asked, outside the US), `weather: failed <why>` (`no-memory`,
`dns`, `unreachable`, `timeout`, `http <code>`, `parse`, `too-large`, `no-clock`, `save`; the old
cache stays), `weather: skipped <why>` (`off` is silent; `job-offline`, `no-wifi`,
`device-network`, `no-clock`, `no-location`, `retry-wait`, `fresh`, `sleeping`).

**On a wall charger at full charge the live screen goes quiet** (the charger's STAT line drops:
the full-charge hold, below, turns Wi-Fi off), so overnight the card keeps the forecast it last
fetched. Its absolute times say so.

**The cache.** `/.crosspoint/sleepcards/weather.dat`, written as `weather.tmp` and renamed over the
old one under the render lock; at most 4 KB of versioned text (`W1`, `sleepcards/WeatherCache.h`)
in SI units: the fetch time and whether the clock had been set from the internet (and agreed with
the server's Date), the rounded point and where the Location came from, the location's UTC offset,
the current conditions, 48 hours, 7 days, and the alerts with their own status and time. A
forecast replaces it only once its answer parsed (a captive portal's HTML never does). The alerts
keep five states: none in force at a check, a list (the three most severe kept, with the count),
"US only" (outside the box, or the service's 400 "out of bounds"), too many to read (the answer
was over 256 KB: "Alerts: 16+"), and not checked; a failed check keeps the last answer and marks
it not rechecked. A damaged file reads as no cache.

**What the card promises.** A sleep card stays on the glass for days, so:

- every time is absolute: "Forecast from 7:10 AM Thu", "No alerts at 7:10 AM Thu (NWS)", "until
  4:00 PM Wed" - never "3 h ago";
- "NOW" is Open-Meteo's current conditions only within an hour of the fetch; after that the block
  says "FORECAST FOR 21:00" and shows that hour's forecast (with no wind direction: the hourly
  series has none);
- the 24-hour strip starts at the hour covering the moment the card is drawn (hours already past
  are dropped), with the draw time marked;
- days are labelled by the date of their middle (Open-Meteo's day starts are the place's
  midnights) in the reader's own zone, so a reader on another zone than the place still pairs
  each high and low with the right date; when the place's offset at the fetch differs from the
  reader's, the card says "Local time there is UTC-6 - check the clock setting" (it never changes
  a clock);
- "No alerts" is said only after a check that found none, with its time; an alert is hidden once
  it has ended (`ends`, or `expires` when the alert gives no end). Only the 3 most severe are kept,
  so "have ended" is said only when every alert in force at the check was among them; otherwise
  "Alerts at 8:05 PM Mon: 2 may still apply", and "+N more" never counts one known to be over;
- the strip's rain bars sit on the hour they describe (Open-Meteo gives each hour's chance for the
  hour before its time) and each temperature on its own hour's tick;
- wind is where it comes FROM ("from SSW 21 km/h, gusts 37"; the day rows' column says "Wind
  from"); the drawn arrow points downwind, where scent goes;
- the place line says where the Location came from ("Wi-Fi fix Nov 2", "phone GPS fix Oct 10",
  "typed location"; a cache from an older firmware's internet-address location still reads "IP
  location (city level)", though the card declines it while that location counts as not set).

It declines (the logo, or Shuffle's next card) without Weather on, a set clock, a Location or a
readable cache, when the forecast is over 36 h old or stamped in the future, for a place more than
~5 km from the Location, or once its last day is over. Shuffle deals it only when the cache's
first line (one small read) says it would draw.

## Live sleep on the charger

On external power the sleep screen does not deep-sleep: it stays up and is redrawn, so the
footer's time and battery (and the sky, the day, the calendar) stay current, and Wi-Fi stays
joined. `src/util/LiveSleepPolicy.h` holds the decisions (host-tested in `test/live_sleep`),
`main.cpp` the session, `SleepActivity` the drawing and `network/StationKeeper` the Wi-Fi.

- **When.** Every sleep (Power, Time to Sleep, the bench's SLEEP) goes through
  `enterDeepSleep()`, which takes the live branch on the X4 Pro when external power is present
  and the sleep screen is a card, or for any sleep screen when Card Cycle When Charging is on.
  External power is the charger STAT line (charging) or a computer's USB SOF frames; the board
  has no VBUS sense. Live wins over Quick Resume after timeout; Quick Resume as the sleep screen
  goes live only with the cycle. Unplugged, nothing changes: the same screen, then deep sleep.
- **Redraws.** On the minute: 2 s past each wall-clock minute that is a multiple of Charging
  Updates (every 2 min by default), so the footer's minute is true; a plain interval when the
  clock is not set. With the cycle each redraw deals the next Shuffle card (the Shuffle deck, so
  every ticked card comes round before one repeats). Pictures picked: a fresh random picture
  from `/.sleep` or `/sleep` (no repeat among the recent ones; `/sleep.bmp` only when the
  folders have none). Pictures picked with no pictures on the card: the next card instead (never
  the one on screen), and Shuffle leaves Pictures out for the rest of the session.
  Without the cycle the card on screen is redrawn (the Quote card keeps its quote:
  `CardContext::repeatLast`); a picture is not redrawn; a logo (every pick declined, say with no
  clock yet) asks for the Sleep Screen card again, so it shows once NTP has set the clock. The
  logo always takes the cards' polarity. Refresh: HALF on a card change, after a picture, every
  5th redraw and for the final frame at the unplug; FAST for a same-card redraw in between.
- **Wake.** Any physical key (not touch, not Home) wakes it once every key is up again, so the
  press never also turns a page (a key still down 1.5 s after the press wakes it anyway, and the
  unplug is watched all the while): the radio goes off and the reader restarts to the book or
  Home, as a deep-sleep wake would, the light as Restore Light on Wake says. The restart is
  marked as a wake (`SILENT_REBOOT_LIVE_WAKE`), so Pull on Open runs on the reopened book as it
  does after deep sleep.
- **Unplug.** External power absent for 20 s without a break counts as unplugged: one last
  redraw (fresh time and battery), then today's deep sleep, and Auto Power Off counts from
  there - unless the battery was full when the power went (the full-charge hold, next). A
  computer on the cable keeps it live at 100 % (its SOF frames prove the power), so there is no
  hold on a computer.
- **The full-charge hold.** The board has no cable-present line: a PINS run (2026-10-03) with
  the cable out ~54 s saw only the charger STAT line (GPIO21) follow it, and none of the six free
  pins. On a wall charger the charger stops at full and STAT drops with the cable still in, which
  read as an unplug (the 2026-10-01 overnight log: live at 18:56, `sleep x=2` at 19:09 at 99 %,
  cut by Auto Power Off at 19:38); kept on the charger at 94-98 % STAT stayed low for hours (the
  charger restarts only below its recharge threshold). So when power has been absent the 20 s and
  the gauge read at least 97 % (`HOLD_MIN_SOC`) at the moment it vanished, the screen holds
  instead: the station keeper stops and the radio goes off through `RadioPower::off()` (the
  driver torn down, not `RadioPower::stop()`, so the keeper can start it again in this boot,
  exactly as at live entry), no NTP, location or weather; the screen is redrawn every
  max(Charging Updates, 15 min) on the same card rules (the cycle keeps dealing, FAST/HALF as
  live), and a card is redrawn at once so the footer loses its charging bolt; between redraws the
  idle loop light-sleeps as on battery. Keys wake it exactly as live. It ends one of three ways:
  external power back (STAT or a computer) makes it fully live again (the keeper rejoins, the
  normal interval, the bolt back); the battery `HOLD_DROP_PCT` (3 %) below the SOC it started at
  means it really was unplugged: the final redraw and today's deep sleep, Auto Power Off from
  there; after `HOLD_MAX_MS` (24 h) it sleeps the same way, so a gauge that never moves cannot hold
  forever. The gauge is read once a minute for those two. The accepted cost: unplugged right
  at full, the reader spends about 3 % before it notices (counted from the reading when power
  went, so more if the gauge crept up after that). Unproven on the wall charger: the hold assumes
  the charger restarts before the battery is 3 % down; if it does not, the hold ends with the
  cable still in (`holdend x=4`, then `sleep x=4`), and the overnight log's `holdend` soc/mv and
  `hold=1` pwr lines say where the threshold has to go. `util/LiveSleepPolicy.h` holds the
  thresholds and the decisions (test/live_sleep); the bench's `POWER fake absent` (dev builds,
  live screen only) fakes the unplug on the bench cable, `POWER fake real` ends it.
- **Plugged in while asleep.** Nothing wakes a deep-sleeping reader when a cable goes in, but
  when the Auto Power Off timer fires on the charger the rail is not cut: the reader boots
  straight into the live screen (or cuts it after all when the sleep screen cannot be live).
- **Wi-Fi.** `StationKeeper` scans, joins the last network if it is in view, else the next-best
  saved one (`network/WifiJoinOrder.h`; never the book-sync peer's or hub's hotspot), syncs the
  clock from NTP on the first join and every 6 h, then runs Update Location When Syncing when
  that is on, then the Weather card's fetch (see Weather). A lost link waits 2 min and scans again; three failed rounds in a row, 10 min.
  Its scans list hidden networks too, so a hidden last network is tried by name when one is in
  range; a join names the scanned access point (fast scan on its channel). Between attempts the
  driver stays initialised (`WiFi.disconnect(false, true)`): the session has no restart to
  clear the heap, so only `RadioPower::off()` at the wake, the unplug or the full-charge hold tears
  it down (the hold's end with power back starts a fresh driver).
  Every start goes through `RadioPower`; it holds the radio lock (full clock, no naps), which on
  the charger costs nothing that matters.
- **Ledger.** `/sleep.log` gets `live` (x: 0 button, 1 timeout, 2 timer boot on the charger),
  `livewake`, `hold` (x = the SOC when power went) and `holdend` (x: 0 power back, 4 the 3 %
  drop, 5 the 24 h cap), and `sleep` with x=2 (unplugged), 3 (the bench's SLEEP deep), 4 (the
  hold's drop) or 5 (the hold's cap) when it ends. A key (`livewake`) or the bench's SLEEP deep
  (`sleep x=3`) also ends a hold, with no `holdend` line. The 5-minute `pwr` lines carry `live=`, the
  free heap / largest block and, last, `hold=`. The ledger's rotation is
  checked at entry and hourly while live, so it keeps its 64 KB cap on a charger for days.

## How a card is built

- `src/sleepcards/SleepCard.h`: `CardContext` (the moment, local date, DST-aware offset
  function, 12/24 h, battery, location, a settings snapshot, the open book's path, a
  random seed, `CardIo`, `dark`), the registry, and `renderCard()` which clears the frame,
  runs the card, adds the shared footer and, for a dark card, inverts the frame except the
  areas a picture kept (`draw::keepTonesRect`/`keepTonesDisc`: `drawMoon` keeps its disc
  inside the ring, Now Reading its cover).
- One file pair per card: `NowReadingCard`, `DayCard`, `CalendarCard`, `QuoteCard`,
  `OwnerCard`, `SkyCard`, `WeatherCard` (with `WeatherCache`, its file), `ShuffleCard`
  (`pickShuffleCard()`; `renderCardOrShuffle()` moves on to the next ticked card when Shuffle's
  pick declines).
- Shared helpers: `CardDraw.h` (circles, ellipses, dithered greys, the moon, progress
  bars, magnified text, the big pre-drawn digits, wrapping, the footer with its battery
  outline), `CardTime.h` (dates, DST day boundaries, clock strings), `CardText.h` (month
  and weekday names), `MoonLabel.h` (how a phase is named: "Full moon" only on the full
  moon's own date), `CoverDraw.h` (the Now Reading cover), `SleepCardSettings.h` (location
  parsing, the hunting-season calendar, shuffle defaults), `lib/Almanac` (sun and moon
  events, phases, legal light; ported from the WiPhone firmware and checked against
  PyEphem by `test/almanac`), `SkyPlanets.h` (Venus, Mars, Jupiter, Saturn; checked
  against PyEphem by `test/sleep_card_sky`, vectors from `scripts/gen_sky_vectors.py`).
- Generated images, each from a script kept beside it: `src/images/X4ProLogo.h` (the X4 Pro
  mark at 160 and 80 px, `scripts/gen_x4pro_logo.py`, original geometry) and
  `src/images/CardDigits.h` (the Day card's numerals from the repository's Noto Sans Bold,
  `scripts/gen_card_digits.py`).
- `DeviceCards.cpp` is the only device-side file: it builds the context from the live
  reader and is shared by `SleepActivity` and the bench `CARD` verb.

A card returns `false` when it has nothing true to show (no time, no book, a bad file);
the reader then shows the X4 Pro logo screen. Work on the sleep path is bounded: no
unbounded allocation, no loop on I/O, a target of 300 ms of compute. The host tests only
catch a runaway (the astronomy is software double precision on the device, roughly 1,500x
slower than a desktop); the budget itself is measured on the reader: the bench reply's
`ms=` (below) and the log line `CARD <name> drawn in N ms`.

## What each card relies on

- **Now Reading**: the open book's metadata cache and `progress.bin`; the cover is one of
  the home screen's 1-bit thumbnails (`thumb_<height>.bmp` in the book's cache), drawn 1:1
  or scaled down by area averaging (never a 2-bit cover: the card is black and white).
  "Time left" comes from the reader's own page timings, kept in
  `/.crosspoint/sleepcards/now_reading.dat` and saved on every sleep.
- **Day** and **Sky**: the RTC and the Location setting. With Hunting Season on, the Day
  card shows today's legal light until it has ended, then tomorrow's (with tomorrow's
  sunrise and sunset beside it). Legal light is rounded inward to the minute after a
  15-second margin for the model's error, so it is never shown a minute early.
- **Quote**: the built-in set, "My quotes" (`/quotes.txt` at the card root, below) and the
  open book's bookmarks, as Quote Source enables them. A coin picks among the enabled categories
  that have something not shown last time; when none of them has an entry, My quotes fill in,
  then the built-in set, so the card never falls back to the logo screen.
- **Owner**: the three Owner rows, typed on the reader.
- **Weather**: `/.crosspoint/sleepcards/weather.dat` (one read of at most 4 KB), the RTC, the
  Location and Weather Units; see Weather above. Its pure parts are host-tested in
  `test/weather_protocol` (the URLs, both parsers on scrubbed captures from public points, the
  alert states, the fetch policy, the cache codec) and `test/sleep_card_weather` (what it shows:
  daily labels across a DST change and a zone mismatch, now vs forecast, declines, units).

## The built-in quotes

`src/sleepcards/BuiltInQuotes.cpp`: 37 short public-domain passages (works published before
1930) about the outdoors, reading, travel and night - Thoreau, Muir, Frost, Stevenson, Whitman,
Emerson, Keats, Dickinson, Byron, Tennyson, Longfellow, Shakespeare, Bacon, Austen, Melville,
Twain, Grahame, Franklin, Roosevelt. Each was checked word for word against the Project
Gutenberg edition whose URL is beside it; an excerpt may begin with a capital and end with a
full stop where the sentence runs on, nothing else is changed. Verse keeps its line breaks, and
a line too long for the card goes on with a hanging indent. `test/sleep_card_quote` checks every
entry (it parses, has an author and a work, fits the card whole in a serif size with the
opening mark) and writes each to `build/cards/quote_builtin_NN.png`.

## The quotes file ("My quotes")

`/quotes.txt` at the root of the SD card holds your own quotes. It stays on the card: nothing in
it is compiled into the firmware or kept in the repository. Plain UTF-8 text (LF, CRLF or CR line
ends):

```text
Adopt the pace of nature: her secret is patience.
 -- Ralph Waldo Emerson

Be kind. -- Plato

A long passage may be hard-wrapped
over several lines; they are joined.
-- Anonymous, An Example Book

# A group of my own
Every group heading is skipped.
-- Anonymous
```

- Entries are separated by blank lines (a line holding only `%`, the fortune(6) separator,
  counts as blank too).
- The attribution goes on its own line starting with `--` (or an em dash), or at the end of
  the entry's last line after ` -- `. After a comma, the rest is set as the work
  ("Name, Title").
- An entry whose first line opens with an em dash is dialogue and stays text.
- A line starting with `#` is a heading for you, to group your quotes (`# BattleTech`): the
  card skips it, and it ends the entry before it.
- While the built-in set is enabled too, an entry whose words match a built-in quote (letters
  and digits compared, so spacing, line breaks, apostrophes and dashes do not matter; the
  attribution, on its own line or after ` -- `, is not compared) is left out, so it is not
  shown twice as often.
- The first 64 KB of the file are read; an entry over 1 KB is skipped. The card never shows
  the same entry twice in a row while there is another.

## Looking at the cards

On the computer (no reader needed), with the real renderer and fonts:

```sh
export PATH=$HOME/.pio-x4/bin:$PATH
cmake -S test -B build/test -G Ninja
cmake --build build/test --target SleepCardPreviewTest
./build/test/sleep_card_preview/SleepCardPreviewTest
open build/cards/*.png
```

The sample moment is 2026-11-02 20:40 in America/Los_Angeles at Seattle's public
city-centre coordinates, battery 73 %, a public-domain sample book. `_helpers.png` is a
sheet of the drawing helpers; `logo.png` the fallback screen; `fallback_<card>.png` each
card (and Shuffle) with no location set and no book open. `weather_<state>.png` is the Weather
card from the caches in `test/sleep_card_preview/fixtures/.crosspoint/sleepcards/` (written by
`make_weather_fixtures.py` beside them): `fresh`, `alert`, `usonly`, `notchecked`, `toomany`,
`us_units`, `dark`, `cooling` (the high beside the now marker; a later alert's 12-hour range with
"+1 more") and `lapsed` (the kept alerts over, two not kept).

On the reader (x4pro dev build, USB attached):

```sh
python3 scripts/x4bench.py card day --shot /tmp/day.png
python3 scripts/x4bench.py key up      # back to where it was
python3 scripts/x4bench.py weather            # the Weather cache, summarised (no coordinates)
python3 scripts/x4bench.py weather fetch      # a fetch now: only on a station already up (STATE wifi=up)
python3 scripts/x4bench.py weather clear      # remove the cache and the retry stamp
```
