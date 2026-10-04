#!/usr/bin/env python3
"""Validate the built-in crosswords and generate lib/Crossword/CwBuiltinData.cpp.

    python3 scripts/crossword/gen_builtin.py          # validate, then write the .cpp
    python3 scripts/crossword/gen_builtin.py --check  # validate; fail when the .cpp is stale
    python3 scripts/crossword/gen_builtin.py --notes  # also print content notes (never fatal)

Source: scripts/crossword/builtin.txt (format in lib/Crossword/CwText.h). Each puzzle must be
rectangular (3..15 a side, A-Z and '#'), have every white square in an entry (runs of 2 or more),
exactly one clue per entry and none extra, a unique id ([a-z0-9-]{1,24}) and a unique solution
(FNV-1a of "WxH:" + solution, as the firmware computes it), a title of 1..48 bytes, and ASCII
clues of at most 160 characters that do not give their answer away: no word of the clue may be
the answer or the answer less a trailing S / ED / ING, nor (for such forms of 4+ letters) start
with one.

Comments: a line that is '#' alone or '#' then a space is a comment; a blank line (spaces and tabs
only) is ignored. Any other line is read as it stands, so a line made only of A-Z and '#' is a GRID
ROW wherever it is (a "#####" rule or "#NOTE" inside a block is a row, not a comment) and a line
like "#note" is an error. The firmware's parser (cw::parseTextPuzzle, lib/Crossword/CwText.cpp)
reads the same rule; write every comment as "# text".

Each puzzle's text in the .cpp is its block of builtin.txt without comments or blank lines; the
firmware parses it with the same parser the host tests run over every built-in.

--notes prints what the content rules (scripts/crossword/README.md) prefer but do not require:
blocks not in 180-degree rotational symmetry, entries under 3 letters, an answer used more than
twice across the set, one entry repeating another inside a puzzle (APE / APES, EYE / EYESHADOW),
and clues over 60 characters.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE = os.path.join(ROOT, "scripts", "crossword", "builtin.txt")
OUTPUT = os.path.join(ROOT, "lib", "Crossword", "CwBuiltinData.cpp")

ID_RE = re.compile(r"^[a-z0-9-]{1,24}$")
ROW_RE = re.compile(r"^[A-Z#]+$")
CLUE_RE = re.compile(r"^([AD]) ([0-9]+) (.+)$")
MAX_SIDE = 15
MIN_SIDE = 3
MAX_ENTRIES = 100
MAX_TITLE = 48
MAX_CLUE = 160


class SourceError(Exception):
    pass


def fnv1a(data, h=2166136261):
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def puzzle_fnv(w, h, rows):
    return fnv1a(("%dx%d:" % (w, h)).encode() + "".join(rows).encode())


def entries(rows):
    """[(number, dir 'A'/'D', row, col, answer)] in the firmware's order: Across, then Down."""
    h, w = len(rows), len(rows[0])

    def white(r, c):
        return 0 <= r < h and 0 <= c < w and rows[r][c] != "#"

    numbers = {}
    n = 1
    for r in range(h):
        for c in range(w):
            if not white(r, c):
                continue
            if (not white(r, c - 1) and white(r, c + 1)) or (not white(r - 1, c) and white(r + 1, c)):
                numbers[(r, c)] = n
                n += 1
    out = []
    covered = set()
    for d, (dr, dc) in (("A", (0, 1)), ("D", (1, 0))):
        for r in range(h):
            for c in range(w):
                if not white(r, c) or white(r - dr, c - dc) or not white(r + dr, c + dc):
                    continue
                k = 0
                letters = ""
                while white(r + dr * k, c + dc * k):
                    covered.add((r + dr * k, c + dc * k))
                    letters += rows[r + dr * k][c + dc * k]
                    k += 1
                out.append((numbers[(r, c)], d, r, c, letters))
    for r in range(h):
        for c in range(w):
            if white(r, c) and (r, c) not in covered:
                raise SourceError("square %d,%d is in no entry" % (r, c))
    return out


def give_away(answer, clue):
    """The clue word that gives the answer away, or None. A clue word equal to the answer (or to it less a
    trailing S / ED / ING) always counts; a clue word that merely starts with it counts only for forms of 4+
    letters, deliberately: a 3-letter prefix ("cat" in "category", "pen" in "penny") is too often unrelated."""
    a = answer.lower()
    forms = {a}
    for suffix in ("s", "ed", "ing"):
        if a.endswith(suffix) and len(a) - len(suffix) >= 2:
            forms.add(a[: -len(suffix)])
    for word in re.findall(r"[a-z]+", clue.lower()):
        for f in forms:
            if word == f or (len(f) >= 4 and word.startswith(f)):
                return word
    return None


