# Crossword built-in puzzles and the tools that made them

The Crossword app ships with 48 puzzles in flash: 30 minis (5x5, `mini-001`..`mini-030`), 12 midis (7x7,
`midi-001`..`midi-012`) and 6 maxis (9x9, `maxi-001`..`maxi-006`), 678 clues in all. They are original: every grid was
filled on a computer by the tools in this folder and every clue was written fresh for this repository. **No published
puzzle or clue was used anywhere**, not as a source, a seed or a check (the test fixtures in `test/crossword/fixtures`
are made by `test/crossword/make_fixtures.py` from our own puzzles and synthetic grids). Nothing in this folder runs on
the device.

## Files

| File | What it is |
|---|---|
| `builtin.txt` | The source of truth: one block per puzzle (format below, and in `lib/Crossword/CwText.h`). |
| `gen_builtin.py` | The validator: checks `builtin.txt` and writes `lib/Crossword/CwBuiltinData.cpp` (generated, committed). |
| `../../lib/Crossword/CwBuiltinData.cpp` | Generated: one `{id, title, w, h, fnv, text}` per puzzle. Do not edit. |
| `prep.py` | Filters the downloaded word list into the fill lexicons. |
| `fill.cpp` | `fill`, a deterministic backtracking grid filler (C++17, built on the Mac). |
| `driver.py` | Runs `fill` over the templates and seeds; writes candidate grids (JSON). |
| `templates.json`, `templates.py`, `search9.py` | The block patterns, their checker, and pattern searches. |
| `vocab.py` | Lists the answers in a candidate file that nobody has reviewed yet. |
| `familyfilter.py` | Reads the private blocklist (below) for `prep.py` and `vocab.py`. |
| `lists/deny.txt`, `lists/allow3.txt`, `lists/ok.txt` | Review lists (not in the repository, below): words picked from the word list in review. |

## Format

```
# a comment: '#' then a space (or '#' alone); blank lines are ignored
=== mini-001 | Mini 1
##MOO
STORM
LANCE
OCEAN
TOY##
A 1 Sound from a dairy cow
D 1 Coins and bills
...
```

`=== <id> | <title>`, then the grid rows (`A`-`Z` and `#` for a block, 3 to 15 squares a side, all rows the same
width), then one `A <n> <clue>` line per Across entry and one `D <n> <clue>` per Down entry. Numbers follow the
standard rule (row by row, a square that starts an Across or Down run of 2 or more gets the next number).

**Comments:** a comment is exactly `#` alone or `#` followed by a space. Any other line is read as it stands, so a
line made only of letters and `#` is a grid row wherever it appears: a `#####` divider or `#NOTE` inside a block is a
row (and breaks the grid or its clues), and `#note` is an error. Always write comments as `# text`. The generator and
the firmware's parser (`cw::parseTextPuzzle`) follow this one rule.

## Making and changing puzzles

1. **Fill the grid on the computer** with the tools below (`prep.py`, `fill`, `driver.py`): blocks in 180-degree
   rotational symmetry, every entry 3 letters or more, every square checked Across and Down.
2. **Pick candidates by hand** (a variety of templates, no answer more than twice across the whole set, no word from
   `deny.txt`).
3. **Write the clues fresh.** Rules:
   - Common, family-friendly words only: no crosswordese, no abbreviations unless everyday, no proper nouns except
     very well-known places. No answer used more than twice across the whole set.
   - Monday-Tuesday difficulty, accurate, 60 characters or fewer preferred (160 at most, ASCII only); every built-in
     clue fits the clue bar whole (the host tests check it with the real fonts).
   - A clue never contains its answer, or the answer less a trailing S / ED / ING (the generator checks this).
   - No quotations from copyrighted works and no real living people.
   - Every puzzle is checked by a second person (or an independent verifier) before it goes in.
4. **Add the block to `builtin.txt`** with a new unique id (`[a-z0-9-]{1,24}`; the id picks the puzzle, while saved
   progress and the solved mark belong to the solution's fnv, so giving an id new content starts it fresh and
   unsolved).
5. **Regenerate and check:**

   ```
   python3 scripts/crossword/gen_builtin.py          # validate, then write CwBuiltinData.cpp
   python3 scripts/crossword/gen_builtin.py --check  # validate only; fails when the .cpp is stale (run by ctest)
   python3 scripts/crossword/gen_builtin.py --notes  # also list content notes (never fatal)
   ```

   The generator refuses a file with a ragged or oversized grid, a white square in no entry, a missing, extra or
   duplicate clue, a duplicate id or puzzle, a title over 48 bytes, or a clue that gives its answer away. `--notes`
   adds what the rules prefer: blocks not in rotational symmetry, entries under 3 letters, an answer used more than
   twice, clues over 60 characters, and one answer repeated inside another in the same puzzle (APE and APES, UNIT and
   UNITE). The host tests parse every built-in with the firmware's parser and pin the fingerprints of `mini-001` and
   `maxi-006` (`test/crossword`), and check every clue against the clue bar with the real fonts
   (`test/crossword_preview`); the rule and drawing tests use their own samples (`test/crossword/samples.h`), never the
   shipped set.

## The fill tools

### Word list (the only outside data)

Fill candidates come from the **Collaborative Word List** by Crossword Nexus, MIT License
(Copyright (c) 2021 Crossword-Nexus): <https://github.com/Crossword-Nexus/collaborative-word-list>.
It is downloaded at run time into `wordlist/` and never committed (`.gitignore`):

