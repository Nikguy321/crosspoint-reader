#!/usr/bin/env python3
"""Validate the survival guide source pack (packs/guide/survival/ format, spec 2026-10-04).

Usage: python3 scripts/guide/validate_guide.py [PACK_DIR]   (default: packs/guide/survival in this repository)
Exit 0 when there are no ERRORs (WARNs are printed but do not fail).

Checks
- guide.txt: required keys and values.
- categories.txt: `<id> | <TITLE> | <blurb>`, id pattern, unique, each has a topics/<id>/ folder with >= 1 topic,
  no topic folder without a category.
- topics/<cat>/<NN>-<topic-id>.md: NN consecutive from 01, ids match patterns, front matter
  (title, summary <= 60 chars, sources >= 1 in `- name | URL or doc+section | accessed YYYY-MM` form,
  quick yes|no, medical yes|no; `key: value` split on the FIRST ': ' only), quick => one page,
  first-aid => medical yes, medical => the reference-only NOTE on every page; a page of a medical: no topic that
  mentions a medical term (CPR, tourniquet, frostbite, heat stroke, hypothermia, ...) carries the same NOTE.
- Pages (split on a line `+++`): `# title` first, optional ONE figure line right after it, then only the Markdown
  subset (paragraphs, **bold**, `- ` bullets one level, `N. ` steps, `WARNING:` / `NOTE:` lines); no tables, links,
  HTML (except a trailing `<!-- myth-ok -->` on a WARNING line), nesting, headings, code, single-star emphasis,
  URLs; ASCII only. Word budget: <= 70 with a figure, <= 170 without; <= 8 steps per page; steps numbered 1..n.
- Figures: every referenced id matches [a-z0-9-]{1,32}, has a master PNG and a figures.tsv row, and is not marked
  drop or redraw. figures.tsv rows: 6 fields, threshold 0-255, id unique, master exists; WARN on unreferenced
  keep rows, masters without rows, and per-category figures-*.tsv rows that differ from figures.tsv.
- banned.txt phrases nowhere in any pack text file (case-insensitive); only exception: a page line starting
  `WARNING:` and ending `<!-- myth-ok -->`. Plus the feasibility study's page-scoped words (snakebite, tourniquet,
  frostbite, burns pages), where `WARNING: Do NOT ...` lines are exempt and a negated use in the same clause
  ("do not rub") is reported as a WARN for a human to confirm.
- Sources: URLs only on federal/state .gov hosts, or check-only references (Wilderness Medical Society and its journal
  WEM by DOI, AHA, Red Cross, the American College of Surgeons Stop the Bleed program, and PubMed records), each
  labelled "checked only" (facts checked against them, nothing copied); never a forbidden source (FM 3-05.70, FM 3-50.3, the V-tech retype, wikiHow, Wikibooks, Survival Codex).
"""
import os
import re
import sys
from urllib.parse import urlparse

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
PACK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'packs', 'guide', 'survival')

CAT_ID = re.compile(r'^[a-z0-9-]{1,20}$')
TOPIC_FILE = re.compile(r'^(\d{2})-([a-z0-9-]{1,32})\.md$')
FIG_ID = re.compile(r'^[a-z0-9-]{1,32}$')
FIG_LINE = re.compile(r'^!\[([^\[\]]+)\]\(([^()\s]+)\)$')
SOURCE_LINE = re.compile(r'^- (.+?) \| (.+) \| accessed \d{4}-\d{2}$')
REF_NOTE = 'NOTE: Reference only - not a substitute for first-aid training. Call for help first.'
MYTH = '<!-- myth-ok -->'
WORDS_FIG, WORDS_TEXT, MAX_STEPS, MAX_SUMMARY = 70, 170, 8, 60

# Check-only references: Wilderness Medical Society, AHA, Red Cross, and the American College of Surgeons Stop the
# Bleed program (wound packing; no federal page covers it). Facts are checked against them; nothing is copied.
CHECK_HOSTS = ('wms.org', 'wemjournal.org', 'heart.org', 'ahajournals.org', 'redcross.org', 'stopthebleed.org')
# Hosts that pass the .gov rule but whose content is a journal paper (e.g. AAP, WMS): also check-only.
CHECK_ONLY_GOV_HOSTS = ('pubmed.ncbi.nlm.nih.gov',)
# WMS's journal Wilderness & Environmental Medicine on SAGE: new DOIs 10.1177/1080603..., pre-2024 ones 10.1016/j.wem.
WEM_DOI = re.compile(r'/10\.1177/1080603|/10\.1016/j\.wem\.')
CHECK_LABEL = 'checked only'
FORBIDDEN_SOURCES = [r'fm ?3-05\.70', r'fm ?3-50\.3', r'v-?tech', r'wikihow', r'wikibooks', r'survival codex',
                     r'\bsc-01\b']
