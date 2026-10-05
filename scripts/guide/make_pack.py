#!/usr/bin/env python3
"""Build the survival guide's DEVICE pack (format 1) from its source pack.

    python3 scripts/guide/make_pack.py           # validate the source, then (re)write packs/guide/build/survival/
    python3 scripts/guide/make_pack.py --check   # validate; fail when the device pack is stale, missing or extra
    python3 scripts/guide/make_pack.py --check --text-only   # the same without re-rendering the figures (ctest)

The source is packs/guide/survival/ (format: packs/guide/README.md; checked by validate_guide.py, which
this runs first and which must pass). The device pack is what the X4 Pro reads from /Guides/survival/ on the card
(lib/Guide/GdPack.h documents it); it is committed under packs/guide/build/survival/ so the bench and the planned
downloader can copy it as it stands:

    pack.txt        key=value lines: format=1 id version title short status status_text note ref_note license min_app
    categories.tsv  <cat-id> TAB <TITLE> TAB <blurb> TAB <order 1..n>            (EMERGENCY first)
    topics.tsv      <topic-id> TAB <cat-id> TAB <title> TAB <flags Q/M or -> TAB t/<topic-id>.gp TAB <pages> TAB <summary>
    search.idx      sorted lines <term> TAB <topic-id>:<weight>,...  (title 3 + summary 2 + body 1; synonyms 1)
    t/<id>.gp       the topic's pages, line-based (see to_gp below and lib/Guide/GdPage.h)
    fig/L/<id>.png  1-bit grayscale PNG, <= 440 x FIG_MAX_H, Lanczos down-scale then the figures.tsv threshold
    fig/XL/<id>.png the full-screen view's raster, <= FIG_XL_W x FIG_XL_H, made from the master the same way (and
                    turned a quarter-turn counter-clockwise when the figure is wide, so it runs down the long
                    axis); written only when it is at least FIG_XL_MIN_GAIN times the page's size (else the
                    full-screen view shows fig/L)
    about.txt       the status, the reference-only notes and a short source list, in the .gp format
    SHA256SUMS      "<sha256>  <path>" for every other file, sorted by path

Deterministic: the same source gives the same bytes (sorted everything, no timestamps, our own PNG writer). The
figures need Pillow; without it (or with --text-only) --check still checks every text file and that each figure is
present and listed in SHA256SUMS, and says it skipped re-rendering them. When it does re-render them, --check
compares their PIXELS, not their bytes (the deflate stream depends on the zlib Python links), and a figure whose
pixels match keeps its committed bytes, so SHA256SUMS is checked against what is committed.
"""
import hashlib
import os
import re
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
SRC = os.path.join(REPO, 'packs', 'guide', 'survival')
OUT = os.path.join(REPO, 'packs', 'guide', 'build', 'survival')
VALIDATOR = os.path.join(HERE, 'validate_guide.py')
STOPWORDS = os.path.join(HERE, 'stopwords.txt')
STEM_VECTORS = os.path.join(HERE, 'stem_vectors.tsv')

FORMAT = 1
MIN_APP = 1
FIG_MAX_W = 440
FIG_MAX_H = 400            # the firmware refuses a figure over 440 x 480 (lib/Guide/GdPack.h)
FIG_XL_W = 456             # the full-screen view's box (GdPack.h MAX_FIG_XL_W/H, GuideDraw drawFigureScreen)
FIG_XL_H = 620
FIG_XL_MAX_UP = 2.0        # never more than twice the master's own size
FIG_XL_MIN_GAIN = 1.15     # an XL raster only when it is this much larger than the page's
FIG_XL_TURN = 1.15         # turned sideways only when that makes it this much larger again
MAX_CATEGORIES = 64
MAX_TOPICS = 1024
MAX_PAGES = 64
MAX_GP_BYTES = 64 * 1024
MAX_INDEX_BYTES = 256 * 1024
MAX_TITLE = 80
MAX_SUMMARY = 96

STATUS_TEXT = {
    'reviewed-by-ai': 'Reviewed against public guidance by AI reviewers - not by a medical professional.',
}
REF_NOTE = 'Reference only - not a substitute for first-aid training. Call for help first.'
MYTH = '<!-- myth-ok -->'

