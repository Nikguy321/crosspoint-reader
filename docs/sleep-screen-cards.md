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
| (under Location) | `sleepCardLocationFix` | where the location came from, shown as the Location row's second line ("From Wi-Fi, ±80 m, Sep 29", "From internet address, city level, Sep 29", "Typed in, Sep 29"). Stored as `wifi 80 2026-09-29 47.6205,-122.3493` (source, accuracy in m, date saved, the location it describes): a location changed anywhere else reads as typed in, and one confirmed unchanged on the keyboard keeps its record. An internal key: saved in `settings.json` but not shown on the web settings page. |
| Locate Me | - | finds the location from the internet; see below |
| Hunting Season | `huntingSeason` | Off / On / Between Dates |
| Season Start / End | `huntStartMonth` `huntStartDay` `huntEndMonth` `huntEndDay` | typed as month-day (`10-01`); a season may run past New Year |
| Legal Light | `legalLightRule` | sunrise - 30 min .. sunset + 30 min, or civil twilight; always rounded inward to the minute |
| Owner Name, Owner Contact 1/2 | `ownerName` `ownerContact1` `ownerContact2` | typed on the reader; nothing is preset |
| Quote Source | `quoteSource` | `/quotes.txt` on the card, the open book's bookmarks, or both |
| Shuffle: ... | `shuffleNowReading` ... `shufflePictures` | default on: Now Reading, Day, Calendar, Quote, Sky |

All of them are ordinary `settings.json` keys and, apart from `sleepCardLocationFix`, appear
on the web settings page under "Sleep Screen Cards".

### Locate Me

A screen says what will be sent where (beaconDB, and ipwho.is for the fallback) and that the
reader restarts afterwards, before anything leaves the reader; nothing is sent without the
Locate tap, and nothing runs in the background.

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

## How a card is built

- `src/sleepcards/SleepCard.h`: `CardContext` (the moment, local date, DST-aware offset
  function, 12/24 h, battery, location, a settings snapshot, the open book's path, a
  random seed, `CardIo`), the registry, and `renderCard()` which clears the frame, runs
  the card and adds the shared footer.
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
- **Quote**: `/quotes.txt` at the card root (below) and/or the open book's bookmarks.
- **Owner**: the three Owner rows, typed on the reader.

## The quotes file

`/quotes.txt` at the root of the SD card, plain UTF-8 text (LF, CRLF or CR line ends):

```text
Adopt the pace of nature: her secret is patience.
 -- Ralph Waldo Emerson

Be kind. -- Plato

A long passage may be hard-wrapped
over several lines; they are joined.
-- Anonymous, An Example Book
```

- Entries are separated by blank lines (a line holding only `%`, the fortune(6) separator,
  counts as blank too).
- The attribution goes on its own line starting with `--` (or an em dash), or at the end of
  the entry's last line after ` -- `. After a comma, the rest is set as the work
  ("Name, Title").
- An entry whose first line opens with an em dash is dialogue and stays text.
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
