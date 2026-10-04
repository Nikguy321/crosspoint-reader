#!/usr/bin/env python3
"""Write the crossword parser fixtures (test/crossword/fixtures/*.ipuz, *.puz).

    python3 test/crossword/make_fixtures.py          # (re)write the fixtures
    python3 test/crossword/make_fixtures.py --check  # fail when a committed fixture differs

Every fixture is made here from OUR OWN puzzles (the two minis below, written for this repository,
and synthetic grids whose "words" are generated letters and whose clues are placeholders) -
never from a published puzzle. ipuz is a trademark of Puzzazz, Inc., used with permission.

The .puz writer follows the Across Lite layout (header table in lib/Crossword/CwPuz.h) and
computes every checksum the way the format defines them, so the C++ reader's checksum code is
checked against an independent implementation.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures")

# Our own minis: (rows, across clues by number, down clues by number).
SHELTER = (
    ["##HUT", "#CASE", "RAVEN", "AGED#", "PEN##"],
    {1: "Simple shelter in the woods", 4: "Detective's puzzle to crack", 5: "Large black bird with a croak",
     6: "Like fine cheese or wine", 7: "Ballpoint, for one"},
    {1: "Safe harbor", 2: "Secondhand", 3: "Fingers on two hands", 4: "Home for a pet hamster",
     5: "Knock on a door"},
)
KEEN = (
    ["#PAY#", "MERIT", "EAGER", "TRULY", "#LED#"],
    {1: "Settle the bill", 4: "Deserve, as praise", 6: "Keen to get started", 7: "In fact; honestly",
     8: "Was in front"},
    {1: "Gem found inside an oyster", 2: "Disagree out loud", 3: "Give way at a traffic sign",
     4: "Was introduced to", 5: "Give it a shot"},
)


def numbering(rows):
    """{(r, c): n}, [(n, 'A'/'D')] in .puz order (by number, Across first)."""
    h, w = len(rows), len(rows[0])

    def white(r, c):
        return 0 <= r < h and 0 <= c < w and rows[r][c] not in "#."

    nums, order, n = {}, [], 1
    for r in range(h):
        for c in range(w):
            if not white(r, c):
                continue
            a = not white(r, c - 1) and white(r, c + 1)
            d = not white(r - 1, c) and white(r + 1, c)
            if a or d:
                nums[(r, c)] = n
                if a:
                    order.append((n, "A"))
                if d:
                    order.append((n, "D"))
                n += 1
    return nums, order


def synthetic(w, h, seed):
    """A w x h grid with symmetric blocks every white square of which is in an entry, filled
    with generated letters, and placeholder clues."""
    rows = [["A"] * w for _ in range(h)]
    for r in range(h):
        for c in range(w):
            if (r % 4 == 3 and c % 5 == 2) or (c % 4 == 3 and r % 5 == 2):
                rows[r][c] = "#"
                rows[h - 1 - r][w - 1 - c] = "#"
    letters = "CROSSWORDPUZZLEGRIDSQUAREMINI"
    k = seed
    for r in range(h):
        for c in range(w):
            if rows[r][c] != "#":
                rows[r][c] = letters[k % len(letters)]
                k += 7
    rows = ["".join(r) for r in rows]
    nums, order = numbering(rows)
    covered = set()
    for (r, c), n in nums.items():
        for d, (dr, dc) in (("A", (0, 1)), ("D", (1, 0))):
            if (n, d) in order:
                k = 0
                while 0 <= r + dr * k < h and 0 <= c + dc * k < w and rows[r + dr * k][c + dc * k] != "#":
                    covered.add((r + dr * k, c + dc * k))
                    k += 1
    assert all(rows[r][c] == "#" or (r, c) in covered for r in range(h) for c in range(w)), "isolated square"
    across = {n: "Across entry %d of a %dx%d test grid" % (n, w, h) for (n, d) in order if d == "A"}
    down = {n: "Down entry %d of a %dx%d test grid" % (n, w, h) for (n, d) in order if d == "D"}
    return rows, across, down


# ---- ipuz ---------------------------------------------------------------------------------------


def ipuz(puzzle, clue_style="pairs", title="Test grid", kind="http://ipuz.org/crossword#1", cells="numbers",
         block="#", solution=True, lower=False, circles=(), extra=None):
    rows, across, down = puzzle
    nums, _order = numbering(rows)
    h, w = len(rows), len(rows[0])
    grid = []
    for r in range(h):
        line = []
        for c in range(w):
            if rows[r][c] == "#":
                line.append(None if cells == "nulls" else block)
                continue
            n = nums.get((r, c), 0)
            if cells == "objects":
                cell = {"cell": str(n) if n else 0}
                if (r, c) in circles:
                    cell["style"] = {"shapebg": "circle"}
                line.append(cell)
            elif (r, c) in circles:
                line.append({"cell": n, "style": {"shapebg": "circle"}})
            else:
                line.append(n)
        grid.append(line)
    doc = {
        "version": "http://ipuz.org/v2",
        "kind": [kind],
        "dimensions": {"width": w, "height": h},
        "title": title,
        "author": "CrossPoint tests",
        "copyright": "Public domain test data",
        "puzzle": grid,
    }
    if block != "#":
        doc["block"] = block
    if solution:
        doc["solution"] = [[(None if cells == "nulls" else block) if ch == "#" else (ch.lower() if lower else ch)
                            for ch in row] for row in rows]

    def clue_list(clues):
        if clue_style == "strings":
            return [clues[n] for n in sorted(clues)]
        if clue_style == "objects":
            return [{"number": n, "clue": clues[n], "enumeration": str(0)} for n in sorted(clues)]
        return [[n, clues[n]] for n in sorted(clues)]

    keys = ("Across:Theme", "Down:Theme") if clue_style == "objects" else ("Across", "Down")
    doc["clues"] = {keys[0]: clue_list(across), keys[1]: clue_list(down)}
    doc["saved"] = [[0] * w for _ in range(h)]  # filtered out by the reader
    if extra:
        doc.update(extra)
    return (json.dumps(doc, indent=1, ensure_ascii=False) + "\n").encode("utf-8")


# ---- puz ----------------------------------------------------------------------------------------


def cksum(data, c=0):
    for b in data:
        c = ((c >> 1) | ((c & 1) << 15)) & 0xFFFF
        c = (c + b) & 0xFFFF
    return c


def puz(puzzle, version=b"1.3", encoding="latin-1", ptype=0x0001, scrambled=0, extensions=(), bad_sums=False,
        title="Test grid", author="CrossPoint tests", copyright="Public domain test data", notes=""):
    rows, across, down = puzzle
    h, w = len(rows), len(rows[0])
    _nums, order = numbering(rows)
    solution = "".join(row.replace("#", ".") for row in rows).encode("ascii")
    state = b"".join(b"." if ch == ord(".") else b"-" for ch in solution)
    clues = [(across if d == "A" else down)[n].encode(encoding) for (n, d) in order]
    t, a, cp, nt = (s.encode(encoding) for s in (title, author, copyright, notes))
    ver = tuple(int(x) for x in version.decode().split("."))

    def text_sum(c):
        for s in (t, a, cp):
            if s:
                c = cksum(s + b"\0", c)
        for s in clues:
            if s:
                c = cksum(s, c)
        if ver >= (1, 3) and nt:
            c = cksum(nt + b"\0", c)
        return c

    cib_bytes = struct.pack("<BBHHH", w, h, len(clues), ptype, scrambled)
    cib = cksum(cib_bytes)
    glob = text_sum(cksum(state, cksum(solution, cib)))
    sums = [cib, cksum(solution), cksum(state), text_sum(0)]
    low = bytes(k ^ (s & 0xFF) for k, s in zip(b"ICHE", sums))
    high = bytes(k ^ (s >> 8) for k, s in zip(b"ATED", sums))
    if bad_sums:
        glob, cib, low, high = glob ^ 0x1111, cib ^ 0x2222, bytes(4), bytes(4)
    header = struct.pack("<H12sH4s4s4sHH12s", glob, b"ACROSS&DOWN\0", cib, low, high, version.ljust(4, b"\0"), 0, 0,
                         bytes(12)) + cib_bytes
    assert len(header) == 0x34
    body = solution + state + t + b"\0" + a + b"\0" + cp + b"\0"
    body += b"".join(c + b"\0" for c in clues) + nt + b"\0"
    for code, data in extensions:
        body += code + struct.pack("<HH", len(data), cksum(data)) + data + b"\0"
    return header + body


def cells_flags(rows, marked, value):
    h, w = len(rows), len(rows[0])
    return bytes(value if (r, c) in marked else 0 for r in range(h) for c in range(w))


# ---- the set ------------------------------------------------------------------------------------


def fixtures():
    f = {}
    fifteen = synthetic(15, 15, 3)
    nine = synthetic(9, 9, 5)
    over = synthetic(17, 17, 1)

    # ipuz: the clue shapes, cell shapes and features the reader accepts
    f["strings.ipuz"] = ipuz(SHELTER, clue_style="strings", title="Shelter", lower=True)
    f["pairs.ipuz"] = ipuz(KEEN, clue_style="pairs", title="Keen &amp; <b>eager</b>", circles=((1, 1), (2, 2)))
    f["objects.ipuz"] = ipuz(SHELTER, clue_style="objects", title="Shelter", cells="objects", circles=((2, 2),))
    f["nulls.ipuz"] = ipuz(KEEN, cells="nulls", title="Keen")
    f["block_override.ipuz"] = ipuz(SHELTER, block="*", title="Shelter")
    html = (KEEN[0], dict(KEEN[1]), dict(KEEN[2]))
    html[1][1] = "Settle the <i>bill</i> &amp; tip"
    html[1][4] = "Deserve&nbsp;&#8216;praise&#8217;, &quot;a&nbsp;lot&quot;"
    html[2][2] = "Disagree &lt;loudly&gt;&#8230; it&#39;s café talk"
    html[2][3] = "Give way — at a “traffic” sign"
    f["html.ipuz"] = ipuz(html, title="<b>Keen</b> &amp; HTML")
    long_clue = (KEEN[0], dict(KEEN[1]), dict(KEEN[2]))
    long_clue[1][8] = "Was in front " + "and kept going " * 40
    f["long_clue.ipuz"] = ipuz(long_clue, title="Keen")
    f["fifteen.ipuz"] = ipuz(fifteen, title="Fifteen")
    f["nine.ipuz"] = ipuz(nine, title="Nine")

    # ipuz: refusals
    f["no_solution.ipuz"] = ipuz(SHELTER, solution=False)
    rebus = json.loads(ipuz(SHELTER))
    rebus["solution"][0][2] = "HU"
    f["rebus.ipuz"] = (json.dumps(rebus, indent=1) + "\n").encode()
    barred = json.loads(ipuz(SHELTER))
    barred["puzzle"][1][1] = {"cell": 4, "style": {"barred": "T"}}
    f["barred.ipuz"] = (json.dumps(barred, indent=1) + "\n").encode()
    f["oversize.ipuz"] = ipuz(over, title="Seventeen")
    badnum = json.loads(ipuz(SHELTER))
    badnum["puzzle"][2][0] = 9
    f["bad_numbering.ipuz"] = (json.dumps(badnum, indent=1) + "\n").encode()
    f["not_crossword.ipuz"] = ipuz(SHELTER, kind="http://ipuz.org/sudoku#1")
    whole = ipuz(KEEN)
    f["truncated.ipuz"] = whole[: len(whole) // 2]
    missing = json.loads(ipuz(SHELTER))
    del missing["clues"]["Down"][4]
    f["missing_clue.ipuz"] = (json.dumps(missing, indent=1) + "\n").encode()

    # puz
    latin = (KEEN[0], dict(KEEN[1]), dict(KEEN[2]))
    latin[1][7] = "In fact; naïvely honest"
    latin[2][2] = "Disagree at the café"
    f["v13_latin1.puz"] = puz(latin, version=b"1.3", encoding="latin-1", title="Café Keen")
    utf = (SHELTER[0], dict(SHELTER[1]), dict(SHELTER[2]))
    utf[1][4] = "Detective’s puzzle — crack it"
    utf[2][2] = "Secondhand, like a café chair"
    f["v20_utf8.puz"] = puz(utf, version=b"2.0", encoding="utf-8", title="Shelter – UTF-8", notes="Notes here")
    f["gext_circles.puz"] = puz(KEEN, extensions=[(b"GEXT", cells_flags(KEEN[0], {(1, 1), (2, 2)}, 0x80))])
    f["grbs_rebus.puz"] = puz(KEEN, extensions=[(b"GRBS", cells_flags(KEEN[0], {(2, 2)}, 1)),
                                                (b"RTBL", b" 0:GE;")])
    f["grbs_empty.puz"] = puz(KEEN, extensions=[(b"GRBS", cells_flags(KEEN[0], set(), 1))])
    f["scrambled.puz"] = puz(KEEN, scrambled=0x0004)
    f["diagramless.puz"] = puz(KEEN, ptype=0x0401)
    f["oversize.puz"] = puz(over)
    whole = puz(SHELTER)
    f["truncated.puz"] = whole[: len(whole) - 40]
    f["bad_checksums.puz"] = puz(SHELTER, bad_sums=True)
    f["fifteen.puz"] = puz(fifteen, title="Fifteen")
    return f


def main():
    check = "--check" in sys.argv[1:]
    files = fixtures()
    stale = []
    os.makedirs(OUT, exist_ok=True)
    for name, data in sorted(files.items()):
        path = os.path.join(OUT, name)
        if check:
            try:
                with open(path, "rb") as fh:
                    if fh.read() != data:
                        stale.append(name)
            except OSError:
                stale.append(name)
            continue
        with open(path, "wb") as fh:
            fh.write(data)
    if check:
        if stale:
            print("make_fixtures: stale or missing: %s" % ", ".join(stale), file=sys.stderr)
            return 1
        print("make_fixtures: %d fixtures current" % len(files))
        return 0
    print("make_fixtures: wrote %d fixtures to %s" % (len(files), os.path.relpath(OUT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