# UTF-8 -> ASCII-safe punctuation (the source is ASCII today; this keeps a future edit drawable).
ASCII_MAP = {
    '‘': "'", '’': "'", '‚': "'", '‛': "'", '′': "'",
    '“': '"', '”': '"', '„': '"', '″': '"',
    '‐': '-', '‑': '-', '‒': '-', '–': '-', '—': '-', '―': '-', '−': '-',
    '…': '...', ' ': ' ', ' ': ' ', ' ': ' ', '½': '1/2', '¼': '1/4', '¾': '3/4',
    '°': ' deg', '×': 'x', '•': '-',
}


class BuildError(Exception):
    pass


# ---- the light stemmer and the stopwords (lib/Guide/GdSearch.cpp has the same) -------------------------------------

def stem(w):
    """Lower-case [a-z0-9]+ in; the stem out. Only all-letter words are changed."""
    if not w.isalpha():
        return w
    n = len(w)
    if w.endswith('ies') and n >= 5:
        w = w[:-3] + 'y'
    elif w.endswith('sses'):
        w = w[:-2]
    elif w.endswith('es') and n >= 5 and (w[-3] in 'sxz' or w[-4:-2] in ('ch', 'sh')):
        w = w[:-2]
    elif w.endswith('s') and n >= 4 and not w.endswith(('ss', 'us', 'is')):
        w = w[:-1]
    n = len(w)
    if w.endswith('ing') and n >= 7:
        w = undouble(w[:-3])
    elif w.endswith('ed') and n >= 6:
        w = undouble(w[:-2])
    return w


def undouble(w):
    if len(w) >= 2 and w[-1] == w[-2] and w[-1] not in 'aeioulsz':
        return w[:-1]
    return w


def load_stopwords():
    out = []
    for line in open(STOPWORDS, encoding='utf-8'):
        line = line.strip()
        if line and not line.startswith('#'):
            out.append(line)
    return out


def check_stem_vectors():
    for i, line in enumerate(open(STEM_VECTORS, encoding='utf-8'), 1):
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        word, want = line.split('\t')
        if stem(word) != want:
            raise BuildError('stem_vectors.tsv:%d: stem(%r) = %r, want %r' % (i, word, stem(word), want))


def terms_of(text, stopwords):
    """The index terms of a text: lower-case [a-z0-9]+ words of 2+ characters (numbers 3+), no stopwords, stemmed."""
    out = []
    for w in re.findall(r'[a-z0-9]+', text.lower()):
        if len(w) < 2 or (w.isdigit() and len(w) < 3) or w in stopwords:
            continue
        out.append(stem(w))
    return out


# ---- the source pack ------------------------------------------------------------------------------------------------

def ascii_safe(s, where):
    out = ''.join(ASCII_MAP.get(c, c) for c in s)
    bad = sorted({c for c in out if not (' ' <= c <= '~')})
    if bad:
        raise BuildError('%s: characters the device fonts may not draw: %r' % (where, ''.join(bad)))
    return out


def read_kv(path):
    kv = {}
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if line.strip() and not line.startswith('#'):
            k, v = line.split('=', 1)
            kv[k] = v
    return kv


def read_categories():
    cats = []
    for line in open(os.path.join(SRC, 'categories.txt'), encoding='utf-8'):
        line = line.rstrip('\n')
        if line.strip():
            cid, title, blurb = line.split(' | ')
            cats.append({'id': cid, 'title': ascii_safe(title, cid), 'blurb': ascii_safe(blurb, cid)})
    return cats


def read_figures():
    figs = {}
    for line in open(os.path.join(SRC, 'figures', 'figures.tsv'), encoding='utf-8'):
        line = line.rstrip('\n')
        if line.strip():
            fid, _doc, _page, _cap, thr, note = line.split(' | ')
            figs[fid] = {'threshold': int(thr), 'status': note.split()[0].rstrip(';,')}
    return figs


def read_topic(path):
    text = open(path, encoding='utf-8').read()
    end = text.find('\n---\n', 4)
    front, body = text[4:end].split('\n'), text[end + 5:]
    fm = {}
    for line in front:
        if line.startswith('- '):
            continue  # a source line: the sources live in ATTRIBUTION.md
        if ': ' in line:
            k, v = line.split(': ', 1)   # the FIRST ': ' only (titles may hold a colon)
            fm[k] = v.strip()
    pages = [p.strip('\n').split('\n') for p in re.split(r'\n\+\+\+\n', body.strip('\n'))]
    return fm, pages


