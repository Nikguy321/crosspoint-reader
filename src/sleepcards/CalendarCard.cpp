#include "CalendarCard.h"

#include <Astro.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CardDraw.h"
#include "CardText.h"
#include "CardTime.h"
#include "fontIds.h"

namespace sleepcards {
namespace calendar {

// ---- the grid -----------------------------------------------------------------------------------

bool monthGrid(const int year, const int month, const int firstWeekday, MonthGrid& out) {
  if (month < 1 || month > 12 || firstWeekday < 0 || firstWeekday > 6) return false;
  out.year = year;
  out.month = month;
  out.firstWeekday = firstWeekday;
  out.lead = (weekdayOf(year, month, 1) - firstWeekday + 7) % 7;
  out.days = daysInMonth(year, month);
  out.rows = (out.lead + out.days + 6) / 7;
  return true;
}

bool cellOfDay(const MonthGrid& g, const int day, int& row, int& col) {
  if (day < 1 || day > g.days) return false;
  const int index = g.lead + day - 1;
  row = index / 7;
  col = index % 7;
  return true;
}

int dayAtCell(const MonthGrid& g, const int row, const int col) {
  if (row < 0 || col < 0 || col > 6) return 0;
  const int day = row * 7 + col - g.lead + 1;
  return (day >= 1 && day <= g.days) ? day : 0;
}

int weekdayOfColumn(const MonthGrid& g, const int col) { return (g.firstWeekday + col) % 7; }

// ---- the moon's quarters ------------------------------------------------------------------------

namespace {

// The shortest gap between two quarters of one kind is ~29.18 days (new and full moons ~29.27, the
// first and last quarters a little less): a kind whose first instant in the month is more than this
// before the month's end may come round again inside it.
constexpr int64_t MIN_LUNATION_S = static_cast<int64_t>(29.1 * 86400);

void nextMonth(const int year, const int month, int& y, int& m) {
  y = month == 12 ? year + 1 : year;
  m = month == 12 ? 1 : month + 1;
}

}  // namespace

int monthMoonQuarters(const int year, const int month, const UtcOffsetFn offsetAt, const PhaseFinder find,
                      QuarterMark* out, const int max) {
  if (month < 1 || month > 12 || offsetAt == nullptr || find == nullptr || out == nullptr || max <= 0) return 0;
  const int64_t start = localDayStart(year, month, 1, offsetAt);
  int ny = 0;
  int nm = 0;
  nextMonth(year, month, ny, nm);
  const int64_t end = localDayStart(ny, nm, 1, offsetAt);

  int n = 0;
  for (int quarter = 0; quarter < 4; quarter++) {
    int64_t from = start - 1;
    for (int pass = 0; pass < 2; pass++) {
      const int64_t t = find(from, quarter);
      if (t <= from || t < start || t >= end) break;
      if (n < max) {
        out[n].utc = t;
        out[n].day = localDateOf(t, offsetAt).day;
        out[n].quarter = static_cast<uint8_t>(quarter);
        n++;
      }
      if (t + MIN_LUNATION_S >= end) break;  // no room for a second one of this kind
      from = t;
    }
  }
  // Time order (at most MAX_QUARTERS entries: insertion sort).
  for (int i = 1; i < n; i++) {
    const QuarterMark m = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].utc > m.utc) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = m;
  }
  return n;
}

// ---- the hunting season -------------------------------------------------------------------------

bool huntDayMarked(const SleepCardSettings& settings, const int year, const int month, const int day) {
  return settings.huntMode == HuntMode::Between && huntingSeasonOn(settings, year, month, day);
}

SeasonCountdown seasonCountdown(const SleepCardSettings& settings, const int year, const int month, const int day) {
  SeasonCountdown out;
  if (settings.huntMode != HuntMode::Between) return out;
  if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month)) return out;
  const int64_t today = daysFromCivil(year, month, day);
  const bool inSeason = huntingSeasonOn(settings, year, month, day);
  // A year and a day is enough to meet any boundary of a yearly season (bounded: 367 cheap tests).
  for (int k = 1; k <= 367; k++) {
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(today + k, y, m, d);
    if (huntingSeasonOn(settings, y, m, d) == inSeason) continue;
    if (inSeason) {
      civilFromDays(today + k - 1, y, m, d);
      out.kind = SeasonCountdown::Kind::Ends;
      out.days = k - 1;
    } else {
      out.kind = SeasonCountdown::Kind::Opens;
      out.days = k;
    }
    out.year = y;
    out.month = m;
    out.day = d;
    return out;
  }
  return out;  // every day, or never: nothing to count down to
}

}  // namespace calendar

// ---- the card -----------------------------------------------------------------------------------

