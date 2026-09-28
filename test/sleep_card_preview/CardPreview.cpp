#include "CardPreview.h"

#include <Bitmap.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <builtinFonts/all.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "src/fontIds.h"
#include "src/sleepcards/BrandScreen.h"
#include "src/sleepcards/CardDraw.h"
#include "src/sleepcards/CoverDraw.h"

#ifndef CARD_PREVIEW_REPO_ROOT
#error "CARD_PREVIEW_REPO_ROOT must point at the repository"
#endif

namespace sleepcards::preview {
namespace {

// The fonts main.cpp registers, from the same flash tables.
EpdFont notoserif12Regular(&notoserif_12_regular), notoserif12Bold(&notoserif_12_bold),
    notoserif12Italic(&notoserif_12_italic), notoserif12BoldItalic(&notoserif_12_bolditalic);
EpdFont notoserif14Regular(&notoserif_14_regular), notoserif14Bold(&notoserif_14_bold),
    notoserif14Italic(&notoserif_14_italic), notoserif14BoldItalic(&notoserif_14_bolditalic);
EpdFont notoserif16Regular(&notoserif_16_regular), notoserif16Bold(&notoserif_16_bold),
    notoserif16Italic(&notoserif_16_italic), notoserif16BoldItalic(&notoserif_16_bolditalic);
EpdFont notoserif18Regular(&notoserif_18_regular), notoserif18Bold(&notoserif_18_bold),
    notoserif18Italic(&notoserif_18_italic), notoserif18BoldItalic(&notoserif_18_bolditalic);
EpdFont notosans12Regular(&notosans_12_regular), notosans12Bold(&notosans_12_bold),
    notosans12Italic(&notosans_12_italic), notosans12BoldItalic(&notosans_12_bolditalic);
EpdFont notosans14Regular(&notosans_14_regular), notosans14Bold(&notosans_14_bold),
    notosans14Italic(&notosans_14_italic), notosans14BoldItalic(&notosans_14_bolditalic);
EpdFont notosans16Regular(&notosans_16_regular), notosans16Bold(&notosans_16_bold),
    notosans16Italic(&notosans_16_italic), notosans16BoldItalic(&notosans_16_bolditalic);
EpdFont notosans18Regular(&notosans_18_regular), notosans18Bold(&notosans_18_bold),
    notosans18Italic(&notosans_18_italic), notosans18BoldItalic(&notosans_18_bolditalic);
EpdFont smallFont(&notosans_8_regular);
EpdFont ui10Regular(&ubuntu_10_regular), ui10Bold(&ubuntu_10_bold);
EpdFont ui12Regular(&ubuntu_12_regular), ui12Bold(&ubuntu_12_bold);

struct HostEnv {
  GfxRenderer renderer{display};
  FontDecompressor decompressor;
  FontCacheManager cache{renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts()};
  HostCardIo io;

  HostEnv() {
    display.clearScreen();
    renderer.begin();
    decompressor.init();
    cache.setFontDecompressor(&decompressor);
    renderer.setFontCacheManager(&cache);
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    const auto family = [](EpdFont& r, EpdFont* b, EpdFont* i, EpdFont* bi) { return EpdFontFamily(&r, b, i, bi); };
    renderer.insertFont(NOTOSERIF_12_FONT_ID,
                        family(notoserif12Regular, &notoserif12Bold, &notoserif12Italic, &notoserif12BoldItalic));
    renderer.insertFont(NOTOSERIF_14_FONT_ID,
                        family(notoserif14Regular, &notoserif14Bold, &notoserif14Italic, &notoserif14BoldItalic));
    renderer.insertFont(NOTOSERIF_16_FONT_ID,
                        family(notoserif16Regular, &notoserif16Bold, &notoserif16Italic, &notoserif16BoldItalic));
    renderer.insertFont(NOTOSERIF_18_FONT_ID,
                        family(notoserif18Regular, &notoserif18Bold, &notoserif18Italic, &notoserif18BoldItalic));
    renderer.insertFont(NOTOSANS_12_FONT_ID,
                        family(notosans12Regular, &notosans12Bold, &notosans12Italic, &notosans12BoldItalic));
    renderer.insertFont(NOTOSANS_14_FONT_ID,
                        family(notosans14Regular, &notosans14Bold, &notosans14Italic, &notosans14BoldItalic));
    renderer.insertFont(NOTOSANS_16_FONT_ID,
                        family(notosans16Regular, &notosans16Bold, &notosans16Italic, &notosans16BoldItalic));
    renderer.insertFont(NOTOSANS_18_FONT_ID,
                        family(notosans18Regular, &notosans18Bold, &notosans18Italic, &notosans18BoldItalic));
    renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&smallFont));
    renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&ui10Regular, &ui10Bold));
    renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&ui12Regular, &ui12Bold));
  }
};