STEP = re.compile(r'^(\d{1,3})\. (.*)$')
FIG = re.compile(r'^!\[([^\[\]]+)\]\(([^()\s]+)\)$')


def needs_escape(line):
    """True when a paragraph line would read as markup on the device (lib/Guide/GdPage.h)."""
    return (line.startswith(('= ', '@fig ', '- ', '! ', '* ', '\\')) or line == '---'
            or re.match(r'^\d{1,3}\. ', line) is not None)


def to_gp(pages, where, figs, used_figs):
    """Source pages -> .gp lines and the plain text of each page (for the index)."""
    out, plain = [], []
    for pn, lines in enumerate(pages, 1):
        if pn > 1:
            out.append('---')
        w = '%s p%d' % (where, pn)
        for line in lines:
            if not line.strip():
                continue
            line = ascii_safe(line, w)
            if line.endswith(MYTH):
                line = line[:-len(MYTH)].rstrip()
            if line.startswith('# '):
                out.append('= ' + line[2:].strip())
                plain.append(line[2:])
                continue
            m = FIG.match(line)
            if m:
                cap, fid = m.group(1), m.group(2)
                if fid not in figs or figs[fid]['status'] != 'keep':
                    raise BuildError('%s: figure %s is not a keep row of figures.tsv' % (w, fid))
                used_figs.add(fid)
                out.append('@fig %s %s' % (fid, cap))
                plain.append(cap)
                continue
            if line.startswith('WARNING: '):
                out.append('! ' + line[len('WARNING: '):])
            elif line.startswith('NOTE: '):
                out.append('* ' + line[len('NOTE: '):])
            elif line.startswith('- ') or STEP.match(line):
                out.append(line)
            else:
                out.append('\\' + line if needs_escape(line) else line)
            plain.append(re.sub(r'^(- |\d+\. )', '', line).replace('**', ''))
    return out, plain


def read_source():
    guide = read_kv(os.path.join(SRC, 'guide.txt'))
    if guide.get('status') not in STATUS_TEXT:
        raise BuildError('guide.txt: status %r has no status text in make_pack.py' % guide.get('status'))
    cats = read_categories()
    figs = read_figures()
    topics, used_figs, seen = [], set(), {}
    for cat in cats:
        cdir = os.path.join(SRC, 'topics', cat['id'])
        for name in sorted(os.listdir(cdir)):
            if not name.endswith('.md'):
                continue
            tid = name[3:-3]
            where = 'topics/%s/%s' % (cat['id'], name)
            if tid in seen:
                raise BuildError('%s: topic id %s already used by %s' % (where, tid, seen[tid]))
            seen[tid] = where
            fm, pages = read_topic(os.path.join(cdir, name))
            gp, plain = to_gp(pages, where, figs, used_figs)
            flags = ('Q' if fm.get('quick') == 'yes' else '') + ('M' if fm.get('medical') == 'yes' else '')
            topics.append({'id': tid, 'cat': cat['id'], 'title': ascii_safe(fm['title'], where),
                           'summary': ascii_safe(fm['summary'], where), 'flags': flags or '-',
                           'pages': len(pages), 'gp': gp, 'plain': plain})
    return guide, cats, topics, figs, used_figs


# ---- the device files -----------------------------------------------------------------------------------------------

def tsv_field(s, where):
    if '\t' in s or '\n' in s or '\r' in s:
        raise BuildError('%s: a TAB or line break in a field' % where)
    return s


