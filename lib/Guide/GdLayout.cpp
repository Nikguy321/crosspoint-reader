#include "GdLayout.h"

#include <cstring>

#include "GdText.h"

namespace gd {

namespace {

constexpr char WARNING_LABEL[] = "WARNING:";
constexpr char NOTE_LABEL[] = "NOTE:";

// A piece of one wrapped line: x is relative to the line's start.
struct Piece {
  const char* text;
  uint16_t len;
  int16_t x;
  Font font;
};
constexpr int MAX_PIECES = 64;  // per line
constexpr int MAX_SEGS = 16;    // bold/regular segments in one word

int utf8Len(const char* p) {
  const uint8_t c = static_cast<uint8_t>(*p);
  int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
  for (int i = 1; i < n; i++) {
    if ((static_cast<uint8_t>(p[i]) & 0xC0) != 0x80) return i;  // a broken sequence: stop at its end
  }
  return n;
}

// Wraps a NUL-terminated text with **bold** toggles into lines, one line at a time.
class Wrapper {
 public:
  Wrapper(const FontMetrics& m, const char* text, const Font regular, const Font bold, const char* label)
      : m(m), p(text), regular(regular), boldFont(bold), label(label) {}

  // The next line's pieces (x relative to the line start). Returns how many (0 when done).
  int nextLine(const int maxWidth, Piece* out) {
    int n = 0;
    int x = 0;
    const int space = m.spaceWidth(regular);
    while (true) {
      if (label) {
        const int w = m.textWidth(boldFont, label, std::strlen(label));
        out[n++] = Piece{label, static_cast<uint16_t>(std::strlen(label)), 0, boldFont};
        x = w;
        label = nullptr;
        continue;
      }
      while (*p == ' ') p++;
      if (*p == '\0') break;
      const char* saveP = p;
      const bool saveBold = bold;
      Seg segs[MAX_SEGS];
      int width = 0;
      const int count = scanWord(segs, width);
      if (count == 0) continue;  // markers only
      const int lead = n > 0 ? space : 0;
      if (x + lead + width <= maxWidth && n + count <= MAX_PIECES) {
        int sx = x + lead;
        for (int i = 0; i < count; i++) {
          out[n++] = Piece{segs[i].s, segs[i].n, static_cast<int16_t>(sx), segs[i].bold ? boldFont : regular};
          sx += segs[i].w;
        }
        x += lead + width;
        continue;
      }
      if (n > 0) {  // the word starts the next line
        p = saveP;
        bold = saveBold;
        break;
      }
      // One word wider than the line: break it between characters (at least one a line).
      return breakWord(segs, count, maxWidth, out);
    }
    return n;
  }

 private:
  struct Seg {
    const char* s;
    uint16_t n;
    bool bold;
    int w;
  };

  // From p (not a space): the word's segments, split where "**" toggles bold. Advances p past the
  // word and keeps the bold state. Returns the number of non-empty segments.
  int scanWord(Seg* segs, int& width) {
    int count = 0;
    width = 0;
    const char* start = p;
    while (true) {
      const bool atMarker = p[0] == '*' && p[1] == '*';
      const bool atEnd = *p == '\0' || *p == ' ';
      if ((atMarker && count < MAX_SEGS - 1) || atEnd) {
        if (p > start) {
          Seg& s = segs[count++];
          s.s = start;
          s.n = static_cast<uint16_t>(p - start);
          s.bold = bold;
          s.w = m.textWidth(bold ? boldFont : regular, start, s.n);
          width += s.w;
        }
        if (atEnd) return count;
        bold = !bold;
        p += 2;
        start = p;
        continue;
      }
      p += utf8Len(p);
    }
  }

  int breakWord(const Seg* segs, const int count, const int maxWidth, Piece* out) {
    int n = 0, x = 0;
    for (int i = 0; i < count; i++) {
      const Seg& s = segs[i];
      const Font f = s.bold ? boldFont : regular;
      const char* c = s.s;
      const char* end = s.s + s.n;
      const char* pieceStart = c;
      int pieceX = x;
      while (c < end) {
        const int cl = utf8Len(c);
        const int cw = m.textWidth(f, c, static_cast<size_t>(cl));
        if (x + cw > maxWidth && (x > 0 || c > pieceStart)) {
          if (c > pieceStart) {
            out[n++] = Piece{pieceStart, static_cast<uint16_t>(c - pieceStart), static_cast<int16_t>(pieceX), f};
          }
          p = c;  // continue mid-word, in this segment's style
          bold = s.bold;
          return n;
        }
        x += cw;
        c += cl;
      }
      if (c > pieceStart) {
        out[n++] = Piece{pieceStart, static_cast<uint16_t>(c - pieceStart), static_cast<int16_t>(pieceX), f};
      }
    }
    return n;  // (cannot happen: the word did not fit)
  }