namespace {

using namespace calendar;

constexpr int FIRST_WEEKDAY = 0;  // no week-start setting exists: Sunday first

// Layout (portrait 480x800; the footer owns the bottom FOOTER_HEIGHT px).
constexpr int TITLE_Y = 20;
constexpr float TITLE_SCALE = 1.5f;
constexpr int HEADER_GAP = 16;  // title bottom to the weekday row
constexpr int WEEKDAY_ROW_H = 34;
constexpr int GRID_MAX_ROW_H = 96;
constexpr int GRID_H = 6 * 76;         // the grid's height for a 6-row month; fewer rows get taller cells
constexpr int TODAY_RADIUS = 25;       // at most; 30 % of a short row
constexpr int TODAY_RING = 3;          // today as a ring when it also carries a moon glyph
constexpr int GLYPH_RADIUS = 8;        // rows under 88 px (a 6-row month: 7)
constexpr int GLYPH_RADIUS_TALL = 10;  // rows of 88 px and more (4- and 5-row months)
constexpr int HUNT_BAR_H = 4;
constexpr int LEGEND_GLYPH_R = 9;
constexpr int FACTS_BOTTOM_GAP = 22;  // the facts' last line to the footer
constexpr int ICON_W = 34;            // the facts' icon column

void formatDaysAway(const int days, char* out, const size_t cap) {
  if (days <= 0) {
    std::snprintf(out, cap, "%s", tr(STR_CAL_TODAY));
  } else if (days == 1) {
    std::snprintf(out, cap, "%s", tr(STR_CAL_TOMORROW));
  } else {
    std::snprintf(out, cap, tr(STR_CAL_IN_DAYS), days);
  }
}

const char* phaseLabel(const uint8_t quarter) {
  switch (quarter) {
    case 0:
      return tr(STR_CAL_PHASE_NEW);
    case 1:
      return tr(STR_CAL_PHASE_FIRST);
    case 2:
      return tr(STR_CAL_PHASE_FULL);
    default:
      return tr(STR_CAL_PHASE_LAST);
  }
}

// The classic calendar symbols: new = dark disc, full = open disc, quarters = half lit.
void drawQuarterGlyph(GfxRenderer& r, const int cx, const int cy, const int radius, const uint8_t quarter,
                      const bool southern) {
  static constexpr double ILLUM[4] = {0.0, 0.5, 1.0, 0.5};
  draw::drawMoon(r, cx, cy, radius, ILLUM[quarter & 3], quarter < 2, southern, draw::DITHER_LEVELS);
}

// A fact under the grid, in three lines beside its icon (the icon is drawn by the caller in the
// ICON_W column): "Next full moon" / "Tue, Nov 24" / "in 22 days".
void drawInfoBlock(GfxRenderer& r, const int x, const int y, const char* label, const char* what, const char* when) {
  r.drawText(UI_10_FONT_ID, x, y, label);
  const int y2 = y + r.getLineHeight(UI_10_FONT_ID) + 2;
  r.drawText(UI_12_FONT_ID, x, y2, what, true, EpdFontFamily::BOLD);
  r.drawText(UI_10_FONT_ID, x, y2 + r.getLineHeight(UI_12_FONT_ID) + 2, when);
}

}  // namespace