def build_text_files(guide, cats, topics, stopwords, synonyms):
    files = {}
    version = '%s.%s' % (guide['reviewed'].replace('-', '.'), guide['version'])
    short = guide['title'].split()[0].upper()
    pack = [
        '# Survival Guide device pack, built by scripts/guide/make_pack.py from packs/guide/survival. Do not edit.',
        'format=%d' % FORMAT,
        'id=%s' % guide['id'],
        'version=%s' % version,
        'title=%s' % guide['title'],
        'short=%s' % short,
        'status=%s' % guide['status'],
        'status_text=%s' % STATUS_TEXT[guide['status']],
        'note=%s' % ascii_safe(guide['note'], 'guide.txt'),
        'ref_note=%s' % REF_NOTE,
        'license=%s' % guide['license'],
        'min_app=%d' % MIN_APP,
    ]
    files['pack.txt'] = '\n'.join(pack) + '\n'

    if not 1 <= len(cats) <= MAX_CATEGORIES:
        raise BuildError('%d categories (1..%d)' % (len(cats), MAX_CATEGORIES))
    if not 1 <= len(topics) <= MAX_TOPICS:
        raise BuildError('%d topics (1..%d)' % (len(topics), MAX_TOPICS))
    files['categories.tsv'] = ''.join(
        '%s\t%s\t%s\t%d\n' % (c['id'], tsv_field(c['title'], c['id']), tsv_field(c['blurb'], c['id']), i)
        for i, c in enumerate(cats, 1))
    rows = []
    for t in topics:
        if len(t['title']) > MAX_TITLE or len(t['summary']) > MAX_SUMMARY:
            raise BuildError('%s: title or summary too long for the device' % t['id'])
        if not 1 <= t['pages'] <= MAX_PAGES:
            raise BuildError('%s: %d pages (1..%d)' % (t['id'], t['pages'], MAX_PAGES))
        rows.append('\t'.join([t['id'], t['cat'], tsv_field(t['title'], t['id']), t['flags'], 't/%s.gp' % t['id'],
                               str(t['pages']), tsv_field(t['summary'], t['id'])]) + '\n')
        gp = '\n'.join(t['gp']) + '\n'
        if len(gp.encode('ascii')) > MAX_GP_BYTES:
            raise BuildError('%s: .gp is %d bytes (> %d)' % (t['id'], len(gp), MAX_GP_BYTES))
        files['t/%s.gp' % t['id']] = gp
    files['topics.tsv'] = ''.join(rows)
    files['search.idx'] = build_index(topics, stopwords, synonyms)
    if len(files['search.idx']) > MAX_INDEX_BYTES:
        raise BuildError('search.idx is %d bytes (> %d)' % (len(files['search.idx']), MAX_INDEX_BYTES))
    files['about.txt'] = build_about(guide, version)
    return files


def build_index(topics, stopwords, synonyms):
    postings = {}
    for t in topics:
        title = set(terms_of(t['title'], stopwords))
        summary = set(terms_of(t['summary'], stopwords))
        body = set()
        for p in t['plain']:
            body.update(terms_of(p, stopwords))
        weights = {}
        for term in title | summary | body:
            weights[term] = (3 if term in title else 0) + (2 if term in summary else 0) + (1 if term in body else 0)
        present = set(weights)
        for group in synonyms:
            if present & group:
                for g in sorted(group - present):
                    weights[g] = 1
        for term, w in weights.items():
            postings.setdefault(term, []).append('%s:%d' % (t['id'], w))
    return ''.join('%s\t%s\n' % (term, ','.join(postings[term])) for term in sorted(postings))


def load_synonyms(stopwords):
    groups = []
    path = os.path.join(SRC, 'synonyms.txt')
    if not os.path.exists(path):
        return groups
    for i, line in enumerate(open(path, encoding='utf-8'), 1):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        words = line.split()
        if len(words) < 2 or any(not re.match(r'^[a-z0-9]+$', w) or w in stopwords for w in words):
            raise BuildError('synonyms.txt:%d: want two or more lower-case words, no stopwords' % i)
        groups.append({stem(w) for w in words})
    return groups


