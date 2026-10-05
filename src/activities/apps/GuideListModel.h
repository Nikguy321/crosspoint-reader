#pragma once

// Survival guide: what the list screens list, and the text of each row. Pure over lib/Guide and the
// i18n tables (no card, no screen), so the host preview test (test/guide_preview) builds the rows
// the device builds. GuideListActivity holds the rows and draws them with GuideDraw.
//
//   home      the pack's first category (EMERGENCY, emphasised), Quick cards (n), Search,
//             Bookmarks (n), Recent, the other categories ("7 topics", their blurb), About & sources
//   category  its topics ("2 pages", the summary)
//   quick     the quick cards, in pack order (their category at the right)
//   search    New search ("3 found for ..."), then the hits as ranked
//   marks     the bookmarks, newest first ("page 2"); a note when there are none
//   recent    the topics read, newest first; a note when there are none

#include <Guide.h>

#include <cstddef>
#include <cstdint>

#include "GuideDraw.h"

namespace gd::lists {

enum class RowKind : uint8_t { Category, Topic, Quick, Search, Marks, Recent, About, NewSearch, Note };

struct Row {
  RowKind kind = RowKind::Note;
  bool emphasis = false;
  uint8_t page = 0;  // a Topic from a bookmark: its page
  int16_t ref = -1;  // Category / Topic: the catalog index
};

struct Source {
  const Catalog* catalog = nullptr;
  const PackInfo* info = nullptr;
  const Marks* marks = nullptr;
  const Recent* recent = nullptr;
  bool home = false;
  ListKind list = ListKind::None;
  const char* category = "";  // ListKind::Category's id
  const char* query = "";     // ListKind::Search's words
  const Hit* hits = nullptr;  // ListKind::Search's results
  int hitCount = 0;
};

// The rows a screen lists (at most cap). Returns how many.
int build(const Source& src, Row* rows, int cap);
// Rows the screen can hold at most (a buffer for build()).
int capacity(const Catalog& catalog);

// A row's text: out's strings point at the catalog, the i18n tables or the two scratch buffers.
void text(const Source& src, const Row& row, draw::ListRow& out, char* value, size_t valueCap, char* sub,
          size_t subCap);

// The header's title.
const char* title(const Source& src);

// The home row a list is reached from (Back selects it there).
int homeRowOf(const Catalog& catalog, ListKind list, const char* category);

// "category", "topic", ... (the bench's row dump).
const char* kindName(RowKind kind);

}  // namespace gd::lists