HostEnv& env() {
  static HostEnv e;
  return e;
}

// The sample book's cover: a 1-bit thumbnail as the reader stores them.
constexpr const char* COVER_FIXTURE = "/cover_thumb_300.bmp";
constexpr int COVER_FIXTURE_W = 180;
constexpr int COVER_FIXTURE_H = 300;

std::string fixturePath(const char* sdPath) {
  return std::string(CARD_PREVIEW_REPO_ROOT) + "/test/sleep_card_preview/fixtures" + (sdPath[0] == '/' ? "" : "/") +
         sdPath;
}

void copyText(char* dest, const size_t cap, const char* text) { std::snprintf(dest, cap, "%s", text); }

// ---- PNG: stored (uncompressed) deflate blocks, so no zlib is needed --------------------------------
uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void putBe32(std::vector<uint8_t>& out, const uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  putBe32(out, static_cast<uint32_t>(data.size()));
  std::vector<uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  putBe32(out, crc32(body.data(), body.size()));
}

bool writePng(const std::string& path, const int w, const int h, const std::vector<uint8_t>& gray) {
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (w + 1));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), gray.begin() + static_cast<long>(y) * w, gray.begin() + static_cast<long>(y + 1) * w);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1;
  uint32_t b = 0;
  for (const uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t pos = 0; pos < raw.size();) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n == raw.size() ? 1 : 0);
    z.push_back(static_cast<uint8_t>(n));
    z.push_back(static_cast<uint8_t>(n >> 8));
    z.push_back(static_cast<uint8_t>(~n));
    z.push_back(static_cast<uint8_t>(~n >> 8));
    z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
    pos += n;
  }
  putBe32(z, (b << 16) | a);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  putBe32(ihdr, static_cast<uint32_t>(w));
  putBe32(ihdr, static_cast<uint32_t>(h));
  ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});  // 8-bit greyscale
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  std::fclose(f);
  return ok;
}

}  // namespace

// ---- the fake SD card ------------------------------------------------------------------------------

int32_t HostCardIo::fileSize(const char* path) const {
  if (!path) return -1;
  std::error_code ec;
  const auto size = std::filesystem::file_size(fixturePath(path), ec);
  return ec ? -1 : static_cast<int32_t>(size);
}

int32_t HostCardIo::readFileAt(const char* path, const uint32_t offset, char* buf, const size_t cap) const {
  if (!path || !buf) return -1;
  std::FILE* f = std::fopen(fixturePath(path).c_str(), "rb");
  if (!f) return -1;
  int32_t got = -1;
  if (std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0) got = static_cast<int32_t>(std::fread(buf, 1, cap, f));
  std::fclose(f);
  return got;
}

bool HostCardIo::writeFile(const char* path, const char* data, size_t len) const {
  if (!path || !data) return false;
  len = std::min<size_t>(len, 4096);
  const std::filesystem::path out = std::filesystem::path(outputDir()) / "state" / (path[0] == '/' ? path + 1 : path);
  std::error_code ec;
  std::filesystem::create_directories(out.parent_path(), ec);
  std::FILE* f = std::fopen(out.string().c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(data, 1, len, f) == len;
  std::fclose(f);
  return ok;
}

bool HostCardIo::loadBook(CardBook& out) const {
  out = CardBook{};
  if (!hasBook) return false;
  copyText(out.title, sizeof(out.title), "Moby-Dick; or, The Whale");
  copyText(out.author, sizeof(out.author), "Herman Melville");
  copyText(out.chapterTitle, sizeof(out.chapterTitle), "Chapter 36. The Quarter-Deck");
  out.spineIndex = 38;
  out.spineCount = 137;
  out.chapterPage = 4;
  out.chapterPageCount = 14;
  out.bookFraction = 0.284f;
  out.percent = 28;
  out.hasCover = true;
  out.coverWidth = COVER_FIXTURE_W;
  out.coverHeight = COVER_FIXTURE_H;
  return true;
}

bool HostCardIo::drawBookCover(GfxRenderer& r, const int x, const int y, const int maxWidth,
                               const int maxHeight) const {
  if (!hasBook) return false;
  // A real 1-bit thumbnail (fixtures/make_cover_thumb.py) through the device's cover path.
  HalFile file;
  if (!Storage.openFileForRead("CARD", fixturePath(COVER_FIXTURE), file)) return false;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || !bitmap.is1Bit()) return false;
  int w = 0;
  int h = 0;
  if (!cover::fitSize(bitmap.getWidth(), bitmap.getHeight(), maxWidth, maxHeight, w, h)) return false;
  return cover::drawOneBitCover(r, bitmap, x + (maxWidth - w) / 2, y + (maxHeight - h) / 2, w, h);
}

