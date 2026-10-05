# Puzzle packs

Extra puzzles for the Crossword and Word Search apps, to copy onto the SD card. Everything here is **original work
made for this fork and released under the MIT License**, like the rest of the repository: every crossword grid was
filled on a computer by the tools in `scripts/crossword/` (fill candidates from the MIT-licensed Collaborative Word
List, credited in `scripts/crossword/README.md`), and every clue and every Word Search word list was written fresh for
this repository. No published puzzle or clue was used, as a source or as a check.

## What is here

| Folder | What it holds |
|---|---|
| `crossword/Minis 2/` | 50 mini crosswords, 5x5 (`mini-031` .. `mini-080`) |
| `crossword/Midis 2/` | 27 midi crosswords, 7x7 (`midi-013` .. `midi-039`) |
| `crossword/Maxis 2/` | 20 maxi crosswords, 9x9 (`maxi-007` .. `maxi-026`) |
| `wordsearch/*.words` | 40 Word Search themes (dog breeds, desserts, sailing, Greek myths, ...), 44 to 50 words each |

The numbering continues the 48 puzzles built into the firmware, and no pack puzzle repeats a built-in one. Each
crossword is an `.ipuz` file (ipuz is a trademark of Puzzazz, Inc., used with permission); each crossword folder
also keeps its `source.txt`, the same block format as the built-ins (`scripts/crossword/README.md`), which the reader
ignores. The `.words` files are plain text: a title line, then one word or phrase a line (format in
`lib/WordSearch/WsThemes.h`).

## Installing

Copy onto the SD card:

- **Crosswords:** copy each pack *folder* (for example `Minis 2`) into `/Puzzles/Crossword/` on the card, giving
  `/Puzzles/Crossword/Minis 2/mini-031.ipuz` and so on. Each folder shows up in the Crossword app as a pack named
  after the folder. The reader lists up to 200 puzzles a folder (and 32 packs); `.ipuz` files loose in
  `/Puzzles/Crossword/` show up as "On the card".
- **Word Search:** copy the `.words` files into `/Puzzles/WordSearch/` on the card. They show up in the theme list
  beside the built-in themes. The reader lists up to 64 theme files.

Create the folders if the card does not have them yet. A puzzle in progress follows its solution, so moving or
renaming its file keeps the progress; the solved marks go by the file's path, so a renamed file shows as unsolved.

A later update (the puzzle downloader, chunk 3 of the puzzle work) will fetch these packs over Wi-Fi from this
repository, so copying by hand is only needed until then.

## Changing a pack

Crosswords: edit the pack's `source.txt`, then run

```
python3 scripts/crossword/make_packs.py          # validate every pack, then rewrite its .ipuz files
python3 scripts/crossword/make_packs.py --check  # validate only; fails when an .ipuz is stale (run by ctest)
```

`make_packs.py` applies the built-in rules (`scripts/crossword/gen_builtin.py`: every square in an entry, one clue
per entry, ASCII clues that do not give their answer away, unique ids) and also refuses an id or solution that
repeats another pack's or a built-in's. A new pack is a new folder under `crossword/` holding a `source.txt`.

The host tests (`test/crossword`, `test/word_search`) read every pack file with the firmware's own parsers: each
`.ipuz` must load with the size its id names (mini 5x5, midi 7x7, maxi 9x9) and a clue for every entry, with no
solution shared with another pack puzzle or a built-in; each `.words` file must keep every word line and fill a
puzzle at every difficulty.
