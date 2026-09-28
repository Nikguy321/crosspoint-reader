#include "QuoteCard.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>

#include "CardDraw.h"
#include "fontIds.h"

namespace sleepcards::quote {
namespace {

constexpr const char* ELLIPSIS = "\xE2\x80\xA6";  // U+2026
constexpr size_t ELLIPSIS_LEN = 3;

bool isSpace(const unsigned char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}
bool isContinuation(const unsigned char c) { return (c & 0xC0) == 0x80; }

// "--", an em dash (U+2014) or a horizontal bar (U+2015) opens an attribution line.
bool isAttributionPrefix(const unsigned char* p, const size_t n) {
  if (n >= 2 && p[0] == '-' && p[1] == '-') return true;
  return n >= 3 && p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x94 || p[2] == 0x95);
}

// An attribution line - except that an entry's FIRST line opening with an em dash or a bar is
// dialogue ("— Where are you going?"), not a name: only "--" can attribute an entry with no text.
bool opensAttribution(const unsigned char* p, const size_t n, const bool firstLine) {
  if (!isAttributionPrefix(p, n)) return false;
  return !firstLine || p[0] == '-';
}

// Length of s[0..n) without a trailing partial UTF-8 sequence.
size_t utf8CompleteLength(const char* s, size_t n) {
  if (n == 0) return 0;
  size_t lead = n;
  while (lead > 0 && isContinuation(static_cast<unsigned char>(s[lead - 1]))) lead--;
  if (lead == 0) return 0;  // only continuation bytes: nothing sound
  const auto c = static_cast<unsigned char>(s[lead - 1]);
  size_t need = 1;
  if (c >= 0xF0) {
    need = 4;
  } else if (c >= 0xE0) {
    need = 3;
  } else if (c >= 0xC0) {
    need = 2;
  }
  return (n - (lead - 1) >= need) ? n : lead - 1;
}

// Appends src[0..n) to dst (len in/out), whitespace runs as one space, a space before it when
// dst already has text. Stops at cap - 1 on a character boundary.
void appendCollapsed(char* dst, const size_t cap, size_t& len, const char* src, const size_t n) {
  bool pendingSpace = len > 0;
  for (size_t i = 0; i < n; i++) {
    const auto c = static_cast<unsigned char>(src[i]);
    if (isSpace(c)) {
      pendingSpace = len > 0;
      continue;
    }
    if (pendingSpace) {
      if (len + 1 >= cap) break;
      dst[len++] = ' ';
      pendingSpace = false;
    }
    if (len + 1 >= cap) break;
    dst[len++] = static_cast<char>(c);
  }
  len = utf8CompleteLength(dst, len);
  dst[len] = '\0';
}

}  // namespace

// ---- randomness ------------------------------------------------------------------------------------

uint32_t nextRandom(uint32_t& state) {
  // splitmix32: every seed (0 included) gives a well-mixed sequence.
  state += 0x9E3779B9u;
  uint32_t z = state;
  z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
  z = (z ^ (z >> 13)) * 0xC2B2AE35u;
  return z ^ (z >> 16);
}

uint32_t randomBelow(uint32_t& state, const uint32_t n) {
  if (n == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(nextRandom(state)) * n) >> 32);
}

uint32_t hashInk(const char* s, const size_t n, uint32_t h) {
  for (size_t i = 0; i < n; i++) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (isSpace(c)) continue;
    h = (h ^ c) * 16777619u;
  }
  return h;
}

// ---- the scanner -----------------------------------------------------------------------------------

void EntryScanner::feed(const char* data, const size_t n) {
  for (size_t i = 0; i < n; i++, pos_++) {
    const auto c = static_cast<unsigned char>(data[i]);
    // Lines end in LF, CRLF or a lone CR (classic Mac); the LF of a CRLF ends nothing more.
    const bool lfAfterCr = c == '\n' && lastWasCr_;
    lastWasCr_ = c == '\r';
    if (lfAfterCr) continue;
    if (c == '\n' || c == '\r') {
      endLine();
      continue;
    }
    if (isSpace(c)) continue;
    if (lineInk_ == 0) {
      lineFirstInk_ = pos_;
      lineHash_ = inEntry_ ? entry_.hash : HASH_SEED;
    }
    if (lineInk_ < sizeof(prefix_)) prefix_[lineInk_] = c;
    if (lineInk_ < UINT16_MAX) lineInk_++;
    lineHash_ = (lineHash_ ^ c) * 16777619u;
    lineInkEnd_ = pos_ + 1;
  }
}

