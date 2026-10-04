#!/usr/bin/env python3
"""Produce candidate fills: 5x5 / 7x7 / 9x9 over templates.json with the C++ `fill` tool.

Deterministic: jobs run in a fixed order, in fixed-size parallel batches, accepted in job order. For every (template,
seed) the fill is tried at a falling minimum word score (90, 80, 70, 60, 50) and the first success is kept, so fills with
a high minimum score win; the hand-reviewed lexicon (words_ok.txt) is tried before the full one. A word may appear in at most CAP accepted candidates (all sizes together) to spread the
vocabulary; identical grids are skipped, and so is a fill where one entry repeats another (APE / APES, EYE / EYESHADOW:
gen_builtin.repeats).

  python3 driver.py [out.json] [--keep prev.json]

--keep: start from the candidates of an earlier run whose every answer is still in the reviewed lexicon
(words_ok.txt, i.e. hand-reviewed and not denied since), then top up each template's quota with new seeds.
"""
import json, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

from gen_builtin import repeats  # one entry repeating another (APE / APES): such a grid is never kept

HERE = os.path.dirname(os.path.abspath(__file__))
FILL = os.path.join(HERE, 'fill')
WORDS = os.path.join(HERE, 'words.txt')
ARGS = [a for a in sys.argv[1:]]
KEEP = None
if '--keep' in ARGS:
    KEEP = ARGS[ARGS.index('--keep') + 1]
    del ARGS[ARGS.index('--keep'):ARGS.index('--keep') + 2]
OUT = ARGS[0] if ARGS else os.path.join(HERE, 'candidates.json')
CAP = 3
WORKERS = 3
WORDS_OK = os.path.join(HERE, 'words_ok.txt')
# (lexicon, floor) attempts in order: the hand-reviewed lexicon first, then the hybrid one (all short words, reviewed
# long words), then the full filtered list; reviewed words sort one band higher.
WORDS_HYBRID = os.path.join(HERE, 'words_hybrid.txt')
ATTEMPTS = [(WORDS_OK, 70), (WORDS_OK, 50), (WORDS_HYBRID, 80), (WORDS_HYBRID, 50), (WORDS, 80), (WORDS, 50)]
if os.environ.get('CW_NO_FULL'):  # top-up mode: long entries only from reviewed words
    ATTEMPTS = [a for a in ATTEMPTS if a[0] != WORDS]

PLAN = {  # size: (target count, [(template, weight)], node budget per attempt, max seeds per template)
    5: (120, [('t5-open', 20), ('t5-c2', 28), ('t5-c4', 24), ('t5-tri', 20), ('t5-pin', 28)], 30000, 400),
    7: (45, [('t7-a', 6), ('t7-b', 6), ('t7-c', 6), ('t7-d', 5), ('t7-e', 6), ('t7-f', 5), ('t7-g', 5), ('t7-h', 6)],
        6000, 200),
    9: (25, [('t9-k', 5), ('t9-l', 2), ('t9-m', 6), ('t9-e', 4), ('t9-g', 4), ('t9-n', 4)], 3000, 700),
}

T = json.load(open(os.path.join(HERE, 'templates.json')))


def number(rows):
    """Standard numbering: row-major; a white cell gets the next number when it starts an Across or a Down run of >= 2."""
    h, w = len(rows), len(rows[0])
    white = lambda r, c: 0 <= r < h and 0 <= c < w and rows[r][c] != '#'
    n = 0
    entries = []
    for r in range(h):
        for c in range(w):
            if not white(r, c):
                continue
            a = not white(r, c - 1) and white(r, c + 1)
            d = not white(r - 1, c) and white(r + 1, c)
            if a or d:
                n += 1
                if a:
                    k = c
                    s = ''
                    while white(r, k):
                        s += rows[r][k]
                        k += 1
                    entries.append({'n': n, 'dir': 'A', 'answer': s})
                if d:
                    k = r
                    s = ''
                    while white(k, c):
                        s += rows[k][c]
                        k += 1
                    entries.append({'n': n, 'dir': 'D', 'answer': s})
    return entries


def has_repeat(answers):
    return any(repeats(a, b) for i, a in enumerate(answers) for b in answers[i + 1:])


def attempt(grid, seed, budget, excl_path, seen):
    """First success over ATTEMPTS that is not a grid already kept (seen is a snapshot taken before the batch)."""
    t0 = time.time()
    for lexicon, ms in ATTEMPTS:
        out = subprocess.run([FILL, '--words', lexicon, '--grid', grid, '--seed', str(seed), '--budget', str(budget),
                              '--min-score', str(ms), '--exclude', excl_path], capture_output=True, text=True,
                             check=True).stdout.split()
        if out and out[0] == 'OK' and out[3] not in seen:
            words = [(x.split(':')[0], int(x.split(':')[1])) for x in out[6].split(',')]
            if has_repeat([w for w, _ in words]):
                continue  # fill refuses only the identical word twice; a repeated stem is tried at the next floor
            return {'rows': out[3].split('|'), 'min': int(out[4]), 'mean': float(out[5]), 'words': words,
                    'floor': ms, 'reviewedOnly': lexicon == WORDS_OK, 'ms': round((time.time() - t0) * 1000)}
    return None


