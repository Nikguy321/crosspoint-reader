#include "CwIpuz.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CwNumbering.h"
#include "CwText.h"

namespace cw {

namespace {

class JsonHeap final : public ArduinoJson::Allocator {
 public:
  explicit JsonHeap(const cw::Allocator* a) : a(a) {}
  void* allocate(const size_t size) override { return a ? a->allocate(size) : std::malloc(size); }
  void deallocate(void* ptr) override {
    if (a) {
      a->deallocate(ptr);
    } else {
      std::free(ptr);
    }
  }
  void* reallocate(void* ptr, const size_t newSize) override {
    return a ? a->reallocate(ptr, newSize) : std::realloc(ptr, newSize);
  }

 private:
  const cw::Allocator* a;
};

// The file as JSON: a UTF-8 byte-order mark and the JSONP wrapper some tools write ("ipuz({...})")
// are dropped; ArduinoJson stops after the root object, so the closing ")" is never read. Bytes
// looked at and not dropped are replayed.
class JsonInput final : public ByteReader {
 public:
  explicit JsonInput(ByteReader& in) : in(in) { skipPrefix(); }
  int read() override { return pos < held ? buf[pos++] : in.read(); }
  size_t readBytes(char* out, const size_t n) override {
    size_t k = 0;
    while (k < n && pos < held) out[k++] = static_cast<char>(buf[pos++]);
    return k < n ? k + in.readBytes(out + k, n - k) : k;
  }

 private:
  static bool space(const int b) { return b == ' ' || b == '\t' || b == '\r' || b == '\n'; }
  static bool identStart(const int b) {
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || b == '_' || b == '$';
  }
  static bool identChar(const int b) { return identStart(b) || (b >= '0' && b <= '9') || b == '.'; }
  // The next byte, kept for replay; -1 at the end (or once the look-ahead is full).
  int take() {
    if (held >= sizeof(buf)) return -1;
    const int b = in.read();
    if (b >= 0) buf[held++] = static_cast<uint8_t>(b);
    return b;
  }
  void skipPrefix() {
    int b = take();
    if (b == 0xEF) {
      if (take() != 0xBB || take() != 0xBF) return;
      held = 0;
      b = take();
    }
    while (space(b)) b = take();
    if (!identStart(b)) return;
    do {
      b = take();
    } while (identChar(b));
    while (space(b)) b = take();
    if (b == '(') held = 0;  // "ipuz(" and what came before it
  }

  ByteReader& in;
  uint8_t buf[48];
  size_t held = 0;
  size_t pos = 0;
};

bool startsWith(const char* s, const char* prefix) { return s && std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

// A positive decimal number in a string ("12"), or 0.
int numberString(const char* s) {
  if (!s || !*s) return 0;
  int v = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9' || v > 9999) return 0;
    v = v * 10 + (*s - '0');
  }
  return v;
}

// A clue number: an int or a numeric string; 0 when neither.
int clueNumber(const JsonVariantConst v) {
  if (v.is<int>()) return v.as<int>() > 0 ? v.as<int>() : 0;
  if (v.is<const char*>()) return numberString(v.as<const char*>());
  return 0;
}

// ASCII case-insensitive compare of n bytes against a lower-case word.
bool sameLetters(const char* s, const char* lower, const size_t n) {
  for (size_t i = 0; i < n; i++) {
    const char c = (s[i] >= 'A' && s[i] <= 'Z') ? static_cast<char>(s[i] - 'A' + 'a') : s[i];
    if (c != lower[i]) return false;
  }
  return true;
}

// "Across", "Across:Theme" -> ACROSS; "Down..." -> DOWN; else -1.
int clueDirection(const char* key) {
  if (!key) return -1;
  const char* colon = std::strchr(key, ':');
  const size_t n = colon ? static_cast<size_t>(colon - key) : std::strlen(key);
  if (n == 6 && sameLetters(key, "across", 6)) return ACROSS;
  if (n == 4 && sameLetters(key, "down", 4)) return DOWN;
  return -1;
}

void cleanField(const JsonVariantConst v, char* out, const size_t cap) {
  if (!v.is<const char*>()) return;
  const char* s = v.as<const char*>();
  cleanText(s, std::strlen(s), out, cap, TEXT_HTML);
}

// What a puzzle cell says: block, white (number 0 = none), or damaged.
enum class CellKind : uint8_t { Block, White, Bad };

struct CellRules {
  const char* block = "#";
  const char* emptyStr = "0";
  int emptyInt = 0;
  bool emptyIsInt = true;
};

CellKind readCellValue(const JsonVariantConst v, const CellRules& rules, int& number) {
  number = 0;
  if (v.isNull()) return CellKind::Block;
  if (v.is<int>()) {
    const int n = v.as<int>();
    if (rules.emptyIsInt && n == rules.emptyInt) return CellKind::White;
    number = n > 0 ? n : 0;
    return CellKind::White;
  }
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    if (std::strcmp(s, rules.block) == 0) return CellKind::Block;
    if (!rules.emptyIsInt && std::strcmp(s, rules.emptyStr) == 0) return CellKind::White;
    number = numberString(s);  // a label that is not a number is still a white square
    return CellKind::White;
  }
  return CellKind::Bad;
}

// The letter a solution square holds: 'A'..'Z', BLOCK, 0 = none given, '?' = not one letter.
// A puzzle square: a plain value, or an object whose "cell" holds it (no "cell" = white).
CellKind readPuzzleCell(const JsonVariantConst v, const CellRules& rules, int& number) {
  if (v.is<JsonObjectConst>()) {
    const JsonVariantConst inner = v["cell"];
    if (inner.isUnbound()) {
      number = 0;
      return CellKind::White;
    }
    return readCellValue(inner, rules, number);
  }
  return readCellValue(v, rules, number);
}

char readSolution(JsonVariantConst v, const CellRules& rules) {
  if (v.is<JsonObjectConst>()) v = v["value"];
  if (v.isNull()) return BLOCK;
  if (v.is<int>()) return 0;  // 0 / a number: no answer here
  if (!v.is<const char*>()) return '?';
  const char* s = v.as<const char*>();
  if (std::strcmp(s, rules.block) == 0) return BLOCK;
  if (s[0] == '\0') return 0;
  if (s[1] != '\0') return '?';
  const char c = s[0];
  if (c >= 'a' && c <= 'z') return static_cast<char>(c - 'a' + 'A');
  if (c >= 'A' && c <= 'Z') return c;
  return '?';
}

}  // namespace