void EntryScanner::endLine() {
  const bool blank = lineInk_ == 0 || (lineInk_ == 1 && prefix_[0] == '%');
  if (blank) {
    if (inEntry_) endEntry();
  } else {
    const bool firstLine = !inEntry_;
    if (!inEntry_) {
      inEntry_ = true;
      entry_ = EntrySpan{};
      entry_.offset = lineFirstInk_;
      sawAttribution_ = false;
    }
    if (opensAttribution(prefix_, std::min<size_t>(lineInk_, sizeof(prefix_)), firstLine)) {
      sawAttribution_ = true;
    } else if (!sawAttribution_) {
      entry_.hasText = true;
    }
    entry_.hash = lineHash_;
    entry_.length = lineInkEnd_ - entry_.offset;
  }
  lineInk_ = 0;
}

void EntryScanner::endEntry() {
  inEntry_ = false;
  if (callback_) callback_(user_, entry_);
}

void EntryScanner::finish(const bool reachedEof) {
  if (!reachedEof) {
    inEntry_ = false;  // cut at the cap: incomplete
    lineInk_ = 0;
    return;
  }
  endLine();
  if (inEntry_) endEntry();
}

// ---- parsing ---------------------------------------------------------------------------------------

namespace {

// "Be kind. -- Plato" on the entry's last line: the text after the last " -- " is the
// attribution when both sides have something and the name is short (a name, not a sentence).
void splitInlineAttribution(char* text, size_t& textLen, const size_t lastLineStart, char* attribution,
                            const size_t attributionCap) {
  constexpr size_t MAX_INLINE_NAME = 80;
  const char* found = nullptr;
  for (const char* p = std::strstr(text + lastLineStart, " -- "); p != nullptr; p = std::strstr(p + 1, " -- ")) {
    found = p;
  }
  if (found == nullptr) return;
  const char* name = found + 4;
  while (*name == ' ' || *name == '-') name++;
  const size_t nameLen = std::strlen(name);
  size_t head = static_cast<size_t>(found - text);
  while (head > 0 && text[head - 1] == ' ') head--;
  if (nameLen == 0 || nameLen > MAX_INLINE_NAME || head == 0) return;
  size_t attributionLen = 0;
  appendCollapsed(attribution, attributionCap, attributionLen, name, nameLen);
  textLen = head;
  text[textLen] = '\0';
}

}  // namespace

bool parseEntry(const char* raw, size_t n, char* text, const size_t textCap, char* attribution,
                const size_t attributionCap) {
  if (!text || textCap == 0 || !attribution || attributionCap == 0) return false;
  text[0] = '\0';
  attribution[0] = '\0';
  if (!raw) return false;
  if (n >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB &&
      static_cast<unsigned char>(raw[2]) == 0xBF) {
    raw += 3;
    n -= 3;
  }
  size_t textLen = 0;
  size_t attributionLen = 0;
  bool inAttribution = false;
  bool firstLine = true;
  size_t lastLineStart = 0;  // where the last text line begins in `text`
  size_t i = 0;
  while (i < n) {
    size_t end = i;
    while (end < n && raw[end] != '\n' && raw[end] != '\r') end++;
    size_t a = i;
    while (a < end && isSpace(static_cast<unsigned char>(raw[a]))) a++;
    if (a < end) {
      const auto* line = reinterpret_cast<const unsigned char*>(raw + a);
      if (opensAttribution(line, end - a, firstLine)) {
        inAttribution = true;
        // Strip the dashes (ASCII or U+2014/U+2015) that open it.
        while (a < end) {
          if (raw[a] == '-' || isSpace(static_cast<unsigned char>(raw[a]))) {
            a++;
          } else if (end - a >= 3 && isAttributionPrefix(reinterpret_cast<const unsigned char*>(raw + a), 3) &&
                     raw[a] != '-') {
            a += 3;
          } else {
            break;
          }
        }
      }
      if (inAttribution) {
        appendCollapsed(attribution, attributionCap, attributionLen, raw + a, end - a);
      } else {
        lastLineStart = textLen;
        appendCollapsed(text, textCap, textLen, raw + a, end - a);
      }
      firstLine = false;
    }
    // A CRLF or a lone CR ends the line as a LF does.
    i = (end + 1 < n && raw[end] == '\r' && raw[end + 1] == '\n') ? end + 2 : end + 1;
  }
  if (attributionLen == 0 && textLen > 0)
    splitInlineAttribution(text, textLen, lastLineStart, attribution, attributionCap);
  return textLen > 0;
}