# Page-scoped words from feasibility.txt SAFETY section 4 (only "WARNING: Do NOT" lines are exempt).
SCOPED = [
    ('first-aid/09-snakebite', [r'incision', r'\bsuck', r'suction', r'\bice\b', r'tourniquet']),
    ('first-aid/03-bleeding-and-shock', [r'\bloosen']),
    ('emergency/05-life-threats', [r'\bloosen']),
    ('first-aid/07-cold-injuries', [r'\brub']),
    ('first-aid/04-wounds-and-burns', [r'\bbutter']),
]
MEDICAL_HINTS = [r'\bCPR\b', r'tourniquet', r'frostbite', r'heat stroke', r'hypothermia', r'antivenom',
                 r'rescue breaths', r'\bsplint']

errors, warns = [], []
stats = {'categories': 0, 'topics': 0, 'pages': 0, 'pages_fig': 0, 'words': 0, 'fig_refs': 0,
         'quick': 0, 'medical': 0, 'sources': 0}


def err(where, msg):
    errors.append(f'ERROR {where}: {msg}')


def warn(where, msg):
    warns.append(f'WARN  {where}: {msg}')


def rel(p):
    return os.path.relpath(p, PACK)


def count_words(lines):
    n = 0
    for l in lines:
        l = l.replace(MYTH, '').replace('**', '')
        l = re.sub(r'^(- |\d+\. )', '', l)
        n += sum(1 for t in l.split() if re.search(r'[A-Za-z0-9]', t))
    return n


def load_banned():
    p = os.path.join(PACK, 'banned.txt')
    if not os.path.exists(p):
        err('banned.txt', 'missing')
        return []
    out = [l.strip().lower() for l in open(p, encoding='utf-8') if l.strip() and not l.lstrip().startswith('#')]
    if not out:
        err('banned.txt', 'no phrases')
    return out


def check_banned_text(where, text, banned, page_lines=False):
    for i, line in enumerate(text.split('\n'), 1):
        low = line.lower()
        for b in banned:
            if b in low:
                if page_lines and line.startswith('WARNING:') and line.rstrip().endswith(MYTH):
                    continue
                err(f'{where}:{i}', f'banned phrase "{b}": {line.strip()[:90]}')


def check_guide():
    p = os.path.join(PACK, 'guide.txt')
    if not os.path.exists(p):
        return err('guide.txt', 'missing')
    kv = {}
    for i, l in enumerate(open(p, encoding='utf-8'), 1):
        l = l.rstrip('\n')
        if not l.strip() or l.startswith('#'):
            continue
        if '=' not in l:
            err(f'guide.txt:{i}', f'not key=value: {l}')
            continue
        k, v = l.split('=', 1)
        if k in kv:
            err(f'guide.txt:{i}', f'duplicate key {k}')
        kv[k] = v
    want = {'id': 'survival', 'title': 'Survival Guide', 'version': None, 'reviewed': None,
            'status': 'reviewed-by-ai', 'license': 'MIT', 'note': None}
    for k, v in want.items():
        if k not in kv or not kv[k].strip():
            err('guide.txt', f'missing key {k}')
        elif v is not None and kv[k] != v:
            err('guide.txt', f'{k}={kv[k]} (expected {v})')
    if 'version' in kv and not kv['version'].isdigit():
        err('guide.txt', 'version must be an integer')
    if 'reviewed' in kv and not re.match(r'^\d{4}-\d{2}$', kv['reviewed']):
        err('guide.txt', 'reviewed must be YYYY-MM')
    if 'note' in kv and ('Reference only' not in kv['note'] or '911' not in kv['note']):
        err('guide.txt', 'note must say "Reference only" and mention 911')


