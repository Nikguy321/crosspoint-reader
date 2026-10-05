#include "GdPack.h"

#include <cstdio>
#include <cstring>

#include "GdText.h"

namespace gd {

namespace {

void setField(PackInfo& info, const std::string_view key, const std::string_view value, bool& known) {
  known = true;
  if (key == "id") {
    copyCut(value, info.id, sizeof(info.id));
  } else if (key == "version") {
    copyCut(value, info.version, sizeof(info.version));
  } else if (key == "title") {
    copyCut(value, info.title, sizeof(info.title));
  } else if (key == "short") {
    copyCut(value, info.shortTitle, sizeof(info.shortTitle));
  } else if (key == "status") {
    copyCut(value, info.status, sizeof(info.status));
  } else if (key == "status_text") {
    copyCut(value, info.statusText, sizeof(info.statusText));
  } else if (key == "note") {
    copyCut(value, info.note, sizeof(info.note));
  } else if (key == "ref_note") {
    copyCut(value, info.refNote, sizeof(info.refNote));
  } else if (key == "license") {
    copyCut(value, info.license, sizeof(info.license));
  } else {
    known = false;
  }
}

bool validCatTitle(const std::string_view s) { return !s.empty() && s.size() <= MAX_TITLE; }

}  // namespace

bool validId(const std::string_view id, const size_t maxLen) {
  if (id.empty() || id.size() > maxLen) return false;
  for (const char c : id) {
    if (!isAlnumLower(c) && c != '-') return false;
  }
  return true;
}

bool validTopicFile(const std::string_view file) {
  if (file.size() < 6 || file.substr(0, 2) != "t/" || file.substr(file.size() - 3) != ".gp") return false;
  return validId(file.substr(2, file.size() - 5), MAX_ID);
}

PackError parsePackInfo(const char* text, const size_t len, PackInfo& out) {
  out = PackInfo{};
  if (!text || len == 0) return PackError::Damaged;
  if (len > MAX_PACKTXT_BYTES) return PackError::Damaged;
  bool haveFormat = false, haveMinApp = false, badNumber = false, badLine = false;
  uint32_t format = 0, minApp = 0;
  bool haveId = false, haveVersion = false, haveTitle = false;
  size_t i = 0;
  while (i < len) {
    size_t j = i;
    while (j < len && text[j] != '\n') j++;
    std::string_view line(text + i, j - i);
    i = j + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() || line.front() == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos || eq == 0 || line.find('\0') != std::string_view::npos) {
      badLine = true;  // decided after the loop: a newer format wins over damage
      continue;
    }
    const std::string_view key = line.substr(0, eq);
    const std::string_view value = line.substr(eq + 1);
    if (key == "format") {
      haveFormat = parseUint(value, 1000000, format);
      if (!haveFormat) badNumber = true;
    } else if (key == "min_app") {
      haveMinApp = parseUint(value, 1000000, minApp);
      if (!haveMinApp) badNumber = true;
    } else {
      bool known = false;
      setField(out, key, value, known);
      if (key == "id") haveId = true;
      if (key == "version") haveVersion = !value.empty();
      if (key == "title") haveTitle = !value.empty();
    }
  }
  // A newer pack first: its other keys may mean something this firmware does not know.
  if (haveFormat && format > static_cast<uint32_t>(FORMAT)) return PackError::NeedsNewerFirmware;
  if (haveMinApp && minApp > static_cast<uint32_t>(APP_VERSION)) return PackError::NeedsNewerFirmware;
  if (!haveFormat || badNumber || badLine || format < 1) return PackError::Damaged;
  if (!haveId || !validId(out.id, MAX_ID) || !haveVersion || !haveTitle) return PackError::Damaged;
  out.format = static_cast<int>(format);
  out.minApp = haveMinApp ? static_cast<int>(minApp) : 1;
  if (!out.shortTitle[0]) copyCut(out.title, out.shortTitle, sizeof(out.shortTitle));
  return PackError::None;
}

void Catalog::clear() {
  catCount = 0;
  topCount = 0;
}