bool snippetText(const char* summary, char* out, const size_t cap) {
  if (!out || cap < 8) return false;
  out[0] = '\0';
  if (!summary) return false;
  const size_t rawLen = std::strlen(summary);
  const bool wasCut = rawLen >= SUMMARY_CUT_LEN;

  // Leave room for an ellipsis at each end.
  char body[CardSnippet::TEXT_CAP];
  size_t len = 0;
  appendCollapsed(body, std::min(sizeof(body), cap - 2 * ELLIPSIS_LEN), len, summary, rawLen);

  size_t visible = 0;
  for (size_t i = 0; i < len; i++) {
    if (!isContinuation(static_cast<unsigned char>(body[i])) && body[i] != ' ') visible++;
  }
  if (visible < 3) return false;

  if (wasCut) {
    // Keep whole words: drop the (probably partial) last one when there are enough before it,
    // then any trailing punctuation that would sit badly against the ellipsis.
    const char* lastSpace = std::strrchr(body, ' ');
    if (lastSpace && static_cast<size_t>(lastSpace - body) >= len / 3) len = static_cast<size_t>(lastSpace - body);
    while (len > 0 && std::strchr(" ,;:-", body[len - 1]) != nullptr) len--;
    body[len] = '\0';
  }
  const bool endsSentence = len > 0 && std::strchr(".!?", body[len - 1]) != nullptr;
  const bool startsMidSentence = body[0] >= 'a' && body[0] <= 'z';

  std::snprintf(out, cap, "%s%s%s", startsMidSentence ? ELLIPSIS : "", body, wasCut && !endsSentence ? ELLIPSIS : "");
  return true;
}

// ---- choosing --------------------------------------------------------------------------------------

Pick chooseSource(const QuoteSource mode, const PoolStats file, const PoolStats bookmarks, uint32_t& rng) {
  const bool hasFile = file.count > 0;
  const bool hasBookmarks = bookmarks.count > 0;
  if (!hasFile && !hasBookmarks) return Pick::None;
  if (mode == QuoteSource::File) return hasFile ? Pick::File : Pick::Bookmarks;
  if (mode == QuoteSource::Bookmarks) return hasBookmarks ? Pick::Bookmarks : Pick::File;
  const bool freshFile = file.fresh > 0;
  const bool freshBookmarks = bookmarks.fresh > 0;
  if (freshFile != freshBookmarks) return freshFile ? Pick::File : Pick::Bookmarks;
  if (hasFile != hasBookmarks) return hasFile ? Pick::File : Pick::Bookmarks;
  return randomBelow(rng, 2) == 0 ? Pick::File : Pick::Bookmarks;
}

void FreshPicker::offer(const uint32_t hash, const uint32_t value, const uint32_t value2) {
  if (count_ == UINT32_MAX) return;
  count_++;
  if (lastHash_ != 0 && hash == lastHash_) {
    if (count_ - fresh_ == 1) {  // the first stale one
      staleValue_ = value;
      staleValue2_ = value2;
    }
    return;
  }
  fresh_++;
  if (randomBelow(rng_, fresh_) == 0) {
    freshValue_ = value;
    freshValue2_ = value2;
    freshHash_ = hash;
  }
}

// ---- layout ----------------------------------------------------------------------------------------

int wrapLines(const char* text, const int width, const int fontIndex, const MeasureFn measure, void* user,
              LineSpan* lines, const int maxLines, bool& overflow) {
  overflow = false;
  if (!text || !measure || !lines || maxLines <= 0) {
    overflow = text && *text;
    return 0;
  }
  const size_t total = std::strlen(text);
  size_t pos = 0;
  int count = 0;
  while (true) {
    while (pos < total && text[pos] == ' ') pos++;
    if (pos >= total) break;
    if (count == maxLines) {
      overflow = true;
      break;
    }
    const size_t start = pos;
    size_t end = start;  // committed end of this line
    size_t p = start;
    while (true) {
      size_t ws = p;
      while (ws < total && text[ws] == ' ') ws++;
      if (ws >= total) break;
      size_t we = ws;
      while (we < total && text[we] != ' ') we++;
      if (measure(user, fontIndex, text + start, we - start, false) <= width) {
        end = we;
        p = we;
        continue;
      }
      if (end == start) {
        // One word wider than the line: as many characters as fit, at least one.
        size_t cut = start;
        size_t q = start;
        while (q < we) {
          size_t next = q + 1;
          while (next < we && isContinuation(static_cast<unsigned char>(text[next]))) next++;
          if (cut > start && measure(user, fontIndex, text + start, next - start, false) > width) break;
          cut = next;
          q = next;
        }
        end = cut;
      }
      break;
    }
    if (end - start > UINT16_MAX || start > UINT16_MAX) {
      overflow = true;
      break;
    }
    lines[count].start = static_cast<uint16_t>(start);
    lines[count].len = static_cast<uint16_t>(end - start);
    count++;
    pos = end;
  }
  return count;
}