  const FontMetrics& m;
  const char* p;
  Font regular;
  Font boldFont;
  const char* label;
  bool bold = false;
};

}  // namespace

// Places elements screen by screen; with a null PageLayout it only counts screens.
class LayoutBuilder {
 public:
  LayoutBuilder(const FontMetrics& m, const Geometry& g, PageLayout* out) : m(m), g(g), out(out) {
    if (out) out->clear();
    beginScreen();
  }

  int finish() {
    endScreen();
    return screens + 1;
  }

  void gap(const int px) { pendingGap = px; }

  // A run of lines of one text: splits across screens between lines. firstLine (optional) is
  // called with the first line's top y (a bullet's dot, a step's number).
  template <typename OnFirst>
  void text(const char* s, const Font regular, const Font bold, const int x, const int width, OnFirst onFirst) {
    Wrapper w(m, s, regular, bold, nullptr);
    const int lh = m.lineHeight(regular);
    Piece pieces[MAX_PIECES];
    bool first = true;
    while (true) {
      const int n = w.nextLine(width, pieces);
      if (n == 0) break;
      if (!first) y += g.lineGap;
      const int top = place(lh, first);
      if (first) onFirst(top);
      emitLine(pieces, n, x, top);
      first = false;
    }
  }
  void text(const char* s, const Font regular, const Font bold, const int x, const int width) {
    text(s, regular, bold, x, width, [](int) {});
  }

  void figure(const char* name, const FigureSizer* sizer) {
    int w = 0, h = 0, fw = 0, fh = 0;
    uint8_t flags = 0;
    if (sizer && sizer->size(name, w, h) && w > 0 && h > 0) {
      fitFigure(w, h, g.width < g.maxFigureW ? g.width : g.maxFigureW, g.maxFigureH, fw, fh);
    } else {
      fw = g.width;
      fh = g.missingFigureH;
      flags = FIGURE_MISSING;
    }
    const int y = place(fh, true);
    addShape(Shape{ShapeKind::Figure, flags, static_cast<int16_t>(g.left + (g.width - fw) / 2), static_cast<int16_t>(y),
                   static_cast<int16_t>(fw), static_cast<int16_t>(fh), name});
  }

  // A boxed WARNING / NOTE: whole on this screen, else whole on the next, else split by lines.
  void box(const char* s, const bool warning) {
    const char* label = warning ? WARNING_LABEL : NOTE_LABEL;
    const ShapeKind kind = warning ? ShapeKind::WarningBox : ShapeKind::NoteBox;
    const int inset = g.boxBorder + g.boxPad;
    const int innerX = g.left + inset;
    const int innerW = g.width - 2 * inset;
    const int lh = m.lineHeight(Font::Body);
    Piece pieces[MAX_PIECES];

    int lines = 0;
    {
      Wrapper count(m, s, Font::Body, Font::Bold, label);
      while (count.nextLine(innerW, pieces) > 0) lines++;
    }
    const int lineStep = lh + g.lineGap;
    const int whole = 2 * inset + lines * lineStep - g.lineGap;
    const int gapNow = y > g.top ? pendingGap : 0;
    if (y + gapNow + whole > g.bottom && y > g.top && whole <= g.bandHeight()) newScreen();

    // Segment by segment (one when it fits).
    Wrapper w(m, s, Font::Body, Font::Bold, label);
    int segTop = place(0, true);
    if (segTop + 2 * inset + lh > g.bottom && segTop > g.top) {  // not even its first line fits here
      newScreen();
      segTop = g.top;
    }
    uint8_t edges = EDGE_TOP;
    int lineY = segTop + inset;
    int inSegment = 0;
    while (true) {
      const int n = w.nextLine(innerW, pieces);
      if (n == 0) break;
      if (lineY + lh + inset > g.bottom && inSegment > 0) {
        addShape(Shape{kind, edges, static_cast<int16_t>(g.left), static_cast<int16_t>(segTop),
                       static_cast<int16_t>(g.width), static_cast<int16_t>(lineY - g.lineGap + g.boxPad - segTop),
                       nullptr});
        newScreen();
        segTop = g.top;
        edges = 0;
        lineY = g.top + g.boxPad;
        inSegment = 0;
      }
      emitLine(pieces, n, innerX, lineY);
      lineY += lineStep;
      inSegment++;
      note(lineY - g.lineGap);
    }
    const int bottom = lineY - g.lineGap + inset;
    addShape(Shape{kind, static_cast<uint8_t>(edges | EDGE_BOTTOM), static_cast<int16_t>(g.left),
                   static_cast<int16_t>(segTop), static_cast<int16_t>(g.width), static_cast<int16_t>(bottom - segTop),
                   nullptr});
    y = bottom;
    note(y);
  }