def build_about(guide, version):
    """about.txt in the .gp format: the status and the notes, then a short source list from ATTRIBUTION.md."""
    attribution = open(os.path.join(SRC, 'ATTRIBUTION.md'), encoding='utf-8').read()
    manuals, agencies, section = [], [], 0
    endorse = ''
    for line in attribution.split('\n'):
        if line.startswith('## '):
            section = int(line[3]) if line[3].isdigit() else 0
        elif line.startswith('### ') and section == 1:
            manuals.append(line[4:].replace('*', '').strip())
        elif line.startswith('### ') and section == 2:
            name = line[4:].strip()
            m = re.match(r'^(.*?) \(([^,)]+)', name)
            if m and ' ' not in m.group(2) and len(m.group(2)) <= 8:
                name = m.group(2)
            elif m:
                name = m.group(1)
            agencies.append(name)
        elif line.startswith('**Adapted;'):
            endorse = line.replace('**', '')
    if not manuals or not agencies or not endorse:
        raise BuildError('ATTRIBUTION.md: could not find the manuals, the agencies or the "Adapted;" line')
    lines = [
        '= About this guide',
        '**%s**, version %s (reviewed %s). License: %s.' % (guide['title'], version, guide['reviewed'],
                                                           guide['license']),
        '* ' + STATUS_TEXT[guide['status']],
        '! ' + ascii_safe(guide['note'], 'guide.txt'),
        'Every first-aid page also says: ' + REF_NOTE,
        endorse,
        '---',
        '= Sources',
        'Fieldcraft and figures, adapted and rewritten from:',
    ]
    lines += ['- ' + ascii_safe(m, 'ATTRIBUTION.md') for m in manuals]
    lines += [
        'Current public guidance, checked and rewritten, from: %s.' % ', '.join(agencies),
        'Medical society and journal guidance was used only to check facts; nothing was copied.',
        'Figures: line art from the Army manuals above, made 1-bit. The full list of sources, page by page, is '
        'packs/guide/survival/ATTRIBUTION.md in the firmware repository.',
    ]
    return '\n'.join(ascii_safe(l, 'about.txt') for l in lines) + '\n'


# ---- figures --------------------------------------------------------------------------------------------------------

def png_1bit(width, height, rows):
    """A 1-bit grayscale PNG (0 = black, 1 = white); rows are lists of 0/1. Our own writer, so the bytes never
    depend on an imaging library's version."""
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for x in range(0, width, 8):
            b = 0
            for i, v in enumerate(row[x:x + 8]):
                b |= (v & 1) << (7 - i)
            raw.append(b)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 1, 0, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def fit_size(w, h):
    scale = min(1.0, FIG_MAX_W / w, FIG_MAX_H / h)
    return max(1, int(round(w * scale))), max(1, int(round(h * scale)))


def master_path(fid):
    return os.path.join(SRC, 'figures', 'masters', fid + '.png')


def png_size(path):
    """A PNG's size from its header (no Pillow needed, so --text-only knows which XL figures exist)."""
    with open(path, 'rb') as f:
        head = f.read(24)
    if len(head) < 24 or head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        raise BuildError('%s: not a PNG' % os.path.relpath(path, REPO))
    return struct.unpack('>II', head[16:24])


def xl_plan(w, h):
    """(turned, width, height) of the figure's full-screen raster, or None when it would not be enough larger
    than the page's. Turned: rotated a quarter-turn counter-clockwise (its top at the screen's left edge)."""
    page_scale = min(1.0, FIG_MAX_W / w, FIG_MAX_H / h)
    upright = min(FIG_XL_MAX_UP, FIG_XL_W / w, FIG_XL_H / h)
    sideways = min(FIG_XL_MAX_UP, FIG_XL_W / h, FIG_XL_H / w)
    turned = sideways > upright * FIG_XL_TURN
    scale = sideways if turned else upright
    if scale < page_scale * FIG_XL_MIN_GAIN:
        return None
    ow, oh = (h, w) if turned else (w, h)
    return turned, min(FIG_XL_W, max(1, int(round(ow * scale)))), min(FIG_XL_H, max(1, int(round(oh * scale))))


def build_figure(fid, threshold, xl=None):
    from PIL import Image  # only the figures need Pillow
    img = Image.open(master_path(fid)).convert('L')
    if xl:
        turned, w, h = xl
        if turned:
            img = img.transpose(Image.ROTATE_90)
    else:
        w, h = fit_size(*img.size)
    if (w, h) != img.size:
        img = img.resize((w, h), Image.LANCZOS)
    px = list(img.getdata())
    rows = [[1 if v >= threshold else 0 for v in px[y * w:(y + 1) * w]] for y in range(h)]
    return png_1bit(w, h, rows)


def png_pixels(data):
    """(IHDR, the inflated scanlines) of a PNG, or None: two figures with the same pixels compare equal whatever
    deflate stream wrote them."""
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    at, ihdr, idat = 8, None, b''
    while at + 12 <= len(data):
        n = struct.unpack('>I', data[at:at + 4])[0]
        kind, body = data[at + 4:at + 8], data[at + 8:at + 8 + n]
        if kind == b'IHDR':
            ihdr = body
        elif kind == b'IDAT':
            idat += body
        at += 12 + n
    try:
        return ihdr, zlib.decompress(idat)
    except zlib.error:
        return None


