#include "GdState.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "GdText.h"

namespace gd {

namespace {

struct Writer {
  char* out;
  size_t cap;
  size_t len = 0;
  bool ok = true;

  __attribute__((format(printf, 2, 3))) void add(const char* fmt, ...) {
    if (!ok) return;
    if (len >= cap) {
      ok = false;
      return;
    }
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(out + len, cap - len, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= cap - len) {
      ok = false;
      return;
    }
    len += static_cast<size_t>(n);
  }
  size_t finish() const { return ok ? len : 0; }
};

struct Reader {
  const char* p;
  const char* end;

  bool line(std::string_view& out) {
    if (p >= end) return false;
    const char* start = p;
    while (p < end && *p != '\n') p++;
    const char* stop = p;
    if (p < end) p++;
    if (stop > start && stop[-1] == '\r') stop--;
    out = std::string_view(start, static_cast<size_t>(stop - start));
    return true;
  }

  // "<keyword> <rest>" (or "<keyword>" alone when emptyOk; rest is then "").
  bool field(const char* keyword, std::string_view& rest, const bool emptyOk = false) {
    std::string_view l;
    if (!line(l)) return false;
    const size_t n = std::strlen(keyword);
    if (l.size() == n && emptyOk && l == keyword) {
      rest = std::string_view();
      return true;
    }
    if (l.size() <= n || l.compare(0, n, keyword) != 0 || l[n] != ' ') return false;
    rest = l.substr(n + 1);
    return true;
  }

