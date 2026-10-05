#pragma once

// Survival guide: the device pack (format 1) - pack.txt, categories.tsv and topics.tsv. Pure (no
// Arduino): the store reads the files from the card and hands the text here.
//
// The pack is a folder on the card, /Guides/<id>/, built on a computer by scripts/guide/make_pack.py
// from the source pack in packs/guide/<id>/ (the built copy is committed in packs/guide/build/<id>/):
//
//   pack.txt        key=value lines ('#' comments and blank lines skipped, unknown keys ignored):
//                     format=1 id=survival version=2026.10.1 title=Survival Guide short=SURVIVAL
//                     status=reviewed-by-ai status_text=<the honest status sentence>
//                     note=<the whole guide's reference-only + 911 note>
//                     ref_note=<the line medical topics show> license=MIT min_app=1
//                   format, id, version and title are required. A format newer than FORMAT, or a
//                   min_app newer than APP_VERSION, is NeedsNewerFirmware (checked first, so a newer
//                   pack is never called damaged).
//   categories.tsv  <cat-id> TAB <TITLE> TAB <blurb> TAB <order>   one a line, order 1, 2, 3, ...
//                   (the file's order; EMERGENCY first). Extra columns are ignored.
//   topics.tsv      <topic-id> TAB <cat-id> TAB <title> TAB <flags> TAB <file> TAB <pages> TAB <summary>
//                   flags: "-" or letters, Q = quick card, M = medical (other letters ignored);
//                   file: "t/<name>.gp"; pages: the authored pages, 1..MAX_PAGES. Topics are listed
//                   category by category, in the categories' order (a topic's place in its category
//                   is its place in the file). Extra columns are ignored.
//   search.idx      GdSearch.h        t/<id>.gp, about.txt   GdPage.h
//   fig/L/<name>.png  1-bit PNG, at most MAX_FIG_W x MAX_FIG_H (GdPng.h reads its size)
//   SHA256SUMS      "<sha256>  <path>" lines (for the downloader; the app does not read it)
// Ids are [a-z0-9-]: categories 1..MAX_CAT_ID, topics 1..MAX_ID bytes, unique. Text is ASCII-safe
// UTF-8 (the builder turns typographic punctuation into ASCII).
//
// Anything that does not parse is PackError::Damaged ("The guide on the card is damaged"), never a
// crash: the tests feed every truncation and random corruption of the real pack through here.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gd {

constexpr int FORMAT = 1;       // the newest pack format this firmware reads
constexpr int APP_VERSION = 1;  // a pack's min_app above this needs newer firmware

constexpr int MAX_CATEGORIES = 64;
constexpr int MAX_TOPICS = 1024;
constexpr int MAX_PAGES = 64;  // authored pages in one topic
constexpr size_t MAX_ID = 32;
constexpr size_t MAX_CAT_ID = 20;
constexpr size_t MAX_TITLE = 80;     // a topic or category title, bytes
constexpr size_t MAX_SUMMARY = 120;  // a topic summary or category blurb, bytes

// Largest file the store should read; a bigger one is Damaged (TooBig on the .gp / index).
constexpr size_t MAX_PACKTXT_BYTES = 4 * 1024;
constexpr size_t MAX_CATEGORIES_BYTES = 16 * 1024;
constexpr size_t MAX_TOPICS_BYTES = 256 * 1024;
constexpr size_t MAX_GP_BYTES = 64 * 1024;
constexpr size_t MAX_ABOUT_BYTES = 16 * 1024;
constexpr size_t MAX_INDEX_BYTES = 256 * 1024;
constexpr int MAX_FIG_W = 440;
constexpr int MAX_FIG_H = 480;
// The full-screen view's own raster (fig/XL, optional): the box drawFigureScreen leaves above the caption.
constexpr int MAX_FIG_XL_W = 456;
constexpr int MAX_FIG_XL_H = 620;

inline constexpr char GUIDES_DIR[] = "/Guides";
inline constexpr char DEFAULT_PACK_ID[] = "survival";
inline constexpr char PACK_FILE[] = "pack.txt";
inline constexpr char CATEGORIES_FILE[] = "categories.tsv";
inline constexpr char TOPICS_FILE[] = "topics.tsv";
inline constexpr char INDEX_FILE[] = "search.idx";
inline constexpr char ABOUT_FILE[] = "about.txt";
inline constexpr char FIGURE_DIR[] = "fig/L";      // fig/L/<name>.png: the page's figure
inline constexpr char FIGURE_XL_DIR[] = "fig/XL";  // fig/XL/<name>.png: the full-screen view's, when the pack has it

