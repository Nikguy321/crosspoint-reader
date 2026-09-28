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
                            "default"):
                self.emit("ERR CARD unknown")
                return
            self.cards.append(rest)
            shown = "none" if rest == "default" else rest
            self.emit(f"OK CARD {rest} shown={shown} outcome={'logo' if rest == 'default' else 'drawn'} ms=42",
                      noise=True)
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