bool renderCalendarCard(const CardContext& ctx, GfxRenderer& r) {
  if (!ctx.timeValid || !plausibleTime(ctx.utcNow) || ctx.utcOffsetAt == nullptr) return false;
  const LocalDate today = localDateOf(ctx.utcNow, ctx.utcOffsetAt);
  MonthGrid g;
  if (!monthGrid(today.year, today.month, FIRST_WEEKDAY, g)) return false;
  const bool southern = ctx.location.valid && ctx.location.lat < 0;

  QuarterMark quarters[MAX_QUARTERS];
  const int nQuarters =
      monthMoonQuarters(today.year, today.month, ctx.utcOffsetAt, &astroNextMoonPhase, quarters, MAX_QUARTERS);

  const int screenW = r.getScreenWidth();
  const int screenH = r.getScreenHeight();
  const int left = SCREEN_MARGIN;
  const int right = screenW - SCREEN_MARGIN;
  const int cellW = (right - left) / 7;
  const int gridLeft = (screenW - cellW * 7) / 2;
  const int gridRight = gridLeft + cellW * 7;

  // ---- title: "November" large, the year on its baseline at the right ----
  const int bigAsc = r.getFontAscenderSize(NOTOSANS_18_FONT_ID);
  draw::drawTextScaled(r, NOTOSANS_18_FONT_ID, gridLeft + 2, TITLE_Y, monthName(today.month), TITLE_SCALE, true,
                       EpdFontFamily::BOLD);
  const int titleBaseline = TITLE_Y + static_cast<int>(bigAsc * TITLE_SCALE + 0.5f);
  char year[8];
  std::snprintf(year, sizeof(year), "%d", today.year);
  draw::drawTextRight(r, NOTOSANS_18_FONT_ID, gridRight - 2, titleBaseline - bigAsc, year);

  // ---- weekday header ----
  const int weekdayY = titleBaseline + HEADER_GAP;
  const int smallAsc = r.getFontAscenderSize(UI_10_FONT_ID);
  for (int col = 0; col < 7; col++) {
    const int cx = gridLeft + col * cellW + cellW / 2;
    draw::drawTextCenteredAt(r, UI_10_FONT_ID, cx, weekdayY + (WEEKDAY_ROW_H - smallAsc) / 2 - 4,
                             weekdayShortName(weekdayOfColumn(g, col)), true, EpdFontFamily::BOLD);
  }
  const int gridTop = weekdayY + WEEKDAY_ROW_H;
  r.fillRect(gridLeft, gridTop - 2, gridRight - gridLeft, 2, true);

  // ---- the grid ----
  int rowH = GRID_H / g.rows;
  if (rowH > GRID_MAX_ROW_H) rowH = GRID_MAX_ROW_H;
  const int numAsc = r.getFontAscenderSize(NOTOSANS_16_FONT_ID);
  const int digitH = numAsc * 72 / 100;  // lining figures stand ~0.72 of the ascender
  // A cell, top down: the number (today's ring around it), the moon glyph, the season underline.
  const int glyphR = rowH >= 88 ? GLYPH_RADIUS_TALL : (rowH >= 80 ? GLYPH_RADIUS : GLYPH_RADIUS - 1);
  const int todayR = std::min(TODAY_RADIUS, rowH * 30 / 100);
  const int numCyOff = rowH * 36 / 100;
  const int glyphCyOff = numCyOff + todayR + glyphR + 3;
  // Today's cell: a filled disc, or a ring when a quarter's glyph sits under it (a black glyph right
  // under a black disc reads as one blob).
  bool todayHasGlyph = false;
  for (int i = 0; i < nQuarters; i++) todayHasGlyph = todayHasGlyph || quarters[i].day == today.day;
  const int barYOff = rowH - HUNT_BAR_H - 1;

  for (int row = 0; row < g.rows; row++) {
    const int y = gridTop + row * rowH;
    draw::drawDashedHLine(r, gridLeft, gridRight, y + rowH, 1, 3);  // under every week
    for (int col = 0; col < 7; col++) {
      const int day = dayAtCell(g, row, col);
      if (day == 0) continue;
      const int x = gridLeft + col * cellW;
      const int cx = x + cellW / 2;
      const int numCy = y + numCyOff;
      char num[4];
      std::snprintf(num, sizeof(num), "%d", day);
      const bool isToday = day == today.day;
      const bool disc = isToday && !todayHasGlyph;
      const EpdFontFamily::Style style = isToday ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      if (disc) draw::fillCircle(r, cx, numCy, todayR);
      if (isToday && !disc) draw::drawCircle(r, cx, numCy, todayR, TODAY_RING);
      draw::drawTextCenteredAt(r, NOTOSANS_16_FONT_ID, cx, numCy + digitH / 2 - numAsc, num, !disc, style);

      if (huntDayMarked(ctx.settings, g.year, g.month, day)) {
        // One band through the season: inset only where the season starts or stops.
        const int64_t dn = daysFromCivil(g.year, g.month, day);
        int py = 0, pm = 0, pd = 0, ny = 0, nm = 0, nd = 0;
        civilFromDays(dn - 1, py, pm, pd);
        civilFromDays(dn + 1, ny, nm, nd);
        const int x0 = huntDayMarked(ctx.settings, py, pm, pd) && col > 0 ? x : x + 6;
        const int x1 = huntDayMarked(ctx.settings, ny, nm, nd) && col < 6 ? x + cellW : x + cellW - 6;
        r.fillRect(x0, y + barYOff, x1 - x0, HUNT_BAR_H, true);
      }
    }
  }
  for (int i = 0; i < nQuarters; i++) {
    int row = 0;
    int col = 0;
    if (!cellOfDay(g, quarters[i].day, row, col)) continue;
    drawQuarterGlyph(r, gridLeft + col * cellW + cellW / 2, gridTop + row * rowH + glyphCyOff, glyphR,
                     quarters[i].quarter, southern);
  }
  const int gridBottom = gridTop + g.rows * rowH;

  // ---- the facts sit on the footer; the glyph legend is centred in the space between ----
  const int factsH = 2 * r.getLineHeight(UI_10_FONT_ID) + r.getLineHeight(UI_12_FONT_ID) + 4;
  const int factsY = screenH - FOOTER_HEIGHT - FACTS_BOTTOM_GAP - factsH;
  const int legendY = gridBottom + (factsY - gridBottom - 2 * LEGEND_GLYPH_R) / 2;
  if (legendY - gridBottom < 8) return true;  // never crowd the footer (no month's layout gets here)
  {
    static constexpr uint8_t ORDER[4] = {0, 1, 2, 3};
    int font = UI_10_FONT_ID;
    int total = 0;
    for (const int f : {UI_10_FONT_ID, SMALL_FONT_ID}) {
      font = f;
      total = 0;
      for (const uint8_t q : ORDER) total += LEGEND_GLYPH_R * 2 + 6 + r.getTextWidth(f, phaseLabel(q));
      if (total <= gridRight - gridLeft) break;
    }
    const int gap = total < gridRight - gridLeft ? (gridRight - gridLeft - total) / 3 : 0;
    const int asc = r.getFontAscenderSize(font);
    int x = gridLeft;
    for (const uint8_t q : ORDER) {
      drawQuarterGlyph(r, x + LEGEND_GLYPH_R, legendY + LEGEND_GLYPH_R, LEGEND_GLYPH_R, q, southern);
      const int tx = x + LEGEND_GLYPH_R * 2 + 6;
      r.drawText(font, tx, legendY + LEGEND_GLYPH_R - asc * 62 / 100, phaseLabel(q));
      x = tx + r.getTextWidth(font, phaseLabel(q)) + gap;
    }
  }
  const int y = factsY;

  const int halfW = (gridRight - gridLeft) / 2;
  const int64_t todayDays = daysFromCivil(today.year, today.month, today.day);
  // The next full moon after the moment of sleep (as the Day card counts it): one already past
  // today reads as next month's, not "today".
  int64_t fullMoon = 0;
  for (int i = 0; i < nQuarters; i++) {
    if (quarters[i].quarter == 2 && quarters[i].utc > ctx.utcNow) {
      fullMoon = quarters[i].utc;
      break;
    }
  }
  if (fullMoon == 0) fullMoon = astroNextMoonPhase(ctx.utcNow, 2);
  const SeasonCountdown season = seasonCountdown(ctx.settings, today.year, today.month, today.day);
  const bool twoFacts = season.kind != SeasonCountdown::Kind::None;
  if (fullMoon > 0) {
    const LocalDate fd = localDateOf(fullMoon, ctx.utcOffsetAt);
    char what[40];
    std::snprintf(what, sizeof(what), tr(STR_CAL_WEEKDAY_DATE), weekdayShortName(fd.weekday), monthShortName(fd.month),
                  fd.day);
    char when[24];
    formatDaysAway(static_cast<int>(daysFromCivil(fd.year, fd.month, fd.day) - todayDays), when, sizeof(when));
    // Alone, the block is centred under the grid; beside the season's, it takes the left half.
    int x = gridLeft;
    if (!twoFacts) {
      const int textW =
          std::max({r.getTextWidth(UI_10_FONT_ID, tr(STR_CAL_NEXT_FULL_MOON)),
                    r.getTextWidth(UI_12_FONT_ID, what, EpdFontFamily::BOLD), r.getTextWidth(UI_10_FONT_ID, when)});
      x = gridLeft + ((gridRight - gridLeft) - (ICON_W + 4 + textW)) / 2;
    }
    drawQuarterGlyph(r, x + ICON_W / 2 - 2, y + ICON_W / 2 - 2, ICON_W / 2 - 4, 2, southern);
    drawInfoBlock(r, x + ICON_W + 4, y, tr(STR_CAL_NEXT_FULL_MOON), what, when);
  }

  if (twoFacts) {
    const int x = gridLeft + halfW;
    r.fillRect(x, y + ICON_W / 2 - 2 - HUNT_BAR_H / 2, ICON_W - 8, HUNT_BAR_H, true);
    char what[40];
    std::snprintf(what, sizeof(what), tr(STR_CAL_WEEKDAY_DATE),
                  weekdayShortName(weekdayOf(season.year, season.month, season.day)), monthShortName(season.month),
                  season.day);
    char away[24];
    formatDaysAway(season.days, away, sizeof(away));
    char when[40];
    std::snprintf(when, sizeof(when),
                  season.kind == SeasonCountdown::Kind::Opens ? tr(STR_CAL_SEASON_OPENS) : tr(STR_CAL_SEASON_ENDS),
                  away);
    drawInfoBlock(r, x + ICON_W + 4, y, tr(STR_CAL_HUNTING_SEASON), what, when);
  }
  return true;
}

}  // namespace sleepcards
