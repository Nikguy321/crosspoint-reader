#pragma once
#include <Print.h>
#include <StreamingJsonParser.h>

#include <string>
#include <vector>

#include "OpdsEntry.h"

/**
 * Streaming parser for OPDS 2.0 JSON catalog feeds (application/opds+json).
 * Built on StreamingJsonParser (SAX-style), so a feed of any size is parsed
 * without buffering the response body.
 *
 * Extracts, per https://specs.opds.io/opds-2.0:
 *  - `navigation` links (top-level and inside `groups`) as NAVIGATION entries
 *  - `publications` (top-level and inside `groups`) with an EPUB acquisition
 *    link as BOOK entries
 *  - feed `links`: rel "search" (templated URI), rel "next"/"previous"/"prev"
 *
 * `facets`, `images` and feed `metadata` are skipped.
 */
class Opds2Parser final : public Print {
 public:
  Opds2Parser();

  Opds2Parser(const Opds2Parser&) = delete;
  Opds2Parser& operator=(const Opds2Parser&) = delete;

  size_t write(uint8_t c) override;
  size_t write(const uint8_t* data, size_t length) override;
  void flush() override {}

  bool error() const;
  bool truncated() const { return feedTruncated; }

  const std::vector<OpdsEntry>& getEntries() const& { return entries; }
  std::vector<OpdsEntry> getEntries() && { return std::move(entries); }
  const std::string& getSearchTemplate() const { return searchTemplate; }
  const std::string& getNextPageUrl() const { return nextPageUrl; }
  const std::string& getPrevPageUrl() const { return prevPageUrl; }

 private:
  // Semantic role of each open JSON container, decided from the parent scope
  // and the key that introduced it. SKIP swallows entire subtrees (facets,
  // images, metadata we don't consume, unknown extensions).
  enum class Scope : uint8_t {
    FEED,        // root object
    FEED_LINKS,  // feed "links" array
    FEED_LINK,   // one feed link object
    LINK_REL,    // "rel" array inside a link object (feed or publication)
    NAV,         // "navigation" array (feed or group)
    NAV_LINK,    // one navigation link object
    PUBS,        // "publications" array (feed or group)
    PUB,         // one publication object
    PUB_META,    // publication "metadata" object
    PUB_TITLE,   // localized title object ({"en": "..."})
    AUTHOR,      // contributor object ({"name": ...})
    AUTHOR_ARR,  // contributor array (strings and/or objects)
    PUB_LINKS,   // publication "links" array
    PUB_LINK,    // one publication link object
    GROUPS,      // "groups" array
    GROUP,       // one group object
    SKIP,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void onContainerStart(bool isObject);
  void onContainerEnd();
  void onStringValue(const char* value, size_t len);

  Scope scopeForChild(Scope parent, bool isObject) const;
  Scope current() const { return depth > 0 ? stack[depth - 1] : Scope::FEED; }

  void resetLink();
  void commitFeedLink();
  void commitPubLink();
  void commitNavLink();
  void commitPublication();
  void applyRel(const char* rel);

  static void assignBounded(std::string& target, const char* value, size_t len, size_t maxLen);
  static bool relIsAcquisition(const char* rel);

  StreamingJsonParser parser;

  static constexpr uint8_t MAX_DEPTH = StreamingJsonParser::MAX_NESTING + 1;
  Scope stack[MAX_DEPTH];
  uint8_t depth = 0;
  bool sawRoot = false;
  bool errorOccured = false;
  bool feedTruncated = false;

  char pendingKey[24] = {0};

  std::vector<OpdsEntry> entries;
  OpdsEntry currentEntry;

  // Accumulator for the link object currently being parsed (feed, navigation
  // or publication link — the scope on commit tells which).
  struct {
    std::string href;
    std::string title;
    bool relSearch = false;
    bool relNext = false;
    bool relPrev = false;
    bool relAcquisition = false;
    bool typeEpub = false;
    bool templated = false;
  } link;
  // Preferred acquisition href already found for the current publication
  // points at a plain EPUB (see commitPubLink()).
  bool pubHasPlainEpub = false;

  std::string searchTemplate;
  std::string nextPageUrl;
  std::string prevPageUrl;
};
