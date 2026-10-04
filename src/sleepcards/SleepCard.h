#pragma once

#include <cstddef>
#include <cstdint>

#include "CardId.h"
#include "CardTime.h"
#include "SleepCardSettings.h"

class GfxRenderer;

// Sleep-screen cards (X4 Pro). A card draws one full screen into the
// framebuffer from a CardContext; SleepActivity shows it with the sleep
// screen's single HALF refresh and the device sleeps. The e-ink keeps the
// picture with no power, also after Auto Power Off cuts the rail, so a card
// says when it was drawn ("Screen updated 9:04 PM, Mon Nov 2"), never a live
// clock. On external power the sleep stays live and redraws the card on the
// minute (util/LiveSleepPolicy.h).
//
// Rules for every card:
//  - render() returns false when it has nothing true to show (no time, no
//    book, a bad file ...): the caller then draws the X4 Pro logo screen. A
//    card that returns false may have drawn half a screen; it is cleared.
//  - Bounded work only: no unbounded allocation, no loop on I/O, target
//    < 300 ms of compute. All card I/O goes through ctx.io (CardIo below), so
//    the same code renders on the host (test/sleep_card_preview).
//  - The frame is portrait 480x800 in normal polarity, white paper, already
//    cleared. Leave the bottom FOOTER_HEIGHT px free: renderCard() draws the
//    shared footer there after a card succeeds.
//  - Every user-facing string through tr(); dates and names via CardText.h.
namespace sleepcards {

// The open book as the reader last left it (loaded by CardIo::loadBook).
struct CardBook {
  static constexpr size_t TITLE_CAP = 96;
  static constexpr size_t AUTHOR_CAP = 64;
  static constexpr size_t CHAPTER_CAP = 96;
  char title[TITLE_CAP] = "";
  char author[AUTHOR_CAP] = "";
  char chapterTitle[CHAPTER_CAP] = "";  // table-of-contents title of the current spine item; "" unknown
  int percent = -1;                     // whole-book progress 0..100; -1 unknown
  float bookFraction = -1.0f;           // the same, 0..1 unrounded; -1 unknown
  int spineIndex = -1;                  // EPUB spine item (chapter file); -1 unknown
  int spineCount = 0;
  int chapterPage = -1;      // 0-based page within the spine item; -1 unknown
  int chapterPageCount = 0;  // pages in the spine item at the current layout; 0 unknown
  bool hasCover = false;     // drawBookCover() has something to draw
  int coverWidth = 0;        // that cover's own size in px (a 1-bit thumbnail); 0 unknown
  int coverHeight = 0;

  // The largest cover box the Now Reading card has; the device prefers a thumbnail that fits it.
  static constexpr int COVER_MAX_W = 250;
  static constexpr int COVER_MAX_H = 360;
};

// A bookmark of the open book, for the Quote card.
struct CardSnippet {
  static constexpr size_t TEXT_CAP = 192;
  static constexpr size_t LABEL_CAP = 64;
  char text[TEXT_CAP] = "";    // the bookmark's page summary (first words of the page)
  char label[LABEL_CAP] = "";  // the bookmark's own name, if the reader gave it one
  float fraction = -1.0f;      // position in the book 0..1; -1 unknown
};

// Everything a card may read or write outside the framebuffer. Device:
// DeviceCards.cpp (SD card through HalStorage, EPUB metadata); host: the
// preview harness's fake. Paths are absolute SD paths ("/quotes.txt").
class CardIo {
 public:
  virtual ~CardIo() = default;
  // Size of a file in bytes, or -1 when it does not exist or cannot be opened.
  virtual int32_t fileSize(const char* path) const = 0;
  // Up to cap bytes from offset. Returns the bytes read, or -1 on failure. One open per call.
  virtual int32_t readFileAt(const char* path, uint32_t offset, char* buf, size_t cap) const = 0;
  // Replace a small state file (a card's own memory, e.g. /.crosspoint/sleepcards/<card>.dat).
  // len is capped at 4 KB. The directory is created when missing.
  virtual bool writeFile(const char* path, const char* data, size_t len) const = 0;
  // The open book (CardContext::bookPath). False when there is none or it cannot be read.
  virtual bool loadBook(CardBook& out) const = 0;
  // Draw the open book's cover (CardBook::coverWidth x coverHeight) centred in the box: 1:1 when it
  // fits, else scaled down cleanly (CoverDraw.h). Uses an already generated 1-bit thumbnail only -
  // never decodes a JPEG on the sleep path.
  virtual bool drawBookCover(GfxRenderer& renderer, int x, int y, int maxWidth, int maxHeight) const = 0;
  // Up to max bookmarks of the open book, spread over the whole list when it holds more. Returns
  // how many were filled.
  virtual int loadBookmarks(CardSnippet* out, int max) const = 0;
  // Does the open book have a bookmark? (Shuffle asks before choosing the Quote card.) The default
  // loads one; the device answers from the bookmark file's size without parsing it.
  virtual bool hasBookmarks() const;
};

struct CardLocation {
  bool valid = false;
  double lat = 0;  // degrees, north positive
  double lon = 0;  // degrees, east positive
};

struct CardContext {
  // Time. When timeValid is false (no RTC, or an RTC that was never set), cards that need a
  // date return false and the footer leaves the time out.
  bool timeValid = false;
  int64_t utcNow = 0;      // UNIX seconds: the moment the card is drawn
  LocalDate localNow;      // local wall time at utcNow (never name anything `local`: zlib #defines it)
  int32_t utcOffsetS = 0;  // local - UTC at utcNow, seconds
  UtcOffsetFn utcOffsetAt = &libcUtcOffset;  // DST-aware offset at any instant; never null
  bool clock12h = false;                     // the Clock setting's 12-hour format

