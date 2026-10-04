#!/usr/bin/env python3
"""List words used in a candidates json that have not been reviewed yet (not in ok.txt / deny.txt / allow3.txt).
  python3 vocab.py round.json          # print unreviewed words by length
  python3 vocab.py round.json --ok     # after review: append every unreviewed word NOT in deny.txt to ok.txt

Words the private blocklist drops (familyfilter.py) are never listed and never appended to ok.txt.
`--ok` refuses to run without a blocklist; `--no-blocklist` overrides that after a full hand review.
"""
import json, os, sys

import familyfilter

HERE = os.path.dirname(os.path.abspath(__file__))
# The review lists live outside the repository (README.md): $CW_LISTS, else scripts/crossword/lists/ (gitignored).
LISTS = os.environ.get('CW_LISTS') or os.path.join(HERE, 'lists')


def load(name):
    p = os.path.join(LISTS, name); s = set()
    if os.path.exists(p):
        for line in open(p): s.update(line.split('#', 1)[0].split())
    return s


flt = familyfilter.load()
if flt.source is None:
    if '--ok' in sys.argv and '--no-blocklist' not in sys.argv:
        familyfilter.warn_missing(familyfilter.path(),
                                  "vocab.py --ok REFUSES to append to ok.txt without one.\n"
                                  "  Pass --no-blocklist only after checking every listed word by hand.")
        sys.exit(1)
    familyfilter.warn_missing(familyfilter.path(), "Offensive words are NOT left out of this listing.")
data = json.load(open(sys.argv[1]))
ok, deny, a3 = load('ok.txt'), load('deny.txt'), load('allow3.txt')
words = sorted({e['answer'] for p in data['puzzles'] for e in p['entries']})
blocked = [w for w in words if flt.blocked(w)]
new = [w for w in words if w not in ok and w not in deny and w not in a3 and not flt.blocked(w)]
if '--ok' in sys.argv:
    os.makedirs(LISTS, exist_ok=True)
    with open(os.path.join(LISTS, 'ok.txt'), 'a') as f:
        f.write(' '.join(new) + '\n')
    print(f"appended {len(new)} reviewed words to ok.txt ({len(blocked)} blocklisted words left out)")
else:
    print(f"{len(words)} distinct, {len(new)} unreviewed, {len(blocked)} blocklisted (not listed)")
    for L in range(4, 10):
        ws = [w for w in new if len(w) == L]
        if ws: print(L, ' '.join(ws))