  void dot(const int lineTop, const int lh) {
    addShape(Shape{ShapeKind::Dot, 0, static_cast<int16_t>(g.left + g.dotX),
                   static_cast<int16_t>(lineTop + (lh - g.dot) / 2), static_cast<int16_t>(g.dot),
                   static_cast<int16_t>(g.dot), nullptr});
  }

  void label(const char* s, const size_t n, const Font f, const int x, const int lineTop) {
    addRun(TextRun{s, static_cast<uint16_t>(n), static_cast<int16_t>(x), static_cast<int16_t>(lineTop), f});
  }

 private:
  // Room for h px (after the pending gap) on this screen, or a new screen; returns the top y and
  // advances. h == 0 only applies the gap.
  int place(const int h, const bool applyGap) {
    int gp = (applyGap && y > g.top) ? pendingGap : 0;
    if (y + gp + h > g.bottom && y > g.top) {
      newScreen();
      gp = 0;
    }
    if (applyGap) pendingGap = 0;
    const int top = y + gp;
    y = top + h;
    if (h > 0) note(y);
    return top;
  }

  void emitLine(const Piece* pieces, const int n, const int x, const int lineTop) {
    for (int i = 0; i < n; i++) {
      addRun(TextRun{pieces[i].text, pieces[i].len, static_cast<int16_t>(x + pieces[i].x),
                     static_cast<int16_t>(lineTop), pieces[i].font});
    }
  }

  void note(const int bottom) {
    if (bottom > screenBottom) screenBottom = bottom;
  }

  void addRun(const TextRun& r) {
    if (!out || !storing) return;
    if (out->runs >= PageLayout::MAX_RUNS) {
      out->cut = true;
      return;
    }
    out->runList[out->runs++] = r;
  }

  void addShape(const Shape& s) {
    if (!out || !storing) return;
    if (out->shapes >= PageLayout::MAX_SHAPES) {
      out->cut = true;
      return;
    }
    out->shapeList[out->shapes++] = s;
  }

  void beginScreen() {
    y = g.top;
    screenBottom = g.top;
    pendingGap = 0;
    if (out && storing) {
      ScreenSpan& s = out->spans[screens];
      s.firstRun = static_cast<uint16_t>(out->runs);
      s.firstShape = static_cast<uint16_t>(out->shapes);
    }
  }

  void endScreen() {
    if (out && storing) {
      ScreenSpan& s = out->spans[screens];
      s.runCount = static_cast<uint16_t>(out->runs - s.firstRun);
      s.shapeCount = static_cast<uint16_t>(out->shapes - s.firstShape);
      s.bottom = static_cast<int16_t>(screenBottom);
      out->screens = screens + 1;
    }
  }

  void newScreen() {
    endScreen();
    if (screens + 1 >= PageLayout::MAX_SCREENS) {
      // Past the last screen: keep measuring on a scratch screen, store nothing more.
      if (out && storing) out->cut = true;
      storing = false;
    } else {
      screens++;
    }
    beginScreen();
  }

 public:
  int y = 0;
  int pendingGap = 0;

