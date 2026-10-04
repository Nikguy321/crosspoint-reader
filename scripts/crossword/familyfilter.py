"""The family-friendly filter's loader, shared by prep.py and vocab.py (importing it has no side effects).

The blocklist itself is NOT in this repository, on purpose: a list that filters offensive words has to spell them
out. It is read from $CW_BLOCKLIST, else scripts/crossword/blocklist.txt (gitignored, with every name containing
'blocklist'). Format: '#' starts a comment; a line starting with '*' is a substring rule, '*PART [STEM ...]',
dropping every word that contains PART unless it contains one of the STEMs (innocent words such as SKILL or SPICE);
any other line lists whole words to drop.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def path():
    return os.environ.get('CW_BLOCKLIST') or os.path.join(HERE, 'blocklist.txt')


def warn_missing(where, consequence):
    bar = '!' * 78
    print(f"{bar}\nWARNING: no blocklist at {where}\n  {consequence}\n"
          "  Set CW_BLOCKLIST=/path/to/blocklist.txt (kept outside the repository; format in familyfilter.py)\n"
          f"  or review every candidate by hand before it goes near builtin.txt.\n{bar}", file=sys.stderr)


class Filter:
    def __init__(self, words=(), subs=(), source=None):
        self.words = set(words)
        self.subs = list(subs)
        self.source = source  # the file read, or None when there was none

    def ok_sub(self, w):
        """False when a substring rule drops w."""
        for part, stems in self.subs:
            if part in w and not any(stem in w for stem in stems):
                return False
        return True

    def blocked(self, w):
        return w in self.words or not self.ok_sub(w)


def load():
    """The blocklist as a Filter (an empty one, source None, when there is no file; the caller warns)."""
    where = path()
    if not os.path.exists(where):
        return Filter()
    words, subs = set(), []
    with open(where, encoding='utf-8') as f:
        for line in f:
            line = line.split('#', 1)[0].strip().upper()
            if not line:
                continue
            if line.startswith('*'):
                part, *stems = line[1:].split()
                subs.append((part, tuple(stems)))
            else:
                words.update(line.split())
    return Filter(words, subs, where)
