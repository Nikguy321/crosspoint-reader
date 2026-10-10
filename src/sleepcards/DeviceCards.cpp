#include "DeviceCards.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "CoverDraw.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "LiveSleep.h"
#include "LocationFix.h"
#include "components/UITheme.h"
#include "util/BookProgress.h"
#include "util/BookmarkFile.h"
#include "util/BookmarkUtil.h"

namespace sleepcards {

static_assert(CrossPointSettings::NOW_READING == SLEEP_MODE_NOW_READING && CrossPointSettings::DAY == SLEEP_MODE_DAY &&
                  CrossPointSettings::CALENDAR == SLEEP_MODE_CALENDAR &&
                  CrossPointSettings::QUOTE == SLEEP_MODE_QUOTE && CrossPointSettings::OWNER == SLEEP_MODE_OWNER &&
                  CrossPointSettings::SKY == SLEEP_MODE_SKY && CrossPointSettings::SHUFFLE == SLEEP_MODE_SHUFFLE &&
                  CrossPointSettings::WEATHER == SLEEP_MODE_WEATHER,
              "sleep card modes out of step with CrossPointSettings::SLEEP_SCREEN_MODE");

namespace {

constexpr size_t MAX_STATE_FILE = 4096;

void copyText(char* dest, const size_t cap, const std::string& text) { std::snprintf(dest, cap, "%s", text.c_str()); }

// The file name without folder or extension: a title for books without metadata.
void titleFromPath(const std::string& path, char* out, const size_t cap) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  copyText(out, cap, name);
}

class DeviceCardIo final : public CardIo {
 public:
  // Optional files (/quotes.txt, a card's state) are often absent: exists() first, so a missing
  // one costs no "failed to open" log line on every sleep.
  int32_t fileSize(const char* path) const override {
    HalFile file;
    if (path == nullptr || !Storage.exists(path) || !Storage.openFileForRead("CARD", path, file)) return -1;
    return static_cast<int32_t>(file.size());
  }

  int32_t readFileAt(const char* path, const uint32_t offset, char* buf, const size_t cap) const override {
    HalFile file;
    if (path == nullptr || buf == nullptr || !Storage.exists(path) || !Storage.openFileForRead("CARD", path, file)) {
      return -1;
    }
    if (offset > 0 && !file.seek(offset)) return -1;
    const int got = file.read(buf, cap);
    return got < 0 ? -1 : got;
  }

  bool writeFile(const char* path, const char* data, size_t len) const override {
    if (path == nullptr || data == nullptr) return false;
    len = std::min(len, MAX_STATE_FILE);
    const char* slash = strrchr(path, '/');
    if (slash != nullptr && slash != path) {
      char dir[128];
      const size_t n = std::min(static_cast<size_t>(slash - path), sizeof(dir) - 1);
      memcpy(dir, path, n);
      dir[n] = '\0';
      Storage.ensureDirectoryExists(dir);
    }
    HalFile file;
    if (!Storage.openFileForWrite("CARD", path, file)) return false;
    return file.write(data, len) == len;
  }

  bool loadBook(CardBook& out) const override {
    out = CardBook{};
    coverPath_.clear();
    const std::string path = APP_STATE.openEpubPath;
    if (path.empty() || !Storage.exists(path.c_str())) return false;
    if (!FsHelpers::hasEpubExtension(path)) {
      // XTC / TXT / Markdown: the file name and the saved percentage.
      titleFromPath(path, out.title, sizeof(out.title));
      out.percent = loadBookProgress(path);
      if (out.percent >= 0) out.bookFraction = out.percent / 100.0f;
      return true;
    }
    // Metadata objects exceed the stack budget (as in BookProgress.cpp).
    auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("CARD", "OOM: book metadata");
      return false;
    }
    if (!epub->load(false, true)) return false;
    copyText(out.title, sizeof(out.title), epub->getTitle());
    copyText(out.author, sizeof(out.author), epub->getAuthor());
    if (out.title[0] == '\0') titleFromPath(path, out.title, sizeof(out.title));
    out.spineCount = epub->getSpineItemsCount();

