#include "GuideListModel.h"

#include <I18n.h>

#include <cstdio>

namespace gd::lists {

namespace {

const char* topicCount(const int n, char* buf, const size_t cap) {
  if (n == 1) return tr(STR_GD_ONE_TOPIC);
  std::snprintf(buf, cap, tr(STR_GD_TOPICS), n);
  return buf;
}

const char* pageCount(const int n, char* buf, const size_t cap) {
  if (n == 1) return tr(STR_GD_ONE_PAGE);
  std::snprintf(buf, cap, tr(STR_GD_PAGES), n);
  return buf;
}

struct Builder {
  Row* rows;
  int cap;
  int count = 0;
  void add(const RowKind kind, const int ref = -1, const bool emphasis = false, const uint8_t page = 0) {
    if (count >= cap) return;
    Row& r = rows[count++];
    r.kind = kind;
    r.ref = static_cast<int16_t>(ref);
    r.emphasis = emphasis;
    r.page = page;
  }
};

}  // namespace

int capacity(const Catalog& catalog) { return catalog.topicCount() + catalog.categoryCount() + 8; }

int build(const Source& src, Row* rows, const int cap) {
  Builder b{rows, cap};
  const Catalog& c = *src.catalog;
  if (src.home) {
    // EMERGENCY (the pack's first category) first, then the tools, then the other categories.
    if (c.categoryCount() > 0) b.add(RowKind::Category, 0, true);
    b.add(RowKind::Quick);
    b.add(RowKind::Search);
    b.add(RowKind::Marks);
    b.add(RowKind::Recent);
    for (int i = 1; i < c.categoryCount(); i++) b.add(RowKind::Category, i);
    b.add(RowKind::About);
    return b.count;
  }
  switch (src.list) {
    case ListKind::Category: {
      const int cat = c.findCategory(src.category);
      if (cat < 0) break;
      const Category& k = c.category(cat);
      for (int i = 0; i < k.count; i++) b.add(RowKind::Topic, k.first + i);
      break;
    }
    case ListKind::Quick:
      for (int i = 0; i < c.topicCount(); i++) {
        if (c.topic(i).quick()) b.add(RowKind::Topic, i);
      }
      break;
    case ListKind::Search:
      b.add(RowKind::NewSearch);
      for (int i = 0; i < src.hitCount; i++) {
        if (c.validTopic(src.hits[i].topic)) b.add(RowKind::Topic, src.hits[i].topic);
      }
      break;
    case ListKind::Marks:
      for (int i = 0; src.marks && i < src.marks->count; i++) {
        const int t = c.findTopic(src.marks->items[i].topic);
        if (t >= 0) b.add(RowKind::Topic, t, false, src.marks->items[i].page);
      }
      if (b.count == 0) b.add(RowKind::Note);
      break;
    case ListKind::Recent:
      for (int i = 0; src.recent && i < src.recent->count; i++) {
        const int t = c.findTopic(src.recent->items[i]);
        if (t >= 0) b.add(RowKind::Topic, t);
      }
      if (b.count == 0) b.add(RowKind::Note);
      break;
    case ListKind::None:
      break;
  }
  return b.count;
}

void text(const Source& src, const Row& r, draw::ListRow& out, char* value, const size_t valueCap, char* sub,
          const size_t subCap) {
  const Catalog& c = *src.catalog;
  out = draw::ListRow{};
  out.emphasis = r.emphasis;
  value[0] = '\0';
  sub[0] = '\0';
  switch (r.kind) {
    case RowKind::Category: {
      const Category& k = c.category(r.ref);
      out.title = k.title;
      out.value = topicCount(k.count, value, valueCap);
      out.subtitle = k.blurb;
      return;
    }
    case RowKind::Topic: {
      const Topic& t = c.topic(r.ref);
      out.title = t.title;
      out.subtitle = t.summary;
      if (src.list == ListKind::Marks) {
        std::snprintf(value, valueCap, tr(STR_GD_PAGE_N), r.page + 1);
        out.value = value;
      } else if (src.list == ListKind::Category) {
        out.value = pageCount(t.pages, value, valueCap);
      } else {
        // Lists across categories say where the topic lives.
        out.value = c.category(t.category).title;
      }
      return;
    }
    case RowKind::Quick:
      out.title = tr(STR_GD_QUICK_CARDS);
      std::snprintf(value, valueCap, "%d", c.quickCount());
      out.value = value;
      out.subtitle = tr(STR_GD_QUICK_DESC);
      return;
    case RowKind::Search:
      out.title = tr(STR_GD_SEARCH);
      out.subtitle = tr(STR_GD_SEARCH_DESC);
      return;
    case RowKind::Marks: {
      const int n = src.marks ? src.marks->count : 0;
      // The title is "Bookmarks (n)"; the subtitle the newest bookmark, or how to add one.
      std::snprintf(sub, subCap, tr(STR_GD_BOOKMARKS_N), n);
      out.title = sub;
      const int t = n > 0 ? c.findTopic(src.marks->items[0].topic) : -1;
      out.subtitle = t >= 0 ? c.topic(t).title : tr(STR_GD_MARK_HOW);
      return;
    }
    case RowKind::Recent: {
      out.title = tr(STR_GD_RECENT);
      const int t = src.recent && src.recent->count > 0 ? c.findTopic(src.recent->items[0]) : -1;
      out.subtitle = t >= 0 ? c.topic(t).title : tr(STR_GD_NOTHING_RECENT);
      return;
    }
    case RowKind::About:
      out.title = tr(STR_GD_ABOUT);
      out.subtitle = src.info ? src.info->statusText : "";
      return;
    case RowKind::NewSearch:
      out.title = tr(STR_GD_NEW_SEARCH);
      if (src.hitCount == 0) {
        std::snprintf(sub, subCap, tr(STR_GD_NOTHING_FOUND), src.query);
      } else if (src.hitCount == 1) {
        std::snprintf(sub, subCap, tr(STR_GD_FOUND_ONE), src.query);
      } else {
        std::snprintf(sub, subCap, tr(STR_GD_FOUND), src.hitCount, src.query);
      }
      out.subtitle = sub;
      return;
    case RowKind::Note:
      out.enabled = false;
      if (src.list == ListKind::Marks) {
        out.title = tr(STR_GD_NO_MARKS);
        out.subtitle = tr(STR_GD_MARK_HOW);
      } else {
        out.title = tr(STR_GD_NOTHING_RECENT);
      }
      return;
  }
}

const char* title(const Source& src) {
  if (src.home || !src.catalog) return src.info ? src.info->title : tr(STR_GUIDE);
  switch (src.list) {
    case ListKind::Category: {
      const int cat = src.catalog->findCategory(src.category);
      return cat >= 0 ? src.catalog->category(cat).title : tr(STR_GUIDE);
    }
    case ListKind::Quick:
      return tr(STR_GD_QUICK_CARDS);
    case ListKind::Search:
      return tr(STR_GD_SEARCH);
    case ListKind::Marks:
      return tr(STR_GD_BOOKMARKS);
    case ListKind::Recent:
      return tr(STR_GD_RECENT);
    case ListKind::None:
      break;
  }
  return tr(STR_GUIDE);
}

int homeRowOf(const Catalog& c, const ListKind list, const char* category) {
  // Home rows: [first category] Quick Search Bookmarks Recent [categories 1..] About.
  const int first = c.categoryCount() > 0 ? 1 : 0;
  switch (list) {
    case ListKind::Category: {
      const int cat = c.findCategory(category);
      return cat <= 0 ? 0 : first + 3 + cat;
    }
    case ListKind::Quick:
      return first;
    case ListKind::Search:
      return first + 1;
    case ListKind::Marks:
      return first + 2;
    case ListKind::Recent:
      return first + 3;
    case ListKind::None:
      break;
  }
  return 0;
}

const char* kindName(const RowKind kind) {
  static constexpr const char* NAMES[] = {"category", "topic", "quick",     "search", "marks",
                                          "recent",   "about", "newsearch", "note"};
  return NAMES[static_cast<int>(kind)];
}

}  // namespace gd::lists