int ellipsizeLine(const char* text, const LineSpan& line, const int width, const int fontIndex, const MeasureFn measure,
                  void* user) {
  const char* s = text + line.start;
  size_t len = line.len;
  const auto trimmed = [&](size_t n) {
    while (n > 0 && std::strchr(" ,;:-", s[n - 1]) != nullptr) n--;
    return n;
  };
  len = trimmed(len);
  if (measure(user, fontIndex, s, len, true) <= width) return static_cast<int>(len);
  // Whole words first.
  while (len > 0) {
    size_t sp = len;
    while (sp > 0 && s[sp - 1] != ' ') sp--;
    if (sp == 0) break;
    len = trimmed(sp - 1);
    if (measure(user, fontIndex, s, len, true) <= width) return static_cast<int>(len);
  }
  // Then characters.
  len = line.len;
  while (len > 0) {
    len--;
    while (len > 0 && isContinuation(static_cast<unsigned char>(s[len]))) len--;
    if (measure(user, fontIndex, s, len, true) <= width) return static_cast<int>(len);
  }
  return 0;
}

bool fitText(const char* text, const int width, const int* lineHeights, const int* maxHeights, const int count,
             const MeasureFn measure, void* user, FitResult& out) {
  out = FitResult{};
  if (!text || !*text || !lineHeights || !maxHeights || count <= 0 || width <= 0) return false;
  for (int fi = 0; fi < count; fi++) {
    const int lh = lineHeights[fi];
    if (lh <= 0) continue;
    const int maxLines = std::min(MAX_LINES, maxHeights[fi] / lh);
    if (maxLines <= 0) continue;
    bool overflow = false;
    const int n = wrapLines(text, width, fi, measure, user, out.lines, maxLines, overflow);
    out.fontIndex = fi;
    out.lineCount = n;
    out.truncated = overflow;
    if (!overflow && n > 0) return true;
  }
  return out.fontIndex >= 0 && out.lineCount > 0;
}

void splitAttribution(const char* attribution, char* name, const size_t nameCap, char* work, const size_t workCap) {
  if (!name || nameCap == 0 || !work || workCap == 0) return;
  name[0] = '\0';
  work[0] = '\0';
  if (!attribution) return;
  const size_t total = std::strlen(attribution);
  size_t split = total;
  if (const char* comma = std::strstr(attribution, ", ")) {
    const char* rest = comma + 2;
    while (*rest == ' ') rest++;
    size_t visible = 0;
    for (const char* p = rest; *p; p++) {
      if (!isContinuation(static_cast<unsigned char>(*p)) && *p != ' ') visible++;
    }
    const bool suffix = std::strncmp(rest, "Jr", 2) == 0 || std::strncmp(rest, "Sr", 2) == 0;
    if (visible >= 4 && !suffix && comma > attribution) {
      split = static_cast<size_t>(comma - attribution);
      size_t len = 0;
      appendCollapsed(work, workCap, len, rest, std::strlen(rest));
    }
  }
  size_t len = 0;
  appendCollapsed(name, nameCap, len, attribution, split);
}

}  // namespace sleepcards::quote

// ---- the card --------------------------------------------------------------------------------------