def check_categories():
    p = os.path.join(PACK, 'categories.txt')
    cats = []
    if not os.path.exists(p):
        err('categories.txt', 'missing')
        return cats
    for i, l in enumerate(open(p, encoding='utf-8'), 1):
        l = l.rstrip('\n')
        if not l.strip():
            continue
        parts = l.split(' | ')
        if len(parts) != 3 or not all(x.strip() for x in parts):
            err(f'categories.txt:{i}', f'need "<id> | <TITLE> | <blurb>": {l}')
            continue
        cid, title, blurb = parts
        if not CAT_ID.match(cid):
            err(f'categories.txt:{i}', f'bad id {cid!r}')
        if title != title.upper():
            err(f'categories.txt:{i}', f'title not upper case: {title}')
        if cid in [c[0] for c in cats]:
            err(f'categories.txt:{i}', f'duplicate id {cid}')
        if len(blurb) > 60:
            warn(f'categories.txt:{i}', f'blurb {len(blurb)} chars (> 60)')
        if re.search(r'[^\x20-\x7e]', l):
            err(f'categories.txt:{i}', 'non-ASCII')
        cats.append((cid, title, blurb))
    if cats and cats[0][0] != 'emergency':
        err('categories.txt', 'EMERGENCY must be the first category')
    tdir = os.path.join(PACK, 'topics')
    for d in sorted(os.listdir(tdir)) if os.path.isdir(tdir) else []:
        if os.path.isdir(os.path.join(tdir, d)) and d not in [c[0] for c in cats]:
            err(f'topics/{d}', 'folder has no category line')
    return cats


def load_figures():
    fdir = os.path.join(PACK, 'figures')
    p = os.path.join(fdir, 'figures.tsv')
    figs = {}
    if not os.path.exists(p):
        err('figures/figures.tsv', 'missing')
        return figs
    for i, l in enumerate(open(p, encoding='utf-8'), 1):
        l = l.rstrip('\n')
        if not l.strip():
            continue
        parts = l.split(' | ')
        where = f'figures.tsv:{i}'
        if len(parts) != 6:
            err(where, f'{len(parts)} fields (want 6): {l[:80]}')
            continue
        fid, doc, page, cap, thr, note = parts
        if not FIG_ID.match(fid):
            err(where, f'bad figure id {fid!r}')
        if fid in figs:
            err(where, f'duplicate figure id {fid}')
        if not (thr.isdigit() and 0 <= int(thr) <= 255):
            err(where, f'threshold {thr!r} not 0-255')
        status = note.split()[0].rstrip(';,') if note.strip() else ''
        if status not in ('keep', 'redraw', 'drop'):
            err(where, f'note must start keep|redraw|drop: {note[:40]}')
        if not (doc.startswith('ATP 3-50.21') or doc.startswith('FM 21-76 (5 Jun 1992')):
            err(where, f'figure source must be ATP 3-50.21 or the 1992 FM 21-76 scan: {doc}')
        if not os.path.exists(os.path.join(fdir, 'masters', fid + '.png')):
            err(where, f'master figures/masters/{fid}.png missing')
        figs[fid] = {'doc': doc, 'page': page, 'cap': cap, 'thr': thr, 'note': note, 'status': status,
                     'used': []}
    mdir = os.path.join(fdir, 'masters')
    for m in sorted(os.listdir(mdir)) if os.path.isdir(mdir) else []:
        if m.endswith('.png') and m[:-4] not in figs:
            warn(f'figures/masters/{m}', 'no figures.tsv row')
    # per-category tsvs must agree with the merged one
    for f in sorted(os.listdir(fdir)):
        if f.startswith('figures-') and f.endswith('.tsv'):
            for i, l in enumerate(open(os.path.join(fdir, f), encoding='utf-8'), 1):
                l = l.rstrip('\n')
                if not l.strip():
                    continue
                fid = l.split(' | ')[0]
                if fid not in figs:
                    warn(f'figures/{f}:{i}', f'{fid} not in figures.tsv')
                elif l != ' | '.join([fid, figs[fid]['doc'], figs[fid]['page'], figs[fid]['cap'],
                                      figs[fid]['thr'], figs[fid]['note']]):
                    warn(f'figures/{f}:{i}', f'{fid} row differs from figures.tsv')
    return figs


def parse_front(path, text):
    if not text.startswith('---\n'):
        err(path, 'no front matter')
        return None, None
    end = text.find('\n---\n', 4)
    if end < 0:
        err(path, 'front matter not closed')
        return None, None
    fm_lines = text[4:end].split('\n')
    body = text[end + 5:]
    fm, sources, key = {}, [], None
    for i, l in enumerate(fm_lines, 2):
        if l.startswith('- '):
            if key != 'sources':
                err(f'{path}:{i}', f'list item outside sources: {l[:60]}')
            sources.append((i, l))
            continue
        if ': ' in l or l.endswith(':'):
            k, v = (l.split(': ', 1) + [''])[:2] if ': ' in l else (l[:-1], '')
            if k in fm:
                err(f'{path}:{i}', f'duplicate key {k}')
            fm[k] = v.strip()
            key = k
            continue
        err(f'{path}:{i}', f'bad front matter line: {l[:60]}')
    return (fm, sources), body


