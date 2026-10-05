#pragma once

// Survival guide: the page layout model. Pure: font sizes come in through FontMetrics (on the device
// a GfxRenderer adapter, src/activities/apps/GuideMetrics.h; on the host the same adapter over the
// real fonts, or a fake), figure sizes through FigureSizer (the PNG header, GdPng.h).
//
// One AUTHORED page (a "= " page of a .gp) is laid out into one or more SCREENS of the content band
// [Geometry::top, Geometry::bottom). The page's first screen holds, top to bottom: the lead line
// (a medical topic's reference-only note, page 0 only, when the caller passes it), the title (bold),
// the figure (centred, scaled down into maxFigureW x maxFigureH), then the blocks. What does not fit
// continues on a follow-on screen: paragraphs, bullets and steps split between lines; a WARNING/NOTE
// box moves whole to the next screen when it fits there, else it splits with its cut edge open; a
// figure moves whole. Every element keeps a gap below it (blockGap; itemGap between items of one
// list), dropped at the top of a screen.
//
// The output is a display list: Runs (a piece of text in one font at x,y = the line's TOP, as
// GfxRenderer::drawText takes it) and Shapes (figure rectangles, box frames, bullet dots), grouped by
// screen. The drawing code draws it as it stands, so what the host tests measure is what the device
// shows. Runs point into the TopicText's buffer (or at static labels) and are NOT NUL-terminated: copy
// a run into a small buffer before drawing it (copyRun).

#include <cstddef>
#include <cstdint>

#include "GdPack.h"
#include "GdPage.h"

namespace gd {

enum class Font : uint8_t {
  Body = 0,  // paragraph, bullet, step and box text
  Bold,      // **bold** runs, step numbers, the WARNING: / NOTE: labels
  Title,     // the page title (bold, larger)
  Lead,      // the reference-only lead line on a medical topic's first page (small)
  Crumb,     // the breadcrumb strip (UI_10); not used inside the content band
};
constexpr int FONT_COUNT = 5;

class FontMetrics {
 public:
  virtual ~FontMetrics() = default;
  // Width in pixels of len bytes of UTF-8 (NOT NUL-terminated).
  virtual int textWidth(Font font, const char* text, size_t len) const = 0;
  virtual int spaceWidth(Font font) const = 0;
  // The line advance; a line's text is drawn at its top y.
  virtual int lineHeight(Font font) const = 0;
};

class FigureSizer {
 public:
  virtual ~FigureSizer() = default;
  // The figure's pixel size; false when it is missing or unreadable (a placeholder is laid out).
  virtual bool size(const char* name, int& w, int& h) const = 0;
};

// The page screen's geometry (portrait 480 x 800, logical pixels). The defaults are what the host
// tests check every real page against; compactGeometry() is the quick cards' (the same band, tighter
// gaps; with the compact font set, src/activities/apps/GuideMetrics.h).
//   y 0..43     the breadcrumb strip (UI 10, a rule under it), drawn by the page screen
//   y 44..733   the content band
//   y 736..799  the bottom bar (64 px): < PREV | MENU n/m | NEXT >
struct Geometry {
  int left = 20;    // the text column's x
  int width = 440;  // ... and width (figures are at most this wide)
  int top = 44;     // the content band [top, bottom)
  int bottom = 734;
  int leadGap = 8;
  int titleGap = 10;
  int figureGap = 12;
  int blockGap = 12;
  int itemGap = 6;
  int lineGap = 0;    // extra space between the wrapped lines of one block
  int indent = 30;    // bullet / step text indent
  int dot = 6;        // the bullet's square dot
  int dotX = 8;       // ... its x inside the indent
  int numberGap = 8;  // between a step's number (right-aligned) and its text
  int boxBorder = 2;
  int boxPad = 8;
  int maxFigureW = MAX_FIG_W;
  int maxFigureH = MAX_FIG_H;
  int missingFigureH = 60;  // the placeholder when a figure cannot be read
  int bandHeight() const { return bottom - top; }
};
// The quick cards' geometry: the same band and column, gaps cut to fit a whole topic on one screen.
Geometry compactGeometry();

struct TextRun {
  const char* text = "";
  uint16_t len = 0;
  int16_t x = 0;
  int16_t y = 0;  // the line's top
  Font font = Font::Body;
};

enum class ShapeKind : uint8_t {
  Figure = 0,  // ref = the figure's name; draw it scaled into w x h at x,y
  WarningBox,  // a frame (boxBorder thick) around a WARNING's text
  NoteBox,     // ... a NOTE's
  Dot,         // a bullet: a filled w x h square
};
constexpr uint8_t EDGE_TOP = 1;        // box: draw the top edge (absent: continued from the last screen)
constexpr uint8_t EDGE_BOTTOM = 2;     // box: draw the bottom edge (absent: continues on the next screen)
constexpr uint8_t FIGURE_MISSING = 4;  // figure: the size lookup failed; draw a placeholder

struct Shape {
  ShapeKind kind = ShapeKind::Dot;
  uint8_t flags = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  const char* ref = nullptr;  // Figure: its name (NUL-terminated); the caption is in PageText
};

struct ScreenSpan {
  uint16_t firstRun = 0;
  uint16_t runCount = 0;
  uint16_t firstShape = 0;
  uint16_t shapeCount = 0;
  int16_t bottom = 0;  // the lowest pixel row used + 1 (top when empty)
};

// One authored page's screens. ~16 KB on the device: keep one (in PSRAM) for the open page.
class PageLayout {
 public:
  static constexpr int MAX_RUNS = 1024;
  static constexpr int MAX_SHAPES = 128;
  static constexpr int MAX_SCREENS = 16;

