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
| Hunting Season | `huntingSeason` | Off / On / Between Dates |
| Season Start / End | `huntStartMonth` `huntStartDay` `huntEndMonth` `huntEndDay` | typed as month-day (`10-01`); a season may run past New Year |
| Legal Light | `legalLightRule` | sunrise - 30 min .. sunset + 30 min, or civil twilight; always rounded inward to the minute |
| Owner Name, Owner Contact 1/2 | `ownerName` `ownerContact1` `ownerContact2` | typed on the reader; nothing is preset |
| Quote Source | `quoteSource` | `/quotes.txt` on the card, the open book's bookmarks, or both |
| Shuffle: ... | `shuffleNowReading` ... `shufflePictures` | default on: Now Reading, Day, Calendar, Quote, Sky |

All of them are ordinary `settings.json` keys (and appear on the web settings page under
"Sleep Screen Cards").

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
