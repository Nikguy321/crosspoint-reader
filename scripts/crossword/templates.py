#!/usr/bin/env python3
"""Template tools: validate American-rule block patterns and search for good 7x7 / 9x9 ones.

Rules checked: square, rotational (180 degree) symmetry, every Across/Down run >= 3 letters (so every white cell is
checked both ways), all white cells connected.

  python3 templates.py check            # validate templates.json
  python3 templates.py search 7 6 10    # list good 7x7 patterns with 6..10 blocks
"""
import itertools, json, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def runs(rows):
    n = len(rows)
    out = []
    for d in (0, 1):
        for a in range(n):
            line = rows[a] if d == 0 else ''.join(rows[r][a] for r in range(n))
            for seg in line.split('#'):
                if seg:
                    out.append(len(seg))
    return out


def problems(rows):
    n = len(rows)
    p = []
    if any(len(r) != n for r in rows):
        p.append('not square')
        return p
    for r in range(n):
        for c in range(n):
            if (rows[r][c] == '#') != (rows[n - 1 - r][n - 1 - c] == '#'):
                p.append('not symmetric')
                break
        if p:
            break
    if any(l < 3 for l in runs(rows)):
        p.append('run < 3')
    whites = [(r, c) for r in range(n) for c in range(n) if rows[r][c] != '#']
    if not whites:
        p.append('no white')
        return p
    seen = {whites[0]}
    stack = [whites[0]]
    while stack:
        r, c = stack.pop()
        for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (r + dr, c + dc)
            if 0 <= q[0] < n and 0 <= q[1] < n and rows[q[0]][q[1]] != '#' and q not in seen:
                seen.add(q)
                stack.append(q)
    if len(seen) != len(whites):
        p.append('not connected')
    return p


def stats(rows):
    rl = runs(rows)
    blocks = sum(r.count('#') for r in rows)
    return {'blocks': blocks, 'words': len(rl), 'threes': rl.count(3), 'max': max(rl),
            'avg': round(sum(rl) / len(rl), 2), 'lens': sorted(rl)}


def canon(rows):
    """Smallest of the 8 symmetries (to dedupe mirror images)."""
    n = len(rows)
    g = [list(r) for r in rows]
    forms = []
    for _ in range(4):
        g = [list(x) for x in zip(*g[::-1])]
        forms.append(tuple(''.join(r) for r in g))
        forms.append(tuple(''.join(r[::-1]) for r in g))
    return min(forms)


def has_2x2_block(rows):
    n = len(rows)
    return any(rows[r][c] == rows[r + 1][c] == rows[r][c + 1] == rows[r + 1][c + 1] == '#'
               for r in range(n - 1) for c in range(n - 1))


def search(n, bmin, bmax, samples=400000, seed=1):
    cells = [(r, c) for r in range(n) for c in range(n)]
    pairs = []
    for r, c in cells:
        q = (n - 1 - r, n - 1 - c)
        if (r, c) < q:
            pairs.append(((r, c), q))
    centre = (n // 2, n // 2)
    rnd = random.Random(seed)
    found = {}
    for _ in range(samples):
        k = rnd.randint(bmin // 2, bmax // 2)
        chosen = rnd.sample(pairs, k)
        use_centre = rnd.random() < 0.3
        blk = set()
        for a, b in chosen:
            blk.add(a)
            blk.add(b)
        if use_centre:
            blk.add(centre)
        if not (bmin <= len(blk) <= bmax):
            continue
        rows = [''.join('#' if (r, c) in blk else '.' for c in range(n)) for r in range(n)]
        if problems(rows):
            continue
        key = canon(rows)
        if key in found:
            continue
        found[key] = list(key)
    return list(found.values())


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'search':
        n, bmin, bmax = map(int, sys.argv[2:5])
        res = search(n, bmin, bmax)
        res = [r for r in res if not has_2x2_block(r)]
        def key(r):
            s = stats(r)
            return (s['threes'] / s['words'], -s['avg'])
        res.sort(key=key)
        print(len(res), 'valid patterns (no 2x2 block clumps)')
        for r in res[:int(sys.argv[5]) if len(sys.argv) > 5 else 40]:
            s = stats(r)
            print('|'.join(r), s['blocks'], s['words'], s['threes'], s['avg'], s['max'])
        return
    T = json.load(open(os.path.join(HERE, 'templates.json')))
    bad = 0
    for name, rows in T.items():
        p = problems(rows)
        s = stats(rows)
        print(f"{name:10s} {'|'.join(rows)}  blocks={s['blocks']} words={s['words']} threes={s['threes']} "
              f"avg={s['avg']} {'BAD ' + ','.join(p) if p else 'ok'}")
        bad += bool(p)
    keys = [canon(r) for r in T.values()]
    if len(set(keys)) != len(keys):
        print('note: some templates are mirror images of each other')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
