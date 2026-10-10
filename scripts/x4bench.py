#!/usr/bin/env python3
"""x4bench - drive a CrossPoint reader's USB bench console from a computer.

The console is compiled into dev builds with CROSSPOINT_BENCH_CONSOLE=1 (the
x4pro env). It shares the USB serial port with the log output.

Usage (auto-detects the reader, Espressif USB Serial/JTAG 303A:1001)
  scripts/x4bench.py ping
  scripts/x4bench.py state
  scripts/x4bench.py key down            # next page (X4 Pro right key)
  scripts/x4bench.py key power 2000 force
  scripts/x4bench.py tap 240 400         # SHOT pixel coordinates
  scripts/x4bench.py swipe 240 700 240 100
  scripts/x4bench.py shot /tmp/screen.png
  scripts/x4bench.py put book.epub "/Books/My Book.epub"
  scripts/x4bench.py push ~/books /Books  # add-only
  scripts/x4bench.py open "/Books/My Book.epub"
  scripts/x4bench.py card day --shot /tmp/day.png   # sleep card preview
  scripts/x4bench.py lightsleep off       # A/B power run (RAM, until the next boot)
  scripts/x4bench.py sleep                # on this cable: the live sleep screen (charging)
  scripts/x4bench.py redraw               # the live sleep screen redraws now
  scripts/x4bench.py wifilast "Some Network"  # test the Wi-Fi fallback (dev builds)
  scripts/x4bench.py weather              # the Weather card's cache (or: weather fetch|clear)
  scripts/x4bench.py locphone 192.0.2.10  # Locate Me reads "phone" NMEA from this computer (or: off)
  scripts/x4bench.py loctest              # the locate pipeline's verdict, nothing saved
  scripts/x4bench.py app wordsearch       # open Word Search (or: app apps / crossword / sudoku / guide)
  scripts/x4bench.py ws                   # dump the puzzle on screen
  scripts/x4bench.py ws new 1234 medium animals   # a deterministic puzzle
  scripts/x4bench.py cw open builtin:mini-001     # Crossword: open a puzzle, then the dump
  scripts/x4bench.py cw type HUT          # type through the keyboard's handler ('-' = Del)
  scripts/x4bench.py su new medium 14     # Sudoku: numbered puzzle 14 of Medium, then the dump
  scripts/x4bench.py su put 1 3 7         # a 7 in row 1, column 3 (1-based)
  scripts/x4bench.py su gen expert 1234   # time one generation (any screen): ms and MHz
  scripts/x4bench.py push packs/guide/build/survival /Guides/survival   # the survival guide's pack
  scripts/x4bench.py app guide            # the survival guide's home
  scripts/x4bench.py gd open fire-lays 2  # a topic's page 2 (1-based), then the dump
  scripts/x4bench.py gd search bow drill  # search results, then the dump (rows ranked)
  scripts/x4bench.py pins 60              # USB-detect pin hunt: pull the cable mid-run
  scripts/x4bench.py power fake absent    # live screen: act unplugged (the full-charge hold)
  scripts/x4bench.py power fake real      # ... and back (or just: power, to read it)
  scripts/x4bench.py sleep deep           # end of a bench session (deep sleep)

Protocol (proto=1)
  host -> device   "CMD:<VERB> <args>\\n" (the host sends "\\n" first so a
                   partial line left on the device cannot swallow the command)
  device -> host   every console line starts with "@B "; each command ends with
                   exactly one "@B OK <VERB> ..." or "@B ERR <VERB> <reason>".
                   Anything else on the port is log output and is ignored
                   (kept in --log FILE when given).
  On connect the host sends "PING <nonce>" and discards everything before the
  reply echoing that nonce: late output of an earlier session is never taken
  for this one's answer.

Verbs
  PING [nonce]             -> OK PING <version> proto=1 [nonce]
  STATE                    -> STATE k=v lines, OK STATE. act/depth/stack (the
                              activities), up heap heapmin heapmax psram, bat,
                              usb (X4 Pro: the charger status line, 0 at full
                              charge), host (a computer on USB: SOF frames),
                              sd sdtotal, pwrshort (short power action:
                              0 ignore 1 sleep 2 page turn ...), dblclick
                              (power double-click = frontlight), sleep_ms
                              sleep_left held (why auto-sleep waits: host,
                              activity, never, usbdrive, none), orient w h inv,
                              ls (idle light sleep on) lsn lsms (naps and time
                              asleep since boot) lsw (naps ended by a key or
                              the charger line, not the 50 ms timer) lsblk (why
                              the last idle pass did not nap: host while this
                              cable is attached, radio, light, charging, input,
                              lock, postwake, render, activity, refused, off)
                              mhz radio (radio initialised: full clock) fls
                              (1: a lit frontlight keeps napping, its PWM
                              survives light sleep; lsblk=light only when not)
                              xtal (light-sleep crystal requests: 1 while lit,
                              else 0) flrun=ran/checked (dev: lit naps of 45 ms
                              or more whose PWM was measured running),
                              live (1: the live sleep screen is up, held=live)
                              power (external power now: charger line or a
                              computer) next_s (seconds to the next redraw, -1
                              none) card screen (card|picture|logo) redraws
                              wifi (station keeper: off none scan join up wait)
                              cycle (Card Cycle When Charging) every (minutes)
                              hold (1: the full-charge hold - the charger idle
                              at full, Wi-Fi off, naps, a redraw every 15 min
                              at most) holdsoc (the SOC it started at, 0 when
                              not holding) hold_s (seconds held) fakepower (1:
                              POWER fake absent is on)
  LS on|off                idle light sleep on or off until the next boot (a
                           deep-sleep wake is a boot: it comes back on);
                           OK LS lightsleep=on|off. For A/B power runs: the
                           naps only happen with no computer on the cable, so
                           read the result from /sleep.log's pwr lines.
  LSFORCE <1..45>          dev only: naps for that many seconds even with this
                           cable attached or a charger in (OK LSFORCE s= tailms=).
                           The USB link DROPS for the window, then the reader
                           stays awake tailms so the host re-enumerates; wait,
                           reconnect and read STATE lsn/lsms (if the port never
                           comes back, one key press on the reader brings it).
  KEY <name> [holdMs] [force]
                           OK KEY <name> req=<ms> held=<delivered ms> [late=1]
                           after the release. late=1: the main loop was
                           blocked, the press lasted longer than asked.
  TAP x y [ms] | LONGTAP x y [ms] | SWIPE x1 y1 x2 y2 [ms]
                           coordinates in SHOT image pixels. OK after release.
  SHOT                     -> SHOT w= h= bytes= crc= pw= ph= rot= inv= ...,
                              D <base64> lines, OK SHOT. The black/white plane
                              only (grayscale anti-aliasing is not captured).
  LS <dir> | MD5 <path> | DF | MKDIR <path> | AWAKE
  CAT [<tailBytes>] <path>  the last tailBytes (default 4096, max 16384) of a
                           text file, one "L <line>" per line (a partial first
                           line is dropped), then OK CAT lines= bytes= size=.
                           For /sleep.log and other small logs.
  OPEN <path>              the reader opens it (epub xtc txt md bmp png); OK
                           once the reader is up, ERR OPEN failed if the book
                           did not load.
  CARD <name|default>      a sleep-screen card drawn exactly as the sleep screen
                           would draw it, shown WITHOUT sleeping: now_reading,
                           day, calendar, quote, owner, sky, pictures, shuffle,
                           weather, or default (the logo screen). OK CARD <name>
                           shown=<card> outcome=drawn|pictures|declined|logo
                           ms=<compute + draw> once it is on the panel; the
                           next key or tap returns to the screen below.
  SLEEP [deep]             sleep through the auto-sleep path (refused while an
                           upload/sync/OTA holds the reader awake). OK SLEEP
                           live: on external power with a card (or Card Cycle
                           When Charging) the live sleep screen comes up and
                           this console stays; OK SLEEP deep: deep sleep, the
                           port drops. "deep" forces deep sleep on power too.
                           On the live screen: SLEEP redraws it (OK SLEEP live
                           redraw), SLEEP deep commits to deep sleep.
  REDRAW                   the live sleep screen redraws now (the cycle deals
                           its next card): OK REDRAW redraws=<count so far>;
                           ERR REDRAW notlive otherwise. The draw lands on a
                           later loop pass: "x4bench.py redraw" waits for
                           STATE redraws= to pass that count.
  APP apps|wordsearch|crossword|sudoku|guide
                           open the Apps list or a game the way their rows
                           do (a replace; guide: its home, GuideHome): OK APP
                           act=<name> once it is up.
  WS                       the Word Search puzzle on screen (ERR WS notopen
                           otherwise): WS state difficulty= size= found=n/m
                           complete= anchor=r,c|- cursor=r,c shown= (the key
                           cursor) elapsed=<s> hints= hint=<word|-1> seed=
                           fnv=<FNV-1a of the grid rows>, WS theme <key>,
                           WS title <text>, one WS row <letters> per row, one
                           WS word <found> <row> <col> <dir> <len> <r0,c0-r1,c1
                           drawn|-> <display text> per word (dir 0 right,
                           1 down-right, 2 down, 3 down-left, 4 left, 5 up-left,
                           6 up, 7 up-right), WS prefs difficulty= choice=
                           recent=, then OK WS.
  WS new <seed> [easy|medium|hard] [key]
                           a deterministic puzzle now (the theme key is the
                           rest of the line: a built-in key such as animals,
                           or file:<name>.words; without one the seed picks
                           a built-in theme); the dump, then OK WS new.
                           ERR WS theme: that theme made no puzzle. "WS new
                           1234 medium animals" always gives fnv=691261517.
                           Drag a word with SWIPE x1 y1 x2 y2 1200 (over 700 ms
                           is a drag, not a swipe); tap-tap with two TAPs.
  CW                       the Crossword on screen (ERR CW notopen otherwise):
                           CW key=<source> fnv=<8 hex> w= h= cur=r,c dir=A|D
                           filled=n/m wrong=<squares marked by a check>
                           solved=0|1 elapsed=<s> checks= reveals= skip=0|1
                           (Skip filled squares), one CW row <letters> per row
                           ('.' empty, '#' block), CW clue <14A> <text>, then
                           OK CW.
  CW open builtin:<id>|<path>    open a puzzle (the key is the rest of the
                           line); the dump, then OK CW open. ERR CW open
                           <reason> <w>x<h> when refused (toobig, rebus,
                           locked, nosolution, notcrossword, damaged, ...).
  CW type <letters>        A-Z through the same handler as the on-screen keys
                           ('-' = Del), at most 120 at once; OK CW type.
  CW cursor <r> <c> [A|D]  the cursor on a white square (0-based); OK CW cursor.
  CW check|reveal letter|word|puzzle
                           as the menu rows; OK CW check / OK CW reveal.
  CW solve                 types every missing or wrong answer letter
                           (completion tests); OK CW solve.
  CW list                  the built-ins: CW builtin <id> <w>x<h> fnv=<hex>
                           <title> lines, then OK CW list n=<count>. Works on
                           any screen.
                           Taps: the keyboard's rows are y 658 / 714 / 770 (key
                           centres x 30 + 46.6 n; row 2 from x 53; row 3: Menu
                           x 42, Z..M from x 100, Del x 438); "<" (31,592),
                           ">" (449,592), the clue text (240,592).
  SU                       the Sudoku on screen (ERR SU notopen otherwise):
                           SU tier= number= seed=<8 hex> fnv=<8 hex> givens=
                           filled=n/81 notes=<marks> cursor=r,c|- mode=digits|
                           notes lock=-|1-9|erase clashes= wrong=<squares a
                           check or hint marked> revealed= solved=0|1
                           elapsed=<s> checks= hints= reveals= undo=<steps>,
                           one SU row <r> <givens> <other digits> per row
                           ('.' empty; rows and columns 1-9), SU prefs tier=
                           next=<easy>,<medium>,<hard>,<expert> removenotes=,
                           then OK SU.
  SU new <tier> <n>        numbered puzzle n of a tier (easy, medium, hard,
                           expert), generated now as New puzzle does (the
                           prefs' counters stay); the dump, then OK SU new.
  SU seed <tier> <seed>    a puzzle from a raw seed (decimal or 0x hex),
                           number 0; OK SU seed.
  SU put <r> <c> <d>       a digit (1-based row, column; 1-9) through the
                           game's model (the cursor moves there); OK SU put.
  SU note <r> <c> <d>      toggles a pencil mark; OK SU note.
  SU erase <r> <c>         the square's digit, else its notes; OK SU erase.
  SU hint                  as the menu's Hint; OK SU hint.
  SU check|reveal [square|puzzle]
                           as the menu rows (default puzzle; square = the
                           cursor's); OK SU check / OK SU reveal.
  SU solve                 writes every missing or wrong digit (completion
                           tests); OK SU solve.
  SU gen <tier> <seed>     generates off-screen on any screen, holding the
                           full-clock lock as the game does: SU gen tier=
                           seed= real=<tier> exact= tries= givens= fnv=
                           hardest=<technique>, then OK SU gen ms=<ms.us>
                           mhz=<before>/<after>.
                           Taps: square r,c at x 37 + 50 (c-1) + (c-1)//3 * 2,
                           y 143 + 50 (r-1) + (r-1)//3 * 2; digit key d at the
                           same x as column d, y 658; Notes (65,753), Erase
                           (182,753), Undo (298,753), Menu (415,753); the
                           solved banner's New puzzle (240,744).
  GD                       the survival guide screen on screen (ERR GD notopen
                           otherwise): GD pack id= version= status= categories=
                           topics= quick= marks= recent= (or GD pack error=
                           nopack|damaged|newer), GD screen=home|list|page|about
                           act= list=none|category|quick|search|marks|recent
                           cat= query="" topic= page=<n>/<pages> sub= screens=
                           n=<screen>/<screens in the topic> sel= rows=
                           marked=0|1 style=page|compact figure=<name>|-
                           full=0|1 menu=0|1 msg=-|nopack|damaged|newer
                           title="", one GD row <i> <kind> <id> | <title> |
                           <value> | <subtitle> per list row (40 at most),
                           then OK GD.
  GD open <topic> [page]   a topic's page (1-based, default 1), its category
                           as the way back; after the screen has drawn, the
                           dump and OK GD open (ERR GD notopic <id>).
  GD about | GD home       About & sources / the guide home; OK GD about|home.
  GD search <words>        the results list for the words (the rest of the
                           line, 1-63 bytes), as the keyboard's Done opens it;
                           OK GD search.
  GD list [category]       the pack's categories (GD cat <id> topics=<n>
                           <title>) or a category's topics (GD topic <id>
                           <Q quick><M medical> pages=<n> <title>), OK GD list
                           n=<rows>; from any screen (the pack is opened
                           for the reply, and closed again unless a guide
                           screen holds it), the screen unchanged.
  GD next | GD prev        the right / left key: a page turn (into the next
                           topic at the end), or a list's selection.
  GD mark                  the page's bookmark toggled (the right key held).
  GD menu                  up a level (the page's MENU, a list's BACK; from
                           the home, Apps: OK GD menu act=Apps).
  GD row <n>               opens list row n (0-based, as a tap).
  GD figure [close]        the page's figure full screen, or closed.
                           Each of these waits for the screen to draw, then
                           dumps. Taps: the bar's thirds (PREV 80,768; MENU or
                           BACK 240,768; NEXT 400,768); list row i at y
                           listTop + 72 i + 36 (listTop 100 on Lyra, 95 on
                           Classic); a finger held on a page = GD mark.
  PINS [seconds]           X4 Pro USB/VBUS-detect hunt, 1-180 s (default 60):
                           OK PINS seconds= probe=<pins> log=/pins.log, then
                           "PINS <ms> pin <n> <0|1>" lines (a start snapshot,
                           then every change), "PINS <ms> sec stat= mv= soc=
                           host= usb= changes= dropped=" once a second, the
                           per-pin "count" lines and "PINS <ms> done". It runs
                           with no computer attached and keeps the reader
                           awake: pull the cable ~10 s mid-run, plug it back,
                           then read /pins.log (overwritten each run). The
                           probe pins get their input buffer enabled only; a
                           reboot restores them.
  WIFILAST <ssid>          dev only: the Wi-Fi list's last-connected network
                           (the rest of the line), so the fallback to the
                           next-best saved network can be tested where the
                           last one is out of range. OK WIFILAST saved=0|1
                           networks=<n>. Never shows a password.
  WEATHER [show|fetch|clear]
                           dev only: the Weather card's cache. show (the
                           default): WEATHER on= units= shuffle=
                           sleep_mode_weather= clock= located=, WEATHER last=
                           <the last fetch's log line> attempt_age_s=, then
                           WEATHER cache=0, or cache=1 fetched_utc= age_s=
                           trusted= place= fixdate= dist_km= offset_s=,
                           WEATHER current= temp_c10= code= hours= days=
                           first_hour_utc= last_day_utc=, WEATHER alerts=
                           asof_utc= total= kept= recheck_failed=, one
                           WEATHER alert severity= ends_utc= event= per alert
                           kept; then OK WEATHER show. No coordinates: the
                           cache's distance from the saved location instead.
                           fetch: the forecast and the alerts now, whatever
                           the cache's age (Weather on, a set clock and a
                           location still needed). It never starts the radio:
                           only on a station already up (STATE wifi=up, the
                           live sleep screen on this cable). OK WEATHER fetch
                           ok 200/200 1.2 s changed=1 | failed <why> |
                           skipped <why> (no-wifi, off, device-network ...).
                           clear: the cache and the retry stamp removed.
  POWER [fake absent|fake real]
                           dev only: external power as the live sleep screen
                           reads it: OK POWER fake=absent|real power= stat=
                           host= soc= holdable=<1: the SOC would hold> hold=.
                           "fake absent" (live screen only, else ERR POWER
                           notlive) makes power read absent whatever the
                           charger line and this cable say: 20 s later the
                           live screen takes the unplug path - the full-charge
                           hold at 97 % or more (STATE hold=1, wifi=off;
                           /sleep.log "hold x=<soc>"), else its final redraw
                           and deep sleep (the port drops). "fake real" (or
                           "real") ends it: from the hold, fully live again
                           ("holdend x=0"). A reboot clears it.
  LOCPHONE [off|<a.b.c.d>[:port]]
                           dev only: LOCTEST's phone source (an NMEA server,
                           normally the phone at the network's gateway, port
                           10110 then 11123) read from this address instead,
                           so a computer on the same network can stand in for
                           the phone: e.g. serve RMC/GGA sentences with
                           correct checksums and current UTC times on port
                           10110. Locate Me and Update Location When Syncing
                           never use it (they always ask the gateway), so a
                           stand-in is never saved as the location. OK
                           LOCPHONE 192.0.2.10 ports=10110,11123 |
                           192.0.2.10:10110 | off (no argument: the setting).
                           RAM only: a reboot (every Locate Me exit) clears it.
  LOCTEST [coords]         dev only: the locate pipeline on the station
                           already up (STATE wifi=up, the live sleep screen on
                           this cable; it never starts the radio): the phone
                           first (LOCPHONE's stand-in when set), then two
                           beaconDB lookups of the Wi-Fi scan's two halves.
                           Prints LOCTEST phone port= why= lines= bad=
                           malformed= settling= stale= nofix= weak= gga= rmc=
                           disagree= skew_s= (the clock allowance; -1 = no
                           trusted clock), LOCTEST wifi verdict= seen= usable=
                           devices= http=a/b acc_m=a/b apart_m= dns_failed=
                           (when Wi-Fi was asked), then OK LOCTEST phone ok
                           ±<N>m | wifi agree ±<N>m | nothing: phone <why>,
                           wifi <verdict>.
                           Nothing is saved. No position unless "coords"
                           (then " at 48.86,2.29": two decimals).
  PUT <size> <md5> <path>  add-only upload: READY <max>, then per chunk the
                           host sends "<len> <crc32hex>\\n" + raw bytes and gets
                           ACK <total> or NAK <total> <reason> (resend).
                           The device checks the MD5 read back from the card
                           before renaming "<path>.bench-part" into place.

X4 Pro key names
  up        physical left key (GPIO0), previous page
  down      physical right key (GPIO7), next page
  power     power button (GPIO3); a press that would sleep the reader (a long
            hold, or any press when the short power action is Sleep) is
            refused unless "force" is given: a sleeping reader's USB is off.
  powerdouble  two quick power clicks (frontlight toggle when dblclick=1)
  back confirm left right   no physical keys on the X4 Pro; they act through
            the front-button remap settings
  home      capacitive Home key tap; homelong = long press; homedouble = two
            taps (the Home double-tap action)

Staying awake: while a computer is on the USB cable (STATE host=1) the reader
never auto-sleeps; Power still sleeps it by hand. It cannot be woken over USB
once asleep: press Power on it. On this cable a sleep is the live sleep screen
(a computer is external power): the console stays, "key down" wakes it (the
reader restarts, so the port drops briefly). End a session with
"x4bench.py sleep deep".

There is no overwrite, delete or rename verb: push never replaces a file, and
reports stale "*.bench-part" files an interrupted upload left behind.

Exit codes: 0 ok, 1 device ERR / bad reply, 2 timeout / no port / port lost,
3 push found conflicts.

Opening the port never touches DTR/RTS: pyserial's defaults are left alone
(clearing them before open() resets an ESP32-S3 into ROM download mode), and
HUPCL is cleared right after open so closing the port leaves them alone too.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import glob
import hashlib
import os
import random
import re
import struct
import sys
import time
import unicodedata
import zlib

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover - reported at runtime
    serial = None

PREFIX = b"@B "
BAUD = 115200  # ignored by USB CDC, kept for pyserial
EXIT_OK, EXIT_ERR, EXIT_TIMEOUT, EXIT_CONFLICT = 0, 1, 2, 3
NOT_ON_USB = "reader not on USB - press Power on it"
PORT_LOST = "reader left USB (asleep? press Power on it)"
USB_VID, USB_PID = 0x303A, 0x1001  # Espressif USB Serial/JTAG (ESP32-S3)
PART_SUFFIX = ".bench-part"


class BenchError(Exception):
    """The device answered ERR."""

    def __init__(self, verb: str, reason: str):
        super().__init__(f"ERR {verb} {reason}".strip())
        self.verb = verb
        self.reason = reason


class BenchTimeout(Exception):
    """No terminal line within the command's timeout."""