  bool atEnd() {
    std::string_view l;
    return !line(l) || (l.empty() && p >= end);
  }
};

constexpr const char* SCREEN_KEYS[] = {"home", "list", "page", "about"};
constexpr const char* LIST_KEYS[] = {"none", "category", "quick", "search", "marks", "recent"};

template <size_t N>
int keyIndex(const char* const (&keys)[N], const std::string_view s) {
  for (size_t i = 0; i < N; i++) {
    if (s == keys[i]) return static_cast<int>(i);
  }
  return -1;
}

// "-" means none.
bool parseIdField(const std::string_view s, const size_t maxLen, char* out, const size_t cap) {
  if (s == "-") {
    out[0] = '\0';
    return true;
  }
  if (!validId(s, maxLen)) return false;
  copyCut(s, out, cap);
  return true;
}

// The query as one safe line: control characters become spaces, then trimmed.
void cleanQuery(const std::string_view in, char* out, const size_t cap) {
  char tmp[MAX_QUERY + 1];
  size_t n = 0;
  for (const char c : in) {
    if (n >= MAX_QUERY) break;
    tmp[n++] = (static_cast<uint8_t>(c) < 0x20 || c == 0x7F) ? ' ' : c;
  }
  copyCut(trim(std::string_view(tmp, n)), out, cap);
}

// "<id> <page>" / "<id>".
bool parseMarkLine(const std::string_view l, Mark& m) {
  const size_t sp = l.find(' ');
  if (sp == std::string_view::npos) return false;
  uint32_t page = 0;
  if (!validId(l.substr(0, sp), MAX_ID) || !parseUint(l.substr(sp + 1), MAX_PAGES - 1, page)) return false;
  copyCut(l.substr(0, sp), m.topic, sizeof(m.topic));
  m.page = static_cast<uint8_t>(page);
  return true;
}

}  // namespace

size_t stateDir(const char* packId, char* out, const size_t cap) {
  const int n = std::snprintf(out, cap, "%s/%s", STATE_ROOT, packId);
  return (n < 0 || static_cast<size_t>(n) >= cap) ? 0 : static_cast<size_t>(n);
}

size_t statePath(const char* packId, const char* file, char* out, const size_t cap) {
  const int n = std::snprintf(out, cap, "%s/%s/%s", STATE_ROOT, packId, file);
  return (n < 0 || static_cast<size_t>(n) >= cap) ? 0 : static_cast<size_t>(n);
}

// ---- state.txt

size_t formatState(const State& s, char* out, const size_t cap) {
  Writer w{out, cap};
  char query[MAX_QUERY + 1];
  cleanQuery(s.query, query, sizeof(query));
  const int screen = static_cast<int>(s.screen) <= 3 ? static_cast<int>(s.screen) : 0;
  const int list = static_cast<int>(s.list) <= 5 ? static_cast<int>(s.list) : 0;
  w.add("GS1\n");
  w.add("screen %s\n", SCREEN_KEYS[screen]);
  w.add("list %s\n", LIST_KEYS[list]);
  w.add("cat %s\n", validId(s.category, MAX_CAT_ID) ? s.category : "-");
  if (query[0]) {
    w.add("query %s\n", query);
  } else {
    w.add("query\n");
  }
  w.add("topic %s\n", validId(s.topic, MAX_ID) ? s.topic : "-");
  w.add("page %u\n", static_cast<unsigned>(s.page < MAX_PAGES ? s.page : MAX_PAGES - 1));
  w.add("sub %u\n", static_cast<unsigned>(s.sub));
  w.add("sel %u\n", static_cast<unsigned>(s.sel < MAX_TOPICS ? s.sel : MAX_TOPICS - 1));
  w.add("end\n");
  return w.finish();
}

bool parseState(const char* text, const size_t len, State& out) {
  out = State{};
  if (!text) return false;
  Reader r{text, text + len};
  std::string_view l, v;
  State s;
  if (!r.line(l) || l != "GS1") return false;
  int k;
  if (!r.field("screen", v) || (k = keyIndex(SCREEN_KEYS, v)) < 0) return false;
  s.screen = static_cast<Screen>(k);
  if (!r.field("list", v) || (k = keyIndex(LIST_KEYS, v)) < 0) return false;
  s.list = static_cast<ListKind>(k);
  if (!r.field("cat", v) || !parseIdField(v, MAX_CAT_ID, s.category, sizeof(s.category))) return false;
  if (!r.field("query", v, true) || v.size() > MAX_QUERY) return false;
  cleanQuery(v, s.query, sizeof(s.query));
  if (!r.field("topic", v) || !parseIdField(v, MAX_ID, s.topic, sizeof(s.topic))) return false;
  uint32_t n = 0;
  if (!r.field("page", v) || !parseUint(v, MAX_PAGES - 1, n)) return false;
  s.page = static_cast<uint8_t>(n);
  if (!r.field("sub", v) || !parseUint(v, 255, n)) return false;
  s.sub = static_cast<uint8_t>(n);
  if (!r.field("sel", v) || !parseUint(v, MAX_TOPICS - 1, n)) return false;
  s.sel = static_cast<uint16_t>(n);
  if (!r.line(l) || l != "end" || !r.atEnd()) return false;
  out = s;
  return true;
}

bool sanitizeState(State& s, const Catalog& catalog) {
  const State before = s;
  if (s.category[0] && catalog.findCategory(s.category) < 0) s.category[0] = '\0';
  const int topic = s.topic[0] ? catalog.findTopic(s.topic) : -1;
  if (s.topic[0] && topic < 0) s.topic[0] = '\0';
  if (topic >= 0 && s.page >= catalog.topic(topic).pages) {
    s.page = static_cast<uint8_t>(catalog.topic(topic).pages - 1);
    s.sub = 0;
  }
  if (topic < 0) {
    s.page = 0;
    s.sub = 0;
  }
  if (s.list == ListKind::Category && !s.category[0]) s.list = ListKind::None;
  if (s.list == ListKind::Search && !s.query[0]) s.list = ListKind::None;
  if (s.screen == Screen::Page && !s.topic[0]) s.screen = s.list == ListKind::None ? Screen::Home : Screen::List;
  if (s.screen == Screen::List && s.list == ListKind::None) s.screen = Screen::Home;
  return before.screen != s.screen || before.list != s.list || std::strcmp(before.category, s.category) != 0 ||
         std::strcmp(before.topic, s.topic) != 0 || before.page != s.page || before.sub != s.sub;
}

// ---- marks.txt

int Marks::find(const std::string_view topic, const int page) const {
  for (int i = 0; i < count; i++) {
    if (topic == items[i].topic && items[i].page == page) return i;
  }
  return -1;
}

void Marks::remove(const int index) {
  if (index < 0 || index >= count) return;
  for (int i = index; i + 1 < count; i++) items[i] = items[i + 1];
  count--;
}

bool Marks::toggle(const std::string_view topic, const int page) {
  const int at = find(topic, page);
  if (at >= 0) {
    remove(at);
    return false;
  }
  if (!validId(topic, MAX_ID) || page < 0 || page >= MAX_PAGES) return false;
  const int keep = count < MAX_MARKS ? count : MAX_MARKS - 1;
  for (int i = keep; i > 0; i--) items[i] = items[i - 1];
  copyCut(topic, items[0].topic, sizeof(items[0].topic));
  items[0].page = static_cast<uint8_t>(page);
  count = keep + 1;
  return true;
}

bool Marks::prune(const Catalog& catalog) {
  bool changed = false;
  for (int i = 0; i < count;) {
    const int t = catalog.findTopic(items[i].topic);
    if (t < 0) {
      remove(i);
      changed = true;
      continue;
    }
    if (items[i].page >= catalog.topic(t).pages) {
      items[i].page = static_cast<uint8_t>(catalog.topic(t).pages - 1);
      changed = true;
    }
    i++;
  }
  // Clamping can make two marks the same: keep the newer.
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count;) {
      if (std::strcmp(items[i].topic, items[j].topic) == 0 && items[i].page == items[j].page) {
        remove(j);
        changed = true;
      } else {
        j++;
      }
    }
  }
  return changed;
}

