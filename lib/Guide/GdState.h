#pragma once

// Survival guide persistence: versioned text codecs (the Sudoku / Word Search pattern). Pure: the
// store reads and writes the files under /.crosspoint/guide/<pack-id>/ (tmp -> remove -> rename,
// loadFrom() picks the .tmp a power cut left) and hands the text here.
//
// state.txt (where to resume)            marks.txt (newest first, cap MAX_MARKS)
//   GS1                                    GM1
//   screen <home|list|page|about>          <topic-id> <page>      one a line
//   list <category|quick|search|marks|recent|none>                 end
//   cat <cat-id|->     (list category)
//   query <text>       (search; may be empty: "query")           recent.txt (newest first, cap MAX_RECENT)
//   topic <topic-id|-> (the open / selected topic)                GR1
//   page <0..63>                                                    <topic-id>             one a line
//   sub <0..255>       (screen within the page; 255 = last)        end
//   sel <0..1023>      (the list's selected row)
//   end
// Everything is keyed by id (never by index), so it survives a pack update: on load, ids the pack
// no longer has are dropped (sanitize* / prune), pages are clamped. state.txt that does not parse
// completely is "no state" (open the guide home). In marks/recent a malformed line is skipped (one
// bad line never costs every bookmark); a missing header means no file.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "GdPack.h"

namespace gd {

inline constexpr char STATE_ROOT[] = "/.crosspoint/guide";
inline constexpr char STATE_FILE[] = "state.txt";
inline constexpr char MARKS_FILE[] = "marks.txt";
inline constexpr char RECENT_FILE[] = "recent.txt";
inline constexpr char TMP_SUFFIX[] = ".tmp";
constexpr int MAX_MARKS = 50;
constexpr int MAX_RECENT = 12;
constexpr size_t MAX_QUERY = 63;
constexpr size_t STATE_TEXT_MAX = 512;
constexpr size_t MARKS_TEXT_MAX = 16 + MAX_MARKS * (MAX_ID + 6);  // ~2 KB
constexpr size_t RECENT_TEXT_MAX = 16 + MAX_RECENT * (MAX_ID + 2);

// "<STATE_ROOT>/<packId>" and "<STATE_ROOT>/<packId>/<file>". Their length, or 0 when cap is too small.
size_t stateDir(const char* packId, char* out, size_t cap);
size_t statePath(const char* packId, const char* file, char* out, size_t cap);

// ---- state.txt -----------------------------------------------------------------------------------

enum class Screen : uint8_t { Home = 0, List, Page, About };
enum class ListKind : uint8_t { None = 0, Category, Quick, Search, Marks, Recent };

struct State {
  Screen screen = Screen::Home;
  ListKind list = ListKind::None;
  char category[MAX_CAT_ID + 1] = {};  // ListKind::Category's id ("" none)
  char query[MAX_QUERY + 1] = {};      // ListKind::Search's query
  char topic[MAX_ID + 1] = {};         // the page's topic, or the list's selected topic ("" none)
  uint8_t page = 0;
  uint8_t sub = 0;
  uint16_t sel = 0;
};

size_t formatState(const State& state, char* out, size_t cap);
// False (and out = a fresh State) unless the text is a complete state file.
bool parseState(const char* text, size_t len, State& out);
// Fits a parsed state to the pack: an unknown category or topic is dropped; a page on an unknown
// topic falls back to its list, a list on an unknown category to home; page clamped to the
// topic's pages. True when anything changed.
bool sanitizeState(State& state, const Catalog& catalog);

// ---- marks.txt -------------------------------------------------------------------------------------

struct Mark {
  char topic[MAX_ID + 1] = {};
  uint8_t page = 0;
};

struct Marks {
  Mark items[MAX_MARKS];
  int count = 0;

  int find(std::string_view topic, int page) const;  // -1 when absent
  bool has(std::string_view topic, int page) const { return find(topic, page) >= 0; }
  // Adds (newest first; the oldest drops past MAX_MARKS) or removes. True when it is now marked.
  bool toggle(std::string_view topic, int page);
  void remove(int index);
  // Drops marks on topics the pack lacks; clamps pages. True when anything changed.
  bool prune(const Catalog& catalog);
};

size_t formatMarks(const Marks& marks, char* out, size_t cap);
bool parseMarks(const char* text, size_t len, Marks& out);

// ---- recent.txt ------------------------------------------------------------------------------------

struct Recent {
  char items[MAX_RECENT][MAX_ID + 1] = {};
  int count = 0;

  // Moves (or adds) the topic to the front; the oldest drops past MAX_RECENT.
  void touch(std::string_view topic);
  bool prune(const Catalog& catalog);
};

size_t formatRecent(const Recent& recent, char* out, size_t cap);
bool parseRecent(const char* text, size_t len, Recent& out);

// ---- atomic writes -----------------------------------------------------------------------------------

// Which file to load: the main file, or, when it is missing, the .tmp a power cut left between
// "remove" and "rename". A main file that is present always wins.
enum class LoadFrom : uint8_t { None = 0, Main, Tmp };
LoadFrom loadFrom(bool mainExists, bool tmpExists);
// "<path>.tmp". Its length, or 0 when cap is too small.
size_t tmpPathOf(const char* path, char* out, size_t cap);

}  // namespace gd