enum class PackError : uint8_t {
  None = 0,
  Missing,             // no pack.txt (the store says where to copy the pack)
  NeedsNewerFirmware,  // format > FORMAT or min_app > APP_VERSION
  Damaged,             // anything that does not parse, or breaks a cap
};

struct PackInfo {
  int format = 0;
  int minApp = 0;
  char id[MAX_ID + 1] = {};
  char version[24] = {};
  char title[48] = {};
  char shortTitle[24] = {};  // the breadcrumb's root ("SURVIVAL"); the title when absent
  char status[32] = {};      // "reviewed-by-ai"
  char statusText[192] = {};
  char note[192] = {};
  char refNote[160] = {};  // shown at the top of a medical topic's first page
  char license[16] = {};
};

// Long values are cut (on a UTF-8 boundary) to their field. Missing / bad required keys: Damaged.
PackError parsePackInfo(const char* text, size_t len, PackInfo& out);

constexpr uint8_t FLAG_QUICK = 1;
constexpr uint8_t FLAG_MEDICAL = 2;

// Strings are NUL-terminated and point into the caller's buffers (parsed in place).
struct Category {
  const char* id = "";
  const char* title = "";
  const char* blurb = "";
  uint16_t first = 0;  // its topics are first .. first + count - 1
  uint16_t count = 0;
};

struct Topic {
  const char* id = "";
  const char* title = "";
  const char* summary = "";
  const char* file = "";  // "t/<name>.gp", relative to the pack folder
  uint16_t category = 0;
  uint8_t flags = 0;
  uint8_t pages = 1;
  bool quick() const { return flags & FLAG_QUICK; }
  bool medical() const { return flags & FLAG_MEDICAL; }
};

// The categories and topics of a pack. Big (~24 KB on the device: allocate it in PSRAM, as a
// member of the store). parse() works IN PLACE: it turns the TABs and line breaks of both buffers
// into NULs and keeps pointers into them, so the buffers must outlive the catalog and stay unchanged;
// each buffer needs one writable byte past its text (text[len], GdText.h). On any error the catalog
// is left empty.
class Catalog {
 public:
  PackError parse(char* categoriesText, size_t categoriesLen, char* topicsText, size_t topicsLen);
  void clear();

  int categoryCount() const { return catCount; }
  const Category& category(int index) const { return cats[index]; }
  int topicCount() const { return topCount; }
  const Topic& topic(int index) const { return topics[index]; }
  bool validTopic(int index) const { return index >= 0 && index < topCount; }

  int findCategory(std::string_view id) const;  // -1 when absent
  int findTopic(std::string_view id) const;     // -1 when absent (binary search)

  // The quick cards (flag Q), in pack order. Returns how many were written (at most cap).
  int quickTopics(uint16_t* out, int cap) const;
  int quickCount() const;
  // Topics whose title (or summary) holds the query, case-insensitive, after trimming; in pack
  // order, titles first. The search's fallback. Returns how many were written.
  int titleSearch(std::string_view query, uint16_t* out, int cap) const;

 private:
  Category cats[MAX_CATEGORIES];
  Topic topics[MAX_TOPICS];
  PackError fail();

  uint16_t byId[MAX_TOPICS] = {};  // topic indices sorted by id
  int catCount = 0;
  int topCount = 0;
};

// [a-z0-9-]{1,maxLen}
bool validId(std::string_view id, size_t maxLen);
// "t/<x>.gp" with x of [a-z0-9-] (no "..", no other folder).
bool validTopicFile(std::string_view file);

// Path helpers ("<GUIDES_DIR>/<packId>/<file>"). Their length, or 0 when cap is too small.
size_t packPath(const char* packId, const char* file, char* out, size_t cap);
// "<GUIDES_DIR>/<packId>/<dir>/<name>.png" (dir: FIGURE_DIR or FIGURE_XL_DIR)
size_t figurePath(const char* packId, const char* name, char* out, size_t cap, const char* dir = FIGURE_DIR);

}  // namespace gd
