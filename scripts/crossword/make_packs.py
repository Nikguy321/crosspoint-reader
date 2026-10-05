#!/usr/bin/env python3
"""Validate the downloadable crossword packs and write their .ipuz files.

    python3 scripts/crossword/make_packs.py          # validate, then (re)write packs/crossword/<pack>/<id>.ipuz
    python3 scripts/crossword/make_packs.py --check  # validate; fail when an .ipuz is stale, missing or extra

Each pack is a folder packs/crossword/<pack>/ whose source.txt holds its puzzles in the built-in block format
(scripts/crossword/README.md, lib/Crossword/CwText.h). Every block is checked with the same rules as the built-ins
(gen_builtin.validate), and ids and solutions must also be unique across every pack and the built-ins. The folder
name is the pack's name on the reader. ipuz is a trademark of Puzzazz, Inc., used with permission.
"""
import json
import os
import sys

import gen_builtin as g

ROOT = g.ROOT
PACKS = os.path.join(ROOT, "packs", "crossword")
AUTHOR = "CrossPoint Reader fork"
COPYRIGHT = "Original puzzle - MIT License"


def numbering(rows):
    """{(r, c): n} by the standard rule (the same numbers gen_builtin.entries gives)."""
    return {(r, c): n for (n, _d, r, c, _a) in g.entries(rows)}


def ipuz(p):
    rows = p["rows"]
    nums = numbering(rows)
    puzzle = [["#" if ch == "#" else nums.get((r, c), 0) for c, ch in enumerate(row)] for r, row in enumerate(rows)]
    solution = [[ch for ch in row] for row in rows]
    across = [[n, clue] for (d, n, clue, _l) in p["clues"] if d == "A"]
    down = [[n, clue] for (d, n, clue, _l) in p["clues"] if d == "D"]
    doc = {
        "version": "http://ipuz.org/v2",
        "kind": ["http://ipuz.org/crossword#1"],
        "title": p["title"],
        "author": AUTHOR,
        "copyright": COPYRIGHT,
        "dimensions": {"width": p["w"], "height": p["h"]},
        "puzzle": puzzle,
        "solution": solution,
        "clues": {"Across": sorted(across), "Down": sorted(down)},
    }
    return dumps(doc)


def dumps(doc):
    """JSON with one grid row or one clue a line (readable diffs); plain json.loads reads it back."""

    def value(v, depth):
        pad = " " * depth
        if isinstance(v, dict):
            items = ['%s %s: %s' % (pad, json.dumps(k), value(x, depth + 1)) for k, x in v.items()]
            return "{\n" + ",\n".join(items) + "\n" + pad + "}"
        if isinstance(v, list) and v and isinstance(v[0], list):
            return "[\n" + ",\n".join(pad + " " + json.dumps(x, ensure_ascii=True) for x in v) + "\n" + pad + "]"
        return json.dumps(v, ensure_ascii=True)

    text = value(doc, 0) + "\n"
    assert json.loads(text) == doc
    return text


def load_packs():
    """[(pack, folder, puzzles)]; raises SourceError."""
    out = []
    for pack in sorted(os.listdir(PACKS)) if os.path.isdir(PACKS) else []:
        folder = os.path.join(PACKS, pack)
        source = os.path.join(folder, "source.txt")
        if pack.startswith(".") or not os.path.isfile(source):
            continue
        with open(source, encoding="utf-8") as f:
            try:
                puzzles = g.parse(f.read())
                g.validate(puzzles)
            except g.SourceError as e:
                raise g.SourceError("%s: %s" % (pack, str(e).replace("builtin.txt", "source.txt")))
        out.append((pack, folder, puzzles))
    return out


def main():
    check = "--check" in sys.argv[1:]
    try:
        packs = load_packs()
        with open(g.SOURCE, encoding="utf-8") as f:
            builtins = g.parse(f.read())
        # Ids and solutions unique across the built-ins and every pack (validate refuses duplicates).
        g.validate(builtins + [p for (_n, _f, ps) in packs for p in ps])
    except g.SourceError as e:
        print("make_packs: %s" % e, file=sys.stderr)
        return 1
    if not packs:
        print("make_packs: no packs under %s" % os.path.relpath(PACKS, ROOT), file=sys.stderr)
        return 1
    stale = []
    total = 0
    for pack, folder, puzzles in packs:
        wanted = {p["id"] + ".ipuz": ipuz(p) for p in puzzles}
        present = {n for n in os.listdir(folder) if n.lower().endswith(".ipuz")}
        for name in sorted(present - set(wanted)):
            if check:
                stale.append("%s/%s (extra)" % (pack, name))
            else:
                os.remove(os.path.join(folder, name))
        for name, text in sorted(wanted.items()):
            path = os.path.join(folder, name)
            current = None
            if os.path.exists(path):
                with open(path, encoding="utf-8") as f:
                    current = f.read()
            if current == text:
                continue
            if check:
                stale.append("%s/%s" % (pack, name))
            else:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
        total += len(puzzles)
        print("make_packs: %s: %d puzzles, %d clues" % (pack, len(puzzles), sum(len(p["clues"]) for p in puzzles)))
    if stale:
        print("make_packs: stale: %s; run python3 scripts/crossword/make_packs.py" % ", ".join(stale), file=sys.stderr)
        return 1
    print("make_packs: %d packs, %d puzzles %s" % (len(packs), total, "current" if check else "written"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