class NoPort(Exception):
    """The reader never appeared on USB."""


def read_local(path: str) -> bytes:
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        raise BenchError("LOCAL", f"cannot read {path}: {exc.strerror}") from None


def write_local(path: str, data: bytes) -> None:
    try:
        with open(path, "wb") as fh:
            fh.write(data)
    except OSError as exc:
        raise BenchError("LOCAL", f"cannot write {path}: {exc.strerror}") from None


def check_arg(text: str) -> str:
    """Refuse control characters: a newline would split one command in two."""
    if any(ord(c) < 0x20 or ord(c) == 0x7F for c in text):
        raise BenchError("ARG", f"control character in {text!r}")
    return text


# --- link --------------------------------------------------------------------


def decode(raw: bytes) -> str:
    return raw.decode("utf-8", "surrogateescape")


def encode(text: str) -> bytes:
    return text.encode("utf-8", "surrogateescape")


class Link:
    """Line framing over a pyserial-like object (read/write/in_waiting)."""

    def __init__(self, ser, log=None):
        self.ser = ser
        self.buf = bytearray()
        self.log = log

    def send(self, data: bytes) -> None:
        self.ser.write(data)
        flush = getattr(self.ser, "flush", None)
        if flush:
            flush()

    def _log(self, line: bytes) -> None:
        if self.log:
            self.log.write(f"{time.strftime('%H:%M:%S')} {decode(line)}\n")
            self.log.flush()

    def read_line(self, deadline: float):
        """Next complete line (without CR/LF) or None at the deadline."""
        while True:
            nl = self.buf.find(b"\n")
            if nl >= 0:
                line = bytes(self.buf[:nl]).rstrip(b"\r")
                del self.buf[: nl + 1]
                return line
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            waiting = getattr(self.ser, "in_waiting", 0) or 1
            chunk = self.ser.read(min(max(waiting, 1), 65536))
            if chunk:
                self.buf.extend(chunk)

    def next_bench(self, deadline: float):
        """Next "@B " line's payload; log lines are passed to the log."""
        while True:
            line = self.read_line(deadline)
            if line is None:
                return None
            at = line.find(PREFIX)
            if at == 0:
                return decode(line[len(PREFIX):])
            if at > 0:
                # A log fragment glued in front (should not happen: lines are
                # written whole); keep the console part.
                self._log(line[:at])
                return decode(line[at + len(PREFIX):])
            self._log(line)

    def drain(self) -> None:
        """Drop pending input (log output) before a new command."""
        deadline = time.monotonic() + 0.05
        while True:
            line = self.read_line(deadline)
            if line is None:
                break
            if not line.startswith(PREFIX):
                self._log(line)

    def sync(self, tries: int = 3, timeout: float = 3.0) -> str:
        """PING with a nonce and drop everything before its echo. The device
        answers in order, so this flushes any late output of an earlier
        session (or of a command this one gave up on). Returns the version."""
        for _ in range(tries):
            nonce = f"s{random.getrandbits(40):010x}"
            self.send(encode(f"\nCMD:PING {nonce}\n"))
            deadline = time.monotonic() + timeout
            while True:
                line = self.next_bench(deadline)
                if line is None:
                    break  # the PING may have been eaten (e.g. by a dead upload): retry
                parts = line.split(" ")
                if parts[:2] == ["OK", "PING"] and parts[-1] == nonce:
                    return " ".join(parts[2:-1])
        raise BenchTimeout(f"PING: the reader did not answer within {tries * timeout:.0f} s")

    def command(self, text: str, timeout: float = 10.0, on_line=None):
        """Send CMD:<text>; return (terminal payload after 'OK VERB', body lines)."""
        check_arg(text)
        verb = text.split(" ", 1)[0].upper()
        self.drain()
        self.send(encode(f"\nCMD:{text}\n"))
        body = []
        deadline = time.monotonic() + timeout
        while True:
            line = self.next_bench(deadline)
            if line is None:
                raise BenchTimeout(f"{verb}: no reply within {timeout:.0f} s")
            parts = line.split(" ", 2)
            if parts[0] == "OK" and len(parts) >= 2 and parts[1] == verb:
                return (parts[2] if len(parts) > 2 else ""), body
            if parts[0] == "ERR" and len(parts) >= 2 and parts[1] in (verb, "?"):
                raise BenchError(parts[1], parts[2] if len(parts) > 2 else "")
            if on_line:
                on_line(line)
            else:
                body.append(line)

    def expect(self, kinds, verb: str, timeout: float):
        """Wait for a line starting with one of `kinds` (or ERR verb)."""
        deadline = time.monotonic() + timeout
        while True:
            line = self.next_bench(deadline)
            if line is None:
                raise BenchTimeout(f"{verb}: no reply within {timeout:.0f} s")
            parts = line.split(" ", 2)
            if parts[0] == "ERR" and len(parts) >= 2 and parts[1] in (verb, "?"):
                raise BenchError(parts[1], parts[2] if len(parts) > 2 else "")
            if parts[0] in kinds:
                return parts