    uint8_t data[10]{};
    HalFile progress;
    if (Storage.openFileForRead("CARD", epub->getCachePath() + "/progress.bin", progress)) {
      const int size = progress.read(data, sizeof(data));
      if (size == 4 || size == 6 || size == 10) {
        out.spineIndex = data[0] | (data[1] << 8);
        const int page = data[2] | (data[3] << 8);
        const int total = size >= 6 ? data[4] | (data[5] << 8) : 0;
        if (page != 0xFFFF) out.chapterPage = page;
        out.chapterPageCount = total;
        if (out.spineCount > 0 && out.spineIndex > out.spineCount) {
          // Past the end: a stale or corrupt progress file (as util/BookProgress.cpp reads it), not "finished".
          out.spineIndex = -1;
          out.chapterPage = -1;
          out.chapterPageCount = 0;
        } else if (out.spineIndex == out.spineCount && out.spineCount > 0) {
          out.bookFraction = 1.0f;
        } else if (out.spineCount > 0 && epub->getBookSize() > 0) {
          const float inChapter =
              total > 0 && out.chapterPage >= 0 ? std::clamp(float(out.chapterPage) / total, 0.0f, 1.0f) : 0.0f;
          out.bookFraction = std::clamp(epub->calculateProgress(out.spineIndex, inChapter), 0.0f, 1.0f);
        }
        if (out.bookFraction >= 0) out.percent = static_cast<int>(out.bookFraction * 100 + 0.5f);
      }
    }
    if (out.spineIndex >= 0 && out.spineIndex < out.spineCount) {
      const int toc = epub->getTocIndexForSpineIndex(out.spineIndex);
      if (toc >= 0) copyText(out.chapterTitle, sizeof(out.chapterTitle), epub->getTocItem(toc).title);
    }

    // An already generated 1-bit thumbnail (the home screen's, any theme's size); making one (a JPEG
    // decode) is too slow for the sleep path, and a 2-bit cover would lose its greys in the card's
    // black-and-white frame (CoverDraw.h). The tallest that fits the card's box 1:1, else the
    // smallest (drawn scaled down).
    const int themeHeight = UITheme::getInstance().getMetrics().homeCoverHeight;
    const int heights[] = {themeHeight, 400, 300, 226};
    int bestH = 0;
    int bestW = 0;
    bool bestFits = false;
    for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
      if (heights[i] <= 0 || std::find(heights, heights + i, heights[i]) != heights + i) continue;
      const std::string candidate = epub->getThumbBmpPath(heights[i]);
      if (!Storage.exists(candidate.c_str())) continue;
      HalFile file;
      if (!Storage.openFileForRead("CARD", candidate, file) || file.size() == 0) continue;  // empty = "no cover"
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok || !bitmap.is1Bit()) continue;
      const int w = bitmap.getWidth();
      const int h = bitmap.getHeight();
      if (w <= 0 || h <= 0) continue;
      const bool fits = w <= CardBook::COVER_MAX_W && h <= CardBook::COVER_MAX_H;
      const bool better = bestH == 0 || (fits && (!bestFits || h > bestH)) || (!fits && !bestFits && h < bestH);
      if (better) {
        bestH = h;
        bestW = w;
        bestFits = fits;
        coverPath_ = candidate;
      }
    }
    out.hasCover = !coverPath_.empty();
    out.coverWidth = bestW;
    out.coverHeight = bestH;
    return true;
  }

  bool drawBookCover(GfxRenderer& renderer, const int x, const int y, const int maxWidth,
                     const int maxHeight) const override {
    if (coverPath_.empty()) {
      CardBook book;
      if (!loadBook(book) || coverPath_.empty()) return false;
    }
    HalFile file;
    if (!Storage.openFileForRead("CARD", coverPath_, file)) return false;
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() != BmpReaderError::Ok || !bitmap.is1Bit()) return false;
    int w = 0;
    int h = 0;
    if (!cover::fitSize(bitmap.getWidth(), bitmap.getHeight(), maxWidth, maxHeight, w, h)) return false;
    return cover::drawOneBitCover(renderer, bitmap, x + (maxWidth - w) / 2, y + (maxHeight - h) / 2, w, h);
  }

  int loadBookmarks(CardSnippet* out, const int max) const override {
    if (out == nullptr || max <= 0 || APP_STATE.openEpubPath.empty()) return 0;
    std::vector<BookmarkEntry> bookmarks;
    if (!BookmarkFile::load(APP_STATE.openEpubPath, bookmarks)) return 0;
    const int total = static_cast<int>(bookmarks.size());
    const int n = std::min(max, total);
    for (int i = 0; i < n; i++) {
      // Spread over the whole list when it is longer than max, so every part of the book can be quoted.
      const auto& b = bookmarks[static_cast<size_t>(static_cast<int64_t>(i) * total / n)];
      out[i] = CardSnippet{};
      copyText(out[i].text, sizeof(out[i].text), b.summary);
      copyText(out[i].label, sizeof(out[i].label), b.name);
      out[i].fraction = b.percentage;
    }
    return n;
  }

  bool hasBookmarks() const override {
    if (APP_STATE.openEpubPath.empty()) return false;
    // A saved list with an entry is far longer than the empty document ({"bookmarks":[]}, 16 bytes).
    return fileSize(BookmarkUtil::getBookmarkPath(APP_STATE.openEpubPath).c_str()) > 24;
  }

 private:
  // The cover image loadBook() found for the open book ("" = none).
  mutable std::string coverPath_;
};