LoadStatus parseIpuz(ByteReader& in, const char* sourceKey, Puzzle& p, const Allocator* alloc) {
  p.reset();
  LoadStatus st;
  const auto fail = [&st, &p](const Error e) {
    p.reset();
    st.error = e;
    return st;
  };
  JsonHeap heap(alloc);
  JsonDocument doc(&heap);
  {
    JsonDocument filter(&heap);
    filter["kind"] = true;
    filter["dimensions"] = true;
    filter["puzzle"] = true;
    filter["solution"] = true;
    filter["clues"] = true;
    filter["title"] = true;
    filter["author"] = true;
    filter["copyright"] = true;
    filter["block"] = true;
    filter["empty"] = true;
    if (filter.overflowed()) return fail(Error::OutOfMemory);
    JsonInput json(in);
    const DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
    if (err == DeserializationError::NoMemory) return fail(Error::OutOfMemory);
    if (err) return fail(Error::Damaged);
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return fail(Error::NotCrossword);

  // kind
  bool crossword = false;
  bool diagramless = false;
  for (const JsonVariantConst k : root["kind"].as<JsonArrayConst>()) {
    const char* s = k.as<const char*>();
    if (startsWith(s, "http://ipuz.org/crossword") || startsWith(s, "https://ipuz.org/crossword")) {
      crossword = true;
      diagramless = diagramless || std::strstr(s, "diagramless") != nullptr;
    }
  }
  if (!crossword) return fail(Error::NotCrossword);
  if (diagramless) return fail(Error::Diagramless);

  // dimensions
  const JsonVariantConst dims = root["dimensions"];
  if (!dims["width"].is<int>() || !dims["height"].is<int>()) return fail(Error::Damaged);
  const int w = dims["width"].as<int>();
  const int h = dims["height"].as<int>();
  st.width = static_cast<uint16_t>(w < 0 ? 0 : (w > 9999 ? 9999 : w));
  st.height = static_cast<uint16_t>(h < 0 ? 0 : (h > 9999 ? 9999 : h));
  if (w > MAX_SIDE || h > MAX_SIDE) return fail(Error::TooBig);
  if (w < MIN_SIDE || h < MIN_SIDE) return fail(Error::TooSmall);
  p.w = static_cast<uint8_t>(w);
  p.h = static_cast<uint8_t>(h);

  CellRules rules;
  if (root["block"].is<const char*>()) rules.block = root["block"].as<const char*>();
  if (root["empty"].is<const char*>()) {
    rules.emptyStr = root["empty"].as<const char*>();
    rules.emptyIsInt = false;
  } else if (root["empty"].is<int>()) {
    rules.emptyInt = root["empty"].as<int>();
  }

  // puzzle: blocks, circles, bars (its numbers are checked after numbering)
  const JsonArrayConst grid = root["puzzle"].as<JsonArrayConst>();
  if (grid.isNull() || static_cast<int>(grid.size()) != h) return fail(Error::Damaged);
  bool anyNumber = false;
  bool allBlocks = true;  // every puzzle square null or a block: a diagramless grid
  for (int r = 0; r < h; r++) {
    const JsonArrayConst row = grid[r].as<JsonArrayConst>();
    if (row.isNull() || static_cast<int>(row.size()) != w) return fail(Error::Damaged);
    for (int c = 0; c < w; c++) {
      const int i = p.index(r, c);
      const JsonVariantConst style = row[c]["style"];
      if (style.is<JsonObjectConst>()) {
        if (!style["barred"].isUnbound()) return fail(Error::Barred);
        const char* shape = style["shapebg"].as<const char*>();
        if (shape && std::strcmp(shape, "circle") == 0) p.setCircled(i);
      }
      int n = 0;
      const CellKind kind = readPuzzleCell(row[c], rules, n);
      if (kind == CellKind::Bad) return fail(Error::Damaged);
      p.solution[i] = kind == CellKind::Block ? BLOCK : 'A';
      allBlocks = allBlocks && kind == CellKind::Block;
      anyNumber = anyNumber || n > 0;
    }
  }

  // solution
  const JsonArrayConst sol = root["solution"].as<JsonArrayConst>();
  if (sol.isNull()) return fail(Error::NoSolution);
  if (static_cast<int>(sol.size()) != h) return fail(Error::Damaged);
  for (int r = 0; r < h; r++) {
    const JsonArrayConst row = sol[r].as<JsonArrayConst>();
    if (row.isNull() || static_cast<int>(row.size()) != w) return fail(Error::Damaged);
    for (int c = 0; c < w; c++) {
      const int i = p.index(r, c);
      const char letter = readSolution(row[c], rules);
      if (p.solution[i] == BLOCK) {
        if (letter != BLOCK && letter != 0) return fail(allBlocks ? Error::Diagramless : Error::Damaged);
        continue;
      }
      if (letter == '?') return fail(Error::Rebus);
      if (letter == 0 || letter == BLOCK) return fail(Error::NoSolution);
      p.solution[i] = letter;
    }
  }

  const Error numbered = numberGrid(p);
  if (numbered != Error::None) return fail(numbered);
  if (anyNumber) {
    for (int r = 0; r < h; r++) {
      const JsonArrayConst row = grid[r].as<JsonArrayConst>();
      for (int c = 0; c < w; c++) {
        int n = 0;
        readPuzzleCell(row[c], rules, n);
        if (n != p.number[p.index(r, c)]) return fail(Error::BadNumbering);
      }
    }
  }

  // clues
  bool seen[MAX_ENTRIES] = {};
  for (const JsonPairConst list : root["clues"].as<JsonObjectConst>()) {
    const int dir = clueDirection(list.key().c_str());
    if (dir < 0) continue;
    int ordinal = 0;
    for (const JsonVariantConst item : list.value().as<JsonArrayConst>()) {
      int number = 0;
      const char* text = nullptr;
      if (item.is<const char*>()) {
        // A bare string: the next entry in this direction.
        const int base = dir == ACROSS ? 0 : p.acrossCount;
        const int count = dir == ACROSS ? p.acrossCount : p.entryCount - p.acrossCount;
        if (ordinal >= count) return fail(Error::ClueMismatch);
        number = p.entries[base + ordinal].number;
        text = item.as<const char*>();
      } else if (item.is<JsonArrayConst>()) {
        number = clueNumber(item[0]);
        text = item[1].as<const char*>();
      } else if (item.is<JsonObjectConst>()) {
        number = clueNumber(item["number"]);
        text = item["clue"].as<const char*>();
      }
      ordinal++;
      if (number <= 0 || !text) return fail(Error::Damaged);
      const int e = findEntry(p, static_cast<uint8_t>(dir), number);
      if (e < 0 || seen[e]) return fail(Error::ClueMismatch);
      seen[e] = true;
      bool cut = false;
      const Error set = setClue(p, e, text, std::strlen(text), TEXT_HTML, &cut);
      if (set != Error::None) return fail(set);
      if (cut) st.truncatedClues++;
    }
  }
  for (int e = 0; e < p.entryCount; e++) {
    if (!seen[e]) return fail(Error::ClueMismatch);
  }

  cleanField(root["title"], p.title, sizeof(p.title));
  cleanField(root["author"], p.author, sizeof(p.author));
  cleanField(root["copyright"], p.copyright, sizeof(p.copyright));
  std::snprintf(p.sourceKey, sizeof(p.sourceKey), "%s", sourceKey ? sourceKey : "");
  return st;
}

}  // namespace cw
