#pragma once

// Survival guide: a topic's page text (t/<id>.gp) and the About text (about.txt). Pure.
//
// .gp format 1 - line-based, ASCII-safe UTF-8, one block a line, blank lines ignored:
//   = <page title>            starts a page (every page starts with one)
//   @fig <name>[ <caption>]   the page's figure, fig/L/<name>.png; only straight after the title
//   1. <text>                 a numbered step (1..999)
//   - <text>                  a bullet
//   ! <text>                  a boxed WARNING
//   * <text>                  a boxed NOTE
//   ---                       an authored page break (the next line is the next page's "= ")
//   \<text>                   a paragraph that would otherwise read as markup (the '\' is dropped)
//   <text>                    a paragraph
// Inside any text, "**" toggles bold. A topic's pages are the pages of its file; topics.tsv's page
// count must agree (the pack tests check it; on the device a mismatch is clamped, never trusted).
// The device never splits an authored page's meaning: when one does not fit the screen, the layout
// (GdLayout.h) continues it on follow-on screens.

#include <cstddef>
#include <cstdint>

#include "GdPack.h"

namespace gd {

enum class BlockKind : uint8_t { Para = 0, Bullet, Step, Warning, Note };

struct Block {
  const char* text = "";         // NUL-terminated, in the caller's buffer, markup prefix removed
  const char* marker = nullptr;  // a Step's "N." in the buffer (markerLen bytes, not NUL-terminated)
  BlockKind kind = BlockKind::Para;
  uint8_t markerLen = 0;
  uint16_t number = 0;  // a Step's number
};

struct PageText {
  const char* title = "";
  const char* figure = nullptr;   // the figure's name, or nullptr
  const char* caption = nullptr;  // its caption ("" when none), or nullptr when no figure
  uint16_t firstBlock = 0;
  uint16_t blockCount = 0;
};

enum class GpError : uint8_t {
  None = 0,
  Empty,      // no page at all
  NoTitle,    // a page (or the file) does not start with "= "
  BadFigure,  // "@fig" without a valid name, or not straight after the title, or a second one
  BadLine,    // an empty item ("- " with no text), a bad step number
  TooBig,     // over MAX_GP_BYTES, MAX_PAGES or MAX_BLOCKS
};

// One parsed .gp. Parses IN PLACE (needs text[len] writable, GdText.h); the strings point into the
// buffer, which must outlive this. ~13 KB on the device (allocate with the store, in PSRAM).
class TopicText {
 public:
  static constexpr int MAX_BLOCKS = 1024;

  GpError parse(char* text, size_t len);
  int errorLine() const { return badLine; }  // 1-based line of the last error (0 = none)

  int pageCount() const { return pages; }
  const PageText& page(const int index) const { return pageList[index]; }
  int blockCount() const { return blocks; }
  const Block& block(const int index) const { return blockList[index]; }
  // The page clamped to 0..pageCount()-1 (a stale page number from a bookmark or topics.tsv).
  int clampPage(int index) const;

 private:
  GpError fail(GpError e, int line);

  PageText pageList[MAX_PAGES];
  Block blockList[MAX_BLOCKS];
  int pages = 0;
  int blocks = 0;
  int badLine = 0;
};

}  // namespace gd