def find_ports():
    """(reader ports, other usbmodem ports). The reader is the ESP32-S3's
    built-in USB Serial/JTAG; anything else (a RAK4631, a Pico, another board)
    must never receive console commands."""
    try:
        from serial.tools import list_ports
    except ImportError:  # pragma: no cover
        return [], sorted(glob.glob("/dev/cu.usbmodem*"))
    ours, others = [], []
    for info in list_ports.comports():
        dev = info.device
        if dev.startswith("/dev/tty."):
            dev = "/dev/cu." + dev[len("/dev/tty."):]
        if info.vid == USB_VID and info.pid == USB_PID:
            ours.append(dev)
        elif "usbmodem" in dev:
            others.append(f"{dev} ({info.vid or 0:04X}:{info.pid or 0:04X})")
    return sorted(set(ours)), sorted(set(others))


def keep_lines_on_close(ser) -> None:
    """Clear HUPCL so closing the port does not drop DTR/RTS: on the way down
    the lines can pass DTR=0/RTS=1, which resets an ESP32-S3. Changing the
    termios flag does not touch the lines themselves."""
    fd = getattr(ser, "fd", None)
    if fd is None:
        return
    try:
        import termios
    except ImportError:  # pragma: no cover - not a POSIX host
        return
    try:
        attrs = termios.tcgetattr(fd)
        if attrs[2] & termios.HUPCL:
            attrs[2] &= ~termios.HUPCL
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except (OSError, termios.error):
        pass