int HostCardIo::loadBookmarks(CardSnippet* out, const int max) const {
  if (!hasBook || !out || max <= 0) return 0;
  static constexpr struct {
    const char* text;
    const char* label;
    float fraction;
  } SAMPLES[] = {
      {"Call me Ishmael. Some years ago - never mind how long precisely - having little or no money in my purse", "",
       0.002f},
      {"It is not down in any map; true places never are.", "Kokovoko", 0.071f},
  };
  const int n = std::min<int>(max, static_cast<int>(std::size(SAMPLES)));
  for (int i = 0; i < n; i++) {
    out[i] = CardSnippet{};
    copyText(out[i].text, sizeof(out[i].text), SAMPLES[i].text);
    copyText(out[i].label, sizeof(out[i].label), SAMPLES[i].label);
    out[i].fraction = SAMPLES[i].fraction;
  }
  return n;
}

// ---- environment -----------------------------------------------------------------------------------

GfxRenderer& renderer() { return env().renderer; }
HostCardIo& hostIo() { return env().io; }

CardContext sampleContext() {
  // US Pacific, the rule set HalClock would be given for America/Los_Angeles.
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
  CardContext ctx;
  ctx.timeValid = true;
  ctx.utcOffsetAt = &libcUtcOffset;
  ctx.utcNow = localDayStart(2026, 11, 2, ctx.utcOffsetAt) + 20 * 3600 + 40 * 60;  // 2026-11-02 20:40 PST
  ctx.utcOffsetS = libcUtcOffset(ctx.utcNow);
  ctx.localNow = localDateOf(ctx.utcNow, ctx.utcOffsetAt);
  ctx.clock12h = false;
  ctx.batteryPercent = 73;
  ctx.awakeSeconds = 25 * 60;
  // Seattle's public city-centre coordinates, for the preview only.
  formatLocation(47.61, -122.33, ctx.settings.location, sizeof(ctx.settings.location));
  ctx.location.valid = parseLocation(ctx.settings.location, ctx.location.lat, ctx.location.lon);
  ctx.settings.shuffleMask = defaultShuffleMask();
  ctx.bookPath = "/Books/Moby-Dick.epub";
  ctx.fromReader = true;
  ctx.seed = 0x5EED1234u;
  ctx.io = &env().io;
  return ctx;
}

CardContext sampleContextNoLocation() {
  CardContext ctx = sampleContext();
  ctx.settings.location[0] = '\0';
  ctx.location = CardLocation{};
  return ctx;
}

std::string outputDir() {
  const std::string dir = std::string(CARD_PREVIEW_REPO_ROOT) + "/build/cards";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

bool writeFramePng(const std::string& path) {
  GfxRenderer& r = renderer();
  const int w = r.getScreenWidth();
  const int h = r.getScreenHeight();
  const uint8_t* fb = display.getFrameBuffer();
  std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // Portrait: logical (x, y) is panel (y, panelHeight - 1 - x), as GfxRenderer maps it.
      const int px = y;
      const int py = HalDisplay::DISPLAY_HEIGHT - 1 - x;
      const bool white = (fb[py * HalDisplay::DISPLAY_WIDTH_BYTES + px / 8] >> (7 - (px & 7))) & 1;
      gray[static_cast<size_t>(y) * w + x] = white ? 255 : 0;
    }
  }
  return writePng(path, w, h, gray);
}

double renderCardPng(const CardId id, const CardContext& ctx, const std::string& name, bool* declined) {
  GfxRenderer& r = renderer();
  const auto start = std::chrono::steady_clock::now();
  const bool drawn = renderCard(id, ctx, r);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  if (!drawn) drawX4ProLogoScreen(r, tr(STR_SLEEPING));
  if (declined) *declined = !drawn;
  writeFramePng(outputDir() + "/" + name + ".png");
  return ms;
}

}  // namespace sleepcards::preview

unsigned long millis() {
  static const auto start = std::chrono::steady_clock::now();
  return static_cast<unsigned long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

unsigned long micros() {
  static const auto start = std::chrono::steady_clock::now();
  return static_cast<unsigned long>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}
