#include "NowReadingCard.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CardDraw.h"
#include "CoverDraw.h"
#include "fontIds.h"

namespace sleepcards {
namespace nowreading {

pace::PaceState currentPace(const char* storedText, const size_t storedLen, const pace::PaceSession& session) {
  pace::PaceState stored;
  if (storedText != nullptr && storedLen > 0) pace::parseState(storedText, storedLen, stored);
  return pace::merged(stored, session);
}

bool bookFinished(const CardBook& book) {
  return (book.spineCount > 0 && book.spineIndex >= book.spineCount) || book.bookFraction >= 0.9995f;
}

}  // namespace nowreading

namespace {

constexpr int TITLE_FONT = NOTOSERIF_18_FONT_ID;
constexpr int AUTHOR_FONT = NOTOSERIF_14_FONT_ID;
constexpr int PERCENT_FONT = NOTOSANS_16_FONT_ID;
constexpr int CHAPTER_FONT = UI_12_FONT_ID;
constexpr int DETAIL_FONT = UI_10_FONT_ID;
constexpr int TIME_FONT = NOTOSANS_16_FONT_ID;

constexpr int HEADER_TOP = 26;
constexpr int COVER_MAX_W = CardBook::COVER_MAX_W;
constexpr int COVER_MAX_H = CardBook::COVER_MAX_H;
constexpr int COVER_MIN_H = 140;  // a smaller cover is left out
constexpr int BAR_H = 12;
constexpr int PERCENT_GAP = 12;  // between the bar and its "28%"
constexpr int COVER_GAP = 24;    // below the cover
constexpr int BLOCK_SLACK = 36;  // kept free above and below the centred block

// The pace for the card, and on the device the session's new samples saved with it.
pace::PaceState loadPace(const CardContext& ctx) {
  char text[pace::STATE_TEXT_CAP];
  const int32_t got = ctx.io->readFileAt(pace::STATE_PATH, 0, text, sizeof(text) - 1);
  pace::PaceSession& session = pace::session();
  const pace::PaceState state = nowreading::currentPace(text, got > 0 ? static_cast<size_t>(got) : 0, session);
  if (session.samples > 0) {
    char out[pace::STATE_TEXT_CAP];
    const size_t len = pace::formatState(state, out, sizeof(out));
    if (len > 0 && ctx.io->writeFile(pace::STATE_PATH, out, len)) pace::clearSamples(session);
  }
  return state;
}

// "NOW READING" between two hairlines.
void drawHeader(GfxRenderer& r, const int y) {
  const int w = r.getScreenWidth();
  const char* text = tr(STR_NR_HEADER);
  const int tw = r.getTextWidth(DETAIL_FONT, text, EpdFontFamily::BOLD);
  const int midY = y + r.getFontAscenderSize(DETAIL_FONT) / 2 + 1;
  const int gap = 14;
  r.fillRect(SCREEN_MARGIN, midY, (w - tw) / 2 - gap - SCREEN_MARGIN, 1, true);
  r.fillRect((w + tw) / 2 + gap, midY, w - SCREEN_MARGIN - (w + tw) / 2 - gap, 1, true);
  draw::drawTextCenteredAt(r, DETAIL_FONT, w / 2, y, text, true, EpdFontFamily::BOLD);
}

// One "~12 min / left in chapter" column centred on cx.
void drawTimeColumn(GfxRenderer& r, const int cx, const int y, const char* value, const char* label) {
  draw::drawTextCenteredAt(r, TIME_FONT, cx, y, value, true, EpdFontFamily::BOLD);
  draw::drawTextCenteredAt(r, DETAIL_FONT, cx, y + r.getLineHeight(TIME_FONT) + 2, label, true);
}

struct Lines {
  std::vector<std::string> title;
  std::vector<std::string> chapter;
};

}  // namespace

bool renderNowReadingCard(const CardContext& ctx, GfxRenderer& r) {
  if (ctx.bookPath == nullptr || ctx.bookPath[0] == '\0') return false;
  // CardBook exceeds the stack budget.
  auto book = makeUniqueNoThrow<CardBook>();
  if (!book) {
    LOG_ERR("CARD", "OOM: book");
    return false;
  }
  if (!ctx.io->loadBook(*book) || book->title[0] == '\0') return false;

  const int w = r.getScreenWidth();
  const int contentW = w - 2 * SCREEN_MARGIN;
  const int bottom = r.getScreenHeight() - FOOTER_HEIGHT;
  const bool finished = nowreading::bookFinished(*book);

  // ---- what to say -------------------------------------------------------------------------------
  const pace::TimeLeft left = finished ? pace::TimeLeft{} : pace::estimateTimeLeft(loadPace(ctx), *book);
  char chapterTime[24] = "";
  char bookTime[24] = "";
  pace::formatMinutes(left.chapterMinutes, chapterTime, sizeof(chapterTime));
  pace::formatMinutes(left.bookMinutes, bookTime, sizeof(bookTime));
  const bool haveTimes = chapterTime[0] != '\0' || bookTime[0] != '\0';

  const bool havePage =
      !finished && book->chapterPageCount > 0 && book->chapterPage >= 0 && book->chapterPage < book->chapterPageCount;
  char pageLine[48] = "";
  if (havePage) {
    std::snprintf(pageLine, sizeof(pageLine), book->chapterTitle[0] ? tr(STR_NR_PAGE_OF) : tr(STR_NR_PAGE_OF_CHAPTER),
                  book->chapterPage + 1, book->chapterPageCount);
  }
  const float fraction = finished ? 1.0f : book->bookFraction;
  char percent[16] = "";
  if (fraction >= 0) std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(fraction * 100 + 0.5f));