def open_port(port: str | None, wait: float, quiet: bool = False):
    """Open the reader's port without touching DTR/RTS (pyserial defaults)."""
    if serial is None:
        raise SystemExit("x4bench needs pyserial: python3 -m pip install pyserial")
    deadline = time.monotonic() + wait
    announced = False
    last_error = None
    others = []
    while True:
        if port:
            candidates = [port]
        else:
            candidates, others = find_ports()
            if len(candidates) > 1 and not quiet and not announced:
                print(f"several ESP32-S3 USB ports: {', '.join(candidates)}; using {candidates[0]} "
                      "(pass --port to choose)", file=sys.stderr)
        for p in candidates:
            try:
                # serial_for_url() also accepts socket:// (the self-test).
                # Do not pass or set dtr/rts here: see the module docstring.
                if "://" in p:
                    return serial.serial_for_url(p, baudrate=BAUD, timeout=0.05)
                # exclusive: a second x4bench or a monitor would each get half the lines.
                ser = serial.Serial(p, baudrate=BAUD, timeout=0.05, exclusive=True)
                keep_lines_on_close(ser)
                return ser
            except (serial.SerialException, OSError, ValueError) as exc:
                last_error = f"{p}: {exc}"
        if time.monotonic() >= deadline:
            detail = f" (last error: {last_error})" if last_error else ""
            if not last_error and others:
                detail = f" (other USB serial devices ignored: {', '.join(others)})"
            raise NoPort(NOT_ON_USB + detail)
        if not announced and not quiet:
            print(f"{NOT_ON_USB} (waiting up to {wait:.0f} s)", file=sys.stderr)
            announced = True
        time.sleep(0.25)


