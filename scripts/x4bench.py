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
  scripts/x4bench.py sleep                # end of a bench session

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
                              activity, never, usbdrive, none), orient w h inv
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
  OPEN <path>              the reader opens it (epub xtc txt md bmp png); OK
                           once the reader is up, ERR OPEN failed if the book
                           did not load.
  SLEEP                    deep sleep through the auto-sleep path (refused
                           while an upload/sync/OTA holds the reader awake)
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
once asleep: press Power on it. End a session with "x4bench.py sleep".

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
    s = sub.add_parser("put")
    s.add_argument("local")
    s.add_argument("remote")
    s = sub.add_parser("push")
    s.add_argument("localdir")
    s.add_argument("remotedir")
    s.add_argument("--all", action="store_true", help="include dot files")
    sub.add_parser("awake")
    sub.add_parser("sleep")
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
    elif op == "mkdir":
        rest, _ = link.command(f"MKDIR {args.path}", t or 10)
        print(rest, file=out)
    elif op == "open":
        rest, _ = link.command(f"OPEN {args.path}", t or 90)  # waits for the book to load
        print(rest, file=out)
    elif op == "awake":
        link.command("AWAKE", t or 5)
        print("awake", file=out)
    elif op == "sleep":
        link.command("SLEEP", t or 5)
        print("sleeping (press Power to wake)", file=out)
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