  const bool haveCover = book->hasCover;
  Lines lines;
  lines.title = r.wrappedText(TITLE_FONT, book->title, contentW, haveCover ? 2 : 3, EpdFontFamily::BOLD);
  if (!finished && book->chapterTitle[0]) {
    lines.chapter = r.wrappedText(CHAPTER_FONT, book->chapterTitle, contentW, 2, EpdFontFamily::BOLD);
  }
  const std::string author =
      book->author[0] ? r.truncatedText(AUTHOR_FONT, book->author, contentW, EpdFontFamily::ITALIC) : std::string();

  // ---- vertical layout: header, cover, then the text block, centred in what is left --------------
  const int titleLh = r.getLineHeight(TITLE_FONT);
  const int authorLh = r.getLineHeight(AUTHOR_FONT);
  const int chapterLh = r.getLineHeight(CHAPTER_FONT);
  const int detailLh = r.getLineHeight(DETAIL_FONT);
  const int timeLh = r.getLineHeight(TIME_FONT) + 2 + detailLh;
  const int percentLh = r.getLineHeight(PERCENT_FONT);

  int textH = static_cast<int>(lines.title.size()) * titleLh;
  if (!author.empty()) textH += 4 + authorLh;
  const bool haveProgress = percent[0] != '\0';
  if (haveProgress) textH += 22 + percentLh;
  const int chapterBlockH =
      static_cast<int>(lines.chapter.size()) * chapterLh + (pageLine[0] ? detailLh : 0) + (finished ? chapterLh : 0);
  if (chapterBlockH > 0) textH += 14 + chapterBlockH;
  if (haveTimes) textH += 18 + 1 + 16 + timeLh;

  const int headerBottom = HEADER_TOP + detailLh + 18;
  // The cover at its own size, or smaller (CoverDraw.h) when the text leaves less room; a full-size
  // cover is worth the breathing room kept around the block.
  int coverH = 0;
  int coverW = 0;
  if (haveCover) {
    const int room = std::min(COVER_MAX_H, bottom - headerBottom - COVER_GAP - textH);
    const int roomWithSlack = std::min(COVER_MAX_H, room - BLOCK_SLACK);
    const bool fullSizeFits = book->coverWidth <= COVER_MAX_W && book->coverHeight <= room;
    cover::fitSize(book->coverWidth, book->coverHeight, COVER_MAX_W, fullSizeFits ? room : roomWithSlack, coverW,
                   coverH);
    if (coverH < COVER_MIN_H) coverH = coverW = 0;
  }

  const auto blockTop = [&](const int blockH) {
    return headerBottom + std::max(0, (bottom - headerBottom - blockH) / 2);
  };
  int y = blockTop((coverH > 0 ? coverH + COVER_GAP : 0) + textH);

  drawHeader(r, HEADER_TOP);

  if (coverH > 0) {
    if (!ctx.io->drawBookCover(r, (w - coverW) / 2, y, coverW, coverH)) {
      r.fillRect((w - coverW) / 2, y, coverW, coverH, false);  // a half-drawn cover goes; the text is centred
      y = blockTop(textH);
    } else {
      y += coverH + COVER_GAP;
    }
  }

  for (const auto& line : lines.title) {
    draw::drawTextCenteredAt(r, TITLE_FONT, w / 2, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLh;
  }
  if (!author.empty()) {
    y += 4;
    draw::drawTextCenteredAt(r, AUTHOR_FONT, w / 2, y, author.c_str(), true, EpdFontFamily::ITALIC);
    y += authorLh;
  }

  if (haveProgress) {
    y += 22;
    const int barW = contentW - r.getTextWidth(PERCENT_FONT, "100%", EpdFontFamily::BOLD) - PERCENT_GAP;
    const int barY = y + (percentLh - BAR_H) / 2;
    draw::drawProgressBar(r, SCREEN_MARGIN, barY, barW, BAR_H, fraction);
    draw::drawTextRight(r, PERCENT_FONT, w - SCREEN_MARGIN, y, percent, true, EpdFontFamily::BOLD);
    y += percentLh;
  }

  if (chapterBlockH > 0) {
    y += 14;
    if (finished) {
      draw::drawTextCenteredAt(r, CHAPTER_FONT, w / 2, y, tr(STR_NR_FINISHED), true, EpdFontFamily::BOLD);
      y += chapterLh;
    }
    for (const auto& line : lines.chapter) {
      draw::drawTextCenteredAt(r, CHAPTER_FONT, w / 2, y, line.c_str(), true, EpdFontFamily::BOLD);
      y += chapterLh;
    }
    if (pageLine[0]) {
      draw::drawTextCenteredAt(r, DETAIL_FONT, w / 2, y, pageLine, true);
      y += detailLh;
    }
  }

  if (haveTimes) {
    y += 18;
    r.fillRect(SCREEN_MARGIN + 40, y, contentW - 80, 1, true);
    y += 1 + 16;
    if (chapterTime[0] && bookTime[0]) {
      drawTimeColumn(r, w / 4 + SCREEN_MARGIN / 2, y, chapterTime, tr(STR_NR_LEFT_IN_CHAPTER));
      drawTimeColumn(r, w * 3 / 4 - SCREEN_MARGIN / 2, y, bookTime, tr(STR_NR_LEFT_IN_BOOK));
      r.fillRect(w / 2, y + 4, 1, timeLh - 8, true);
    } else if (chapterTime[0]) {
      drawTimeColumn(r, w / 2, y, chapterTime, tr(STR_NR_LEFT_IN_CHAPTER));
    } else {
      drawTimeColumn(r, w / 2, y, bookTime, tr(STR_NR_LEFT_IN_BOOK));
    }
  }
  return true;
}

}  // namespace sleepcards