 private:
  const FontMetrics& m;
  const Geometry& g;
  PageLayout* out;
  int screens = 0;  // the index of the screen being filled
  int screenBottom = 0;
  bool storing = true;
};

Geometry compactGeometry() {
  Geometry g;
  g.leadGap = 4;
  g.titleGap = 4;
  g.figureGap = 6;
  g.blockGap = 6;
  g.itemGap = 2;
  g.boxPad = 4;
  return g;
}

void PageLayout::clear() {
  runs = 0;
  shapes = 0;
  screens = 0;
  cut = false;
}

void fitFigure(const int w, const int h, const int maxW, const int maxH, int& outW, int& outH) {
  outW = w;
  outH = h;
  if (w <= 0 || h <= 0) {
    outW = outH = 0;
    return;
  }
  if (outW > maxW) {
    outH = static_cast<int>((static_cast<int64_t>(outH) * maxW + w / 2) / w);
    outW = maxW;
  }
  if (outH > maxH) {
    outW = static_cast<int>((static_cast<int64_t>(w) * maxH + h / 2) / h);
    outH = maxH;
  }
  if (outW < 1) outW = 1;
  if (outH < 1) outH = 1;
}

int layoutPage(const TopicText& text, const int page, const char* lead, const FontMetrics& metrics,
               const Geometry& geometry, const FigureSizer* sizer, PageLayout* out) {
  if (out) out->clear();
  if (page < 0 || page >= text.pageCount()) return 0;
  const Geometry& g = geometry;
  LayoutBuilder b(metrics, g, out);
  const PageText& pt = text.page(page);

  if (lead && *lead) {
    b.text(lead, Font::Lead, Font::Lead, g.left, g.width);
    b.gap(g.leadGap);
  }
  b.text(pt.title, Font::Title, Font::Title, g.left, g.width);
  b.gap(g.titleGap);
  if (pt.figure) {
    b.figure(pt.figure, sizer);
    b.gap(g.figureGap);
  }
  const int textX = g.left + g.indent;
  const int textW = g.width - g.indent;
  const int lh = metrics.lineHeight(Font::Body);
  for (int i = 0; i < pt.blockCount; i++) {
    const Block& blk = text.block(pt.firstBlock + i);
    switch (blk.kind) {
      case BlockKind::Para:
        b.text(blk.text, Font::Body, Font::Bold, g.left, g.width);
        break;
      case BlockKind::Bullet:
        b.text(blk.text, Font::Body, Font::Bold, textX, textW, [&](const int top) { b.dot(top, lh); });
        break;
      case BlockKind::Step:
        b.text(blk.text, Font::Body, Font::Bold, textX, textW, [&](const int top) {
          const int w = metrics.textWidth(Font::Bold, blk.marker, blk.markerLen);
          b.label(blk.marker, blk.markerLen, Font::Bold, textX - g.numberGap - w, top);
        });
        break;
      case BlockKind::Warning:
      case BlockKind::Note:
        b.box(blk.text, blk.kind == BlockKind::Warning);
        break;
    }
    const bool list = blk.kind == BlockKind::Bullet || blk.kind == BlockKind::Step;
    const bool nextSameList = i + 1 < pt.blockCount && text.block(pt.firstBlock + i + 1).kind == blk.kind && list;
    b.gap(nextSameList ? g.itemGap : g.blockGap);
  }
  return b.finish();
}

int topicScreens(const TopicText& text, const char* lead, const FontMetrics& metrics, const Geometry& geometry,
                 const FigureSizer* sizer, uint8_t* perPage, const int cap) {
  int total = 0;
  for (int p = 0; p < text.pageCount(); p++) {
    const int n = layoutPage(text, p, p == 0 ? lead : nullptr, metrics, geometry, sizer, nullptr);
    if (perPage && p < cap) perPage[p] = static_cast<uint8_t>(n);
    total += n;
  }
  return total;
}

size_t copyRun(const TextRun& run, char* out, const size_t cap) {
  if (cap == 0) return 0;
  const size_t n = copyCut(std::string_view(run.text, run.len), out, cap);
  return n;
}

size_t breadcrumb(const char* const* parts, const int count, const FontMetrics& metrics, const Font font,
                  const int maxWidth, char* out, const size_t cap) {
  if (cap == 0) return 0;
  out[0] = '\0';
  if (count <= 0) return 0;
  constexpr char SEP[] = " / ";
  constexpr char DROPPED[] = "..";
  constexpr char CUT[] = "...";
  // Try from all parts down to only the last one (with ".." in front when some are dropped).
  for (int from = 0; from < count; from++) {
    size_t len = 0;
    bool fits = true;
    auto put = [&](const char* s, const bool upper) {
      for (; *s && fits; s++) {
        if (len + 1 >= cap) {
          fits = false;
          break;
        }
        out[len++] = upper ? upperAscii(*s) : *s;
      }
    };
    if (from > 0) {
      put(DROPPED, false);
      put(SEP, false);
    }
    for (int i = from; i < count && fits; i++) {
      if (i > from) put(SEP, false);
      put(parts[i] ? parts[i] : "", true);
    }
    out[len] = '\0';
    if (fits && metrics.textWidth(font, out, len) <= maxWidth) return len;
    if (from == count - 1) {
      // Only the last part left and still too wide: the last part alone, cut with "...".
      len = 0;
      fits = true;
      put(parts[from] ? parts[from] : "", true);
      out[len] = '\0';
      if (metrics.textWidth(font, out, len) <= maxWidth) return len;
      size_t keep = len;
      while (keep > 0) {
        keep--;
        while (keep > 0 && (static_cast<uint8_t>(out[keep]) & 0xC0) == 0x80) keep--;
        if (keep + sizeof(CUT) > cap) continue;
        std::memcpy(out + keep, CUT, sizeof(CUT));  // with its NUL
        if (metrics.textWidth(font, out, keep + sizeof(CUT) - 1) <= maxWidth) return keep + sizeof(CUT) - 1;
      }
      out[0] = '\0';
      return 0;
    }
  }
  return std::strlen(out);
}

}  // namespace gd
