# Sleep screen cards (X4 Pro)

On the X4 Pro the Sleep Screen setting offers, besides the classic screens, a set of
cards: **Now Reading**, **Day Card**, **Month Calendar**, **Quote**, **Owner Card**,
**Tonight's Sky** and **Shuffle** (a different ticked card each sleep; it may also pick
the picture frame, which is the existing Custom screen). Their options live in
Settings > Display > **Sleep Screen Cards**. The Dark and Light screens, the boot screen
and every card's fallback show the X4 Pro mark. Other boards keep the CrossPoint screens
and do not list the cards.

The e-ink keeps the last picture with no power, also after Auto Power Off cuts the rail,
so a card shows the moment the reader fell asleep ("Asleep since 21:04"), never a clock
that pretends to be live.

## Settings

| Row | Stored as | Notes |
|---|---|---|
| Location | `sleepCardLocation` | decimal degrees, latitude first: `51.4779, -0.0015`, `51.48 N 0.00 W`. Empty = not set; sun and moon times then read "Set location in Settings" (the moon's phase needs no place). |
| (under Location) | `sleepCardLocationFix` | where the location came from, shown as the Location row's second line ("From Wi-Fi, ±80 m, Sep 29", "From Wi-Fi (auto), ±80 m, Sep 29", "From internet address, city level, Sep 29", "Typed in, Sep 29"). Stored as `wifi 80 2026-09-29 47.6205,-122.3493` (source `typed` / `wifi` / `ip` / `wifi-auto`, accuracy in m, date saved, the location it describes): a location changed anywhere else reads as typed in, and one confirmed unchanged on the keyboard keeps its record. An internal key: saved in `settings.json` but not shown on the web settings page. |
| Locate Me | - | finds the location from the internet; see below |
| Update Location When Syncing | `autoLocateOnSync` | Off by default. While a book sync or Sync clock now already has Wi-Fi up, refreshes a location not saved today from nearby Wi-Fi (beaconDB only; replaces a typed location), at most once a day; see below |
| Hunting Season | `huntingSeason` | Off / On / Between Dates |
| Season Start / End | `huntStartMonth` `huntStartDay` `huntEndMonth` `huntEndDay` | typed as month-day (`10-01`); a season may run past New Year |
| Legal Light | `legalLightRule` | sunrise - 30 min .. sunset + 30 min, or civil twilight; always rounded inward to the minute |
| Owner Name, Owner Contact 1/2 | `ownerName` `ownerContact1` `ownerContact2` | typed on the reader; nothing is preset |
| Quote Source | `quoteSources` | which categories feed the Quote card: All (default), Built-in + My Quotes, My Quotes + Bookmarks, Built-in + Bookmarks, Built-in Only, My Quotes Only, Bookmarks Only (stored by that index). Replaces the old `quoteSource` key, migrated once on load: quotes file -> My Quotes Only, bookmarks -> Bookmarks Only, both -> All |
| Shuffle: ... | `shuffleNowReading` ... `shufflePictures` | default on: Now Reading, Day, Calendar, Quote, Sky |

All of them are ordinary `settings.json` keys and, apart from `sleepCardLocationFix`, appear
on the web settings page under "Sleep Screen Cards".

**White on black.** Settings > Display > Sleep Screen Cover Filter = **Inverted**
(`sleepScreenCoverFilter` 2) also turns every card, and the logo screen a card falls back to,
white on black. The moon and the Now Reading cover keep their real tones: the lit part of the
moon stays white inside a white ring (a new moon is a dark disc, never the white disc of a full
moon), and the cover is never a negative. **Contrast** changes nothing on the cards (they are
black and white already); it still applies to the picture frame.

### Locate Me

A screen says what will be sent where (beaconDB, and ipwho.is for the fallback) and that the
reader restarts afterwards, before anything leaves the reader; nothing is sent without the
Locate tap, and nothing runs in the background (the one opt-in exception is Update Location
When Syncing, below, which rides a sync's own Wi-Fi).

1. **Join** a saved Wi-Fi network through `WifiSelectionActivity`'s auto-connect (the last
   network, then any saved one in range; the network list only when none is). Every radio start
   goes through `RadioPower` (full clock, no light sleep while it is up).
2. **Scan** (a blocking station scan on the joined radio) and send up to 20 access points, the
   strongest first, to [beaconDB](https://beacondb.net) (`POST https://api.beacondb.net/v1/geolocate`,
   the MLS / Ichnaea geolocate API, `considerIp:false`, a `CrossPoint-X4Pro/<version>` user agent
   as beaconDB asks). Hidden networks, SSIDs ending in `_nomap` or `_optout`, and locally
   administered (randomised / hotspot) or group BSSIDs are never sent; fewer than two left means
   no Wi-Fi lookup. beaconDB keeps no record of the access points queried or the location
   returned ([privacy notice](https://beacondb.net/privacy/)); its web logs (with the IP address)
   go after 28 days.
3. **Fall back** to the internet address when beaconDB fails, has too few access points, or
   answers vaguer than 5 km: [ipwho.is](https://ipwhois.io/docs) (`GET https://ipwho.is/?fields=...`,
   free, no key, HTTPS, commercial use allowed, 1,000 requests a day per address). It gives a
   city and region but no accuracy, so it is shown as "city level", never as a distance: on a
   phone hotspot or a VPN the address belongs to the carrier's or the VPN's city, which can be
   100 km or more away, and the result screen says so.
4. **Result**: the place ("Seattle, Washington" with its coordinates, or "Near 47.62, -122.35" for
   a Wi-Fi fix) and how sure it is ("About 80 m, from Wi-Fi" / "City level, from your internet
   address"). **Save** stores the location in the usual `sleepCardLocation` form (the same check a
   typed entry passes) and the source record above; **Cancel** changes nothing.
5. **Failures** (`geolocate::classifyFailure`, host-tested): "No saved Wi-Fi in range" (the
   network list was left without joining); "Couldn't reach the location service" (neither
   service was heard from: the network may not reach the internet, e.g. a sync peer's or a
   hub's hotspot - **Choose Wi-Fi Network** opens the list and tries again on the one picked);
   "Not enough Wi-Fi networks nearby" (the internet works, fewer than two access points);
   "No location found" (the services answered without a location); "Not enough memory".
6. After the lookups the RF is stopped (`RadioPower::stop()`) for the result screen, or the radio
   is turned fully off (`RadioPower::off()`) on a failure. Leaving reboots back to Sleep Screen
   Cards (over Settings) like every network activity, and the Location row shows the new line.
   The lookup logs only `located: wifi|ip, accuracy N m`, the scan's network count and, on a
   failed connect, the error class (DNS, TCP, TLS, certificate flags). The dev build's Wi-Fi join
   (`WifiSelectionActivity`, every network activity) additionally logs the joined network's name
   and BSSID at debug level, and the last lines of the log are kept in `/crash_report.txt` after
   a panic; the release build (`LOG_LEVEL=1`) logs neither.

Both requests use `esp_http_client` with the framework's certificate bundle
(`esp_crt_bundle_attach`): the chain and the host name are verified. The fork's wolfSSL client
is not used here because it has no CA bundle and checks no host name. The client is compiled
for the X4 Pro only (`FREEINK_DEVICE_X4PRO`), so the other boards link no TLS stack or bundle
for it. Limits: 10 s for the connect and for each blocking handshake read, 15 s from the
connect for the rest of the request, a 4 KB response cap, and the request is not started below
56 KB of free internal RAM (mbedTLS's record buffers live there). DNS comes before those
timeouts, so on a network that does not reach the internet one request can take ~30 s and the
two about a minute; the buttons wait meanwhile, and the screen says it can take up to a minute.

### Update Location When Syncing

Off unless turned on (Sleep Screen Cards, under Locate Me; the row's second line says what it
sends). It never starts the radio and adds no network job of its own: `network/AutoLocate`
runs only while a job that needed Wi-Fi for its own work still has it up, just before that job
takes the radio down, with the job's result already on screen:

- `KOReaderSyncActivity::performUpload()` after the upload, once its result is drawn and before
  `RadioPower::stop()` (an upload, including the smart sync's and the sync on close);
- `KOReaderSyncActivity::completeAlreadySynced()` with "already synced" on screen, before the
  timed return;
- `KOReaderSyncActivity::onExit()` when the station is still joined (the compare screen, no
  remote progress, a remote place applied; sync on open included), before the disconnect and
  the reboot;
- `ClockSyncActivity::loop()` after a successful Settings > Clock > Sync clock now, with the
  result on screen. The automatic clock sync on a Wi-Fi join (`WifiSelectionActivity`) does not
  run it.

It then runs only when all of these hold (`sleepcards/AutoLocatePolicy`, host-tested in
`test/sleep_card_location`): the setting is on; the job's own request got an answer from its
server (a KOSync request with an HTTP status, or the clock synced) - after a network failure it
adds no wait of its own; the station is joined; the network is not the book-sync peer's or hub's
hotspot (`BookSync` Peer / Hub Wi-Fi Name: no internet behind them); the clock is set; it has not
been tried today (the day of the last try is kept in RTC memory across the reboot that ends every
sync and across deep sleep; a cold boot forgets it); and the location was not saved today (its
record's date, a typed location's too; an unset or undated location counts as due). "Older than
a day" is "not saved today", because the record keeps the date only. A try counts from the scan
on, whatever comes of it, so an area beaconDB does not cover costs one scan and one request a
day, not one per sync. Deep sleep that has already started skips it.

The lookup is Locate Me's Wi-Fi half and nothing else: the TLS heap is checked first, then a
scan through `RadioPower`, then up to 20 access points (their hardware addresses and signal
strengths, never their names; the same hidden / `_nomap` / `_optout` / randomised-address
filters) to beaconDB. Fewer than two usable access points: skipped. The internet-address lookup
(ipwho.is) is never used (`scripts/check_radio_power.py` fails if `AutoLocate.cpp` names it or
passes anything but `BEACONDB_URL`), and beaconDB's own IP/cell "fallback" answers are refused by
the parser. A fix is saved only within 1,000 m, recorded as `wifi-auto` with today's date. It
replaces a typed location. A stored Wi-Fi fix (Locate Me or an earlier refresh) at least as tight
as the answer and within the two accuracies of it is kept instead - its place and accuracy,
re-dated today as `wifi-auto` - so a ±30 m Locate Me fix is not traded for a ±900 m one.

The run is bounded by `AutoLocate::BUDGET_MS` (14 s): the scan, a name lookup of
`api.beacondb.net` capped at `DNS_MS` (3 s, lwIP's `dns_gethostbyname` under the core lock; the
answer sits in lwIP's cache for the request), then one request cut to what is left with a 4 s
connect timeout. Only a server that answers the TCP connect and then stalls mid-handshake can
hold it a few seconds past the budget (each blocking handshake read may wait the connect
timeout). Any failure leaves the settings as they were and the calling job carries on. The log
gets one line per sync and nothing about the place: `autolocate: saved`, `autolocate: skipped
<reason>` (`job-offline`, `not-connected`, `device-network`, `no-clock`, `tried-today`, `fresh`,
`sleeping`, `too-few-aps`) or `autolocate: failed <reason>` (`no-memory`, `dns`, `unreachable`,
`no-fix`, `too-vague`, `timeout`, `save`). ESP-IDF's own error lines (esp-tls, HTTP_CLIENT) can
add a line on a connect failure; they name the host, never a place or a network.

## How a card is built

- `src/sleepcards/SleepCard.h`: `CardContext` (the moment, local date, DST-aware offset
  function, 12/24 h, battery, location, a settings snapshot, the open book's path, a
  random seed, `CardIo`, `dark`), the registry, and `renderCard()` which clears the frame,
  runs the card, adds the shared footer and, for a dark card, inverts the frame except the
  areas a picture kept (`draw::keepTonesRect`/`keepTonesDisc`: `drawMoon` keeps its disc
  inside the ring, Now Reading its cover).
- One file pair per card: `NowReadingCard`, `DayCard`, `CalendarCard`, `QuoteCard`,
  `OwnerCard`, `SkyCard`, `ShuffleCard` (`pickShuffleCard()`; `renderCardOrShuffle()` moves on
  to the next ticked card when Shuffle's pick declines).
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
card (and Shuffle) with no location set and no book open.

On the reader (x4pro dev build, USB attached):

```sh
python3 scripts/x4bench.py card day --shot /tmp/day.png
python3 scripts/x4bench.py key up      # back to where it was
```
