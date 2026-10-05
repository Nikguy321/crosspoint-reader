#!/usr/bin/env python3
"""Self-test for x4bench.py against a fake bench-console device.

The fake speaks the console protocol over a local TCP socket, which x4bench
opens through pyserial's socket:// URL handler, and interleaves log noise with
its replies the way the real device's other tasks do.

    python3 scripts/x4bench_selftest.py
"""

from __future__ import annotations

import base64
import hashlib
import io
import os
import random
import socket
import struct
import sys
import tempfile
import threading
import time
import unicodedata
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x4bench  # noqa: E402

NOISE = b"[12345] [INF] [MEM] Free: 123456 bytes, Total: 300000 bytes\n"


class FakeDevice(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(4)
        self.url = f"socket://127.0.0.1:{self.srv.getsockname()[1]}"
        self.files = {}
        self.dirs = {"/"}
        self.nak_next_chunk = 0
        self.stall_after_ready = False
        self.corrupt_md5 = False
        self.naks_sent = 0
        self.chunks = 0
        self.puts = []
        self.keys = []
        self.cards = []
        self.lightsleep = "on"
        self.lsforce = []
        self.on_power = True  # STATE power=1: a SLEEP keeps the live sleep screen up
        self.live = False
        self.redraws = 0
        self.redraw_lands = True  # False: REDRAW is accepted but the draw never comes
        self.sleeps = []
        self.wifi_last = None
        self.weather_ops = []
        self.power_ops = []
        self.fake_absent = False
        self.hold = False  # the full-charge hold (the fake device enters it at once, not after 20 s)
        self.soc = 100
        self.act = "Home"
        self.ws_new = []
        self.cw_ops = []
        self.su_ops = []
        self.pins_runs = []
        self.pins_drop = False  # True: the cable goes mid-run
        self.fb = b""
        self.shot = {}
        self.stop = False
        self.conn = None
        self.buf = bytearray()
        self.key_delay = 0.05
        self.on_connect = None  # bytes sent first on each connection (stale output)
        self.drop_after_shot_header = False
        self.damage_d_line = False

    # --- io
    def emit(self, text: str, noise: bool = False):
        data = b"@B " + text.encode("utf-8", "surrogateescape") + b"\n"
        if noise:
            data = NOISE + data
        self.conn.sendall(data)

    def fill(self):
        chunk = self.conn.recv(65536)
        if not chunk:
            raise ConnectionError
        self.buf.extend(chunk)

    def read_line(self) -> bytes:
        while b"\n" not in self.buf:
            self.fill()
        i = self.buf.index(b"\n")
        line = bytes(self.buf[:i]).rstrip(b"\r")
        del self.buf[: i + 1]
        return line

    def read_exact(self, n: int) -> bytes:
        while len(self.buf) < n:
            self.fill()
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out

    def run(self):
        while not self.stop:
            try:
                self.conn, _ = self.srv.accept()
            except OSError:
                return
            self.buf = bytearray()
            if self.on_connect:
                self.conn.sendall(self.on_connect)
            try:
                while True:
                    line = self.read_line().decode("utf-8", "surrogateescape")
                    if line.startswith("CMD:"):
                        self.handle(line[4:])
            except (ConnectionError, OSError):
                pass
            finally:
                self.conn.close()

    # --- verbs
    def lookup(self, path: str):
        """FAT name lookup: case-insensitive."""
        for p in self.files:
            if p.casefold() == path.casefold():
                return p
        return None

    def handle(self, text: str):
        verb, _, rest = text.partition(" ")
        verb = verb.upper()
        if verb == "PING":
            self.emit("OK PING 1.6.105-x4pro proto=1" + (f" {rest}" if rest else ""), noise=True)
        elif verb == "MD5":
            key = self.lookup(rest)
            if key is not None:
                data = self.files[key]
                self.emit(f"OK MD5 {hashlib.md5(data).hexdigest()} {len(data)}")
            elif rest in self.dirs:
                self.emit("ERR MD5 isdir")
            else:
                self.emit("ERR MD5 notfound")
        elif verb == "CAT":
            tail = 4096
            first, _, more = rest.partition(" ")
            if first.isdigit() and more:
                tail, rest = max(1, min(int(first), 16384)), more.lstrip(" ")
            key = self.lookup(rest)
            if key is None:
                self.emit("ERR CAT notfound")
                return
            data = self.files[key]
            window = data[-tail:]
            if len(data) > tail and b"\n" in window:
                window = window.split(b"\n", 1)[1]
            lines = window.split(b"\n")
            if lines and lines[-1] == b"":
                lines.pop()
            for line in lines:
                self.emit("L " + line.decode("utf-8", "surrogateescape"))
            self.emit(f"OK CAT lines={len(lines)} bytes={len(window)} size={len(data)}")
        elif verb == "MKDIR":
            self.dirs.add(rest)
            self.emit(f"OK MKDIR created {rest}")
        elif verb == "LSFORCE":
            if not rest.isdigit() or not 1 <= int(rest) <= 45:
                self.emit("ERR LSFORCE badarg")
                return
            self.lsforce.append(int(rest))
            self.emit(f"OK LSFORCE s={int(rest)} tailms=5000")
        elif verb == "LS" and rest in ("on", "off"):
            self.lightsleep = rest
            self.emit(f"OK LS lightsleep={rest}")
        elif verb == "LS":
            base = rest.rstrip("/")
            names = sorted(p for p in self.files if p.rsplit("/", 1)[0] == base)
            subdirs = sorted({p[len(base) + 1:].split("/", 1)[0] for p in self.files
                              if p.startswith(base + "/") and "/" in p[len(base) + 1:]})
            if not names and not subdirs and (base or "/") not in self.dirs:
                self.emit("ERR LS notfound")
                return
            for d in subdirs:
                self.emit(f"DIR {d}")
            for p in names:
                self.emit(f"F {len(self.files[p])} {p.rsplit('/', 1)[1]}")
            self.emit(f"OK LS {len(names) + len(subdirs)}")
        elif verb == "PUT":
            self.put(rest)
        elif verb == "SHOT":
            fields = " ".join(f"{k}={v}" for k, v in self.shot.items())
            crc = zlib.crc32(self.fb) & 0xFFFFFFFF
            self.emit(f"SHOT bytes={len(self.fb)} crc={crc:08x} {fields}")
            if self.drop_after_shot_header:
                raise ConnectionError  # the reader went to sleep mid-stream
            for i in range(0, len(self.fb), 96):
                line = "D " + base64.b64encode(self.fb[i:i + 96]).decode()
                if self.damage_d_line and i == 96 * 3:
                    line = line[:-1]  # a short write lost one character
                self.emit(line, noise=(i // 96) % 50 == 7)
            self.emit("OK SHOT")
        elif verb == "CARD":
            if rest not in ("now_reading", "day", "calendar", "quote", "owner", "sky", "pictures", "shuffle",
                            "weather", "default"):
                self.emit("ERR CARD unknown")
                return
            self.cards.append(rest)
            shown = "none" if rest == "default" else rest
            self.emit(f"OK CARD {rest} shown={shown} outcome={'logo' if rest == 'default' else 'drawn'} ms=42",
                      noise=True)
        elif verb == "SLEEP":
            if rest not in ("", "deep"):
                self.emit("ERR SLEEP usage")
                return
            self.sleeps.append(rest or "default")
            if self.live:
                if rest == "deep":
                    self.emit("OK SLEEP deep")
                    raise ConnectionError  # deep sleep: USB goes down
                self.redraws += 1
                self.emit("OK SLEEP live redraw")
            elif rest != "deep" and self.on_power:
                self.live = True
                self.emit("OK SLEEP live")
            else:
                self.emit("OK SLEEP deep")
                raise ConnectionError
        elif verb == "REDRAW":
            if not self.live:
                self.emit("ERR REDRAW notlive")
                return
            self.emit(f"OK REDRAW redraws={self.redraws}")
            if self.redraw_lands:
                self.redraws += 1
        elif verb == "APP":
            names = {"apps": "Apps", "wordsearch": "WordSearch", "crossword": "Crossword", "sudoku": "Sudoku"}
            if rest.lower() not in names:
                self.emit("ERR APP usage")
                return
            self.act = names[rest.lower()]
            self.emit(f"OK APP act={self.act}", noise=True)
        elif verb == "WS":
            if self.act != "WordSearch":
                self.emit(f"ERR WS notopen act={self.act}")
                return
            parts = rest.split(" ", 3)
            if rest and (parts[0].lower() != "new" or len(parts) < 2 or not parts[1].isdigit()):
                self.emit("ERR WS usage")
                return
            if rest:
                self.ws_new.append(rest)
            self.emit("WS state difficulty=medium size=3 found=1/2 complete=0 anchor=- cursor=0,0 shown=0 "
                      "elapsed=12 hints=0 hint=-1 seed=1234 fnv=691261517")
            self.emit("WS theme animals")
            self.emit("WS title Animals", noise=True)
            for row in ("CAT", "OWL", "XYZ"):
                self.emit(f"WS row {row}")
            self.emit("WS word 1 0 0 0 3 0,0-0,2 CAT")
            self.emit("WS word 0 1 0 0 3 - OWL")
            self.emit("WS prefs difficulty=medium choice=random recent=animals,-,-")
            self.emit("OK WS new seed=" + parts[1] if rest else "OK WS")
        elif verb == "CW":
            op = rest.split(" ", 1)[0].lower() if rest else ""
            if op not in ("", "open", "type", "cursor", "check", "reveal", "solve", "list"):
                self.emit("ERR CW usage")
                return
            if op == "list":
                self.emit("CW builtin mini-001 5x5 fnv=1a2b3c4d Shelter")
                self.emit("OK CW list n=1")
                return
            if self.act != "Crossword":
                self.emit(f"ERR CW notopen act={self.act}")
                return
            if op == "open" and not rest.split(" ", 1)[1:]:
                self.emit("ERR CW usage")
                return
            if op == "open" and rest.endswith(".jpz"):
                self.emit("ERR CW open notcrossword 0x0")
                return
            if op:
                self.cw_ops.append(rest)
            solved = op == "solve"
            self.emit(f"CW key=builtin:mini-001 fnv=1a2b3c4d w=5 h=5 cur=0,2 dir=A filled={21 if solved else 3}/21 "
                      f"wrong=0 solved={1 if solved else 0} elapsed=12 checks=0 reveals=0 skip=1")
            for row in ("##HUT", "#....", ".....", "....#", "...##"):
                self.emit(f"CW row {row}")
            self.emit("CW clue 1A Simple shelter in the woods", noise=True)
            self.emit(f"OK CW {op}".rstrip())
        elif verb == "SU":
            words = rest.lower().split()
            op = words[0] if words else ""
            if op not in ("", "new", "seed", "put", "note", "erase", "hint", "check", "reveal", "solve", "gen"):
                self.emit("ERR SU usage")
                return
            tiers = ("easy", "medium", "hard", "expert")
            if op in ("new", "seed", "gen") and (len(words) != 3 or words[1] not in tiers):
                self.emit("ERR SU usage")
                return
            if op in ("put", "note") and (len(words) != 4 or not all(w in "123456789" and len(w) == 1
                                                                    for w in words[1:])):
                self.emit("ERR SU usage")
                return
            if op == "gen":
                self.su_ops.append(rest)
                self.emit(f"SU gen tier={words[1]} seed=000004d2 real={words[1]} exact=1 tries=9 givens=28 "
                          "fnv=94927fb0 hardest=Trial", noise=True)
                self.emit("OK SU gen ms=84.512 mhz=240/240")
                return
            if self.act != "Sudoku":
                self.emit(f"ERR SU notopen act={self.act}")
                return
            if op:
                self.su_ops.append(rest)
            solved = op == "solve"
            self.emit(f"SU tier=medium number=14 seed=1a2b3c4d fnv=7db44385 givens=28 filled={81 if solved else 29}/81 "
                      f"notes=0 cursor=1,3 mode=digits lock=- clashes=0 wrong=0 revealed=0 solved={1 if solved else 0} "
                      "elapsed=12 checks=0 hints=0 reveals=0 undo=1")
            for r in range(1, 10):
                self.emit(f"SU row {r} 5.3..7... ..7......" if r == 1 else f"SU row {r} ......... .........")
            self.emit("SU prefs tier=medium next=1,15,1,1 removenotes=1", noise=True)
            self.emit(f"OK SU {op}".rstrip())
        elif verb == "PINS":
            seconds = rest or "60"
            if not seconds.isdigit() or not 1 <= int(seconds) <= 180:
                self.emit("ERR PINS usage")
                return
            self.pins_runs.append(int(seconds))
            self.emit(f"OK PINS seconds={seconds} probe=15,16,17,46,47,48 log=/pins.log")
            self.emit("PINS 0 start seconds=" + seconds)
            self.emit("PINS 0 pin 21 1", noise=True)
            if self.pins_drop:
                raise ConnectionError  # the cable is pulled
            self.emit("PINS 1000 sec stat=1 mv=4100 soc=90 host=1 usb=1 changes=0 dropped=0")
            self.emit("STATE act=Home depth=0 stack=-")  # another command's line is ignored
            self.emit("PINS 1020 count pin 21 0")
            self.emit("PINS 1020 done")
        elif verb == "WEATHER":
            op = (rest or "show").lower()
            if op not in ("show", "fetch", "clear"):
                self.emit("ERR WEATHER usage")
                return
            self.weather_ops.append(op)
            if op == "show":
                self.emit("WEATHER on=1 units=us shuffle=1 sleep_mode_weather=0 clock=1 located=1", noise=True)
                self.emit("WEATHER cache=1 fetched_utc=1793678700 age_s=2100 trusted=1 place=wifi-auto")
                self.emit("WEATHER alerts=none asof_utc=1793678700 total=0 kept=0 recheck_failed=0")
                self.emit("OK WEATHER show")
            elif op == "fetch":
                self.emit("OK WEATHER fetch ok 200/200 1.2 s changed=1", noise=True)
            else:
                self.emit("OK WEATHER clear")
        elif verb == "POWER":
            op = " ".join(rest.lower().split())
            if op not in ("", "fake absent", "fake real", "real"):
                self.emit("ERR POWER usage")
                return
            if op == "fake absent" and not self.live:
                self.emit("ERR POWER notlive")
                return
            self.power_ops.append(op or "show")
            if op == "fake absent":
                self.fake_absent = True
            elif op:
                self.fake_absent = False
                self.hold = False  # power back: fully live again
            self.emit(f"OK POWER fake={'absent' if self.fake_absent else 'real'} "
                      f"power={int(self.on_power and not self.fake_absent)} stat=1 host=1 soc={self.soc} "
                      f"holdable={int(self.soc >= 97)} hold={int(self.hold)}", noise=True)
            if self.fake_absent and self.soc >= 97:
                self.hold = True  # what the reader does 20 s later
        elif verb == "WIFILAST":
            if not rest or len(rest.encode()) > 32:
                self.emit("ERR WIFILAST usage")
                return
            self.wifi_last = rest
            self.emit(f"OK WIFILAST saved={1 if rest == 'Saved Net' else 0} networks=2")
        elif verb == "STATE":
            self.emit("STATE act=Sleep depth=0 stack=-" if self.live else "STATE act=Home depth=0 stack=-")
            wifi = "up" if self.live and not self.hold else "off"
            self.emit(f"STATE live={int(self.live)} power={int(self.on_power and not self.fake_absent)} "
                      f"next_s={94 if self.live else -1} card=day screen=card redraws={self.redraws} wifi={wifi} "
                      f"cycle=1 every=2 hold={int(self.hold)} holdsoc={self.soc if self.hold else 0} "
                      f"hold_s={25 if self.hold else 0} fakepower={int(self.fake_absent)}", noise=True)
            self.emit("OK STATE")
        elif verb == "KEY":
            time.sleep(self.key_delay)
            self.keys.append(rest)
            self.emit(f"OK KEY {rest.split()[0]} req=60 held=61")
        else:
            self.emit(f"ERR {verb} unknown")

    def put(self, rest: str):
        size_s, md5, path = rest.split(" ", 2)
        size = int(size_s)
        if self.lookup(path) is not None:
            self.emit("ERR PUT exists")
            return
        self.emit("READY 4096")
        if self.stall_after_ready:
            while True:
                self.fill()  # swallow everything, never answer
        data = bytearray()
        naks = 0
        while len(data) < size:
            header = self.read_line().decode()
            length_s, crc_s = header.split()
            payload = self.read_exact(int(length_s))
            if self.nak_next_chunk > 0:
                self.nak_next_chunk -= 1
                self.naks_sent += 1
                naks += 1
                self.emit(f"NAK {len(data)} crc", noise=True)
                continue
            if zlib.crc32(payload) & 0xFFFFFFFF != int(crc_s, 16):
                self.emit(f"NAK {len(data)} crc")
                continue
            data.extend(payload)
            self.chunks += 1
            self.emit(f"ACK {len(data)}", noise=self.chunks % 3 == 0)
        got = hashlib.md5(bytes(data)).hexdigest()
        if self.corrupt_md5:
            got = "0" * 32
        if got != md5:
            self.emit(f"ERR PUT md5 {got} {len(data)}")
            return
        self.files[path] = bytes(data)
        self.puts.append(path)
        self.emit(f"OK PUT {got} {path}")


# --- PNG reader for checking shot output (1-bit grayscale, filter 0) --------


def read_png_1bit(data: bytes):
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    idat = b""
    w = h = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        (crc,) = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])
        assert crc == zlib.crc32(kind + body) & 0xFFFFFFFF
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 1 and ctype == 0
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = (w + 7) // 8
    pixels = []
    for y in range(h):
        row = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        assert row[0] == 0
        pixels.append([(row[1 + (x >> 3)] >> (7 - (x & 7))) & 1 for x in range(w)])
    return w, h, pixels


def panel_with_black(pw: int, ph: int, points):
    fb = bytearray(b"\xff" * (pw // 8 * ph))
    for px, py in points:
        fb[py * (pw // 8) + px // 8] &= ~(0x80 >> (px % 8)) & 0xFF
    return bytes(fb)


class BenchSelfTest(unittest.TestCase):
    def setUp(self):
        self.dev = FakeDevice()
        self.dev.start()
        self.ser = x4bench.open_port(self.dev.url, wait=2, quiet=True)
        self.link = x4bench.Link(self.ser)

    def tearDown(self):
        self.ser.close()
        self.dev.stop = True
        self.dev.srv.close()

    def cli(self, *argv):
        out = io.StringIO()
        code = x4bench.main(["--port", self.dev.url, "--wait", "2", *argv], out=out)
        return code, out.getvalue()

    # --- basics
    def test_ping_ignores_log_noise(self):
        rest, _ = self.link.command("PING", timeout=2)
        self.assertEqual(rest, "1.6.105-x4pro proto=1")

    def test_err_reply_and_unknown_verb(self):
        with self.assertRaises(x4bench.BenchError) as ctx:
            self.link.command("BOGUS", timeout=2)
        self.assertEqual(ctx.exception.reason, "unknown")
        self.ser.close()
        code, _ = self.cli("md5", "/missing.epub")
        self.assertEqual(code, x4bench.EXIT_ERR)

    def test_cat_prints_the_tail_lines(self):
        self.dev.files["/sleep.log"] = b"1 boot up=3\n2 sleep up=9\n3 cut up=1\n"
        self.ser.close()
        code, out = self.cli("cat", "/sleep.log")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(out, "1 boot up=3\n2 sleep up=9\n3 cut up=1\n")
        # A window that opens mid-line drops the fragment before its first newline.
        code, out = self.cli("cat", "--tail", "12", "/sleep.log")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(out, "3 cut up=1\n")
        code, _ = self.cli("cat", "/missing.log")
        self.assertEqual(code, x4bench.EXIT_ERR)

    def test_card_shows_a_sleep_card(self):
        self.ser.close()
        code, out = self.cli("card", "day")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(out, "day shown=day outcome=drawn ms=42\n")
        code, out = self.cli("card", "default")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("outcome=logo", out)
        self.assertEqual(self.dev.cards, ["day", "default"])
        code, _ = self.cli("card", "bogus")
        self.assertEqual(code, x4bench.EXIT_ERR)

    def test_sleep_on_power_is_the_live_screen(self):
        self.ser.close()
        code, out = self.cli("sleep")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("live sleep screen", out)
        self.assertTrue(self.dev.live)
        code, out = self.cli("state")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("live=1", out)
        self.assertIn("wifi=up", out)
        # On the live screen SLEEP redraws, and REDRAW does too.
        code, out = self.cli("sleep")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "live sleep screen: redraw requested\n"))
        code, out = self.cli("redraw")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "redrawn card=day screen=card next_s=94\n"))
        self.assertEqual(self.dev.redraws, 2)
        # SLEEP deep commits: the port goes down with the reply already in.
        code, out = self.cli("sleep", "deep")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("press Power", out)
        self.assertEqual(self.dev.sleeps, ["default", "default", "deep"])

    def test_redraw_that_never_lands_is_a_timeout(self):
        self.ser.close()
        self.cli("sleep")
        self.dev.redraw_lands = False
        code, out = self.cli("--timeout", "1", "redraw")
        self.assertEqual(code, x4bench.EXIT_TIMEOUT)
        self.assertEqual(out, "")

    def test_sleep_off_power_and_redraw_refused(self):
        self.dev.on_power = False
        self.ser.close()
        code, _ = self.cli("redraw")
        self.assertEqual(code, x4bench.EXIT_ERR)
        code, out = self.cli("sleep")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("press Power", out)
        self.assertFalse(self.dev.live)

    def test_app_and_word_search(self):
        self.ser.close()
        code, _ = self.cli("ws")
        self.assertEqual(code, x4bench.EXIT_ERR)  # not open yet
        code, out = self.cli("app", "wordsearch")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "act=WordSearch\n"))
        code, out = self.cli("ws")
        self.assertEqual(code, x4bench.EXIT_OK)
        lines = out.splitlines()
        self.assertIn("fnv=691261517", lines[0])
        self.assertEqual(lines[1], "theme animals")
        self.assertEqual(lines[3:6], ["row CAT", "row OWL", "row XYZ"])
        self.assertEqual(lines[-1], "OK WS")
        code, out = self.cli("ws", "new", "1234", "medium", "file:Pond", "Life.words")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(out.splitlines()[-1], "OK WS new seed=1234")
        self.assertEqual(self.dev.ws_new, ["new 1234 medium file:Pond Life.words"])
        code, _ = self.cli("ws", "new", "x")
        self.assertEqual(code, x4bench.EXIT_ERR)
        code, out = self.cli("app", "apps")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "act=Apps\n"))

    def test_app_and_crossword(self):
        self.ser.close()
        code, out = self.cli("cw", "list")  # works on any screen
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(out.splitlines(), ["builtin mini-001 5x5 fnv=1a2b3c4d Shelter", "OK CW list n=1"])
        code, _ = self.cli("cw")
        self.assertEqual(code, x4bench.EXIT_ERR)  # not open yet
        code, out = self.cli("app", "crossword")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "act=Crossword\n"))
        code, out = self.cli("cw")
        self.assertEqual(code, x4bench.EXIT_OK)
        lines = out.splitlines()
        self.assertTrue(lines[0].startswith("key=builtin:mini-001 fnv=1a2b3c4d"))
        self.assertEqual(lines[1:6], ["row ##HUT", "row #....", "row .....", "row ....#", "row ...##"])
        self.assertEqual(lines[6], "clue 1A Simple shelter in the woods")
        self.assertEqual(lines[-1], "OK CW")
        code, out = self.cli("cw", "open", "/Puzzles/Crossword/My", "Pack/Sunday.ipuz")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(out.splitlines()[-1], "OK CW open")
        code, out = self.cli("cw", "type", "HUT")
        self.assertEqual(out.splitlines()[-1], "OK CW type")
        code, out = self.cli("cw", "solve")
        self.assertIn("solved=1", out.splitlines()[0])
        self.assertEqual(self.dev.cw_ops, ["open /Puzzles/Crossword/My Pack/Sunday.ipuz", "type HUT", "solve"])
        code, _ = self.cli("cw", "open", "/Puzzles/Crossword/x.jpz")
        self.assertEqual(code, x4bench.EXIT_ERR)
        code, _ = self.cli("cw", "fill")
        self.assertEqual(code, x4bench.EXIT_ERR)

    def test_app_and_sudoku(self):
        self.ser.close()
        code, out = self.cli("su", "gen", "expert", "1234")  # works on any screen
        self.assertEqual(code, x4bench.EXIT_OK)
        lines = out.splitlines()
        self.assertTrue(lines[0].startswith("gen tier=expert seed=000004d2 real=expert"))
        self.assertEqual(lines[-1], "OK SU gen ms=84.512 mhz=240/240")
        code, _ = self.cli("su")
        self.assertEqual(code, x4bench.EXIT_ERR)  # not open yet
        code, out = self.cli("app", "sudoku")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "act=Sudoku\n"))
        code, out = self.cli("su", "new", "medium", "14")
        self.assertEqual(code, x4bench.EXIT_OK)
        lines = out.splitlines()
        self.assertTrue(lines[0].startswith("tier=medium number=14"))
        self.assertEqual(lines[1], "row 1 5.3..7... ..7......")
        self.assertEqual(len([l for l in lines if l.startswith("row ")]), 9)
        self.assertEqual(lines[10], "prefs tier=medium next=1,15,1,1 removenotes=1")
        self.assertEqual(lines[-1], "OK SU new")
        code, out = self.cli("su", "put", "1", "3", "7")
        self.assertEqual(out.splitlines()[-1], "OK SU put")
        code, out = self.cli("su", "solve")
        self.assertIn("solved=1", out.splitlines()[0])
        self.assertEqual(self.dev.su_ops, ["gen expert 1234", "new medium 14", "put 1 3 7", "solve"])
        code, _ = self.cli("su", "put", "1", "3")
        self.assertEqual(code, x4bench.EXIT_ERR)
        code, _ = self.cli("su", "fill")
        self.assertEqual(code, x4bench.EXIT_ERR)

    def test_pins_streams_until_done(self):
        self.ser.close()
        code, out = self.cli("pins", "30")
        self.assertEqual(code, x4bench.EXIT_OK)
        lines = out.splitlines()
        self.assertEqual(lines[0], "started seconds=30 probe=15,16,17,46,47,48 log=/pins.log")
        self.assertIn("0 pin 21 1", lines)
        self.assertEqual(lines[-1], "1020 done")
        self.assertFalse(any(line.startswith("STATE") for line in lines))
        self.assertEqual(self.dev.pins_runs, [30])
        code, _ = self.cli("pins", "500")
        self.assertEqual(code, x4bench.EXIT_ERR)

    def test_pins_survives_the_cable_being_pulled(self):
        self.dev.pins_drop = True
        self.ser.close()
        code, out = self.cli("pins")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("port dropped", out)
        self.assertIn("/pins.log", out)
        self.assertEqual(self.dev.pins_runs, [60])

    def test_weather_show_fetch_clear(self):
        self.ser.close()
        code, out = self.cli("weather")
        self.assertEqual(code, x4bench.EXIT_OK)
        lines = out.splitlines()
        self.assertEqual(lines[0], "on=1 units=us shuffle=1 sleep_mode_weather=0 clock=1 located=1")
        self.assertIn("cache=1 fetched_utc=1793678700", out)
        self.assertEqual(lines[-1], "show")
        code, out = self.cli("weather", "fetch")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "fetch ok 200/200 1.2 s changed=1\n"))
        code, out = self.cli("weather", "clear")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "clear\n"))
        self.assertEqual(self.dev.weather_ops, ["show", "fetch", "clear"])
        with self.assertRaises(SystemExit):  # argparse refuses an unknown op before the device
            x4bench.main(["--port", self.dev.url, "weather", "refresh"], out=io.StringIO())
        self.assertEqual(self.dev.weather_ops, ["show", "fetch", "clear"])

    def test_power_fake_absent_holds_and_real_resumes(self):
        self.ser.close()
        code, _ = self.cli("power", "fake", "absent")
        self.assertEqual(code, x4bench.EXIT_ERR)  # only on the live screen
        code, out = self.cli("power")
        self.assertEqual((code, out), (x4bench.EXIT_OK,
                                       "fake=real power=1 stat=1 host=1 soc=100 holdable=1 hold=0\n"))
        self.cli("sleep")
        code, out = self.cli("power", "fake", "absent")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("fake=absent power=0", out)
        code, out = self.cli("state")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("hold=1 holdsoc=100", out)
        self.assertIn("wifi=off", out)
        self.assertIn("fakepower=1", out)
        code, out = self.cli("power", "FAKE", "Real")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("fake=real power=1", out)
        code, out = self.cli("state")
        self.assertIn("hold=0 holdsoc=0", out)
        self.assertIn("wifi=up", out)
        code, _ = self.cli("power", "real")
        self.assertEqual(code, x4bench.EXIT_OK)
        # A bad form never reaches the reader.
        code, _ = self.cli("power", "absent")
        self.assertEqual(code, x4bench.EXIT_ERR)
        code, _ = self.cli("power", "fake", "absent", "now")
        self.assertEqual(code, x4bench.EXIT_ERR)
        self.assertEqual(self.dev.power_ops, ["show", "fake absent", "fake real", "real"])

    def test_wifilast_sets_the_last_network(self):
        self.ser.close()
        code, out = self.cli("wifilast", "Saved Net")
        self.assertEqual((code, out), (x4bench.EXIT_OK, "saved=1 networks=2\n"))
        self.assertEqual(self.dev.wifi_last, "Saved Net")
        code, _ = self.cli("wifilast", "bad\nname")
        self.assertEqual(code, x4bench.EXIT_ERR)  # a newline never reaches the device
        self.assertEqual(self.dev.wifi_last, "Saved Net")

    def test_lightsleep_toggles_and_ls_still_lists(self):
        self.ser.close()
        code, out = self.cli("lightsleep", "off")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(out, "lightsleep=off\n")
        self.assertEqual(self.dev.lightsleep, "off")
        code, out = self.cli("lightsleep", "on")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(self.dev.lightsleep, "on")
        self.dev.files["/Books/a.epub"] = b"x"
        code, out = self.cli("ls", "/Books")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertIn("a.epub", out)

    def test_lsforce_opens_a_window_and_refuses_bad_lengths(self):
        self.ser.close()
        code, out = self.cli("lsforce", "20")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(out, "s=20 tailms=5000\n")
        self.assertEqual(self.dev.lsforce, [20])
        code, _ = self.cli("lsforce", "46")
        self.assertEqual(code, x4bench.EXIT_ERR)
        self.assertEqual(self.dev.lsforce, [20])

    def test_no_port_exit_code(self):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        sock.close()  # nothing listens here now
        out = io.StringIO()
        code = x4bench.main(["--port", f"socket://127.0.0.1:{port}", "--wait", "0", "ping"], out=out)
        self.assertEqual(code, x4bench.EXIT_TIMEOUT)

    # --- put
    def test_put_ack_path(self):
        data = random.Random(1).randbytes(10000) if hasattr(random.Random, "randbytes") else os.urandom(10000)
        md5 = x4bench.put_file(self.link, data, "/Books/My Book.epub", ack_timeout=3)
        self.assertEqual(md5, hashlib.md5(data).hexdigest())
        self.assertEqual(self.dev.files["/Books/My Book.epub"], data)
        self.assertEqual(self.dev.chunks, 3)  # 4096 + 4096 + 1808

    def test_put_empty_file(self):
        x4bench.put_file(self.link, b"", "/empty.txt", ack_timeout=3)
        self.assertEqual(self.dev.files["/empty.txt"], b"")

    def test_put_nak_then_resend(self):
        data = os.urandom(9000)
        self.dev.nak_next_chunk = 2
        notes = []
        x4bench.put_file(self.link, data, "/n.bin", ack_timeout=3, progress=notes.append)
        self.assertEqual(self.dev.files["/n.bin"], data)
        self.assertEqual(self.dev.naks_sent, 2)
        self.assertTrue(any(n.startswith("NAK at 0") for n in notes))

    def test_put_timeout(self):
        self.dev.stall_after_ready = True
        start = time.monotonic()
        with self.assertRaises(x4bench.BenchTimeout):
            x4bench.put_file(self.link, os.urandom(5000), "/stall.bin", ack_timeout=0.5)
        self.assertLess(time.monotonic() - start, 3)
        self.assertNotIn("/stall.bin", self.dev.files)

    def test_put_md5_mismatch(self):
        self.dev.corrupt_md5 = True
        with self.assertRaises(x4bench.BenchError) as ctx:
            x4bench.put_file(self.link, os.urandom(3000), "/bad.bin", ack_timeout=3)
        self.assertTrue(ctx.exception.reason.startswith("md5"))
        self.assertNotIn("/bad.bin", self.dev.files)

    def test_put_exists_is_refused(self):
        self.dev.files["/keep.epub"] = b"original"
        with self.assertRaises(x4bench.BenchError) as ctx:
            x4bench.put_file(self.link, b"replacement", "/keep.epub", ack_timeout=3)
        self.assertEqual(ctx.exception.reason, "exists")
        self.assertEqual(self.dev.files["/keep.epub"], b"original")

    # --- push
    def test_push_is_add_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "sub"))
            files = {
                "new.epub": b"new book",
                "same.epub": b"same bytes",
                "sub/differs.epub": b"local version",
                ".DS_Store": b"junk",
            }
            for rel, data in files.items():
                with open(os.path.join(tmp, rel), "wb") as fh:
                    fh.write(data)
            self.dev.files["/Books/same.epub"] = b"same bytes"
            self.dev.files["/Books/sub/differs.epub"] = b"device version"
            self.ser.close()
            code, out = self.cli("push", tmp, "/Books")
            self.assertEqual(code, x4bench.EXIT_CONFLICT)
            self.assertEqual(self.dev.files["/Books/new.epub"], b"new book")
            self.assertEqual(self.dev.files["/Books/sub/differs.epub"], b"device version")  # never replaced
            self.assertNotIn("/Books/.DS_Store", self.dev.files)
            self.assertEqual(self.dev.puts, ["/Books/new.epub"])
            self.assertIn("CONFLICT  /Books/sub/differs.epub", out)
            self.assertIn("1 added, 1 unchanged, 1 conflicts, 0 failed", out)

            # Second run: everything already there -> nothing uploaded, still reports the conflict.
            code, out = self.cli("push", tmp, "/Books")
            self.assertEqual(code, x4bench.EXIT_CONFLICT)
            self.assertEqual(self.dev.puts, ["/Books/new.epub"])
            self.assertIn("0 added, 2 unchanged, 1 conflicts", out)

    def test_push_nfc_names_case_and_stale_parts(self):
        with tempfile.TemporaryDirectory() as tmp:
            nfd = unicodedata.normalize("NFD", "Émile.epub")
            with open(os.path.join(tmp, nfd), "wb") as fh:
                fh.write(b"emile")
            with open(os.path.join(tmp, "Case.epub"), "wb") as fh:
                fh.write(b"case")
            self.dev.files["/Books/case.epub"] = b"case"  # FAT: same file, other case
            self.dev.files["/Books/old.epub.bench-part"] = b"half"
            self.ser.close()
            code, out = self.cli("push", tmp, "/Books")
            self.assertEqual(code, x4bench.EXIT_OK)
            self.assertEqual(self.dev.puts, [unicodedata.normalize("NFC", "/Books/Émile.epub")])
            self.assertIn("STALE     /Books/old.epub.bench-part", out)
            self.assertEqual(self.dev.files["/Books/old.epub.bench-part"], b"half")
            self.assertIn("1 added, 1 unchanged, 0 conflicts, 0 failed, 1 stale part files", out)

    # --- shot
    def check_shot(self, rot, w, h, logical_black, inv="0"):
        pw, ph = 800, 480
        panel = [x4bench.panel_pixel(rot, x, y, pw, ph) for x, y in logical_black]
        self.dev.fb = panel_with_black(pw, ph, panel)
        self.dev.shot = {"w": w, "h": h, "pw": pw, "ph": ph, "rot": rot, "inv": inv, "settled": 1}
        with tempfile.TemporaryDirectory() as tmp:
            out_png = os.path.join(tmp, "shot.png")
            self.ser.close()
            code, _ = self.cli("shot", out_png)
            self.assertEqual(code, x4bench.EXIT_OK)
            with open(out_png, "rb") as fh:
                gw, gh, pixels = read_png_1bit(fh.read())
        self.assertEqual((gw, gh), (w, h))
        black = 1 if inv == "1" else 0
        for x, y in logical_black:
            self.assertEqual(pixels[y][x], black, (rot, x, y))
        total_black = sum(row.count(black) for row in pixels)
        self.assertEqual(total_black, len(logical_black))

    def test_shot_portrait(self):
        # Corners and an off-centre mark pin down the rotation and both flips.
        self.check_shot(0, 480, 800, [(0, 0), (479, 0), (0, 799), (10, 20), (300, 700)])

    def test_shot_landscape_ccw_native(self):
        self.check_shot(3, 800, 480, [(0, 0), (799, 479), (5, 400)])

    def test_shot_portrait_inverted_and_landscape_cw(self):
        self.check_shot(2, 480, 800, [(1, 2), (478, 790)])
        self.setUp()
        self.check_shot(1, 800, 480, [(1, 2), (790, 470)])

    def test_shot_night_mode_inverts(self):
        self.check_shot(0, 480, 800, [(7, 9)], inv="1")

    def test_shot_crc_mismatch_detected(self):
        self.dev.fb = b"\xff" * 48000
        self.dev.shot = {"w": 480, "h": 800, "pw": 800, "ph": 480, "rot": 0, "inv": 0}
        orig = self.dev.handle

        def corrupt(text):
            if text == "SHOT":
                self.dev.fb, good = b"\x00" + self.dev.fb[1:], self.dev.fb
                # Header CRC from the good buffer, data from the corrupted one.
                crc = zlib.crc32(good) & 0xFFFFFFFF
                self.dev.emit(f"SHOT bytes=48000 crc={crc:08x} w=480 h=800 pw=800 ph=480 rot=0 inv=0")
                for i in range(0, 48000, 96):
                    self.dev.emit("D " + base64.b64encode(self.dev.fb[i:i + 96]).decode())
                self.dev.emit("OK SHOT")
            else:
                orig(text)

        self.dev.handle = corrupt
        with self.assertRaises(x4bench.BenchError):
            x4bench.take_shot(self.link, timeout=5)

    def test_key_waits_for_ok(self):
        rest, _ = self.link.command("KEY confirm", timeout=3)
        self.assertEqual(rest, "confirm req=60 held=61")

    def test_key_force_is_passed_through(self):
        self.ser.close()
        code, out = self.cli("key", "power", "2000", "force")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertEqual(self.dev.keys, ["power 2000 force"])
        code, _ = self.cli("key", "power", "bogus")
        self.assertEqual(code, x4bench.EXIT_ERR)

    # --- robustness
    def test_stale_reply_is_flushed_by_sync(self):
        self.dev.key_delay = 0.5
        with self.assertRaises(x4bench.BenchTimeout):
            self.link.command("KEY first", timeout=0.1)
        self.dev.key_delay = 0.01
        self.link.sync(timeout=2)
        rest, _ = self.link.command("KEY second", timeout=2)
        self.assertTrue(rest.startswith("second"), rest)

    def test_stale_output_of_an_earlier_session_is_skipped(self):
        self.ser.close()
        self.dev.on_connect = b"@B OK KEY stale req=60 held=61\n@B OK PING old proto=1 s0\n"
        code, out = self.cli("key", "down")
        self.assertEqual(code, x4bench.EXIT_OK)
        self.assertTrue(out.startswith("down "), out)

    def test_port_lost_mid_command_is_exit_2(self):
        self.dev.fb = b"\xff" * 48000
        self.dev.shot = {"w": 480, "h": 800, "pw": 800, "ph": 480, "rot": 0, "inv": 0}
        self.dev.drop_after_shot_header = True
        self.ser.close()
        with tempfile.TemporaryDirectory() as tmp:
            code, _ = self.cli("shot", os.path.join(tmp, "s.png"))
        self.assertEqual(code, x4bench.EXIT_TIMEOUT)

    def test_damaged_shot_line_is_an_error_not_a_crash(self):
        self.dev.fb = b"\xff" * 48000
        self.dev.shot = {"w": 480, "h": 800, "pw": 800, "ph": 480, "rot": 0, "inv": 0}
        self.dev.damage_d_line = True
        with self.assertRaises(x4bench.BenchError):
            x4bench.take_shot(self.link, timeout=5)
        self.ser.close()
        with tempfile.TemporaryDirectory() as tmp:
            code, _ = self.cli("shot", os.path.join(tmp, "s.png"))
        self.assertEqual(code, x4bench.EXIT_ERR)

    def test_shot_size_mismatch_is_refused(self):
        with self.assertRaises(x4bench.BenchError):
            x4bench.shot_rows({"w": "480", "h": "800", "pw": "800", "ph": "480", "rot": "0"}, b"\xff" * 100)

    def test_control_characters_never_reach_the_device(self):
        with self.assertRaises(x4bench.BenchError):
            self.link.command("OPEN /a\nCMD:SLEEP", timeout=1)
        with self.assertRaises(x4bench.BenchError):
            x4bench.put_file(self.link, b"x", "/a\rb", ack_timeout=1)

    def test_legacy_screenshot_is_refused_by_cmd(self):
        self.ser.close()
        code, _ = self.cli("cmd", "SCREENSHOT")
        self.assertEqual(code, x4bench.EXIT_ERR)

    def test_port_detection_only_takes_the_reader(self):
        from serial.tools import list_ports

        class Info:
            def __init__(self, device, vid, pid):
                self.device, self.vid, self.pid = device, vid, pid

        fake = [Info("/dev/cu.usbmodem1101", 0x239A, 0x8029),  # RAK4631
                Info("/dev/cu.usbmodem146201", 0x303A, 0x1001),
                Info("/dev/cu.Bluetooth-Incoming-Port", None, None)]
        orig = list_ports.comports
        list_ports.comports = lambda: fake
        try:
            ours, others = x4bench.find_ports()
        finally:
            list_ports.comports = orig
        self.assertEqual(ours, ["/dev/cu.usbmodem146201"])
        self.assertEqual(others, ["/dev/cu.usbmodem1101 (239A:8029)"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