def have_pillow():
    try:
        import PIL  # noqa: F401
        return True
    except ImportError:
        return False


# ---- main -----------------------------------------------------------------------------------------------------------

def sha256sums(files):
    return ''.join('%s  %s\n' % (hashlib.sha256(files[p]).hexdigest(), p) for p in sorted(files))


def existing_files():
    out = {}
    for root, _dirs, names in os.walk(OUT):
        for n in names:
            full = os.path.join(root, n)
            out[os.path.relpath(full, OUT).replace(os.sep, '/')] = full
    return out


def main(argv):
    opts = argv[1:]
    check = '--check' in opts
    text_only = '--text-only' in opts
    unknown = [a for a in opts if a not in ('--check', '--text-only')]
    if unknown or (text_only and not check):
        print(__doc__)
        return 2
    v = subprocess.run([sys.executable, VALIDATOR, SRC], capture_output=True, text=True)
    if v.returncode != 0:
        sys.stdout.write(v.stdout)
        print('make_pack: the source pack does not validate')
        return 1
    try:
        check_stem_vectors()
        stopwords = set(load_stopwords())
        guide, cats, topics, figs, used = read_source()
        text = build_text_files(guide, cats, topics, stopwords, load_synonyms(stopwords))
        xl = {fid: xl_plan(*png_size(master_path(fid))) for fid in sorted(used)}
    except (BuildError, OSError, ValueError, KeyError) as e:
        print('make_pack: %s' % e)
        return 1
    files = {p: s.encode('ascii') for p, s in text.items()}
    render = have_pillow() and not text_only
    disk = existing_files()
    fig_jobs = []  # (path, fid, xl plan or None)
    for fid in sorted(used):
        fig_jobs.append(('fig/L/%s.png' % fid, fid, None))
        if xl[fid]:
            fig_jobs.append(('fig/XL/%s.png' % fid, fid, xl[fid]))
    fig_paths = [p for p, _f, _x in fig_jobs]
    redrawn = set()  # --check: figures whose committed pixels differ from a fresh render
    if render:
        for p, fid, plan in fig_jobs:
            data = build_figure(fid, figs[fid]['threshold'], plan)
            if check and p in disk:
                committed = open(disk[p], 'rb').read()
                if committed != data and png_pixels(committed) != png_pixels(data):
                    redrawn.add(p)
                data = committed  # same pixels: keep the committed bytes (SHA256SUMS is checked against them)
            files[p] = data
    elif not check:
        print('make_pack: building the figures needs Pillow (pip install pillow)')
        return 1
    else:
        for p in fig_paths:  # cannot re-render: take the committed bytes, so SHA256SUMS is still checked
            if p in disk:
                files[p] = open(disk[p], 'rb').read()
        print('make_pack: %s; the figures were not re-rendered (only checked for presence)'
              % ('--text-only' if text_only else 'Pillow not found'))
    files['SHA256SUMS'] = sha256sums(files).encode('ascii')

    expected = set(files) | set(fig_paths)
    if check:
        problems = []
        for p in sorted(expected):
            if p not in disk:
                problems.append('missing ' + p)
            elif p in redrawn or (p in files and open(disk[p], 'rb').read() != files[p]):
                problems.append('stale   ' + p)
        problems += ['extra   ' + p for p in sorted(set(disk) - expected)]
        if problems:
            for line in problems:
                print(line)
            print('make_pack --check: packs/guide/build/survival is out of date; run scripts/guide/make_pack.py')
            return 1
        print('make_pack --check: %d files current (%d topics, %d figures, %d full-screen)'
              % (len(expected), len(topics), len(used), sum(1 for f in xl.values() if f)))
        return 0

    written = 0
    for p, data in sorted(files.items()):
        full = os.path.join(OUT, p)
        if p in disk and open(disk[p], 'rb').read() == data:
            continue
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'wb') as f:
            f.write(data)
        written += 1
    for p in sorted(set(disk) - set(files)):
        os.remove(disk[p])
        print('removed ' + p)
    total = sum(len(d) for d in files.values())
    print('make_pack: %d files (%d written), %d bytes; %d topics, %d figures (%d full-screen), search.idx %d bytes'
          % (len(files), written, total, len(topics), len(used), sum(1 for f in xl.values() if f),
             len(files['search.idx'])))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