namespace sleepcards {
namespace {

using namespace quote;

// Quote sizes, largest first. The serif italic at four sizes with the opening mark; then, for a
// long passage, the same smallest serif without the mark and with tight leading, then the smaller
// UI faces (no italic).
struct Candidate {
  int fontId;
  EpdFontFamily::Style style;
  int leadingPercent;
  bool mark;
};
constexpr Candidate CANDIDATES[] = {
    {NOTOSERIF_18_FONT_ID, EpdFontFamily::ITALIC, 115, true},   // a line or a few
    {NOTOSERIF_16_FONT_ID, EpdFontFamily::ITALIC, 115, true},   //
    {NOTOSERIF_14_FONT_ID, EpdFontFamily::ITALIC, 115, true},   //
    {NOTOSERIF_12_FONT_ID, EpdFontFamily::ITALIC, 112, true},   //
    {NOTOSERIF_12_FONT_ID, EpdFontFamily::ITALIC, 100, false},  // a paragraph: no mark, tight
    {UI_12_FONT_ID, EpdFontFamily::REGULAR, 100, false},        // a long passage (~500+ characters)
    {UI_10_FONT_ID, EpdFontFamily::REGULAR, 100, false},        //
    {SMALL_FONT_ID, EpdFontFamily::REGULAR, 100, false},        // up to ENTRY_CAP
};
constexpr int CANDIDATE_COUNT = static_cast<int>(sizeof(CANDIDATES) / sizeof(CANDIDATES[0]));
constexpr int CENTRE_MAX_LINES = 4;  // a quote this short is centred, a longer one flush left

// The attribution: "- Name" (serif), the work (serif italic), and for a bookmark a note (UI).
constexpr int NAME_FONT = NOTOSERIF_14_FONT_ID;
constexpr int COMPACT_NAME_FONT = NOTOSERIF_12_FONT_ID;  // under a long passage
constexpr int WORK_FONT = NOTOSERIF_12_FONT_ID;
constexpr int NOTE_FONT = UI_10_FONT_ID;

// The opening mark: U+201C from the 18 pt serif, magnified (one glyph: cheap even in soft float).
constexpr const char* OPEN_QUOTE = "\xE2\x80\x9C";
constexpr int MARK_FONT = NOTOSERIF_18_FONT_ID;
constexpr float MARK_SCALE = 4.0f;

constexpr size_t LINE_BUF = 320;
constexpr size_t PART_CAP = 160;
constexpr int SIDE = 44;         // left/right margin of the text
constexpr int AREA_TOP = 48;     // the block is centred between these two
constexpr int AREA_BOTTOM = 32;  // above the footer

// Everything the card needs besides the framebuffer, in one nothrow allocation (~6 KB) rather
// than on the small task stack.
struct Work {
  char chunk[READ_CHUNK];
  char text[TEXT_CAP];
  char attribution[ATTRIBUTION_CAP];
  char name[PART_CAP];
  char work[PART_CAP];
  char note[PART_CAP];
  char line[LINE_BUF];
  FitResult fit;
  FitResult nameFit;
  FitResult workFit;
  FitResult noteFit;
};

struct Measure {
  GfxRenderer* renderer;
  const int* fontIds;  // per candidate index
  const EpdFontFamily::Style* styles;
  char* buf;
  size_t cap;
};

int measureRun(void* user, const int fontIndex, const char* s, const size_t n, const bool ellipsis) {
  const auto* m = static_cast<const Measure*>(user);
  if (n + ELLIPSIS_LEN + 1 > m->cap) return INT_MAX / 2;  // never fits: the wrap breaks sooner
  std::memcpy(m->buf, s, n);
  size_t len = n;
  if (ellipsis) {
    std::memcpy(m->buf + len, ELLIPSIS, ELLIPSIS_LEN);
    len += ELLIPSIS_LEN;
  }
  m->buf[len] = '\0';
  return m->renderer->getTextWidth(m->fontIds[fontIndex], m->buf, m->styles[fontIndex]);
}

// Line i of a fit as a terminated string in m.buf, with "..." on the last line of a truncated fit.
const char* lineText(const char* text, const FitResult& fit, const int i, const int width, Measure& m) {
  const LineSpan& span = fit.lines[i];
  size_t len = span.len;
  const bool ellipsis = fit.truncated && i == fit.lineCount - 1;
  if (ellipsis) len = static_cast<size_t>(ellipsizeLine(text, span, width, fit.fontIndex, &measureRun, &m));
  len = std::min(len, m.cap - ELLIPSIS_LEN - 1);
  std::memmove(m.buf, text + span.start, len);
  if (ellipsis) {
    std::memcpy(m.buf + len, ELLIPSIS, ELLIPSIS_LEN);
    len += ELLIPSIS_LEN;
  }
  m.buf[len] = '\0';
  return m.buf;
}

void drawLines(GfxRenderer& r, const char* text, const FitResult& fit, Measure& m, const int x, const int y,
               const int width, const int lineHeight, const draw::Align align) {
  if (fit.fontIndex < 0) return;
  const int fontId = m.fontIds[fit.fontIndex];
  const EpdFontFamily::Style style = m.styles[fit.fontIndex];
  for (int i = 0; i < fit.lineCount; i++) {
    const char* s = lineText(text, fit, i, width, m);
    int lx = x;
    if (align != draw::Align::Left) {
      const int lw = r.getTextWidth(fontId, s, style);
      lx = align == draw::Align::Center ? x + (width - lw) / 2 : x + width - lw;
    }
    r.drawText(fontId, lx, y + i * lineHeight, s, true, style);
  }
}

// One attribution part in a single font: fitted to maxLines, height 0 when empty.
int fitPart(const char* text, Measure& m, const int width, const int lineHeight, const int maxLines, FitResult& fit) {
  fit = FitResult{};
  if (!text || !text[0]) return 0;
  const int lhs[] = {lineHeight};
  const int maxH[] = {lineHeight * maxLines};
  return fitText(text, width, lhs, maxH, 1, &measureRun, &m, fit) ? fit.lineCount * lineHeight : 0;
}

// Where the magnified mark's ink lands relative to the y/x it is drawn at, from the glyph itself.
struct MarkInk {
  int top = 0;     // ink top below the draw y
  int height = 0;  // ink height
  int left = 0;    // ink left of the draw x
  int width = 0;
};
MarkInk markInk(const GfxRenderer& r) {
  MarkInk ink;
  const auto& fonts = r.getFontMap();
  const auto it = fonts.find(MARK_FONT);
  const EpdGlyph* glyph = it == fonts.end() ? nullptr : it->second.getGlyph(0x201C, EpdFontFamily::REGULAR);
  const EpdFontData* data = it == fonts.end() ? nullptr : it->second.getData(EpdFontFamily::REGULAR);
  if (!glyph || !data) return ink;
  ink.top = static_cast<int>((data->ascender - glyph->top) * MARK_SCALE);
  ink.height = static_cast<int>(glyph->height * MARK_SCALE + 0.5f);
  ink.left = static_cast<int>(glyph->left * MARK_SCALE);
  ink.width = static_cast<int>(glyph->width * MARK_SCALE + 0.5f);
  return ink;
}

uint32_t readLastHash(const CardIo& io) {
  char buf[16] = {};
  const int32_t got = io.readFileAt(STATE_PATH, 0, buf, sizeof(buf) - 1);
  if (got <= 0) return 0;
  buf[got] = '\0';
  unsigned long v = 0;
  if (std::sscanf(buf, "%8lx", &v) != 1) return 0;
  return static_cast<uint32_t>(v);
}

void writeLastHash(const CardIo& io, const uint32_t hash) {
  char buf[16];
  const int n = std::snprintf(buf, sizeof(buf), "%08lx\n", static_cast<unsigned long>(hash));
  if (n > 0) io.writeFile(STATE_PATH, buf, static_cast<size_t>(n));
}

void onEntry(void* user, const EntrySpan& entry) {
  if (!entry.hasText || entry.length == 0 || entry.length > ENTRY_CAP) return;
  static_cast<FreshPicker*>(user)->offer(entry.hash, entry.offset, entry.length);
}

// Every usable entry of the quotes file into the picker: at most FILE_READ_CAP bytes in
// READ_CHUNK reads, so at most 16 file opens.
void scanQuotesFile(const CardIo& io, char* chunk, FreshPicker& picker) {
  const int32_t size = io.fileSize(QUOTES_PATH);
  if (size <= 0) return;
  const uint32_t limit = std::min<uint32_t>(static_cast<uint32_t>(size), FILE_READ_CAP);
  EntryScanner scanner(&onEntry, &picker);
  uint32_t offset = 0;
  bool failed = false;
  for (int reads = 0; offset < limit && reads <= static_cast<int>(FILE_READ_CAP / READ_CHUNK); reads++) {
    const auto want = static_cast<size_t>(std::min<uint32_t>(READ_CHUNK, limit - offset));
    const int32_t got = io.readFileAt(QUOTES_PATH, offset, chunk, want);
    if (got <= 0) {
      failed = true;
      break;
    }
    size_t skip = 0;
    if (offset == 0 && got >= 3 && static_cast<unsigned char>(chunk[0]) == 0xEF &&
        static_cast<unsigned char>(chunk[1]) == 0xBB && static_cast<unsigned char>(chunk[2]) == 0xBF) {
      skip = 3;  // a byte order mark
      scanner.skip(skip);
    }
    scanner.feed(chunk + skip, static_cast<size_t>(got) - skip);
    offset += static_cast<uint32_t>(got);
    if (static_cast<size_t>(got) < want) break;
  }
  scanner.finish(!failed && offset >= static_cast<uint32_t>(size));
}

// Usable bookmarks of the open book into the picker (value = index into snippets).
int scanBookmarks(const CardIo& io, CardSnippet* snippets, char* scratch, const size_t scratchCap,
                  FreshPicker& picker) {
  const int count = std::max(0, std::min(io.loadBookmarks(snippets, MAX_BOOKMARKS), MAX_BOOKMARKS));
  for (int i = 0; i < count; i++) {
    snippets[i].text[sizeof(snippets[i].text) - 1] = '\0';
    snippets[i].label[sizeof(snippets[i].label) - 1] = '\0';
    if (!snippetText(snippets[i].text, scratch, scratchCap)) continue;
    picker.offer(hashInk(snippets[i].text, std::strlen(snippets[i].text)), static_cast<uint32_t>(i));
  }
  return count;
}

// A bookmark's attribution: the author, the book's title, and a note of where it was marked.
void bookmarkAttribution(const CardIo& io, const CardSnippet& snippet, Work& w) {
  auto book = makeUniqueNoThrow<CardBook>();
  if (book && io.loadBook(*book)) {
    std::snprintf(w.name, sizeof(w.name), "%s", book->author);
    std::snprintf(w.work, sizeof(w.work), "%s", book->title);
  }
  char where[48];
  if (snippet.fraction >= 0.0f && snippet.fraction <= 1.0f) {
    // "at 0%" reads like "nowhere": the first page is at 1 %.
    const int percent = std::max(snippet.fraction > 0.0f ? 1 : 0, static_cast<int>(snippet.fraction * 100.0f + 0.5f));
    std::snprintf(where, sizeof(where), tr(STR_QUOTE_BOOKMARK_AT), percent);
  } else {
    std::snprintf(where, sizeof(where), "%s", tr(STR_QUOTE_BOOKMARK));
  }
  if (snippet.label[0]) {
    // U+201C label U+201D, a middle dot, then where.
    std::snprintf(w.note, sizeof(w.note), "\xE2\x80\x9C%s\xE2\x80\x9D \xC2\xB7 %s", snippet.label, where);
  } else {
    std::snprintf(w.note, sizeof(w.note), "%s", where);
  }
}

}  // namespace

bool renderQuoteCard(const CardContext& ctx, GfxRenderer& renderer) {
  if (!ctx.io) return false;
  const CardIo& io = *ctx.io;
  auto work = makeUniqueNoThrow<Work>();
  if (!work) {
    LOG_ERR("CARD", "quote: no memory");
    return false;
  }
  Work& w = *work;
  uint32_t rng = ctx.seed ^ 0x51C0FFEEu;
  const uint32_t lastHash = readLastHash(io);
  const QuoteSource mode = ctx.settings.quoteSource;

  // The candidates: the Setting's source, and the other one only when that has nothing.
  FreshPicker filePick(lastHash, rng);
  FreshPicker bookmarkPick(lastHash, rng);
  std::unique_ptr<CardSnippet[]> snippets;
  bool fileScanned = false;
  bool bookmarksScanned = false;
  const auto scanFile = [&] {
    if (fileScanned) return;
    fileScanned = true;
    scanQuotesFile(io, w.chunk, filePick);
  };
  const auto scanMarks = [&] {
    if (bookmarksScanned) return;
    bookmarksScanned = true;
    if (!ctx.bookPath || !ctx.bookPath[0]) return;
    snippets = makeUniqueNoThrow<CardSnippet[]>(MAX_BOOKMARKS);  // ~6 KB, only when bookmarks are wanted
    if (snippets) scanBookmarks(io, snippets.get(), w.text, sizeof(w.text), bookmarkPick);
  };
  if (mode != QuoteSource::Bookmarks) scanFile();
  if (mode != QuoteSource::File) scanMarks();
  if (!filePick.has()) scanMarks();
  if (!bookmarkPick.has()) scanFile();

  w.name[0] = w.work[0] = w.note[0] = '\0';
  uint32_t shownHash = 0;
  switch (chooseSource(mode, filePick.stats(), bookmarkPick.stats(), rng)) {
    case Pick::File: {
      const uint32_t offset = filePick.value();
      const uint32_t length = std::min<uint32_t>(filePick.value2(), ENTRY_CAP);
      const int32_t got = io.readFileAt(QUOTES_PATH, offset, w.chunk, length);
      if (got <= 0 || !parseEntry(w.chunk, static_cast<size_t>(got), w.text, sizeof(w.text), w.attribution,
                                  sizeof(w.attribution))) {
        LOG_ERR("CARD", "quote: entry at %lu unreadable", static_cast<unsigned long>(offset));
        return false;
      }
      splitAttribution(w.attribution, w.name, sizeof(w.name), w.work, sizeof(w.work));
      shownHash = filePick.hash();
      break;
    }
    case Pick::Bookmarks: {
      const CardSnippet& snippet = snippets[bookmarkPick.value()];
      if (!snippetText(snippet.text, w.text, sizeof(w.text))) return false;
      bookmarkAttribution(io, snippet, w);
      shownHash = bookmarkPick.hash();
      break;
    }
    case Pick::None:
      return false;  // nothing to quote: the logo screen
  }
  if (w.name[0]) {
    // "- Name" with an em dash.
    std::snprintf(w.attribution, sizeof(w.attribution), "\xE2\x80\x94 %s", w.name);
    std::snprintf(w.name, sizeof(w.name), "%s", w.attribution);
  }

  // ---- layout ----
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int textX = SIDE;
  const int textW = screenW - 2 * SIDE;
  const int areaTop = AREA_TOP;
  const int areaBottom = screenH - FOOTER_HEIGHT - AREA_BOTTOM;

  // The opening mark's box: exactly its ink (none when the font lacks the glyph).
  const MarkInk ink = markInk(renderer);
  const int markH = ink.height;
  constexpr int MARK_GAP = 14;
  constexpr int NOTE_GAP = 8;

  const EpdFontFamily::Style regular[] = {EpdFontFamily::REGULAR};
  const EpdFontFamily::Style italic[] = {EpdFontFamily::ITALIC};
  const int workFont[] = {WORK_FONT};
  const int noteFont[] = {NOTE_FONT};
  Measure workM{&renderer, workFont, italic, w.line, sizeof(w.line)};
  Measure noteM{&renderer, noteFont, regular, w.line, sizeof(w.line)};
  const int workLh = renderer.getLineHeight(WORK_FONT);
  const int noteLh = renderer.getLineHeight(NOTE_FONT);
  const int workH = fitPart(w.work, workM, textW, workLh, 2, w.workFit);
  const int noteH = fitPart(w.note, noteM, textW, noteLh, 1, w.noteFit);

  int fontIds[CANDIDATE_COUNT];
  EpdFontFamily::Style styles[CANDIDATE_COUNT];
  int lineHeights[CANDIDATE_COUNT];
  int maxHeights[CANDIDATE_COUNT];
  for (int i = 0; i < CANDIDATE_COUNT; i++) {
    fontIds[i] = CANDIDATES[i].fontId;
    styles[i] = CANDIDATES[i].style;
    lineHeights[i] = renderer.getLineHeight(CANDIDATES[i].fontId) * CANDIDATES[i].leadingPercent / 100;
  }
  Measure m{&renderer, fontIds, styles, w.line, sizeof(w.line)};

  // The attribution first, the quote gets what is left. When that pushes the quote past the
  // serif-with-mark sizes, a second pass sets the name smaller and closer to make room.
  int nameFont[] = {NAME_FONT};
  Measure nameM{&renderer, nameFont, regular, w.line, sizeof(w.line)};
  int nameLh = 0;
  int nameH = 0;
  int attributionH = 0;
  int attributionGap = 0;
  for (int pass = 0; pass < 2; pass++) {
    const bool compact = pass == 1;
    nameFont[0] = compact ? COMPACT_NAME_FONT : NAME_FONT;
    nameLh = renderer.getLineHeight(nameFont[0]);
    nameH = fitPart(w.name, nameM, textW, nameLh, 2, w.nameFit);
    attributionH = nameH + workH + (noteH > 0 ? NOTE_GAP + noteH : 0);
    attributionGap = attributionH > 0 ? (compact ? 18 : 28) : 0;
    const int avail = areaBottom - areaTop - attributionGap - attributionH;
    for (int i = 0; i < CANDIDATE_COUNT; i++) {
      maxHeights[i] = avail - (CANDIDATES[i].mark && markH > 0 ? markH + MARK_GAP : 0);
    }
    if (!fitText(w.text, textW, lineHeights, maxHeights, CANDIDATE_COUNT, &measureRun, &m, w.fit)) return false;
    if (CANDIDATES[w.fit.fontIndex].mark && !w.fit.truncated) break;
  }
  const Candidate& chosen = CANDIDATES[w.fit.fontIndex];
  const int lh = lineHeights[w.fit.fontIndex];
  const int textH = w.fit.lineCount * lh;
  const int markBlock = chosen.mark && markH > 0 ? markH + MARK_GAP : 0;

  // Centre the block a touch above the middle; short quotes centred, paragraphs flush left with
  // the attribution flush right.
  const int blockH = markBlock + textH + attributionGap + attributionH;
  const int top = areaTop + std::max(0, (areaBottom - areaTop - blockH) * 9 / 20);
  const bool centred = w.fit.lineCount <= CENTRE_MAX_LINES;
  const draw::Align align = centred ? draw::Align::Center : draw::Align::Left;
  const draw::Align attributionAlign = centred ? draw::Align::Center : draw::Align::Right;

  if (markBlock > 0) {
    // Ink centred, or its left edge on the text's left edge.
    const int inkX = centred ? (screenW - ink.width) / 2 : textX;
    draw::drawTextScaled(renderer, MARK_FONT, inkX - ink.left, top - ink.top, OPEN_QUOTE, MARK_SCALE);
  }
  int y = top + markBlock;
  drawLines(renderer, w.text, w.fit, m, textX, y, textW, lh, align);
  y += textH + attributionGap;
  drawLines(renderer, w.name, w.nameFit, nameM, textX, y, textW, nameLh, attributionAlign);
  y += nameH;
  drawLines(renderer, w.work, w.workFit, workM, textX, y, textW, workLh, attributionAlign);
  y += workH;
  if (noteH > 0) drawLines(renderer, w.note, w.noteFit, noteM, textX, y + NOTE_GAP, textW, noteLh, attributionAlign);

  if (shownHash != lastHash) writeLastHash(io, shownHash);  // unchanged: no SD write
  return true;
}

}  // namespace sleepcards