PackError Catalog::parse(char* categoriesText, const size_t categoriesLen, char* topicsText, const size_t topicsLen) {
  clear();
  if (!categoriesText || !topicsText || categoriesLen > MAX_CATEGORIES_BYTES || topicsLen > MAX_TOPICS_BYTES) {
    return PackError::Damaged;
  }

  // ---- categories.tsv
  LineSplitter cl(categoriesText, categoriesLen);
  char* line;
  size_t lineLen;
  while (cl.next(line, lineLen)) {
    if (lineLen == 0 || line[0] == '#') continue;
    char* f[4];
    if (splitTabs(line, f, 4) < 4) return fail();
    uint32_t order = 0;
    if (!validId(f[0], MAX_CAT_ID) || !validCatTitle(f[1]) || std::strlen(f[2]) > MAX_SUMMARY ||
        !parseUint(f[3], 0xFFFF, order)) {
      return fail();
    }
    if (catCount >= MAX_CATEGORIES || order != static_cast<uint32_t>(catCount + 1)) return fail();
    for (int c = 0; c < catCount; c++) {
      if (std::strcmp(cats[c].id, f[0]) == 0) return fail();
    }
    Category& cat = cats[catCount++];
    cat.id = f[0];
    cat.title = f[1];
    cat.blurb = f[2];
    cat.first = 0;
    cat.count = 0;
  }
  if (catCount == 0) return fail();

  // ---- topics.tsv
  LineSplitter tl(topicsText, topicsLen);
  int currentCat = 0;
  while (tl.next(line, lineLen)) {
    if (lineLen == 0 || line[0] == '#') continue;
    char* f[7];
    if (splitTabs(line, f, 7) < 7) return fail();
    uint32_t pages = 0;
    if (!validId(f[0], MAX_ID) || f[2][0] == '\0' || std::strlen(f[2]) > MAX_TITLE || !validTopicFile(f[4]) ||
        !parseUint(f[5], MAX_PAGES, pages) || pages < 1 || std::strlen(f[6]) > MAX_SUMMARY) {
      return fail();
    }
    if (topCount >= MAX_TOPICS) return fail();
    // Topics come category by category, in the categories' order.
    int cat = -1;
    for (int c = currentCat; c < catCount; c++) {
      if (std::strcmp(cats[c].id, f[1]) == 0) {
        cat = c;
        break;
      }
    }
    if (cat < 0) return fail();
    if (cat != currentCat) {
      currentCat = cat;
    }
    uint8_t flags = 0;
    if (std::strcmp(f[3], "-") != 0) {
      if (f[3][0] == '\0') return fail();
      for (const char* p = f[3]; *p; p++) {
        if (*p == 'Q') {
          flags |= FLAG_QUICK;
        } else if (*p == 'M') {
          flags |= FLAG_MEDICAL;
        } else if (*p < 'A' || *p > 'Z') {
          return fail();
        }  // another capital letter: a later format's flag, ignored
      }
    }
    Topic& t = topics[topCount];
    t.id = f[0];
    t.category = static_cast<uint16_t>(cat);
    t.title = f[2];
    t.flags = flags;
    t.file = f[4];
    t.pages = static_cast<uint8_t>(pages);
    t.summary = f[6];
    Category& c = cats[cat];
    if (c.count == 0) c.first = static_cast<uint16_t>(topCount);
    c.count++;
    topCount++;
  }
  if (topCount == 0) return fail();

  // Ids sorted for findTopic (insertion sort: at most 1024, done once), and unique.
  for (int i = 0; i < topCount; i++) {
    const uint16_t v = static_cast<uint16_t>(i);
    int j = i;
    while (j > 0 && std::strcmp(topics[byId[j - 1]].id, topics[v].id) > 0) {
      byId[j] = byId[j - 1];
      j--;
    }
    byId[j] = v;
  }
  for (int i = 1; i < topCount; i++) {
    if (std::strcmp(topics[byId[i - 1]].id, topics[byId[i]].id) == 0) return fail();
  }
  return PackError::None;
}

PackError Catalog::fail() {
  clear();
  return PackError::Damaged;
}

int Catalog::findCategory(const std::string_view id) const {
  for (int c = 0; c < catCount; c++) {
    if (id == cats[c].id) return c;
  }
  return -1;
}

int Catalog::findTopic(const std::string_view id) const {
  int lo = 0, hi = topCount;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    const int cmp = std::string_view(topics[byId[mid]].id).compare(id);
    if (cmp == 0) return byId[mid];
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return -1;
}

int Catalog::quickTopics(uint16_t* out, const int cap) const {
  int n = 0;
  for (int i = 0; i < topCount && n < cap; i++) {
    if (topics[i].quick()) out[n++] = static_cast<uint16_t>(i);
  }
  return n;
}

int Catalog::quickCount() const {
  int n = 0;
  for (int i = 0; i < topCount; i++) n += topics[i].quick() ? 1 : 0;
  return n;
}

int Catalog::titleSearch(const std::string_view query, uint16_t* out, const int cap) const {
  const std::string_view q = trim(query);
  if (q.empty()) return 0;
  int n = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < topCount && n < cap; i++) {
      const bool inTitle = containsNoCase(topics[i].title, q);
      const bool hit = pass == 0 ? inTitle : (!inTitle && containsNoCase(topics[i].summary, q));
      if (hit) out[n++] = static_cast<uint16_t>(i);
    }
  }
  return n;
}

size_t packPath(const char* packId, const char* file, char* out, const size_t cap) {
  const int n = std::snprintf(out, cap, "%s/%s/%s", GUIDES_DIR, packId, file);
  return (n < 0 || static_cast<size_t>(n) >= cap) ? 0 : static_cast<size_t>(n);
}

size_t figurePath(const char* packId, const char* name, char* out, const size_t cap, const char* dir) {
  const int n = std::snprintf(out, cap, "%s/%s/%s/%s.png", GUIDES_DIR, packId, dir ? dir : FIGURE_DIR, name);
  return (n < 0 || static_cast<size_t>(n) >= cap) ? 0 : static_cast<size_t>(n);
}

}  // namespace gd