  // Device.
  int batteryPercent = -1;    // 0..100; -1 unknown
  bool charging = false;      // on external power: the footer's battery carries a bolt
  uint32_t awakeSeconds = 0;  // how long it was awake before this sleep (since the last boot/wake)

  // Place: Settings > Display > Sleep Screen Cards > Location. Sun/moon times need it; the
  // moon's phase does not. Unset: show tr(STR_SET_LOCATION_IN_SETTINGS) instead.
  CardLocation location;

  SleepCardSettings settings;

  // The open book (APP_STATE.openEpubPath), "" when none; details via io->loadBook().
  const char* bookPath = "";
  bool fromReader = false;  // it went to sleep from inside the reader

  uint32_t seed = 0;  // fresh randomness for this sleep (fixed on the host)
  // A redraw of the card already on screen (live sleep without the cycle): a card that picks
  // something (Quote) shows its last pick again instead of a new one.
  bool repeatLast = false;

  // White on black (Dark Cards, sleepcards::cardsDark): renderCard inverts the finished card,
  // pictures excepted (draw::invertKeepingTones). Cards draw the same either way.
  bool dark = false;

  const CardIo* io = nullptr;  // never null while a card renders
};

using CardRenderFn = bool (*)(const CardContext& ctx, GfxRenderer& renderer);

struct CardInfo {
  CardId id;
  const char* name;     // bench / preview name: "now_reading", "day", ...
  CardRenderFn render;  // nullptr for Pictures (SleepActivity's picture frame) and Shuffle
};

// Layout shared by every card (portrait 480x800).
constexpr int SCREEN_MARGIN = 28;
constexpr int FOOTER_HEIGHT = 44;

// CrossPointSettings::SLEEP_SCREEN_MODE values of the cards (static_assert'ed against the enum
// in DeviceCards.cpp; this header stays free of the settings store so it builds on the host).
constexpr uint8_t SLEEP_MODE_NOW_READING = 8;
constexpr uint8_t SLEEP_MODE_DAY = 9;
constexpr uint8_t SLEEP_MODE_CALENDAR = 10;
constexpr uint8_t SLEEP_MODE_QUOTE = 11;
constexpr uint8_t SLEEP_MODE_OWNER = 12;
constexpr uint8_t SLEEP_MODE_SKY = 13;
constexpr uint8_t SLEEP_MODE_SHUFFLE = 14;
constexpr uint8_t SLEEP_MODE_WEATHER = 15;

// The registry. cardForSleepMode maps a CrossPointSettings::SLEEP_SCREEN_MODE value to its
// card (None for the classic modes); cardByName accepts the names above.
const CardInfo* cardInfo(CardId id);
CardId cardForSleepMode(uint8_t sleepScreenMode);
CardId cardByName(const char* name);
const char* cardName(CardId id);

// Clear the frame, run the card, and on success draw the shared footer (then invert it all when
// ctx.dark). False = the card declined; the frame is then cleared again (white) for the
// caller's fallback.
bool renderCard(CardId id, const CardContext& ctx, GfxRenderer& renderer);

// Shuffle (ShuffleCard.cpp): the card to show this time among the ticked ones in
// ctx.settings.shuffleMask - may be Pictures, never Shuffle; None when nothing is ticked.
CardId pickShuffleCard(const CardContext& ctx);

// renderCard() with Shuffle resolved: when the pick declines, the next usable card is tried
// (each card at most once). shown = the card drawn, Pictures (nothing drawn: the caller shows the
// picture frame) or the last card that declined. True when the frame holds a card. Shuffle never
// picks a card in `excluded` (cardBit()s), e.g. Pictures once the picture frame found no picture.
bool renderCardOrShuffle(CardId requested, const CardContext& ctx, GfxRenderer& renderer, CardId& shown,
                         uint16_t excluded = 0);

// The card files' entry points (one per card, see the registry in SleepCardRegistry.cpp).
bool renderNowReadingCard(const CardContext& ctx, GfxRenderer& renderer);
bool renderDayCard(const CardContext& ctx, GfxRenderer& renderer);
bool renderCalendarCard(const CardContext& ctx, GfxRenderer& renderer);
bool renderQuoteCard(const CardContext& ctx, GfxRenderer& renderer);
bool renderOwnerCard(const CardContext& ctx, GfxRenderer& renderer);
bool renderSkyCard(const CardContext& ctx, GfxRenderer& renderer);
bool renderWeatherCard(const CardContext& ctx, GfxRenderer& renderer);

}  // namespace sleepcards