def check_sources(path, sources):
    if not sources:
        err(path, 'no sources')
    for i, l in sources:
        where = f'{path}:{i}'
        m = SOURCE_LINE.match(l)
        if not m:
            err(where, f'source not "- name | URL or doc+section | accessed YYYY-MM": {l[:80]}')
        low = l.lower()
        for fs in FORBIDDEN_SOURCES:
            if re.search(fs, low):
                err(where, f'forbidden source ({fs}): {l[:80]}')
        for url in re.findall(r'https?://[^\s|]+', l):
            host = (urlparse(url).hostname or '').lower()
            if host in CHECK_ONLY_GOV_HOSTS:
                if CHECK_LABEL not in low:
                    err(where, f'{host} is a check-only reference: label it "{CHECK_LABEL}"')
                continue
            if host.endswith('.gov') or host == 'gov':
                continue
            if any(host == h or host.endswith('.' + h) for h in CHECK_HOSTS):
                if CHECK_LABEL not in low:
                    err(where, f'check-only reference not labelled "{CHECK_LABEL}": {host}')
                continue
            if host == 'journals.sagepub.com' and WEM_DOI.search(url):
                if 'wilderness medical society' not in low:
                    err(where, 'WEM DOI not labelled Wilderness Medical Society')
                if CHECK_LABEL not in low:
                    err(where, f'WEM DOI not labelled "{CHECK_LABEL}"')
                continue
            err(where, f'URL host {host} is not .gov or an allowed check-only reference')


