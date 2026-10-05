#include "GdNav.h"

namespace gd {

int Seq::indexOf(const int topic) const {
  for (int i = 0; i < count; i++) {
    if (topics[i] == topic) return i;
  }
  return -1;
}

Seq categorySeq(const Catalog& catalog, const int category, uint16_t* out, const int cap) {
  Seq s;
  s.topics = out;
  if (category < 0 || category >= catalog.categoryCount()) return s;
  const Category& c = catalog.category(category);
  for (int i = 0; i < c.count && s.count < cap; i++) out[s.count++] = static_cast<uint16_t>(c.first + i);
  return s;
}

int neighborTopic(const Seq& seq, const int topic, const int dir) {
  const int i = seq.indexOf(topic);
  if (i < 0) return -1;
  const int j = i + (dir < 0 ? -1 : 1);
  return (j >= 0 && j < seq.count) ? seq.topics[j] : -1;
}

Step nextPos(const Pos& at, const int screensOnPage, const int pageCount, const Seq& seq, const Catalog& catalog,
             Pos& out) {
  out = at;
  if (!at.valid()) return Step::None;
  const int sub = resolveSub(at.sub, screensOnPage);
  if (sub + 1 < screensOnPage) {
    out.sub = static_cast<uint8_t>(sub + 1);
    return Step::SamePage;
  }
  if (at.page + 1 < pageCount) {
    out.page = static_cast<uint8_t>(at.page + 1);
    out.sub = 0;
    return Step::OtherPage;
  }
  const int t = neighborTopic(seq, at.topic, +1);
  if (t < 0 || !catalog.validTopic(t)) return Step::None;
  out.topic = static_cast<int16_t>(t);
  out.page = 0;
  out.sub = 0;
  return Step::OtherTopic;
}

Step prevPos(const Pos& at, const int screensOnPage, const int pageCount, const Seq& seq, const Catalog& catalog,
             Pos& out) {
  out = at;
  if (!at.valid()) return Step::None;
  const int sub = resolveSub(at.sub, screensOnPage);
  if (sub > 0) {
    out.sub = static_cast<uint8_t>(sub - 1);
    return Step::SamePage;
  }
  if (at.page > 0) {
    const int page = at.page < pageCount ? at.page : pageCount;  // a stale page past the end
    out.page = static_cast<uint8_t>(page > 0 ? page - 1 : 0);
    out.sub = SUB_LAST;
    return Step::OtherPage;
  }
  const int t = neighborTopic(seq, at.topic, -1);
  if (t < 0 || !catalog.validTopic(t)) return Step::None;
  out.topic = static_cast<int16_t>(t);
  out.page = static_cast<uint8_t>(catalog.topic(t).pages - 1);
  out.sub = SUB_LAST;
  return Step::OtherTopic;
}

uint8_t resolveSub(const uint8_t sub, const int screens) {
  if (screens <= 1) return 0;
  return sub >= screens ? static_cast<uint8_t>(screens - 1) : sub;
}

int screenOrdinal(const Pos& at, const uint8_t* screensPerPage, const int pageCount) {
  int n = 0;
  for (int p = 0; p < at.page && p < pageCount; p++) n += screensPerPage[p];
  const int page = at.page < pageCount ? at.page : pageCount - 1;
  return n + (page >= 0 ? resolveSub(at.sub, screensPerPage[page]) : 0) + 1;
}

int screenTotal(const uint8_t* screensPerPage, const int pageCount) {
  int n = 0;
  for (int p = 0; p < pageCount; p++) n += screensPerPage[p];
  return n;
}

bool posOfOrdinal(const int ordinal, const uint8_t* screensPerPage, const int pageCount, uint8_t& page, uint8_t& sub) {
  int left = ordinal - 1;
  if (left < 0) return false;
  for (int p = 0; p < pageCount; p++) {
    if (left < screensPerPage[p]) {
      page = static_cast<uint8_t>(p);
      sub = static_cast<uint8_t>(left);
      return true;
    }
    left -= screensPerPage[p];
  }
  return false;
}

}  // namespace gd