  int screenCount() const { return screens; }
  const ScreenSpan& screen(const int index) const { return spans[index]; }
  const TextRun& run(const int index) const { return runList[index]; }
  const Shape& shape(const int index) const { return shapeList[index]; }
  int runCount() const { return runs; }
  int shapeCount() const { return shapes; }
  // Ran out of runs, shapes or screens: what fitted is kept (only a damaged / huge page does this).
  bool truncated() const { return cut; }
  void clear();

 private:
  friend class LayoutBuilder;
  TextRun runList[MAX_RUNS];
  Shape shapeList[MAX_SHAPES];
  ScreenSpan spans[MAX_SCREENS];
  int runs = 0;
  int shapes = 0;
  int screens = 0;
  bool cut = false;
};

// Lays out page `page` of `text`. lead: the line to put above the title (nullptr or "" for none;
// pass PackInfo::refNote on page 0 of a medical topic). sizer may be nullptr (every figure missing).
// out == nullptr only counts. Returns the number of screens (1..PageLayout::MAX_SCREENS); an
// invalid page gives 0.
int layoutPage(const TopicText& text, int page, const char* lead, const FontMetrics& metrics, const Geometry& geometry,
               const FigureSizer* sizer, PageLayout* out);

// The screens of every page of a topic (perPage[i] for page i, at most cap pages written); returns
// the total. The lead goes on page 0 only.
int topicScreens(const TopicText& text, const char* lead, const FontMetrics& metrics, const Geometry& geometry,
                 const FigureSizer* sizer, uint8_t* perPage, int cap);

// A figure's size scaled down (never up) into maxW x maxH, keeping its shape.
void fitFigure(int w, int h, int maxW, int maxH, int& outW, int& outH);

// Copies a run's text into out as a NUL-terminated string (cut to cap - 1 bytes). Its length.
size_t copyRun(const TextRun& run, char* out, size_t cap);

// The breadcrumb, e.g. parts {"SURVIVAL", "FIRE", "Fire lays"} -> "SURVIVAL / FIRE / FIRE LAYS"
// (upper-cased, ASCII). When it is wider than maxWidth the leading parts are dropped first
// (".. / FIRE / FIRE LAYS", then ".. / FIRE LAYS"), then the last part alone is cut with "..."
// ("FIRE LA..."); "" when not even "..." fits. Its length.
size_t breadcrumb(const char* const* parts, int count, const FontMetrics& metrics, Font font, int maxWidth, char* out,
                  size_t cap);

}  // namespace gd