def main():
    t_all = time.time()
    use = {}
    seen = set()
    results = {5: [], 7: [], 9: []}
    log = []
    excl_path = os.path.join(HERE, 'exclude.tmp')
    kept_from = {5: [], 7: [], 9: []}
    if KEEP:
        reviewed = {line.split()[0] for line in open(WORDS_OK)}
        for p in json.load(open(KEEP))['puzzles']:
            ans = [e['answer'] for e in p['entries']]
            if all(a in reviewed for a in ans) and not has_repeat(ans):
                size = len(p['rows'])
                kept_from[size].append({'rows': p['rows'], 'min': p['minScore'], 'mean': p['meanScore'],
                                        'words': list(p['scores'].items()), 'floor': None,
                                        'reviewedOnly': p.get('reviewedOnly'), 'template': p['template'],
                                        'seed': p['seed'], 'kept': True})
        print('kept from', KEEP, {k: len(v) for k, v in kept_from.items()}, flush=True)
    # carry-overs claim their words before any new fill (all sizes), best minimum score first
    for size in (5, 7, 9):
        want = dict(PLAN[size][1])
        have = {name: 0 for name in want}
        for r in sorted(kept_from[size], key=lambda r: (-r['min'], -r['mean'])):
            if r['template'] not in want or have[r['template']] >= want[r['template']]:
                continue
            key = '|'.join(r['rows'])
            if key in seen or any(use.get(w, 0) >= CAP for w, _ in r['words']):
                continue
            seen.add(key)
            for w, _ in r['words']:
                use[w] = use.get(w, 0) + 1
            have[r['template']] += 1
            results[size].append(r)
    for size in (5, 7, 9):
        target, tpls, budget, max_seeds = PLAN[size]
        jobs = []  # round-robin over templates by weight
        for s in range(max_seeds):
            for name, wgt in tpls:
                jobs.append((name, s + 1))
        want = {name: wgt for name, wgt in tpls}
        have = {name: 0 for name, _ in tpls}
        done = {(r['template'], r['seed']) for r in results[size]}
        for r in results[size]:
            have[r['template']] += 1
        t_size = time.time()
        tries = fails = 0
        i = 0
        while len(results[size]) < target and i < len(jobs):
            batch = []
            while len(batch) < WORKERS and i < len(jobs):
                name, seed = jobs[i]
                i += 1
                if have[name] < want[name] and (name, seed) not in done:
                    batch.append((name, seed))
            if not batch:
                continue
            with open(excl_path, 'w') as f:
                f.write('\n'.join(sorted(w for w, k in use.items() if k >= CAP)))
            with ThreadPoolExecutor(WORKERS) as ex:
                snap = frozenset(seen)
                res = list(ex.map(lambda j: attempt('|'.join(T[j[0]]), j[1] * 7919 + size, budget, excl_path, snap),
                                  batch))
            for (name, seed), r in zip(batch, res):
                tries += 1
                if r is None:
                    fails += 1
                    continue
                key = '|'.join(r['rows'])
                if key in seen or have[name] >= want[name]:
                    continue
                if any(use.get(w, 0) >= CAP for w, _ in r['words']):
                    continue  # a parallel job took a capped word first
                seen.add(key)
                for w, _ in r['words']:
                    use[w] = use.get(w, 0) + 1
                have[name] += 1
                r['template'] = name
                r['seed'] = seed
                results[size].append(r)
                if len(results[size]) >= target:
                    break
        log.append(f"{size}x{size}: {len(results[size])} kept ({sum(1 for r in results[size] if r.get('kept'))} carried over) "
                   f"from {tries} new tries ({fails} failed at every floor) in "
                   f"{time.time() - t_size:.0f} s; per template {have}")
        print(log[-1], flush=True)
    out = []
    for size in (5, 7, 9):
        rs = sorted(results[size], key=lambda r: (-r['min'], -r['mean'], r['template'], r['seed']))
        for k, r in enumerate(rs, 1):
            out.append({'id': f"c{size}-{k:03d}", 'size': f"{size}x{size}", 'template': r['template'],
                        'seed': r['seed'], 'reviewedOnly': r['reviewedOnly'], 'rows': r['rows'], 'minScore': r['min'], 'meanScore': r['mean'],
                        'entries': number(r['rows']),
                        'scores': {w: s for w, s in r['words']}})
    json.dump({'wordlist': 'https://raw.githubusercontent.com/Crossword-Nexus/collaborative-word-list/main/xwordlist.dict',
               'license': 'MIT (Copyright (c) 2021 Crossword-Nexus)', 'cap': CAP, 'log': log,
               'seconds': round(time.time() - t_all), 'puzzles': out}, open(OUT, 'w'), indent=1)
    os.remove(excl_path)
    print(f"wrote {len(out)} candidates to {OUT} in {time.time() - t_all:.0f} s")


if __name__ == '__main__':
    main()