```sh
cd scripts/crossword
mkdir -p wordlist
curl -fsSL -o wordlist/xwordlist.dict \
  https://raw.githubusercontent.com/Crossword-Nexus/collaborative-word-list/main/xwordlist.dict
curl -fsSL -o wordlist/LICENSE \
  https://raw.githubusercontent.com/Crossword-Nexus/collaborative-word-list/main/LICENSE
```

No other word, frequency or clue data is used: no published puzzles and no clue databases. Nothing derived from
FrequencyWords, wordfreq, SUBTLEX, Spread the Wordlist, Wiktionary, xd / New York Times data, the GNOME Crosswords
puzzle sets or Crosshare may be used, ever: those are ShareAlike, non-commercial or copyrighted. Only the finished
grids and these tools enter the repository: the word list, the review lists and the blocklist stay outside it.

### The review lists (kept outside the repository too)

`deny.txt`, `allow3.txt` and `ok.txt` are the owner's working files from review, words picked out of the downloaded
Collaborative Word List. They are read from `$CW_LISTS`, else `scripts/crossword/lists/` (gitignored); `vocab.py --ok`
appends to `ok.txt` there. Without them `prep.py` still runs, with no reviewed words and no denied ones, so a fill needs
a full review.

### The blocklist (kept outside the repository, on purpose)

A filter for offensive words has to spell them out, so the family-friendly **blocklist is not in this repository**.
The owner keeps it elsewhere and points `prep.py` and `vocab.py` at it (`familyfilter.py` reads it):

```sh
CW_BLOCKLIST=/path/outside/the/repo/blocklist.txt python3 prep.py
```

(`scripts/crossword/blocklist.txt` is read too when present; every name in this folder containing `blocklist`, its
editor swap and backup files included, is gitignored.) Format: `#` starts a comment; a line
starting with `*` is a substring rule, `*PART [STEM ...]`, that drops every word containing `PART` unless it contains
one of the innocent `STEM`s (so a rule can spare SKILL or SPICE); any other line lists whole words to drop. Without a
blocklist `prep.py` still runs but prints a loud warning, and every fill then needs a careful human review;
`vocab.py --ok` refuses to run without one (`--no-blocklist` overrides it after a full hand review), and with one it
never lists or approves a blocklisted word, so an offensive word can never reach `ok.txt`.

### Filters (prep.py)

`prep.py [min_score]` reads `wordlist/xwordlist.dict` (`WORD;score`; override the path with `CW_WORDLIST=...`) and
writes three lexicons:

- keeps A-Z words of 3..9 letters with score >= 50 (default);
- drops the blocklist's words and substrings (above);
- `deny.txt` (picked in review): names, brands, abbreviations, crosswordese, partials, archaic/foreign words, words for
  real peoples and faiths (in their own section), run-together phrases. A word there is not judged offensive, only
  unsuited to a light mini;
- `allow3.txt` (picked in review): the ONLY 3-letter answers allowed (everyday words);
- words of 6+ letters must have an inflection/derivation relative in the list (drops run-together phrases and names)
  unless they are in `ok.txt`;
- `ok.txt` (reviewed, appended by `vocab.py --ok`): words already reviewed and approved; they sort one score band
  higher.

Outputs (gitignored): `words.txt` (all kept), `words_hybrid.txt` (3..6 letters + reviewed longer words),
`words_ok.txt` (reviewed only).

### Fill (fill.cpp, driver.py)

```sh
cd scripts/crossword
clang++ -O2 -std=c++17 -o fill fill.cpp
export CW_BLOCKLIST=/path/to/blocklist.txt CW_LISTS=/path/to/lists   # both outside the repository
python3 prep.py
python3 driver.py candidates.json            # deterministic; minutes on 3 workers
python3 vocab.py candidates.json             # list answers not yet reviewed
# review them by hand: bad ones -> deny.txt, offensive ones -> your blocklist
# (vocab.py --ok never approves a blocklisted word), then
python3 vocab.py candidates.json --ok        # approve the rest into ok.txt; rerun prep.py + driver.py --keep
```

- `fill` is a deterministic backtracking filler (MRV + forward checking, splitmix32 seed, high-score-first bands,
  no repeated word in a grid, node budget): `./fill --words words.txt --grid "#....|.....|.....|.....|....#" --seed 7`.
- `templates.json` holds the block patterns: square, 180-degree rotational symmetry, every run >= 3, connected.
  `python3 templates.py check` validates them; `templates.py search` / `search9.py` look for new 7x7 / 9x9 patterns.
- `driver.py` fills each template over many seeds at a falling minimum score (reviewed lexicon first), caps how often
  any word appears across candidates, skips identical grids, and rejects a grid where one entry repeats another or
  its stem (APE / APES, SNAPPED / SNAPPER).

The candidate files, the binary and the lexicons are all gitignored; only the picked grids, with clues written fresh,
go into `builtin.txt`.

## Flash budget

Each 5x5 costs roughly 0.35 KB of flash, a 7x7 0.5 KB, a 9x9 0.9 KB and a 15x15 about 3 KB; the 48 built-ins take
about 21 KB. The public OTA build (`x4pro-gh_release`) fills a 6.25 MB slot; check its size after adding many puzzles.