def check_page(where, lines, banned_scoped, medical, figs, topic_key):
    title = lines[0]
    if not title.startswith('# ') or not title[2:].strip():
        err(where, f'page must start "# <title>": {title[:60]}')
        return 0, False
    if len(title) - 2 > 40:
        warn(where, f'page title {len(title) - 2} chars: {title[2:]}')
    rest = lines[1:]
    fig = None
    # optional figure line: first non-blank line after the title
    nb = [i for i, l in enumerate(rest) if l.strip()]
    if nb and rest[nb[0]].startswith('!['):
        m = FIG_LINE.match(rest[nb[0]])
        if not m:
            err(where, f'bad figure line: {rest[nb[0]]}')
        else:
            fig = m.group(2)
            if not FIG_ID.match(fig):
                err(where, f'bad figure id {fig!r}')
            elif fig not in figs:
                err(where, f'figure {fig} not in figures.tsv')
            else:
                figs[fig]['used'].append(where)
                if figs[fig]['status'] in ('drop', 'redraw'):
                    err(where, f'figure {fig} is marked {figs[fig]["status"]} in figures.tsv')
            if fig and not os.path.exists(os.path.join(PACK, 'figures', 'masters', fig + '.png')):
                err(where, f'figure master {fig}.png missing')
        rest = rest[:nb[0]] + rest[nb[0] + 1:]
    text_lines = [l for l in rest if l.strip()]
    if not text_lines:
        err(where, 'page has no text')
    step_runs, prev_step = [], None
    for l in rest:
        lw = f'{where} "{l.strip()[:50]}"'
        if not l.strip():
            prev_step = None if prev_step is None else prev_step
            continue
        if l != l.rstrip():
            err(lw, 'trailing whitespace')
        if l[0].isspace():
            err(lw, 'indented line (nested list or code)')
        if l.startswith('#'):
            err(lw, 'heading inside a page')
        if l.startswith('!['):
            err(lw, 'figure line not directly after the page title, or a second figure')
        if re.match(r'^(\* |\+ |> |\||-{3,}$|\+{3,}$|={3,}$)', l):
            err(lw, 'Markdown outside the subset (list marker, quote, table or rule)')
        if '|' in l:
            err(lw, 'pipe character (table?)')
        if re.search(r'\]\(|\[[^\]]*\]', l):
            err(lw, 'link or bracket markup')
        if re.search(r'https?://|www\.', l):
            err(lw, 'URL in page text')
        if '`' in l:
            err(lw, 'code markup')
        stripped = l
        if MYTH in l:
            if not (l.startswith('WARNING:') and l.endswith(MYTH)):
                err(lw, 'myth-ok marker only allowed at the end of a WARNING line')
            stripped = l.replace(MYTH, '')
        if '<' in stripped or '>' in stripped:
            err(lw, 'HTML or angle bracket')
        if stripped.count('**') % 2:
            err(lw, 'unbalanced **bold**')
        if re.search(r'(?<!\*)\*(?!\*)', stripped) or re.search(r'(^|\s)_[^_]+_(\s|$)', stripped):
            err(lw, 'single-star or underscore emphasis')
        if re.search(r'[^\x20-\x7e]', l):
            err(lw, 'non-ASCII character')
        if re.match(r'^(WARNING|NOTE)\b', l) and not re.match(r'^(WARNING|NOTE): \S', l):
            err(lw, 'WARNING/NOTE must be "WARNING: text" / "NOTE: text"')
        if re.match(r'^(Warning|Note|warning|note):', l):
            err(lw, 'WARNING/NOTE label must be upper case')
        m = re.match(r'^(\d+)\. ', l)
        if m:
            n = int(m.group(1))
            if prev_step is None:
                step_runs.append(1)
                if n != 1:
                    err(lw, f'step list starts at {n}')
            else:
                step_runs[-1] += 1
                if n != prev_step + 1:
                    err(lw, f'step {n} follows {prev_step}')
            prev_step = n
        elif re.match(r'^\d+\.\S', l) or re.match(r'^\d+\) ', l):
            err(lw, 'malformed step marker')
        else:
            prev_step = None
        for pat in banned_scoped:
            for sm in re.finditer(pat, l, re.I):
                if re.match(r'^WARNING: Do NOT\b', l, re.I):
                    continue
                # a negated instruction in the same clause ("do not rub", "never loosen", "no ice") is allowed
                clause = re.split(r'[.;:!?]', l[:sm.start()])[-1].lower()
                if re.search(r"\b(do not|don't|never|no|not|without)\b", clause):
                    warn(lw, f'page-scoped word /{pat}/ allowed only because it is negated')
                    continue
                err(lw, f'page-scoped banned word /{pat}/ on {topic_key}')
    words = count_words(text_lines)
    limit = WORDS_FIG if fig else WORDS_TEXT
    if words > limit:
        err(where, f'{words} words > {limit}' + (' (page has a figure)' if fig else ''))
    steps = sum(step_runs)
    if steps > MAX_STEPS:
        err(where, f'{steps} steps > {MAX_STEPS}')
    if medical and REF_NOTE not in rest:
        err(where, 'medical topic page lacks the reference-only NOTE')
    if not medical and REF_NOTE not in rest:
        hits = sorted({h.replace('\\b', '') for h in MEDICAL_HINTS if re.search(h, '\n'.join(lines), re.I)})
        if hits:
            err(where, f'mentions {", ".join(hits)} but lacks the reference-only NOTE')
    return words, bool(fig)