size_t formatMarks(const Marks& marks, char* out, const size_t cap) {
  Writer w{out, cap};
  w.add("GM1\n");
  for (int i = 0; i < marks.count && i < MAX_MARKS; i++) {
    if (validId(marks.items[i].topic, MAX_ID)) w.add("%s %u\n", marks.items[i].topic, marks.items[i].page);
  }
  w.add("end\n");
  return w.finish();
}

bool parseMarks(const char* text, const size_t len, Marks& out) {
  out = Marks{};
  if (!text) return false;
  Reader r{text, text + len};
  std::string_view l;
  if (!r.line(l) || l != "GM1") return false;
  while (r.line(l) && l != "end") {
    Mark m;
    if (!parseMarkLine(l, m) || out.count >= MAX_MARKS || out.has(m.topic, m.page)) continue;
    out.items[out.count++] = m;
  }
  return true;
}

// ---- recent.txt

void Recent::touch(const std::string_view topic) {
  if (!validId(topic, MAX_ID)) return;
  int at = -1;
  for (int i = 0; i < count; i++) {
    if (topic == items[i]) at = i;
  }
  const int from = at >= 0 ? at : (count < MAX_RECENT ? count : MAX_RECENT - 1);
  for (int i = from; i > 0; i--) std::memcpy(items[i], items[i - 1], sizeof(items[i]));
  copyCut(topic, items[0], sizeof(items[0]));
  if (at < 0 && count < MAX_RECENT) count++;
}

bool Recent::prune(const Catalog& catalog) {
  bool changed = false;
  for (int i = 0; i < count;) {
    if (catalog.findTopic(items[i]) < 0) {
      for (int k = i; k + 1 < count; k++) std::memcpy(items[k], items[k + 1], sizeof(items[k]));
      count--;
      changed = true;
    } else {
      i++;
    }
  }
  return changed;
}

size_t formatRecent(const Recent& recent, char* out, const size_t cap) {
  Writer w{out, cap};
  w.add("GR1\n");
  for (int i = 0; i < recent.count && i < MAX_RECENT; i++) {
    if (validId(recent.items[i], MAX_ID)) w.add("%s\n", recent.items[i]);
  }
  w.add("end\n");
  return w.finish();
}

bool parseRecent(const char* text, const size_t len, Recent& out) {
  out = Recent{};
  if (!text) return false;
  Reader r{text, text + len};
  std::string_view l;
  if (!r.line(l) || l != "GR1") return false;
  while (r.line(l) && l != "end") {
    if (!validId(l, MAX_ID) || out.count >= MAX_RECENT) continue;
    bool dup = false;
    for (int i = 0; i < out.count; i++) dup = dup || l == out.items[i];
    if (dup) continue;
    copyCut(l, out.items[out.count++], sizeof(out.items[0]));
  }
  return true;
}

// ---- atomic writes

LoadFrom loadFrom(const bool mainExists, const bool tmpExists) {
  if (mainExists) return LoadFrom::Main;
  return tmpExists ? LoadFrom::Tmp : LoadFrom::None;
}

size_t tmpPathOf(const char* path, char* out, const size_t cap) {
  const int n = std::snprintf(out, cap, "%s%s", path, TMP_SUFFIX);
  return (n < 0 || static_cast<size_t>(n) >= cap) ? 0 : static_cast<size_t>(n);
}

}  // namespace gd