# --- operations --------------------------------------------------------------


def parse_kv(text: str) -> dict:
    out = {}
    for token in text.split():
        if "=" in token:
            k, v = token.split("=", 1)
            out[k] = v
    return out


def state_fields(link: Link, timeout: float = 10.0) -> dict:
    """Every k=v of one STATE reply."""
    _, body = link.command("STATE", timeout)
    fields = {}
    for line in body:
        if line.startswith("STATE "):
            fields.update(parse_kv(line[6:]))
    return fields


def wait_live_redraw(link: Link, before: int, timeout: float = 30.0) -> dict:
    """The live sleep screen draws on a later loop pass than the reply: wait for its count to
    pass `before` (the count the REDRAW reply gave)."""
    deadline = time.monotonic() + timeout
    while True:
        fields = state_fields(link)
        if int(fields.get("redraws", "0")) > before:
            return fields
        if time.monotonic() > deadline:
            raise BenchTimeout(f"REDRAW: the redraw did not land within {timeout:.0f} s")
        time.sleep(0.3)


def put_file(link: Link, data: bytes, remote: str, ack_timeout: float = 15.0,
             progress=None) -> str:
    """Upload bytes to a new file; returns the verified MD5."""
    md5 = hashlib.md5(data).hexdigest()
    check_arg(remote)
    link.drain()
    link.send(encode(f"\nCMD:PUT {len(data)} {md5} {remote}\n"))
    parts = link.expect(("READY",), "PUT", ack_timeout)
    chunk_max = int(parts[1])
    offset = 0
    naks = 0
    while offset < len(data):
        chunk = data[offset: offset + chunk_max]
        header = f"{len(chunk)} {zlib.crc32(chunk) & 0xFFFFFFFF:08x}\n"
        link.send(header.encode("ascii") + chunk)
        reply = link.expect(("ACK", "NAK"), "PUT", ack_timeout)
        total = int(reply[1])
        if reply[0] == "NAK":
            naks += 1
            if progress:
                progress(f"NAK at {total} ({reply[2] if len(reply) > 2 else '?'}), resending")
            offset = total
            continue
        if total != offset + len(chunk):
            raise BenchError("PUT", f"ack mismatch: device has {total}, sent {offset + len(chunk)}")
        offset = total
        if progress:
            progress(f"{offset}/{len(data)}")
    # Final read-back check scales with size (SD reads ~1 MB/s or better).
    final_timeout = ack_timeout + len(data) / 250_000
    deadline = time.monotonic() + final_timeout
    while True:
        line = link.next_bench(deadline)
        if line is None:
            raise BenchTimeout(f"PUT: no final reply within {final_timeout:.0f} s")
        parts = line.split(" ", 3)
        if parts[:2] == ["OK", "PUT"]:
            got = parts[2] if len(parts) > 2 else ""
            if got != md5:
                raise BenchError("PUT", f"device reported md5 {got}, expected {md5}")
            return md5
        if parts[:2] == ["ERR", "PUT"]:
            raise BenchError("PUT", " ".join(parts[2:]))


def remote_md5(link: Link, path: str):
    """(md5, size) of a remote file, or None when it does not exist."""
    try:
        rest, _ = link.command(f"MD5 {path}", timeout=120)
    except BenchError as exc:
        if exc.reason.startswith("notfound"):
            return None
        raise
    md5, size = rest.split(" ", 1)
    return md5, int(size)


def remote_listing(link: Link, remote_dir: str):
    """Names in a remote directory (files and dirs), or an empty list when it
    does not exist yet."""
    try:
        _, body = link.command(f"LS {remote_dir or '/'}", timeout=60)
    except BenchError as exc:
        if exc.reason.startswith(("notfound", "notdir")):
            return []
        raise
    names = []
    for line in body:
        if line.startswith("DIR "):
            names.append(line[4:])
        elif line.startswith("F "):
            parts = line.split(" ", 2)
            if len(parts) == 3:
                names.append(parts[2])
    return names


