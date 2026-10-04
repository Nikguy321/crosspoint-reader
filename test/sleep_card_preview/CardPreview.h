#pragma once

// Host preview of the sleep cards: the REAL GfxRenderer and built-in fonts
// drawing into an in-memory 800x480 framebuffer (host/HalDisplay.cpp), a fixed
// sample context, a fake CardIo, and a PNG writer. Linked as SleepCardHost by
// the preview test and by every card's own test.
//
//   renderer()           the one renderer, portrait, fonts installed
//   sampleContext()      2026-11-02 20:40 America/Los_Angeles (PST), Seattle's public
//                        coordinates (47.61, -122.33), battery 73 %, a sample book, Weather on
//   writeFramePng(path)  the framebuffer as the reader would show it (480x800 PNG)
//   renderCardPng(id, ctx, name)  renderCard(), else the logo fallback, into
//                        <repo>/build/cards/<name>.png; returns the compute time
#include <GfxRenderer.h>

#include <string>

#include "src/sleepcards/SleepCard.h"

namespace sleepcards::preview {

// Host timings only catch a runaway (a loop on I/O, an unbounded search): the device does its
// double-precision astronomy in software, roughly 1,500x slower than a desktop, so the < 300 ms
// sleep budget is measured on the device (x4bench.py card <name> reports ms=, the firmware logs
// "CARD <name> drawn in N ms").
constexpr double HOST_RUNAWAY_MS = 250.0;  // generous: parallel ctest runs stall a test now and then

// The fake CardIo: files under the fixtures directory stand in for the SD card
// ("/quotes.txt" -> test/sleep_card_preview/fixtures/quotes.txt), state files go to
// <repo>/build/cards/state/, the book is a fixed public-domain sample.
class HostCardIo final : public CardIo {
 public:
  int32_t fileSize(const char* path) const override;
  int32_t readFileAt(const char* path, uint32_t offset, char* buf, size_t cap) const override;
  bool writeFile(const char* path, const char* data, size_t len) const override;
  bool loadBook(CardBook& out) const override;
  bool drawBookCover(GfxRenderer& renderer, int x, int y, int maxWidth, int maxHeight) const override;
  int loadBookmarks(CardSnippet* out, int max) const override;

  bool hasBook = true;  // false: behave like a reader with no open book
  // The Weather cache the card reads (/.crosspoint/sleepcards/weather.dat) is this fixture of
  // fixtures/.crosspoint/sleepcards/ (make_weather_fixtures.py); "" = no cache on the card.
  std::string weatherFixture = "weather.dat";
};

GfxRenderer& renderer();
HostCardIo& hostIo();

// The fixed sample context (sets TZ for the process). Hunting season Off, default Shuffle picks.
CardContext sampleContext();
// The same moment somewhere with no location set.
CardContext sampleContextNoLocation();

// <repo>/build/cards (created on demand).
std::string outputDir();
// Framebuffer -> PNG as seen on the device in portrait (logical 480x800).
bool writeFramePng(const std::string& path);
// Draw a card (or, when it declines, the X4 Pro logo screen the device would fall back to)
// and write <outputDir>/<name>.png. declined is set when the card returned false.
double renderCardPng(CardId id, const CardContext& ctx, const std::string& name, bool* declined = nullptr);

}  // namespace sleepcards::preview
