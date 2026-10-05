# CrossPoint for the X4 Pro (Nick H.'s fork)

This is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for the
**Xteink X4 Pro**, on the `booksync` branch. It keeps CrossPoint's features and adds the ones below for the X4
Pro. CrossPoint's own README follows further down.

## What this fork adds

- **Reading-position sync with a [WiPhone](https://github.com/Nikguy321/wiphone-meshtastic) and COVEY**: KOReader
  Sync pointed at the WiPhone's sync window on its `WiPhone-Books` hotspot, or at a home KOSync server, optionally on
  book open and close.
- **Sleep screen cards**, drawn fresh at each sleep:
  - Now Reading: cover, progress and pace.
  - Day: sunrise and sunset, the moon, and legal shooting light during hunting season.
  - Calendar: the month with its moon phases.
  - Quote: 37 built-in public-domain quotes, your own `/quotes.txt` and your bookmarks.
  - Owner: return-if-found details.
  - Sky: the moon and the planets tonight.
  - Weather (opt-in): NWS alerts, the conditions now, the next 24 hours and the next days from Open-Meteo, fetched
    only while Wi-Fi is already up (on the charger, or during a clock or book sync); every time on it is absolute.
  - Shuffle: a different card each sleep.

  An X4 Pro logo replaces the CrossPoint one, and the cards can be white on black.
- **A live sleep screen on the charger:** the card is redrawn on the minute (every 2 minutes by default) and can deal
  a new Shuffle card each time (Card Cycle When Charging); unplugged, the reader sleeps as usual.
- **Locate Me** sets the cards' location from nearby Wi-Fi (beaconDB), or from your internet address as a fallback.
  Nothing is sent until you press Locate. It can also refresh the location once a day during a sync.
- **Battery behaviour like the stock firmware:**
  - Auto Power Off turns the reader fully off after a set time asleep.
  - The awake reader naps between key presses, with the light on too.
  - Wi-Fi is on only while a job needs it, or while the sleep screen is live on the charger.
- **Apps on the Home screen**, starting with **Word Search**: touch or key play, three difficulties, 31
  built-in themes plus your own `.words` lists in `/Puzzles/WordSearch/`, hints, and the puzzle kept across sleep.
- **Crossword** (touch readers): American-style crosswords typed on an on-screen keyboard, with checks, reveals, a
  clue list, 48 built-in puzzles written for this repository (30 5x5, 12 7x7, 6 9x9; grids filled on a computer from
  the MIT Collaborative Word List, every clue written fresh: see
  [scripts/crossword/README.md](scripts/crossword/README.md)), and your own `.ipuz` / `.puz` files in
  `/Puzzles/Crossword/` (ipuz is a trademark of Puzzazz, Inc., used with permission); progress is kept per puzzle
  and across sleep.
- **Sudoku** (touch readers): classic 9x9 puzzles made on the reader, each with one solution, graded Easy / Medium /
  Hard / Expert by the hardest step a person needs, and numbered (Medium 14 is the same puzzle on every reader);
  square-first entry plus a digit lock, pencil marks, undo, live clash marks, checks, reveals and logic hints; the
  puzzle is kept across sleep.
- **Survival guide** (touch readers): a built-in, menu-driven field guide read from a pack on the card
  (`/Guides/survival/`, built from [packs/guide](packs/guide/README.md)): 12 sections with EMERGENCY first, 86 topics,
  228 pages and 69 line drawings from U.S. Army manuals approved for public release; first aid written fresh from
  current CDC / NPS / NWS guidance. Breadcrumbs, PREV / MENU / NEXT across pages and topics, quick cards, search,
  bookmarks and recent, resumed after sleep. Reviewed by AI reviewers, not by a medical professional: reference only.
- **More room**: the stock 16 MB flash layout (two 7.875 MB app slots) for the `x4pro` build.
- **A USB bench console** (`x4pro` dev build): drive the reader from a computer with `scripts/x4bench.py`, including
  keys, taps, screenshots, files and card previews.

More: [sleep screen cards](docs/sleep-screen-cards.md), [awake power](docs/awake-power.md) and the
[user guide](USER_GUIDE.md).

## Installing it

There is no prebuilt release yet. Build the `x4pro` environment with PlatformIO (`pio run -e x4pro`) and flash it
over USB. Back up the stock firmware first: this fork has no way back to it on its own.

## Credit

The reader, the rendering and the wireless features are CrossPoint's work (MIT License); this fork adds the items
above. To sync with upstream, `develop` is kept as CrossPoint's own branch.

---

# CrossPoint Reader

[![Fund contributors](https://img.shields.io/badge/%F0%9F%91%91_Fund_contributors-royalty.dev-BB953A?style=for-the-badge&labelColor=1a1a1a)](https://app.royalty.dev/crosspoint-reader/crosspoint-reader)

CrossPoint is open-source e-reader firmware - community-built, fully hackable, free forever. It's maintained by a growing community of developers and readers who believe your device should do what you want - not what a manufacturer decided for you.

### Now running on:
- **ESP32C3-based** Xteink X4 and X3.
- **ESP32S3-based** Xteink X4Pro and X4Classic, Seeed reTerminal Sticky, M5PaperMono

Check [our Devices page](https://crosspointreader.com/devices) for the full list.

![CrossPoint Reader running on Xteink device](./docs/images/cover.jpg)

> If you're planning to buy an Xteink device, consider purchasing an **X3/X4 Developer Edition** through https://crosspointreader.com. CrossPoint receives a small share of each sale, helping fund development costs.

## What can CrossPoint do?

- **Reader engine**: EPUB 2/3 rendering with embedded-style option, image handling, hyphenation, kerning, adaptive table layouts, native CJK ruby annotations, chapter navigation, footnotes, bookmarks, dictionary lookups ([StarDict](docs/dictionary.md)), go-to-percent, auto page turn, orientation control, focus reading, KOReader progress sync and more.

- **Various formats**: native handling for `.epub`, `.xtc/.xtch`, `.txt`, and `.bmp`.

- **Touch reading**: follow EPUB links and look up words in the dictionary on touch-enabled devices.

- **Screenshots.**

- **Custom fonts**: install your favorite fonts on the SD card.

- **Tilt page turn (X3 and Sticky)**.

- **USB Drive mode (X4Pro)**: access the SD card as USB mass storage.

- **Library workflow**: indexed title/author search, recently-added and alphabetical views, multilingual grouping, folder browser, recent books, and SD-cache management.

- **Wireless workflows**:
  
  - File transfer web UI
  - EPUB Optimizer
  - Web settings UI/API (edit many device settings from browser)
  - WebSocket fast uploads
  - WebDAV handler
  - AP mode (hotspot) and STA mode (join existing Wi-Fi), both with QR helpers
  - Calibre wireless connect flow
  - OPDS browser with saved servers (up to 8), search, pagination, and direct download
  - OTA update checks and installs from GitHub releases

- **Customization**: night mode, multiple themes (Classic, Lyra, Lyra Extended, RoundedRaff), sleep screen modes including transparent overlays, front/side button remapping, status bar controls, power-button behavior, refresh cadence, and more.

- **Localization**: 34 UI languages and counting, including CJK font fallback and RTL support.

### Coming soon:

- More themes.

- Web plugins.

- Bluetooth pageturner.

- Much more! stay tuned.

---

## USB-locked devices (Xteink Unlocker)

Some Xteink units purchased from third-party stores (e.g. AliExpress) ship with USB flashing locked from the factory.
If your device is locked, you will need to use the **Xteink Unlocker** tool available at
https://crosspointreader.com/#unlock-tool before you can flash CrossPoint.

**You do not need this tool if you bought your device directly from xteink.com.** Those units are not locked.

**Not sure if your device is locked?** Power it on, connect the USB-C cable, and try flashing via the web flasher first (see
[Install firmware](#install-firmware) below). If the browser's serial device picker does not show your device, try a different
USB port or browser before assuming the device is locked. Only reach for the unlocker if the device still doesn't appear.

> ### ⚠️ WARNING: READ THIS BEFORE USING THE UNLOCKER ⚠️
> 
> **The only officially supported firmwares in the unlock tool are CrossPoint and CrossInk.**
> 
> Flashing any other firmware on a USB-locked device may **permanently brick the device** or leave it **permanently
> stuck on that firmware with no recovery path**. Once USB flashing is re-locked, your only way back is via OTA, and if
> the firmware you flashed doesn't support OTA, **there is no way out**.

## Install firmware

### Web installer (recommended)

1. Connect your device to your computer via USB-C and wake/unlock the device
2. Go to https://crosspointreader.com/#flash-tools, select your device (X3, X4, Xteink X4Pro, Seeed reTerminal Sticky, or M5PaperMono), and choose an official CrossPoint release.

### Web installer (specific version)

1. Connect your device to your computer via USB-C and wake/unlock the device
2. Download the firmware file for your device from [Releases](https://github.com/crosspoint-reader/crosspoint-reader/releases), or compile yourself.
3. Go to https://crosspointreader.com/#flash-tools, select your device, click "Custom .bin" and upload the firmware file.

### Revert to Official Firmware

To revert to the official firmware, you can also flash the latest official firmware using https://crosspointreader.com/#flash-tools.

### Command line

1. Install [`esptool`](https://github.com/espressif/esptool):

```bash
pip install esptool
```

2. Download the firmware file for your device from the [releases page](https://github.com/crosspoint-reader/crosspoint-reader/releases).
3. Connect your device via USB-C.
4. Find the device port. On Linux, run `dmesg` after connecting. On macOS:

```bash
log stream --predicate 'subsystem == "com.apple.iokit"' --info
```

5. Flash an X3 or X4:

```bash
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

   Flash an Xteink X4Pro, Seeed reTerminal Sticky, or M5PaperMono:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

### Manual

See [Development quick start](#development-quick-start) below.

---

## Custom SD-card fonts

On devices with external RAM enabled in CrossPoint, copy `.ttf`, `.otf`, or `.ttc` files to the SD card and select them as reader fonts. Put one file in `/fonts/` or `/.fonts/`, or put one family's files in a subfolder. See the [SD card font guide](./docs/sd-card-fonts.md) for the folder layout and styles.

On other devices, convert the font to `.cpfont` first. `.cpfont` files also work on devices with external RAM enabled and have better performance. No firmware reflash is needed to add fonts.

To make `.cpfont` files:

1. Go to https://crosspointreader.com/fonts and open the "SD-card font builder" form.
2. Upload up to four styles (regular, bold, italic, bold-italic), set the family name, point sizes, and Unicode range.
3. Download the generated `.cpfont` files.
4. Copy them to your SD card under `/fonts/YourFont/` (or `/.fonts/YourFont/` to hide the folder).
5. Select the font on the device from the font settings.

Conversion runs the firmware repo's `lib/EpdFont/scripts/fontconvert_sdcard.py` script unmodified, so output matches a local host build.

---

## Documentation

- [User Guide](./USER_GUIDE.md)
- [Web server usage](./docs/webserver.md)
- [Web server endpoints](./docs/webserver-endpoints.md)
- [Project scope](./SCOPE.md)
- [Contributing docs](./docs/contributing/README.md)
- [Touch and UI development](./docs/contributing/touch-and-ui.md) - how to build new screens on the FreeInkUI activity bases (UiListActivity and friends), plus build envs for the non-Xteink touch devices

---

## Development quick start

### Prerequisites

- [pioarduino PlatformIO Core](https://github.com/pioarduino/platformio-core) or [VS Code + pioarduino IDE](https://github.com/pioarduino/pioarduino-vscode-ide)
- Python 3.8+
- `clang-format` 21
- USB-C cable supporting data transfer

### Setup

```bash
git clone --recursive https://github.com/crosspoint-reader/crosspoint-reader
cd crosspoint-reader

# if cloned without --recursive:
git submodule update --init --recursive
```

### Nix/NixOS

Nix/NixOS users can enter the development shell with either `nix develop` (flakes) or `nix-shell`:

```bash
nix develop -f nix
# or
nix-shell nix
```

To flash a connected ESP32-C3 device, enable PlatformIO's udev rules in your NixOS configuration:

```nix
services.udev.packages = with pkgs; [ platformio-core.udev ];
```

After rebuilding the system configuration, reconnect the device or reload udev rules.

### Build / flash / monitor

```bash
pio run --target upload
```

### Contributor pre-PR checks

```bash
./bin/clang-format-fix
pio check -e default
pio run -e default
```

### Debugging

After flashing the new features, it’s recommended to capture detailed logs from the serial port.

First, make sure all required Python packages are installed:

```python
python3 -m pip install pyserial colorama matplotlib
```

After that run the script:

```sh
# For Linux
# This was tested on Debian and should work on most Linux systems.
python3 scripts/debugging_monitor.py

# For macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

Minor adjustments may be required for Windows.

---

## Internals

CrossPoint Reader is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:

```text
.crosspoint/
├── epub_<hash>/         # one directory per book, named by content hash
│   ├── progress.bin     # reading position (chapter, page, etc.)
│   ├── cover.bmp        # generated cover image
│   ├── book.bin         # metadata: title, author, spine, TOC
│   ├── css_rules.cache  # parsed CSS rule cache
│   ├── img_*            # rendered image cache files
│   └── sections/        # per-chapter layout cache
│       ├── 0.bin
│       ├── 1.bin
│       └── ...
├── settings.json        # device settings
├── state.json           # resume/runtime state
└── recent.json          # recent books list
```

Removing `/.crosspoint` clears all cached metadata and forces a full regeneration on next open. Book deletes, overwrites, and moves done through the firmware or web UI clear or re-key matching caches; manual SD-card edits may leave stale cache directories behind.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

---

## Contributing

Contributions are welcome. If you're new to the codebase, start with the [contributing docs](./docs/contributing/README.md). For things to work on, check the [ideas discussion board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas) — leave a comment before starting so we don't duplicate effort.

Everyone here is a volunteer, so please be respectful and patient. For governance and community expectations, see [GOVERNANCE.md](./GOVERNANCE.md).

---

## Community forks

One of the best things about open source is that anyone can take the code in a different direction. If you need something outside CrossPoint's [scope](./SCOPE.md), check out the community forks:

- [CrossInk](https://github.com/uxjulia/CrossInk) — UX focused with minimal reading stats and broader customizations for the reading experience.

- [papyrix-reader](https://github.com/bigbag/papyrix-reader) — Adds FB2 and MD format support. Actively maintained with Arabic script support. Custom themes.

- [inx](https://github.com/obijuankenobiii/inx) — Completely reimagines the user interface with tabbed navigation.

- [Witch(hunt) Reader](https://github.com/jpirnay/witchhunt-reader) — More faithful CSS styling and background work for slightly snappier interaction. Weather information panel. Markdown support.

**Note:** Many of these features will make their way into CrossPoint over time. Each project chooses its own priorities and tradeoffs.

Want to build your own device? Be sure to check out the [de-link](https://github.com/iandchasse/de-link) project or [OnePage Reader](https://github.com/MoveCall/onepage-reader).

---

CrossPoint Reader is **not affiliated with Xteink or any device manufacturer**.
