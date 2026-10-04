#!/usr/bin/env python3
"""Prepare the fill lexicon from the MIT Collaborative Word List.

Input : wordlist/xwordlist.dict (or $CW_WORDLIST)  (word;score, downloaded at run time from
        https://raw.githubusercontent.com/Crossword-Nexus/collaborative-word-list/main/xwordlist.dict)
Output: words.txt  ("WORD score" per line, A-Z only, 3..9 letters, score >= MIN_SCORE, minus the
        blocklist and deny.txt), words_ok.txt and words_hybrid.txt (see README.md).

The family-friendly BLOCKLIST is not in this repository, on purpose: a list that filters offensive
words has to spell them out. familyfilter.py reads it ($CW_BLOCKLIST, else
scripts/crossword/blocklist.txt, gitignored) and gives its format. Without it the run goes on,
loudly warned, and every fill needs a careful human review.

deny.txt and allow3.txt (written in review) and ok.txt (reviewed, appended by vocab.py --ok) are
words picked from the same Collaborative Word List, kept outside the repository ($CW_LISTS, README.md); no other outside word
data is used.
"""
import os, re, sys

import familyfilter

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get('CW_WORDLIST') or os.path.join(HERE, 'wordlist', 'xwordlist.dict')
OUT = os.path.join(HERE, 'words.txt')
MIN_SCORE = int(sys.argv[1]) if len(sys.argv) > 1 else 50


def load_blocklist():
    """The family-friendly filter (familyfilter.py), loudly warned when there is none."""
    flt = familyfilter.load()
    if flt.source is None:
        familyfilter.warn_missing(familyfilter.path(),
                                  "The family-friendly filter is OFF: offensive words can reach words.txt and the fills.")
    else:
        print(f"blocklist: {flt.source} ({len(flt.words)} words, {len(flt.subs)} substrings)")
    return flt


# The review lists (deny.txt, allow3.txt, ok.txt) are the owner's working files, kept outside the repository with
# the blocklist: $CW_LISTS, else scripts/crossword/lists/ (gitignored).
LISTS = os.environ.get('CW_LISTS') or os.path.join(HERE, 'lists')


def load_list(name):
    path = os.path.join(LISTS, name)
    out = set()
    if os.path.exists(path):
        for line in open(path):
            line = line.split('#', 1)[0]
            out.update(w.strip().upper() for w in line.split())
    return out


def load_all():
    allw = set()
    with open(SRC, encoding='utf-8', errors='replace') as f:
        for line in f:
            w = line.strip().rpartition(';')[0]
            if re.fullmatch(r'[A-Z]+', w):
                allw.add(w)
    return allw


def family(w, allw):
    """Inflection/derivation relatives of w that the full list also holds (a single real word usually has some; a
    name or a run-together phrase usually has none)."""
    c = set()
    for suf in ('S', 'ES', 'ED', 'D', 'ING', 'ER', 'ERS', 'LY', 'NESS', 'AL'):
        c.add(w + suf)
    for suf in ('S', 'ES', 'ED', 'D', 'ING', 'ER', 'ERS', 'LY', 'Y'):
        if w.endswith(suf) and len(w) - len(suf) >= 3:
            st = w[:-len(suf)]
            c.update((st, st + 'E', st + 'S', st + 'ED', st + 'ING', st + 'ER'))
            if len(st) >= 2 and st[-1] == st[-2]:
                c.update((st[:-1], st[:-1] + 'S'))
    if w.endswith('E'):
        c.update((w[:-1] + 'ING', w + 'D', w + 'R'))
    if w.endswith('Y'):
        c.update((w[:-1] + 'IES', w[:-1] + 'IED', w[:-1] + 'IER'))
    c.discard(w)
    return [x for x in c if x in allw]


def main():
    flt = load_blocklist()
    allw = load_all()
    ok_reviewed = load_list('ok.txt')  # words already approved by review skip the family test
    n_fam = 0
    allow3 = load_list('allow3.txt')   # the only 3-letter words allowed (picked in review)
    deny = load_list('deny.txt')       # picked in review: names, crosswordese, abbreviations, odd partials
    kept = []
    n_in = n_shape = n_score = n_block = 0
    with open(SRC, encoding='utf-8', errors='replace') as f:
        for line in f:
            n_in += 1
            w, _, s = line.strip().rpartition(';')
            if not re.fullmatch(r'[A-Z]{3,9}', w):
                continue
            n_shape += 1
            try:
                s = int(s)
            except ValueError:
                continue
            if s < MIN_SCORE:
                continue
            n_score += 1
            if flt.blocked(w) or w in deny or (len(w) == 3 and w not in allow3):
                n_block += 1
                continue
            if len(w) >= 6 and w not in ok_reviewed and not family(w, allw):
                n_fam += 1
                continue
            kept.append((w, s))
    kept.sort(key=lambda t: (len(t[0]), -t[1], t[0]))
    with open(OUT, 'w') as f:
        for w, s in kept:
            # third field = ordering key for fill: reviewed words (ok.txt / allow3.txt) sort one band higher
            f.write(f"{w} {s} {s + (10 if (w in ok_reviewed or len(w) == 3) else 0)}\n")
    # words_ok.txt: only hand-reviewed words (ok.txt + allow3.txt) - the strict lexicon the driver tries first
    with open(os.path.join(HERE, 'words_ok.txt'), 'w') as f:
        for w, s in kept:
            if w in ok_reviewed or len(w) == 3:
                f.write(f"{w} {s}\n")
    # words_hybrid.txt: every kept word of 3..6 letters, but only hand-reviewed words of 7+ letters (long entries in the
    # list are mostly run-together phrases and names, so the driver tries this before the full list)
    with open(os.path.join(HERE, 'words_hybrid.txt'), 'w') as f:
        for w, s in kept:
            if len(w) <= 6 or w in ok_reviewed:
                f.write(f"{w} {s} {s + (10 if (w in ok_reviewed or len(w) == 3) else 0)}\n")
    by = {}
    for w, s in kept:
        by[len(w)] = by.get(len(w), 0) + 1
    print(f"lines {n_in}; A-Z 3..9 {n_shape}; score>={MIN_SCORE} {n_score}; blocked {n_block}; no-family (len>=6) {n_fam}; kept {len(kept)}")
    print("by length:", dict(sorted(by.items())))
    if flt.source is None:
        print("WARNING: these lists were made WITHOUT a blocklist (see the warning above).", file=sys.stderr)


if __name__ == '__main__':
    main()