def push_dir(link: Link, local_dir: str, remote_dir: str, include_hidden: bool = False,
             ack_timeout: float = 15.0, out=sys.stdout) -> dict:
    """Add-only mirror: new files are uploaded, identical ones skipped,
    different ones reported and left alone."""
    result = {"added": [], "skipped": [], "conflicts": [], "failed": [], "stale": []}
    remote_dir = "/" + remote_dir.strip("/") if remote_dir.strip("/") else ""
    listings = {}

    def listed(rdir: str):
        # FAT ignores case: compare folded names.
        if rdir not in listings:
            names = remote_listing(link, rdir)
            listings[rdir] = {n.casefold() for n in names}
            for n in names:
                if n.casefold().endswith(PART_SUFFIX):
                    stale = f"{rdir}/{n}"
                    result["stale"].append(stale)
                    print(f"STALE     {stale}: an interrupted upload's part file; left as is", file=out)
        return listings[rdir]

    for root, dirs, files in os.walk(local_dir):
        dirs.sort()
        if not include_hidden:
            dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in sorted(files):
            if not include_hidden and name.startswith("."):
                continue
            local = os.path.join(root, name)
            # macOS file names are decomposed (NFD); store them composed (NFC)
            # as other tools do, so one title never becomes two look-alikes.
            rel = unicodedata.normalize("NFC", os.path.relpath(local, local_dir).replace(os.sep, "/"))
            remote = f"{remote_dir}/{rel}"
            rdir, rname = remote.rsplit("/", 1)
            try:
                check_arg(remote)
                data = read_local(local)
                want = hashlib.md5(data).hexdigest()
                have = remote_md5(link, remote) if rname.casefold() in listed(rdir) else None
                if have is None:
                    put_file(link, data, remote, ack_timeout)
                    result["added"].append(remote)
                    print(f"added     {remote} ({len(data)} bytes)", file=out)
                elif have[0] == want:
                    result["skipped"].append(remote)
                    print(f"same      {remote}", file=out)
                else:
                    result["conflicts"].append(remote)
                    print(f"CONFLICT  {remote}: device copy differs "
                          f"({have[1]} bytes, md5 {have[0]}); left as is", file=out)
            except BenchError as exc:
                result["failed"].append((remote, str(exc)))
                print(f"FAILED    {remote}: {exc}", file=out)
    stale = f", {len(result['stale'])} stale part files" if result["stale"] else ""
    print(f"{len(result['added'])} added, {len(result['skipped'])} unchanged, "
          f"{len(result['conflicts'])} conflicts, {len(result['failed'])} failed{stale}", file=out)
    return result


def panel_pixel(rot: int, x: int, y: int, pw: int, ph: int):
    """Logical (SHOT image) pixel -> panel pixel; GfxRenderer rotateCoordinates."""
    if rot == 0:  # portrait
        return y, ph - 1 - x
    if rot == 1:  # landscape clockwise
        return pw - 1 - x, ph - 1 - y
    if rot == 2:  # portrait inverted
        return pw - 1 - y, x
    return x, y  # landscape counter-clockwise = native panel frame


