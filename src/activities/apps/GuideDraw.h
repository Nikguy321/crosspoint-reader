#pragma once

// Survival guide drawing: every guide screen below the theme header, as free functions over
// GfxRenderer and the built-in fonts, so the host preview test (test/guide_preview) renders the same
// pixels as the device. Pure 1-bit (Night mode's global inversion is all).
//
// The page screen (480 x 800, gd::Geometry):
//   y 0..43     the breadcrumb strip: "SURVIVAL / FIRE / FIRE LAYS" (UI 10, GdLayout's breadcrumb),
//               a ribbon at the right end when the page is bookmarked, a 2 px rule under it
//   y 44..733   the content band: the page's display list (GdLayout) for one screen
//   y 736..799  the bar: < PREV | MENU n/m | NEXT > (three thirds, each a label over a detail line:
//               the neighbouring topic's title when the step leaves the topic, the screen count or a
//               short message under MENU)
// The list screens (home, a category, quick cards, search results, bookmarks, recent): the theme
// header (GUI.drawHeader, device only) above listTop, rows of ROW_H (bold title, a value at the
// right, a subtitle), paged, with the same bar (< PREV | BACK n/m | NEXT >) turning the list's pages.
// Nothing is drawn in the bezel columns (x < 8, x > 471), where the left-edge Back swipe starts.

#include <cstddef>
#include <cstdint>

#include "GuideMetrics.h"

class GfxRenderer;

namespace gd::draw {

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
constexpr int BEZEL = 8;  // x 0..7 and 472..479 stay clear below the header
constexpr int CRUMB_H = 44;
constexpr int BAR_TOP = 736;
constexpr int BAR_H = SCREEN_H - BAR_TOP;
constexpr int BAR_THIRD = SCREEN_W / 3;
constexpr int ROW_H = 72;
constexpr int ROW_LEFT = BEZEL;
constexpr int ROW_RIGHT = SCREEN_W - BEZEL;  // exclusive
constexpr int TEXT_LEFT = 20;
constexpr int TEXT_RIGHT = SCREEN_W - 20;  // exclusive
constexpr int RIBBON_W = 14;

// ---- the bar --------------------------------------------------------------------------------------

enum class BarButton : uint8_t { None = 0, Prev, Middle, Next };
// The bar button under a touch (the bar's thirds), or None.
BarButton barButtonAt(int x, int y);

struct BarView {
  const char* prevLabel = "";   // "< PREV"
  const char* prevDetail = "";  // the previous topic's title, when PREV leaves the topic
  bool prevEnabled = false;
  const char* middleLabel = "";   // "MENU" / "BACK"
  const char* middleDetail = "";  // "2/5", or a message ("Bookmarked")
  bool middleDetailBold = false;  // a message
  const char* nextLabel = "";
  const char* nextDetail = "";
  bool nextEnabled = false;
};
void drawBar(GfxRenderer& r, const BarView& view);

// ---- the page screen ------------------------------------------------------------------------------

// The breadcrumb strip (text already fitted by gd::breadcrumb into crumbWidth()).
int crumbWidth();
void drawCrumb(GfxRenderer& r, const char* text, bool marked);

// Draws a figure scaled into w x h at x,y; false when it could not (a placeholder is drawn). The
// device decodes fig/L/<name>.png from the card; the preview test, from the pack folder.
using FigureFn = bool (*)(void* ctx, GfxRenderer& r, const char* name, int x, int y, int w, int h);

struct PageView {
  const PageLayout* layout = nullptr;
  int screen = 0;
  const FontSet* fonts = nullptr;
  FigureFn figure = nullptr;
  void* figureCtx = nullptr;
  const char* missingLabel = "";  // the placeholder's text
};
// One screen of a laid-out page (the content band only). Returns the index of the figure shape on
// it (the full-screen view opens on a tap there), or -1.
int drawPageBody(GfxRenderer& r, const PageView& view);

// The figure screen: the figure as large as fits FIGURE_BOX_W x FIGURE_BOX_H (whole multiples only, so
// 1-bit lines stay even), centred, its caption below, and a hint at the bottom. The pack's fig/XL
// raster (gd::MAX_FIG_XL_W/H, made from the master at this box's size) is drawn 1:1; without one, the
// page's fig/L figure. figW/figH: the figure's own size (0 when it cannot be read: only the placeholder).
constexpr int FIGURE_BOX_TOP = 16;
constexpr int FIGURE_BOX_W = 456;  // x 12..467, inside the bezel columns
constexpr int FIGURE_BOX_H = 620;  // y 16..635; the caption (up to 3 lines) from y 648
struct FigureView {
  const char* name = "";
  int figW = 0;
  int figH = 0;
  const char* caption = "";
  const char* hint = "";  // "Tap to return"
  const char* missingLabel = "";
  FigureFn figure = nullptr;
  void* figureCtx = nullptr;
};
void drawFigureScreen(GfxRenderer& r, const FigureView& view);

// ---- the list screens ------------------------------------------------------------------------------

struct ListRow {
  const char* title = "";
  const char* value = "";     // right of the title, small ("7 topics"), or ""
  const char* subtitle = "";  // the second line, small
  bool emphasis = false;      // drawn inverted (EMERGENCY on the home list)
  bool enabled = true;        // false: a note row (nothing to open), no selection frame
};

// Rows that fit between listTop and the bar.
int rowsPerPage(int listTop);
// The row (index within the page, 0-based) under a touch, or -1.
int rowAt(int listTop, int rowsOnPage, int x, int y);

struct ListView {
  int listTop = 96;               // the theme header's bottom (+ a little)
  const ListRow* rows = nullptr;  // this page's rows
  int rowCount = 0;
  int selected = -1;  // index within the page, or -1
  BarView bar;
};
void drawList(GfxRenderer& r, const ListView& view);

// A page of text under the header (the guide with no pack, a damaged or newer pack): a bold title
// and paragraphs, wrapped. The bar is drawn by the caller.
void drawMessage(GfxRenderer& r, int top, const char* title, const char* const* paragraphs, int count);

// ---- the guide menu (Home held) -------------------------------------------------------------------

constexpr int MENU_ROWS_MAX = 6;
constexpr int MENU_ROW_H = 64;
constexpr int MENU_LEFT = 50;
constexpr int MENU_W = SCREEN_W - 2 * MENU_LEFT;

struct MenuView {
  const char* title = "";
  const char* labels[MENU_ROWS_MAX] = {};
  int count = 0;
  int selected = -1;
};
// The box's top (centred on the screen).
int menuTop(int count);
// The row under a touch; -1 inside the box but on no row; -2 outside the box (closes it).
int menuRowAt(int count, int x, int y);
void drawMenu(GfxRenderer& r, const MenuView& view);

}  // namespace gd::draw