def check_topics(cats, figs, banned):
    tdir = os.path.join(PACK, 'topics')
    seen_ids = {}
    for cid, ctitle, _ in cats:
        cdir = os.path.join(tdir, cid)
        files = sorted(f for f in os.listdir(cdir)) if os.path.isdir(cdir) else []
        mds = [f for f in files if f.endswith('.md')]
        for f in files:
            if not f.endswith('.md'):
                err(f'topics/{cid}/{f}', 'stray file in topic folder')
        if not mds:
            err(f'topics/{cid}', 'category has no topics')
            continue
        stats['categories'] += 1
        for idx, f in enumerate(mds, 1):
            path = f'topics/{cid}/{f}'
            m = TOPIC_FILE.match(f)
            if not m:
                err(path, 'file name must be NN-<topic-id>.md, id [a-z0-9-]{1,32}')
                continue
            if int(m.group(1)) != idx:
                err(path, f'number {m.group(1)} out of sequence (expected {idx:02d})')
            tid = m.group(2)
            if tid in seen_ids:
                warn(path, f'topic id {tid} also used in {seen_ids[tid]}')
            seen_ids[tid] = path
            raw = open(os.path.join(cdir, f), encoding='utf-8').read()
            if '\r' in raw or '\t' in raw:
                err(path, 'CR or TAB characters')
            check_banned_text(path, raw, banned, page_lines=True)
            front, body = parse_front(path, raw)
            if front is None:
                continue
            fm, sources = front
            stats['topics'] += 1
            for k in ('title', 'summary', 'sources', 'quick', 'medical'):
                if k not in fm:
                    err(path, f'front matter missing {k}:')
            for k in fm:
                if k not in ('title', 'summary', 'sources', 'quick', 'medical'):
                    warn(path, f'unknown front matter key {k}')
            if not fm.get('title'):
                err(path, 'empty title')
            elif len(fm['title']) > 40:
                warn(path, f'title {len(fm["title"])} chars')
            if not fm.get('summary'):
                err(path, 'empty summary')
            elif len(fm['summary']) > MAX_SUMMARY:
                err(path, f'summary {len(fm["summary"])} chars > {MAX_SUMMARY}')
            for k in ('title', 'summary'):
                if re.search(r'[^\x20-\x7e]', fm.get(k, '')):
                    err(path, f'{k} non-ASCII')
            if fm.get('sources'):
                err(path, 'sources: must be followed by list lines, not a value')
            for k in ('quick', 'medical'):
                if fm.get(k) not in ('yes', 'no'):
                    err(path, f'{k}: must be yes or no (got {fm.get(k)!r})')
            medical = fm.get('medical') == 'yes'
            if cid == 'first-aid' and not medical:
                err(path, 'FIRST AID topic must be medical: yes')
            check_sources(path, sources)
            stats['sources'] += len(sources)
            if medical:
                stats['medical'] += 1
            scoped = []
            for key, pats in SCOPED:
                if f'{cid}/{f[:-3]}' == key:
                    scoped = pats
            if body.endswith('\n+++\n') or body.startswith('+++'):
                err(path, 'empty page at start or end')
            pages = re.split(r'\n\+\+\+\n', body.strip('\n'))
            if fm.get('quick') == 'yes':
                stats['quick'] += 1
                if len(pages) != 1:
                    err(path, f'quick: yes but {len(pages)} pages')
            titles = set()
            for pn, page in enumerate(pages, 1):
                lines = page.strip('\n').split('\n')
                where = f'{path} p{pn}'
                if lines[0] in titles:
                    warn(where, f'duplicate page title {lines[0]}')
                titles.add(lines[0])
                w, hasfig = check_page(where, lines, scoped, medical, figs, f'{cid}/{f[:-3]}')
                stats['pages'] += 1
                stats['words'] += w
                stats['pages_fig'] += hasfig
    stats['fig_refs'] = sum(len(v['used']) for v in figs.values())


def main():
    banned = load_banned()
    check_guide()
    cats = check_categories()
    figs = load_figures()
    for f in ('guide.txt', 'categories.txt'):
        p = os.path.join(PACK, f)
        if os.path.exists(p):
            check_banned_text(f, open(p, encoding='utf-8').read(), banned)
    check_topics(cats, figs, banned)
    if not os.path.exists(os.path.join(PACK, 'ATTRIBUTION.md')):
        err('ATTRIBUTION.md', 'missing')
    else:
        a = open(os.path.join(PACK, 'ATTRIBUTION.md'), encoding='utf-8').read()
        if 'not endorsed' not in a:
            err('ATTRIBUTION.md', 'missing the "Adapted; not endorsed" line')
        for fid, v in figs.items():
            if v['used'] and fid not in a:
                err('ATTRIBUTION.md', f'no provenance line for figure {fid}')
    for fid, v in figs.items():
        if v['status'] == 'keep' and not v['used']:
            warn(f'figures.tsv {fid}', 'marked keep but no page uses it')
        if len(v['used']) > 1:
            warn(f'figures.tsv {fid}', f'used on {len(v["used"])} pages: {", ".join(v["used"])}')
    keep = sum(1 for v in figs.values() if v['status'] == 'keep')
    used = sum(1 for v in figs.values() if v['used'])
    for w in warns:
        print(w)
    for e in errors:
        print(e)
    print(f'\ncategories={stats["categories"]} topics={stats["topics"]} pages={stats["pages"]} '
          f'(with figure {stats["pages_fig"]}) words={stats["words"]} quick={stats["quick"]} '
          f'medical={stats["medical"]} source-lines={stats["sources"]}')
    print(f'figures: {len(figs)} in figures.tsv ({keep} keep, '
          f'{sum(1 for v in figs.values() if v["status"] == "redraw")} redraw, '
          f'{sum(1 for v in figs.values() if v["status"] == "drop")} drop), {used} distinct used, '
          f'{stats["fig_refs"]} page references')
    print(f'{len(errors)} errors, {len(warns)} warnings -> {"FAIL" if errors else "PASS"}')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