def shot_rows(fields: dict, fb: bytes):
    """Packed 1-bit rows (1 = white) of the screen as the user sees it."""
    try:
        w, h = int(fields["w"]), int(fields["h"])
        pw, ph = int(fields["pw"]), int(fields["ph"])
        rot = int(fields["rot"])
    except (KeyError, ValueError) as exc:
        raise BenchError("SHOT", f"bad header field: {exc}") from None
    if pw % 8 or len(fb) != pw * ph // 8:
        raise BenchError("SHOT", f"{len(fb)} bytes do not fit a {pw}x{ph} panel")
    if sorted((w, h)) != sorted((pw, ph)) or rot not in (0, 1, 2, 3):
        raise BenchError("SHOT", f"screen {w}x{h} rot={rot} does not match the {pw}x{ph} panel")
    invert = fields.get("inv", "0") == "1"
    pwb = pw // 8
    rows = []
    for y in range(h):
        row = bytearray((w + 7) // 8)
        for x in range(w):
            px, py = panel_pixel(rot, x, y, pw, ph)
            bit = (fb[py * pwb + (px >> 3)] >> (7 - (px & 7))) & 1
            if invert:
                bit ^= 1
            if bit:
                row[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(row))
    return w, h, rows


def png_bytes(w: int, h: int, rows) -> bytes:
    """1-bit grayscale PNG with zlib only (no Pillow needed)."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + row for row in rows)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 1, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def take_shot(link: Link, timeout: float = 30.0):
    """Returns (fields, framebuffer bytes)."""
    header = {}
    parts = []

    def on_line(line: str):
        if line.startswith("SHOT "):
            header.update(parse_kv(line[5:]))
        elif line.startswith("D "):
            parts.append(line[2:])

    link.command("SHOT", timeout=timeout, on_line=on_line)
    if not header:
        raise BenchError("SHOT", "no header")
    chunks = []
    for i, text in enumerate(parts):
        try:
            chunks.append(base64.b64decode(text, validate=True))
        except (binascii.Error, ValueError):
            raise BenchError("SHOT", f"damaged data line {i}") from None
    fb = b"".join(chunks)
    try:
        want_len, want_crc = int(header["bytes"]), int(header["crc"], 16)
    except (KeyError, ValueError) as exc:
        raise BenchError("SHOT", f"bad header field: {exc}") from None
    if len(fb) != want_len:
        raise BenchError("SHOT", f"got {len(fb)} bytes, header says {want_len}")
    if zlib.crc32(fb) & 0xFFFFFFFF != want_crc:
        raise BenchError("SHOT", "crc mismatch")
    return header, fb


# --- CLI ---------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="CrossPoint X4 Pro bench console client",
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog=__doc__)
    p.add_argument("--port", help="serial port or pyserial URL (default: /dev/cu.usbmodem*)")
    p.add_argument("--wait", type=float, default=10.0, help="seconds to wait for the port")
    p.add_argument("--log", help="append non-console (log) lines to this file")
    p.add_argument("--timeout", type=float, default=None, help="override the command timeout")
    sub = p.add_subparsers(dest="op", required=True)
    sub.add_parser("ping")
    sub.add_parser("state")
    s = sub.add_parser("key")
    s.add_argument("name")
    s.add_argument("extra", nargs="*", metavar="holdMs|force",
                   help="hold time in ms; 'force' allows a power press that would sleep the reader")
    for name in ("tap", "longtap"):
        s = sub.add_parser(name)
        s.add_argument("x", type=int)
        s.add_argument("y", type=int)
        s.add_argument("ms", nargs="?", type=int)
    s = sub.add_parser("swipe")
    for a in ("x1", "y1", "x2", "y2"):
        s.add_argument(a, type=int)
    s.add_argument("ms", nargs="?", type=int)
    s = sub.add_parser("shot")
    s.add_argument("out", help="PNG path")
    s.add_argument("--raw", help="also save the panel framebuffer bytes here")
    for name in ("ls", "md5", "mkdir", "open"):
        s = sub.add_parser(name)
        s.add_argument("path", nargs="?" if name == "ls" else None, default="/")
    sub.add_parser("df")
    s = sub.add_parser("cat", help="print the tail of a text file (e.g. /sleep.log)")
    s.add_argument("path")
    s.add_argument("--tail", type=int, help="bytes from the end of the file (default 4096, max 16384)")
    s = sub.add_parser("put")
    s.add_argument("local")
    s.add_argument("remote")
    s = sub.add_parser("push")
    s.add_argument("localdir")
    s.add_argument("remotedir")
    s.add_argument("--all", action="store_true", help="include dot files")
    s = sub.add_parser("card", help="show a sleep-screen card without sleeping (next key/tap returns)")
    s.add_argument("name", help="now_reading day calendar quote owner sky pictures shuffle weather default")
    s.add_argument("--shot", help="also save a screenshot of it here (PNG)")
    sub.add_parser("awake")
    s = sub.add_parser("lightsleep", help="idle light sleep on|off until the next boot")
    s.add_argument("state", choices=("on", "off"))
    s = sub.add_parser("lsforce", help="nap N seconds (1-45) with the cable attached; the port drops meanwhile")
    s.add_argument("seconds", type=int)
    s = sub.add_parser("sleep", help="sleep: the live sleep screen on power; 'deep' forces deep sleep")
    s.add_argument("kind", nargs="?", choices=("deep",))
    sub.add_parser("redraw", help="the live sleep screen redraws now")
    s = sub.add_parser("wifilast", help="dev: set the Wi-Fi list's last-connected network (fallback tests)")
    s.add_argument("ssid")
    s = sub.add_parser("weather", help="dev: the Weather card's cache: show (default), fetch now, or clear")
    s.add_argument("action", nargs="?", default="show", choices=("show", "fetch", "clear"))
    s = sub.add_parser("locphone", help="dev: LOCTEST's phone NMEA from <a.b.c.d>[:port] instead of the gateway, or 'off'")
    s.add_argument("target", nargs="?", default="", metavar="a.b.c.d[:port]|off")
    s = sub.add_parser("loctest", help="dev: run Locate Me's pipeline on the station up and print the verdict (no save)")
    s.add_argument("coords", nargs="?", choices=("coords",), help="also print the position at two decimals")
    s = sub.add_parser("power", help="dev: read external power, or 'fake absent' / 'fake real' (live screen)")
    s.add_argument("words", nargs="*", metavar="fake absent|fake real")
    s = sub.add_parser("app", help="open the Apps list, Word Search, Crossword, Sudoku or the survival guide")
    s.add_argument("name", choices=("apps", "wordsearch", "crossword", "sudoku", "guide"))
    s = sub.add_parser("ws", help="Word Search: dump the puzzle, or 'new <seed> [difficulty] [theme key]'")
    s.add_argument("args", nargs=argparse.REMAINDER)
    s = sub.add_parser("cw", help="Crossword: dump, or open/type/cursor/check/reveal/solve/list (see the top)")
    s.add_argument("args", nargs=argparse.REMAINDER)
    s = sub.add_parser("su", help="Sudoku: dump, or new/seed/put/note/erase/hint/check/reveal/solve/gen (see the top)")
    s.add_argument("args", nargs=argparse.REMAINDER)
    s = sub.add_parser("gd", help="survival guide: dump, or open/about/home/search/list/next/prev/mark/menu/row/figure")
    s.add_argument("args", nargs=argparse.REMAINDER)
    s = sub.add_parser("pins", help="X4 Pro USB-detect pin hunt (pull the cable mid-run)")
    s.add_argument("seconds", nargs="?", type=int, default=60)
    s = sub.add_parser("cmd", help="raw passthrough: prints every console line")
    s.add_argument("text", nargs=argparse.REMAINDER)
    return p


def run(args, link: Link, out=sys.stdout) -> int:
    t = args.timeout
    op = args.op
    if op != "ping":
        link.sync()
    if op == "ping":
        print(link.sync(), file=out)
    elif op == "state":
        _, body = link.command("STATE", t or 10)
        for line in body:
            print(line[6:] if line.startswith("STATE ") else line, file=out)
    elif op == "key":
        hold = 1000
        for tok in args.extra:
            if tok.isdigit():
                hold = int(tok)
            elif tok != "force":
                raise BenchError("KEY", f"bad argument {tok!r}: a hold in ms or 'force'")
        text = " ".join(["KEY", args.name, *args.extra])
        # Generous: a press that opens a big book blocks the reader's loop.
        rest, _ = link.command(text, t or hold / 1000 + 30)
        print(rest, file=out)
    elif op in ("tap", "longtap"):
        extra = f" {args.ms}" if args.ms is not None else ""
        rest, _ = link.command(f"{op.upper()} {args.x} {args.y}{extra}", t or (args.ms or 800) / 1000 + 30)
        print(rest, file=out)
    elif op == "swipe":
        extra = f" {args.ms}" if args.ms is not None else ""
        rest, _ = link.command(f"SWIPE {args.x1} {args.y1} {args.x2} {args.y2}{extra}",
                               t or (args.ms or 250) / 1000 + 30)
        print(rest, file=out)
    elif op == "shot":
        fields, fb = take_shot(link, t or 30)
        w, h, rows = shot_rows(fields, fb)
        write_local(args.out, png_bytes(w, h, rows))
        if args.raw:
            write_local(args.raw, fb)
        print(f"{args.out} {w}x{h} orient={fields.get('orient', fields.get('rot'))} "
              f"settled={fields.get('settled', '?')}", file=out)
    elif op == "ls":
        rest, body = link.command(f"LS {args.path}", t or 30)
        for line in body:
            print(line, file=out)
        print(f"{rest} entries", file=out)
    elif op == "md5":
        rest, _ = link.command(f"MD5 {args.path}", t or 120)
        print(rest, file=out)
    elif op == "df":
        rest, _ = link.command("DF", t or 30)
        print(rest, file=out)
    elif op == "cat":
        window = f"{args.tail} " if args.tail else ""
        rest, body = link.command(f"CAT {window}{args.path}", t or 30)
        for line in body:
            print(line[2:] if line.startswith("L ") else line, file=out)
        print(f"# {args.path}: {rest}", file=sys.stderr)
    elif op == "mkdir":
        rest, _ = link.command(f"MKDIR {args.path}", t or 10)
        print(rest, file=out)
    elif op == "open":
        rest, _ = link.command(f"OPEN {args.path}", t or 90)  # waits for the book to load
        print(rest, file=out)
    elif op == "card":
        rest, _ = link.command(f"CARD {args.name}", t or 30)  # the card is on the panel when this returns
        print(rest, file=out)
        if args.shot:
            fields, fb = take_shot(link, t or 30)
            w, h, rows = shot_rows(fields, fb)
            write_local(args.shot, png_bytes(w, h, rows))
            print(f"{args.shot} {w}x{h}", file=out)
    elif op == "awake":
        link.command("AWAKE", t or 5)
        print("awake", file=out)
    elif op == "lightsleep":
        rest, _ = link.command(f"LS {args.state}", t or 5)
        print(rest, file=out)
    elif op == "lsforce":
        rest, _ = link.command(f"LSFORCE {args.seconds}", t or 5)
        print(rest, file=out)
    elif op == "sleep":
        rest, _ = link.command("SLEEP deep" if args.kind == "deep" else "SLEEP", t or 5)
        if rest == "live":
            print("live sleep screen (on external power): a key wakes it", file=out)
        elif rest == "live redraw":
            print("live sleep screen: redraw requested", file=out)
        else:
            print("sleeping (press Power to wake)", file=out)
    elif op == "redraw":
        rest, _ = link.command("REDRAW", t or 30)
        # "redraws=N": the count when the request landed, so a scheduled redraw that was already
        # due cannot pass for this one.
        before = int(dict(f.split("=", 1) for f in rest.split() if "=" in f).get("redraws", "0"))
        fields = wait_live_redraw(link, before, t or 30)
        print(f"redrawn card={fields.get('card', '?')} screen={fields.get('screen', '?')} "
              f"next_s={fields.get('next_s', '?')}", file=out)
    elif op == "power":
        words = [w.lower() for w in args.words]
        if words not in ([], ["fake", "absent"], ["fake", "real"], ["real"]):
            raise BenchError("POWER", "usage: power [fake absent|fake real]")
        rest, _ = link.command(" ".join(["POWER", *words]), t or 10)
        print(rest, file=out)
    elif op == "app":
        rest, _ = link.command(f"APP {args.name}", t or 30)
        print(rest, file=out)
    elif op == "ws":
        text = " ".join(["WS", *args.args]).strip()
        rest, body = link.command(text, t or 30)
        for line in body:
            print(line[3:] if line.startswith("WS ") else line, file=out)
        print(f"OK WS {rest}".rstrip(), file=out)
    elif op == "cw":
        text = " ".join(["CW", *args.args]).strip()
        rest, body = link.command(text, t or 30)
        for line in body:
            print(line[3:] if line.startswith("CW ") else line, file=out)
        print(f"OK CW {rest}".rstrip(), file=out)
    elif op == "su":
        text = " ".join(["SU", *args.args]).strip()
        rest, body = link.command(text, t or 30)
        for line in body:
            print(line[3:] if line.startswith("SU ") else line, file=out)
        print(f"OK SU {rest}".rstrip(), file=out)
    elif op == "gd":
        text = " ".join(["GD", *args.args]).strip()
        rest, body = link.command(text, t or 30)
        for line in body:
            print(line[3:] if line.startswith("GD ") else line, file=out)
        print(f"OK GD {rest}".rstrip(), file=out)
    elif op == "pins":
        return run_pins(link, args.seconds, t, out)
    elif op == "wifilast":
        rest, _ = link.command(f"WIFILAST {check_arg(args.ssid)}", t or 10)
        print(rest, file=out)
    elif op == "weather":
        # A fetch is two bounded HTTPS requests (14 s each at most) on the reader's loop.
        rest, body = link.command(f"WEATHER {args.action}", t or (45 if args.action == "fetch" else 15))
        for line in body:
            print(line[8:] if line.startswith("WEATHER ") else line, file=out)
        print(rest, file=out)
    elif op == "locphone":
        target = check_arg(args.target.strip())
        if target and target.lower() != "off" and not re.fullmatch(r"\d{1,3}(\.\d{1,3}){3}(:\d{1,5})?", target):
            raise BenchError("LOCPHONE", f"bad address {target!r}: a.b.c.d[:port] or off")
        rest, _ = link.command(f"LOCPHONE {target}".rstrip(), t or 10)
        print(rest, file=out)
    elif op == "loctest":
        # The phone (11 s at most), a scan and two HTTPS requests on the reader's loop.
        rest, body = link.command(f"LOCTEST {args.coords or ''}".rstrip(), t or 90)
        for line in body:
            print(line[8:] if line.startswith("LOCTEST ") else line, file=out)
        print(rest, file=out)
    elif op == "put":
        data = read_local(args.local)

        def progress(msg):
            print(f"\r{args.remote}: {msg}   ", end="", file=sys.stderr, flush=True)

        md5 = put_file(link, data, args.remote, ack_timeout=t or 15, progress=progress)
        print(file=sys.stderr)
        print(f"{md5} {args.remote}", file=out)
    elif op == "push":
        result = push_dir(link, args.localdir, args.remotedir, include_hidden=args.all,
                          ack_timeout=t or 15, out=out)
        if result["failed"]:
            return EXIT_ERR
        if result["conflicts"]:
            return EXIT_CONFLICT
    elif op == "cmd":
        text = " ".join(args.text).strip()
        if text.startswith("CMD:"):
            text = text[4:]
        if not text:
            raise SystemExit("cmd needs a verb")
        if text.split(" ", 1)[0].upper() == "SCREENSHOT":
            # The legacy verb dumps 48 KB of raw binary and ends with no @B line.
            raise BenchError("SCREENSHOT", "legacy raw dump, not a console verb: use x4bench.py shot")
        rest, _ = link.command(text, t or 30, on_line=lambda line: print(line, file=out))
        print(f"OK {text.split(' ', 1)[0].upper()} {rest}".rstrip(), file=out)
    return EXIT_OK


def run_pins(link: Link, seconds: int, timeout, out) -> int:
    """PINS: start the run, then print its lines until "done". The cable is meant to be pulled
    mid-run: then the port goes and the run carries on by itself, recording to /pins.log."""
    rest, _ = link.command(f"PINS {seconds}", timeout or 10)
    print(f"started {rest}", file=out)
    deadline = time.monotonic() + seconds + 15
    try:
        while True:
            line = link.next_bench(deadline)
            if line is None:
                raise BenchTimeout(f"PINS: no 'done' within {seconds + 15} s")
            if not line.startswith("PINS "):
                continue
            print(line[5:], file=out)
            if line.split()[-1] == "done":
                return EXIT_OK
    except OSError as exc:  # pyserial's SerialException is an OSError
        print(f"port dropped ({exc}): the run goes on without the cable and records to /pins.log. "
              f"Plug back in and, after the {seconds} s, read it with: x4bench.py cat --tail 16384 /pins.log",
              file=out)
        return EXIT_OK


def main(argv=None, out=sys.stdout) -> int:
    args = build_parser().parse_args(argv)
    log = open(args.log, "a", encoding="utf-8", errors="replace") if args.log else None
    try:
        try:
            ser = open_port(args.port, args.wait)
        except NoPort as exc:
            print(exc, file=sys.stderr)
            return EXIT_TIMEOUT
        try:
            return run(args, Link(ser, log), out)
        except BenchError as exc:
            print(exc, file=sys.stderr)
            return EXIT_ERR
        except BenchTimeout as exc:
            print(f"timeout: {exc}", file=sys.stderr)
            return EXIT_TIMEOUT
        except (serial.SerialException, OSError) as exc:
            # The port vanished mid-command: the reader slept or the cable went.
            print(f"{PORT_LOST} ({exc})", file=sys.stderr)
            return EXIT_TIMEOUT
        except (ValueError, KeyError, IndexError) as exc:
            print(f"bad reply from the reader: {exc!r}", file=sys.stderr)
            return EXIT_ERR
        finally:
            try:
                ser.close()
            except (serial.SerialException, OSError):
                pass
    finally:
        if log:
            log.close()


if __name__ == "__main__":
    sys.exit(main())
