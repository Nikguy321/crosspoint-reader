# Guide packs

The **Survival** app (Apps, touch readers) reads a *guide pack* from the SD card. This folder holds the survival
guide's source and the device pack built from it.

| Folder | What it holds |
|---|---|
| `survival/` | The source: 12 categories, 86 topics, 228 pages, 77 figure masters, the sources and the open questions |
| `build/survival/` | The device pack the reader reads (made by `scripts/guide/make_pack.py`; do not edit) |

## Status: read this first

The guide was written fresh from current public guidance and **reviewed page by page by AI reviewers - not by a medical
professional or a field expert**. Its `status` is `reviewed-by-ai`; the app says so on its About page (the last row of
its home list), and every first-aid page opens with the reference-only line. It is **reference only, not a substitute for first-aid or survival training**; in an emergency call 911
or send a satellite SOS first. Every question the reviewers left for a human expert is in
[`survival/OPEN-QUESTIONS.md`](survival/OPEN-QUESTIONS.md); until someone qualified answers them, the status stays.

## Sources and licence

The pack text and the 1-bit figures are released under the **MIT License**, like the rest of the repository.

- **Fieldcraft and figures** are adapted and rewritten from U.S. government manuals approved for public release: ATP
  3-50.21 *Survival* (2018) and FM 21-76 *Survival* (1992, the original scan, for line art and signaling); AFH 10-644
  (text only). Their medical procedures were **not** used, nor any combat or evasion content.
- **First aid and hazards** are written fresh from current federal guidance (CDC, NPS, NWS/NOAA, USDA Forest Service,
  FEMA / Ready.gov, EPA and others), each fact credited per topic. Wilderness Medical Society, American Heart
  Association, American Red Cross and Stop the Bleed material, and journal papers, were used **only to check** facts;
  nothing was copied from them.
- **Adapted; not endorsed by the U.S. Army, the U.S. Air Force, CDC, NPS, NOAA or any agency.** No seals, logos or
  insignia are used.

[`survival/ATTRIBUTION.md`](survival/ATTRIBUTION.md) lists every source (edition, distribution statement, URL), which
topics cite it, and where each figure came from. Never use, for this pack: FM 3-05.70, FM 3-50.3, any third-party retype
of FM 21-76, FM 21-76-1's evasion content, wikiHow, Wikibooks, state-agency text, the Survival Codex, or commercial
survival-device text.

## Installing

Copy `build/survival/` to the card as **`/Guides/survival/`**, the whole folder (about 1.6 MB):

```
/Guides/survival/pack.txt
/Guides/survival/categories.tsv
/Guides/survival/topics.tsv
/Guides/survival/search.idx
/Guides/survival/about.txt
/Guides/survival/SHA256SUMS
/Guides/survival/t/<topic>.gp          (86 files)
/Guides/survival/fig/L/<figure>.png    (69 files: the page's size)
/Guides/survival/fig/XL/<figure>.png   (67 files: the full-screen view's size)
```

Over USB with a dev build's bench console: `scripts/x4bench.py push packs/guide/build/survival /Guides/survival`
(add-only: a file already on the card that differs is reported and left alone). The reader's own state - bookmarks,
recent topics, where you were - is kept apart in `/.crosspoint/guide/survival/`, so a newer pack copied over the old one
keeps it (ids the new pack no longer has are dropped).

## Building

```
python3 scripts/guide/validate_guide.py      # the source's rules (make_pack runs it first)
python3 scripts/guide/make_pack.py           # rewrites build/survival/ (needs Pillow for the figures)
python3 scripts/guide/make_pack.py --check   # fails when build/survival/ is stale; re-renders the figures and
                                             # compares their pixels (run it before a commit)
python3 scripts/guide/make_pack.py --check --text-only   # the same without the figures (the ctest guide_pack_current)
```

The build is deterministic (same source, same bytes). Commit `build/survival/` with every source change.

## Source format (`survival/`)

- `guide.txt` - `key=value`: `id`, `title`, `version`, `reviewed`, `status`, `license`, `note`.
- `categories.txt` - one line a category, in the home list's order: `<cat-id> | <TITLE> | <blurb>`.
- `topics/<cat-id>/<NN>-<topic-id>.md` - one file a topic, in order by `NN`. Front matter between `---` lines:
  `title:`, `summary:` (at most 60 characters), `sources:` (`- <name> | <URL or doc + section> | accessed YYYY-MM`),
  `quick: yes|no` (a quick card: one page that fits one screen), `medical: yes|no`. The body is pages separated by a
  line `+++`; each starts `# <page title>`, then an optional figure `![<caption>](<fig-id>)`, then paragraphs,
  `**bold**`, `- ` bullets, `1. ` steps, and lines starting `WARNING:` or `NOTE:` (drawn boxed). About 170 words a
  page (70 with a figure), at most 8 steps; imperial units first, metric in parentheses.
- `figures/masters/<fig-id>.png` and `figures/figures.tsv` (`<fig-id> | <source doc> | <page> | <caption> |
  <threshold> | keep|redraw|drop <note>`): the builder makes each kept master 1-bit and at most 440 x 400.
- `banned.txt` - phrases that must never appear (outdated or dangerous advice); a page may quote a myth only in a
  `WARNING: Do NOT ...` line marked `<!-- myth-ok -->`.
- `synonyms.txt` - search synonym groups (a topic holding one word is also found by the others).

The device pack's format (format 1) is in [docs/file-formats.md](../../docs/file-formats.md#survival-guide).
