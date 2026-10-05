#pragma once

// Survival guide: moving through pages. Pure.
//
// A position is (topic, page, sub): the authored page and the screen inside it (a page that does
// not fit continues on follow-on screens, GdLayout.h). NEXT on the last screen of a topic's last
// page goes to the first screen of the next topic in the sequence the topic was opened from (by
// default its category, Catalog::category(c).first ..; the quick-card list for a quick card opened
// there); PREV on the first screen goes to the LAST screen of the previous topic's last page. That
// screen is not known until the page is laid out, so the position says SUB_LAST and the page
// screen resolves it (resolveSub) after layout. The bar's "n/m" counts screens across the topic.

#include <cstdint>

#include "GdPack.h"

namespace gd {

constexpr uint8_t SUB_LAST = 0xFF;

struct Pos {
  int16_t topic = -1;
  uint8_t page = 0;
  uint8_t sub = 0;  // the screen within the page, or SUB_LAST
  bool valid() const { return topic >= 0; }
  bool operator==(const Pos& o) const { return topic == o.topic && page == o.page && sub == o.sub; }
};

// The topics NEXT/PREV walk through, in order (indices into the Catalog).
struct Seq {
  const uint16_t* topics = nullptr;
  int count = 0;
  int indexOf(int topic) const;  // -1 when absent
};

// A category's topics as a sequence: fills out (cap entries) and returns the Seq over it.
Seq categorySeq(const Catalog& catalog, int category, uint16_t* out, int cap);

// The topic before (dir -1) or after (dir +1) `topic` in seq, or -1 (none, or topic not in seq).
int neighborTopic(const Seq& seq, int topic, int dir);

enum class Step : uint8_t {
  None = 0,    // nowhere to go (the first screen of the first topic, the last of the last)
  SamePage,    // another screen of the same page
  OtherPage,   // another page of the same topic
  OtherTopic,  // the neighbouring topic (the page screen opens it, saves Recent, ...)
};

// screensOnPage: the open page's screen count (layout); the topic's page count comes from the
// open TopicText (pageCount), the neighbour's from the catalog (Topic::pages).
Step nextPos(const Pos& at, int screensOnPage, int pageCount, const Seq& seq, const Catalog& catalog, Pos& out);
Step prevPos(const Pos& at, int screensOnPage, int pageCount, const Seq& seq, const Catalog& catalog, Pos& out);

// SUB_LAST (or anything past the end) -> screens - 1.
uint8_t resolveSub(uint8_t sub, int screens);

// The bar's n (1-based) and m from the screens of every page of the topic (topicScreens()).
int screenOrdinal(const Pos& at, const uint8_t* screensPerPage, int pageCount);
int screenTotal(const uint8_t* screensPerPage, int pageCount);
// The position of the n-th (1-based) screen of the topic: page and sub. False when out of range.
bool posOfOrdinal(int ordinal, const uint8_t* screensPerPage, int pageCount, uint8_t& page, uint8_t& sub);

}  // namespace gd