def parse(text):
    """[{id, title, rows, clues, block}] from builtin.txt; raises SourceError."""
    puzzles = []
    cur = None
    for lineno, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r")
        if line.strip(" \t") == "" or line == "#" or line.startswith("# "):
            continue  # the firmware's isSkippable(): blank (spaces / tabs) or a "# " comment

        def fail(msg):
            raise SourceError("builtin.txt:%d: %s" % (lineno, msg))

        if line.startswith("=== "):
            head = line[4:]
            if "|" not in head:
                fail("header needs '<id> | <title>'")
            pid, title = (s.strip() for s in head.split("|", 1))
            if not ID_RE.match(pid):
                fail("bad id %r" % pid)
            if not title or len(title.encode()) > MAX_TITLE:
                fail("title must be 1..%d bytes" % MAX_TITLE)
            if not all(0x20 <= ord(ch) < 0x7F for ch in title):
                fail("titles are printable ASCII (the picker draws them as written)")
            cur = {"id": pid, "title": title, "rows": [], "clues": [], "line": lineno}
            puzzles.append(cur)
            continue
        if cur is None:
            fail("text before the first '=== ' header")
        if ROW_RE.match(line) and not cur["clues"]:
            cur["rows"].append(line)
            continue
        m = CLUE_RE.match(line)
        if not m:
            if ROW_RE.match(line):
                fail("a grid row after the clues: %r (a comment is '# ' then text)" % line)
            if line.startswith("#"):
                fail("not a comment, a grid row or a clue: %r (a comment is '# ' then text)" % line)
            fail("not a grid row or a clue: %r" % line)
        cur["clues"].append((m.group(1), int(m.group(2)), m.group(3).strip(), lineno))
    return puzzles


def validate(puzzles):
    if not puzzles:
        raise SourceError("builtin.txt holds no puzzles")
    ids = set()
    fnvs = {}
    for p in puzzles:
        where = "%s (line %d)" % (p["id"], p["line"])
        if p["id"] in ids:
            raise SourceError("%s: duplicate id" % where)
        ids.add(p["id"])
        rows = p["rows"]
        if not rows:
            raise SourceError("%s: no grid" % where)
        w, h = len(rows[0]), len(rows)
        if any(len(r) != w for r in rows):
            raise SourceError("%s: rows are not all %d wide" % (where, w))
        if not (MIN_SIDE <= w <= MAX_SIDE and MIN_SIDE <= h <= MAX_SIDE):
            raise SourceError("%s: %dx%d is outside %d..%d" % (where, w, h, MIN_SIDE, MAX_SIDE))
        try:
            ents = entries(rows)
        except SourceError as e:
            raise SourceError("%s: %s" % (where, e))
        if len(ents) > MAX_ENTRIES:
            raise SourceError("%s: %d entries (max %d)" % (where, len(ents), MAX_ENTRIES))
        wanted = {(d, n): ans for (n, d, _r, _c, ans) in ents}
        seen = set()
        for d, n, clue, lineno in p["clues"]:
            key = (d, n)
            if key not in wanted:
                raise SourceError("line %d: %d%s is not an entry of %s" % (lineno, n, d, p["id"]))
            if key in seen:
                raise SourceError("line %d: %d%s has two clues" % (lineno, n, d))
            seen.add(key)
            if not clue:
                raise SourceError("line %d: empty clue" % lineno)
            if not all(0x20 <= ord(ch) < 0x7F for ch in clue):
                raise SourceError("line %d: clues are ASCII" % lineno)
            if len(clue) > MAX_CLUE:
                raise SourceError("line %d: clue longer than %d" % (lineno, MAX_CLUE))
            word = give_away(wanted[key], clue)
            if word:
                raise SourceError("line %d: the clue for %s gives it away (%r)" % (lineno, wanted[key], word))
        missing = [("%d%s" % (n, d)) for (d, n) in wanted if (d, n) not in seen]
        if missing:
            raise SourceError("%s: no clue for %s" % (where, ", ".join(sorted(missing))))
        f = puzzle_fnv(w, h, rows)
        if f in fnvs:
            raise SourceError("%s: the same grid as %s" % (where, fnvs[f]))
        fnvs[f] = p["id"]
        p["w"], p["h"], p["fnv"] = w, h, f


REPEAT_SUFFIXES = ("ING", "ES", "ED", "ER", "S", "D", "R", "E", "Y", "LY")


def repeats(a, b):
    """True when one entry repeats another inside a puzzle: the same word less a common ending (APE / APES,
    UNIT / UNITE, SNAPPED / SNAPPER), or the shorter one (3+ letters) starting a longer compound (EYE / EYESHADOW).
    driver.py applies the same test to its fills."""
    if len(a) > len(b):
        a, b = b, a
    if len(a) < 3:
        return False
    if b.startswith(a) and (b[len(a):] in REPEAT_SUFFIXES or len(b) - len(a) >= 3):
        return True

    def stems(w):
        out = {w}
        for suffix in REPEAT_SUFFIXES:
            if w.endswith(suffix) and len(w) - len(suffix) >= 3:
                stem = w[: -len(suffix)]
                out.add(stem)
                if len(stem) >= 4 and stem[-1] == stem[-2]:
                    out.add(stem[:-1])  # SNAPP -> SNAP
        return out

    return bool(stems(a) & stems(b))