DeviceCardIo deviceIo;

SleepCardSettings snapshotSettings() {
  SleepCardSettings s;
  // An internet-address location (older firmware) counts as not set: it names a carrier's city.
  copyText(s.location, sizeof(s.location), usableLocation(SETTINGS.sleepCardLocationFix, SETTINGS.sleepCardLocation));
  s.huntMode = static_cast<HuntMode>(std::min<uint8_t>(SETTINGS.huntingSeason, 2));
  s.huntStart = {SETTINGS.huntStartMonth, SETTINGS.huntStartDay};
  s.huntEnd = {SETTINGS.huntEndMonth, SETTINGS.huntEndDay};
  s.legalRule = static_cast<LegalLightRule>(std::min<uint8_t>(SETTINGS.legalLightRule, 1));
  copyText(s.ownerName, sizeof(s.ownerName), SETTINGS.ownerName);
  copyText(s.ownerContact1, sizeof(s.ownerContact1), SETTINGS.ownerContact1);
  copyText(s.ownerContact2, sizeof(s.ownerContact2), SETTINGS.ownerContact2);
  s.quoteSource = SETTINGS.quoteSources < static_cast<uint8_t>(QuoteSource::Count)
                      ? static_cast<QuoteSource>(SETTINGS.quoteSources)
                      : QuoteSource::All;
  const struct {
    uint8_t value;
    CardId id;
  } shuffle[] = {{SETTINGS.shuffleNowReading, CardId::NowReading}, {SETTINGS.shuffleDay, CardId::Day},
                 {SETTINGS.shuffleCalendar, CardId::Calendar},     {SETTINGS.shuffleQuote, CardId::Quote},
                 {SETTINGS.shuffleOwner, CardId::Owner},           {SETTINGS.shuffleSky, CardId::Sky},
                 {SETTINGS.shufflePictures, CardId::Pictures},     {SETTINGS.shuffleWeather, CardId::Weather}};
  s.shuffleMask = 0;
  for (const auto& entry : shuffle) {
    if (entry.value) s.shuffleMask |= cardBit(entry.id);
  }
  s.weatherOn = SETTINGS.weatherEnabled != 0;
  s.weatherUnits = SETTINGS.weatherUnits < static_cast<uint8_t>(WeatherUnits::Count)
                       ? static_cast<WeatherUnits>(SETTINGS.weatherUnits)
                       : WeatherUnits::Metric;
  return s;
}

}  // namespace

bool cardsDark() { return SETTINGS.darkCards != 0; }

void buildDeviceContext(CardContext& ctx) {
  ctx = CardContext{};
  time_t now = 0;
  halClock.invalidate();  // a fresh RTC read: "Screen updated" must not be up to 10 s (a minute) early
  if (halClock.isAvailable() && halClock.utcEpoch(now) && plausibleTime(static_cast<int64_t>(now))) {
    ctx.timeValid = true;
    ctx.utcNow = static_cast<int64_t>(now);
    ctx.utcOffsetAt = &libcUtcOffset;
    ctx.utcOffsetS = libcUtcOffset(ctx.utcNow);
    ctx.localNow = localDateOf(ctx.utcNow, ctx.utcOffsetAt);
  }
  ctx.clock12h = SETTINGS.clockFormat == 1;
  ctx.batteryPercent = std::min<int>(100, powerManager.getBatteryPercentage());
  ctx.charging = externalPowerPresent();
  ctx.awakeSeconds = static_cast<uint32_t>(millis() / 1000UL);
  ctx.settings = snapshotSettings();
  ctx.location.valid = parseLocation(ctx.settings.location, ctx.location.lat, ctx.location.lon);
  ctx.bookPath = APP_STATE.openEpubPath.c_str();
  ctx.fromReader = APP_STATE.lastSleepFromReader;
  ctx.seed = esp_random();
  ctx.dark = cardsDark();
  ctx.io = &deviceIo;
}

CardOutcome drawDeviceCard(GfxRenderer& renderer, CardId requested, CardId* shown, const CardDrawOptions& options) {
  const unsigned long start = millis();
  CardContext ctx;
  buildDeviceContext(ctx);
  ctx.repeatLast = options.repeatLast;
  CardId picked = requested;
  const bool drawn = renderCardOrShuffle(requested, ctx, renderer, picked, options.excluded);
  if (shown) *shown = picked;
  if (picked == CardId::Pictures) return CardOutcome::Pictures;
  LOG_INF("CARD", "%s %s in %lu ms", cardName(picked), drawn ? "drawn" : "declined", millis() - start);
  if (!drawn) renderer.clearScreen();
  return drawn ? CardOutcome::Drawn : CardOutcome::Declined;
}

}  // namespace sleepcards