def notes(puzzles):
    """Non-fatal content notes (--notes): symmetry, short entries, answer reuse, repeats in a puzzle, long clues."""
    out = []
    use = {}
    for p in puzzles:
        rows, w, h = p["rows"], p["w"], p["h"]
        if any((rows[r][c] == "#") != (rows[h - 1 - r][w - 1 - c] == "#") for r in range(h) for c in range(w)):
            out.append("%s: blocks are not rotationally symmetric" % p["id"])
        answers = {(d, n): ans for (n, d, _r, _c, ans) in entries(rows)}
        for (d, n), ans in sorted(answers.items()):
            use.setdefault(ans, []).append(p["id"])
            if len(ans) < 3:
                out.append("%s: %d%s is %d letters" % (p["id"], n, d, len(ans)))
        items = sorted(answers.items())
        for i, ((d1, n1), a1) in enumerate(items):
            for (d2, n2), a2 in items[i + 1:]:
                if repeats(a1, a2):
                    out.append("%s: %d%s %s and %d%s %s repeat a word" % (p["id"], n1, d1, a1, n2, d2, a2))
        for d, n, clue, lineno in p["clues"]:
            if len(clue) > 60:
                out.append("line %d: %s %d%s clue is %d characters (60 or fewer preferred)" % (lineno, p["id"], n, d,
                                                                                               len(clue)))
    for ans, where in sorted(use.items()):
        if len(where) > 2:
            out.append("answer %s is used %d times: %s" % (ans, len(where), ", ".join(where)))
    return out


def block_text(p):
    lines = ["=== %s | %s" % (p["id"], p["title"])] + p["rows"]
    lines += ["%s %d %s" % (d, n, clue) for (d, n, clue, _l) in p["clues"]]
    return [line + "\n" for line in lines]


def literal_chunks(line, limit=100):
    """A line cut (after a space where possible) into pieces short enough that clang-format
    (ColumnLimit 120, BreakStringLiterals) leaves the generated file as it is."""
    out = []
    while len(line) > limit:
        cut = line.rfind(" ", 0, limit) + 1 or limit
        out.append(line[:cut])
        line = line[cut:]
    out.append(line)
    return out


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def render(puzzles):
    out = [
        "// GENERATED by scripts/crossword/gen_builtin.py from scripts/crossword/builtin.txt: do not edit.",
        "// Regenerate with `python3 scripts/crossword/gen_builtin.py` (`--check` fails when this is stale).",
        "",
        "#include <cstring>",
        "",
        '#include "CwBuiltin.h"',
        "",
        "namespace cw {",
        "",
        "namespace {",
        "",
        "constexpr BuiltinPuzzle BUILTINS[] = {",
    ]
    for p in puzzles:
        out.append("    {%s, %s, %d, %d, 0x%08xu," % (c_string(p["id"]), c_string(p["title"]), p["w"], p["h"], p["fnv"]))
        chunks = [c for line in block_text(p) for c in literal_chunks(line)]
        for i, chunk in enumerate(chunks):
            out.append("     " + c_string(chunk) + ("}," if i == len(chunks) - 1 else ""))
    out += [
        "};",
        "",
        "}  // namespace",
        "",
        "size_t builtinCount() { return sizeof(BUILTINS) / sizeof(BUILTINS[0]); }",
        "",
        "const BuiltinPuzzle& builtinPuzzle(const size_t index) { return BUILTINS[index < builtinCount() ? index : 0]; }",
        "",
        "int findBuiltin(const char* id) {",
        "  if (!id) return -1;",
        "  for (size_t i = 0; i < builtinCount(); i++) {",
        "    if (std::strcmp(BUILTINS[i].id, id) == 0) return static_cast<int>(i);",
        "  }",
        "  return -1;",
        "}",
        "",
        "}  // namespace cw",
        "",
    ]
    return "\n".join(out)


def main():
    check = "--check" in sys.argv[1:]
    try:
        with open(SOURCE, encoding="utf-8") as f:
            puzzles = parse(f.read())
        validate(puzzles)
    except SourceError as e:
        print("gen_builtin: %s" % e, file=sys.stderr)
        return 1
    if "--notes" in sys.argv[1:]:
        sizes = {}
        for p in puzzles:
            sizes[(p["w"], p["h"])] = sizes.get((p["w"], p["h"]), 0) + 1
        print("gen_builtin: %d puzzles, %d clues (%s)" % (
            len(puzzles), sum(len(p["clues"]) for p in puzzles),
            ", ".join("%dx%d: %d" % (w, h, k) for (w, h), k in sorted(sizes.items()))))
        found = notes(puzzles)
        for n in found:
            print("note: %s" % n)
        if not found:
            print("gen_builtin: no content notes")
    text = render(puzzles)
    if check:
        try:
            with open(OUTPUT, encoding="utf-8") as f:
                current = f.read()
        except OSError:
            current = None
        if current != text:
            print("gen_builtin: %s is stale; run python3 scripts/crossword/gen_builtin.py" % os.path.relpath(OUTPUT, ROOT),
                  file=sys.stderr)
            return 1
        print("gen_builtin: %d puzzles, CwBuiltinData.cpp is current" % len(puzzles))
        return 0
    with open(OUTPUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("gen_builtin: wrote %d puzzles to %s" % (len(puzzles), os.path.relpath(OUTPUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
