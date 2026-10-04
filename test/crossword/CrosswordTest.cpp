// Crossword's pure half on the host: numbering, the text cleaner, the built-in format and every
// built-in, the .ipuz and .puz readers over the fixtures (test/crossword/make_fixtures.py), the
// play rules, the keyboard, the layout, the digits and the save codecs.
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Crossword.h"
#include "samples.h"
// The UI fonts' glyph tables, to check uiFontHasGlyph against (generated headers).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "builtinFonts/ubuntu_10_bold.h"
#include "builtinFonts/ubuntu_10_regular.h"
#include "builtinFonts/ubuntu_12_bold.h"
#include "builtinFonts/ubuntu_12_regular.h"
#pragma GCC diagnostic pop

using namespace cw;

namespace {

// The test-only samples (samples.h): the rule tests never depend on the shipped built-ins.
using cw_samples::SHELTER;

std::unique_ptr<Puzzle> newPuzzle() { return std::make_unique<Puzzle>(); }
std::unique_ptr<Progress> newProgress() { return std::make_unique<Progress>(); }

std::unique_ptr<Puzzle> parseText(const char* text) {
  auto p = newPuzzle();
  const TextStatus st = parseTextPuzzle(text, std::strlen(text), *p);
  EXPECT_EQ(st.error, Error::None) << "line " << st.line;
  return p;
}

// A bare grid (rows of A-Z and '#'), numbered.
std::unique_ptr<Puzzle> gridOf(const std::vector<std::string>& rows, Error* error = nullptr) {
  auto p = newPuzzle();
  p->h = static_cast<uint8_t>(rows.size());
  p->w = static_cast<uint8_t>(rows.empty() ? 0 : rows[0].size());
  for (size_t r = 0; r < rows.size(); r++) std::memcpy(p->solution + r * p->w, rows[r].data(), rows[r].size());
  const Error e = numberGrid(*p);
  if (error) *error = e;
  return p;
}

std::string fixture(const char* name) {
  std::ifstream in(std::string(CROSSWORD_FIXTURES) + "/" + name, std::ios::binary);
  EXPECT_TRUE(in.good()) << name;
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

LoadStatus loadIpuz(const std::string& data, Puzzle& p, const Allocator* alloc = nullptr) {
  MemoryReader in(data.data(), data.size());
  return parseIpuz(in, "/Puzzles/Crossword/test.ipuz", p, alloc);
}

LoadStatus loadPuz(const std::string& data, Puzzle& p) {
  MemoryReader in(data.data(), data.size());
  return parsePuz(in, "/Puzzles/Crossword/test.puz", p);
}

std::string clueOf(const Puzzle& p, const uint8_t dir, const int number) {
  const int e = findEntry(p, dir, number);
  return e < 0 ? std::string("<none>") : std::string(p.clue(e));
}

std::string cleaned(const std::string& in, const uint8_t flags = 0, const size_t cap = 512) {
  std::vector<char> out(cap);
  cleanText(in.data(), in.size(), out.data(), cap, flags);
  return std::string(out.data());
}

std::string solutionText(const Puzzle& p) { return std::string(p.solution, p.cells()); }

// Every invariant a loaded puzzle keeps (whatever its source).
void expectConsistent(const Puzzle& p) {
  ASSERT_GE(p.w, MIN_SIDE);
  ASSERT_LE(p.w, MAX_SIDE);
  ASSERT_GE(p.h, MIN_SIDE);
  ASSERT_LE(p.h, MAX_SIDE);
  ASSERT_GT(p.entryCount, 0);
  EXPECT_EQ(p.fnv, puzzleFnv(p.w, p.h, p.solution));
  for (int i = 0; i < p.cells(); i++) {
    const char c = p.solution[i];
    ASSERT_TRUE(c == BLOCK || (c >= 'A' && c <= 'Z'));
    if (c != BLOCK) EXPECT_TRUE(p.entryAt[ACROSS][i] != NO_ENTRY || p.entryAt[DOWN][i] != NO_ENTRY);
  }
  for (int e = 0; e < p.entryCount; e++) {
    const Entry& en = p.entries[e];
    EXPECT_EQ(en.dir, e < p.acrossCount ? ACROSS : DOWN);
    EXPECT_GE(en.len, 2);
    EXPECT_LE(en.clueLen, MAX_CLUE_BYTES);
    EXPECT_EQ(std::strlen(p.clue(e)), en.clueLen);
    for (int i = 0; i < en.len; i++) EXPECT_EQ(p.entryAt[en.dir][p.entryCell(e, i)], e);
  }
}

}  // namespace

// ---- model and numbering ------------------------------------------------------------------------

TEST(CrosswordModel, SizesStayBounded) {
  EXPECT_LT(sizeof(Puzzle), 20u * 1024u);
  EXPECT_LT(sizeof(Progress), 640u);
  EXPECT_LT(sizeof(TextCleaner), 64u);
}

TEST(CrosswordModel, Fnv1aVectors) {
  EXPECT_EQ(fnv1a("", 0), 2166136261u);
  EXPECT_EQ(fnv1a("a", 1), 0xE40C292Cu);
  EXPECT_EQ(fnv1a("foobar", 6), 0xBF9CF968u);
  const char grid[] = "AB#CD";
  EXPECT_EQ(puzzleFnv(5, 1, grid), fnv1a("5x1:AB#CD", 9));
}

TEST(CrosswordModel, FormatElapsedAndLabels) {
  char buf[16];
  ASSERT_GT(formatElapsed(252, buf, sizeof(buf)), 0u);
  EXPECT_STREQ(buf, "4:12");
  ASSERT_GT(formatElapsed(3723, buf, sizeof(buf)), 0u);
  EXPECT_STREQ(buf, "1:02:03");
  EXPECT_EQ(formatElapsed(3723, buf, 4), 0u);
  const auto p = parseText(SHELTER);
  ASSERT_GT(formatClueLabel(*p, findEntry(*p, ACROSS, 4), buf, sizeof(buf)), 0u);
  EXPECT_STREQ(buf, "4A");
  ASSERT_GT(formatClueLabel(*p, findEntry(*p, DOWN, 5), buf, sizeof(buf)), 0u);
  EXPECT_STREQ(buf, "5D");
}

TEST(CrosswordNumbering, StandardMini) {
  const auto p = parseText(SHELTER);
  expectConsistent(*p);
  // ##HUT / #CASE / RAVEN / AGED# / PEN##
  const int expected[25] = {0, 0, 1, 2, 3, 0, 4, 0, 0, 0, 5, 0, 0, 0, 0, 6, 0, 0, 0, 0, 7, 0, 0, 0, 0};
  for (int i = 0; i < 25; i++) EXPECT_EQ(p->number[i], expected[i]) << i;
  ASSERT_EQ(p->entryCount, 10);
  EXPECT_EQ(p->acrossCount, 5);
  const int acrossNumbers[5] = {1, 4, 5, 6, 7};
  const int downNumbers[5] = {1, 2, 3, 4, 5};
  const int downLens[5] = {5, 4, 3, 4, 3};
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(p->entries[i].number, acrossNumbers[i]);
    EXPECT_EQ(p->entries[5 + i].number, downNumbers[i]);
    EXPECT_EQ(p->entries[5 + i].len, downLens[i]);
  }
  EXPECT_EQ(std::string(p->clue(findEntry(*p, DOWN, 1))), "Safe harbor");
  EXPECT_EQ(findEntry(*p, ACROSS, 2), -1);
}

TEST(CrosswordNumbering, TwoLetterRunsAreEntries) {
  Error e = Error::Damaged;
  const auto p = gridOf({"AB#", "CDE", "#FG"}, &e);
  ASSERT_EQ(e, Error::None);
  const int expected[9] = {1, 2, 0, 3, 0, 4, 0, 5, 0};
  for (int i = 0; i < 9; i++) EXPECT_EQ(p->number[i], expected[i]) << i;
  EXPECT_EQ(p->acrossCount, 3);  // AB, CDE, FG
  EXPECT_EQ(p->entryCount, 6);   // + AC, BDF, EG
  EXPECT_EQ(p->entries[0].len, 2);
  EXPECT_EQ(p->entries[3].len, 2);
}

TEST(CrosswordNumbering, UncheckedSquaresAreFine) {
  Error e = Error::Damaged;
  const auto p = gridOf({"ABC", "#D#", "EFG"}, &e);
  ASSERT_EQ(e, Error::None);
  EXPECT_EQ(p->number[0], 1);
  EXPECT_EQ(p->number[1], 2);
  EXPECT_EQ(p->number[6], 3);
  EXPECT_EQ(p->acrossCount, 2);
  EXPECT_EQ(p->entryCount, 3);
  EXPECT_EQ(p->entryAt[DOWN][0], NO_ENTRY);    // A: Across only
  EXPECT_EQ(p->entryAt[ACROSS][4], NO_ENTRY);  // D: Down only
  EXPECT_NE(p->entryAt[DOWN][4], NO_ENTRY);
}

TEST(CrosswordNumbering, Refusals) {
  Error e = Error::None;
  gridOf({"ABC", "###", "#D#"}, &e);
  EXPECT_EQ(e, Error::NotCrossword);  // D is in no entry
  gridOf({"###", "###", "###"}, &e);
  EXPECT_EQ(e, Error::NotCrossword);
  gridOf({"AB", "CD"}, &e);
  EXPECT_EQ(e, Error::TooSmall);
  gridOf(std::vector<std::string>(16, std::string(16, 'A')), &e);
  EXPECT_EQ(e, Error::TooBig);
  // Two-square islands everywhere: exactly 100 entries is the cap.
  std::vector<std::string> rows;
  for (int r = 0; r < 15; r++) {
    std::string row;
    for (int c = 0; c < 15; c++) row += (r % 3 == 2 || c % 3 == 2) ? '#' : 'A';
    rows.push_back(row);
  }
  const auto full = gridOf(rows, &e);
  EXPECT_EQ(e, Error::None);
  EXPECT_EQ(full->entryCount, MAX_ENTRIES);
  rows[14] = "AA#AA#AA#AA#AA#";  // five more Across entries: 105
  gridOf(rows, &e);
  EXPECT_EQ(e, Error::TooManyClues);
}

// ---- text cleaning ------------------------------------------------------------------------------

TEST(CrosswordText, WhitespaceAndControls) {
  EXPECT_EQ(cleaned("  a \t b\n\r c  "), "a b c");
  EXPECT_EQ(cleaned("a\x01"
                    "b\x7f"
                    "c"),
            "abc");
  EXPECT_EQ(cleaned(""), "");
  EXPECT_EQ(cleaned("   "), "");
  EXPECT_EQ(cleaned("a\xc2\xa0\xc2\xa0"
                    "b"),
            "a b");  // no-break spaces
  EXPECT_EQ(cleaned("soft\xc2\xad"
                    "hyphen"),
            "softhyphen");  // U+00AD dropped
  EXPECT_EQ(cleaned("zero\xe2\x80\x8bwidth"), "zerowidth");
}

TEST(CrosswordText, TypographyBecomesAscii) {
  EXPECT_EQ(cleaned("\xe2\x80\x9cHi\xe2\x80\x9d \xe2\x80\x93 it\xe2\x80\x99s\xe2\x80\xa6 ok \xe2\x80\x94 "
                    "\xe2\x80\x98q\xe2\x80\x99"),
            "\"Hi\" - it's... ok - 'q'");
  EXPECT_EQ(cleaned("1\xe2\x88\x92"
                    "2"),
            "1-2");
}

TEST(CrosswordText, Latin1AndUtf8Accents) {
  EXPECT_EQ(cleaned("caf\xe9 na\xefve \xc0 la", TEXT_LATIN1), "caf\xc3\xa9 na\xc3\xafve \xc3\x80 la");
  EXPECT_EQ(cleaned("Se\xc3\xb1or M\xc3\xbcller \xc3\x85ngstr\xc3\xb6m"),
            "Se\xc3\xb1or M\xc3\xbcller \xc3\x85ngstr\xc3\xb6m");
  // Pre-2.0 .puz text is Windows-1252: its quotes, dashes and ellipsis map to ASCII; the bytes
  // it leaves undefined are dropped; Latin-1 punctuation is kept.
  EXPECT_EQ(cleaned("a\x92"
                    "b \xbf"
                    "c?",
                    TEXT_LATIN1),
            "a'b \xc2\xbf"
            "c?");
  EXPECT_EQ(cleaned("Don\x92t \x93go\x94 Pitcher\x97or catcher\x85 x\x96y a\x81"
                    "b",
                    TEXT_LATIN1),
            "Don't \"go\" Pitcher-or catcher... x-y ab");
}

TEST(CrosswordText, MissingGlyphsAndBadBytes) {
  EXPECT_EQ(cleaned("\xe6\x97\xa5\xe6\x9c\xac go"), "?? go");  // CJK
  EXPECT_EQ(cleaned("smile \xf0\x9f\x98\x80"), "smile ?");     // emoji
  EXPECT_EQ(cleaned("a\xff"
                    "b"),
            "a?b");                                                            // not UTF-8
  EXPECT_EQ(cleaned("a\xc3"), "a?");                                           // cut short
  EXPECT_EQ(cleaned("e\xcc\x81t\xc3\xa9"), "et\xc3\xa9");                      // a combining mark dropped
  EXPECT_EQ(cleaned("\xd0\x9f\xd1\x80\xd0\xb8"), "\xd0\x9f\xd1\x80\xd0\xb8");  // Cyrillic is in the fonts
}

TEST(CrosswordText, HtmlTagsAndEntities) {
  EXPECT_EQ(cleaned("<i>Bold</i> &amp; &lt;x&gt; &#39;q&#39; &#x2019;&nbsp;end", TEXT_HTML), "Bold & <x> 'q' ' end");
  EXPECT_EQ(cleaned("a < b and c<5", TEXT_HTML), "a < b and c<5");
  EXPECT_EQ(cleaned("AT&T &unknown; & done", TEXT_HTML), "AT&T &unknown; & done");
  EXPECT_EQ(cleaned("line<br/>break<p>para</p>next", TEXT_HTML), "line break para next");
  EXPECT_EQ(cleaned("<span class=\"x\">in</span>side", TEXT_HTML), "inside");
  EXPECT_EQ(cleaned("caf&eacute; &mdash; &hellip;", TEXT_HTML), "caf\xc3\xa9 - ...");
  EXPECT_EQ(cleaned("trailing &amp", TEXT_HTML), "trailing &amp");
  EXPECT_EQ(cleaned("<i>x</i>", 0), "<i>x</i>");  // without TEXT_HTML, text is text
}

TEST(CrosswordText, CutsOnACodePoint) {
  bool cut = false;
  std::vector<char> out(MAX_CLUE_BYTES + 1);
  const std::string longAscii(600, 'a');
  EXPECT_EQ(cleanText(longAscii.data(), longAscii.size(), out.data(), out.size(), 0, &cut), MAX_CLUE_BYTES);
  EXPECT_TRUE(cut);
  std::string accents = "x";
  for (int i = 0; i < 300; i++) accents += "\xc3\xa9";
  const size_t n = cleanText(accents.data(), accents.size(), out.data(), out.size(), 0, &cut);
  EXPECT_TRUE(cut);
  EXPECT_EQ(n, 399u);  // "x" + 199 two-byte letters; the 200th would pass 400
  EXPECT_EQ(std::strlen(out.data()), n);
  EXPECT_EQ(static_cast<unsigned char>(out[n - 1]), 0xA9);
  cleanText("short", 5, out.data(), out.size(), 0, &cut);
  EXPECT_FALSE(cut);
}

TEST(CrosswordText, GlyphTableMatchesTheUiFonts) {
  struct Font {
    const EpdUnicodeInterval* intervals;
    size_t count;
  };
  const Font fonts[] = {
      {ubuntu_10_regularIntervals, sizeof(ubuntu_10_regularIntervals) / sizeof(ubuntu_10_regularIntervals[0])},
      {ubuntu_10_boldIntervals, sizeof(ubuntu_10_boldIntervals) / sizeof(ubuntu_10_boldIntervals[0])},
      {ubuntu_12_regularIntervals, sizeof(ubuntu_12_regularIntervals) / sizeof(ubuntu_12_regularIntervals[0])},
      {ubuntu_12_boldIntervals, sizeof(ubuntu_12_boldIntervals) / sizeof(ubuntu_12_boldIntervals[0])},
  };
  int kept = 0;
  for (uint32_t cp = 0x20; cp < 0x3000; cp++) {
    if (!uiFontHasGlyph(cp)) continue;
    kept++;
    for (const Font& f : fonts) {
      bool found = false;
      for (size_t i = 0; i < f.count && !found; i++) found = cp >= f.intervals[i].first && cp <= f.intervals[i].last;
      EXPECT_TRUE(found) << std::hex << cp;
    }
  }
  EXPECT_GT(kept, 400);
  for (uint32_t cp : {0xE9u, 0xFCu, 0xC5u, 0xF1u, 0x153u, 0x41Fu}) EXPECT_TRUE(uiFontHasGlyph(cp)) << std::hex << cp;
  for (uint32_t cp : {0x3B1u, 0x5D0u, 0x65E5u, 0x1F600u}) EXPECT_FALSE(uiFontHasGlyph(cp)) << std::hex << cp;
}

// ---- the built-in text format -------------------------------------------------------------------

TEST(CrosswordBuiltins, EveryBuiltinParsesAndValidates) {
  // The shipped set (scripts/crossword/builtin.txt): 30 minis (5x5), 12 midis (7x7), 6 maxis (9x9).
  ASSERT_EQ(builtinCount(), 48u);
  std::set<std::string> ids;
  std::set<uint32_t> fnvs;
  int bySize[16] = {};
  int clues = 0;
  auto p = newPuzzle();
  for (size_t i = 0; i < builtinCount(); i++) {
    const BuiltinPuzzle& b = builtinPuzzle(i);
    SCOPED_TRACE(b.id);
    EXPECT_TRUE(validBuiltinId(b.id, std::strlen(b.id)));
    EXPECT_TRUE(ids.insert(b.id).second);
    const TextStatus st = parseTextPuzzle(b.text, std::strlen(b.text), *p);
    ASSERT_EQ(st.error, Error::None) << "line " << st.line;
    expectConsistent(*p);
    EXPECT_EQ(std::string(p->sourceKey), std::string("builtin:") + b.id);
    EXPECT_STREQ(p->title, b.title);
    EXPECT_EQ(p->w, b.w);
    EXPECT_EQ(p->h, b.h);
    EXPECT_EQ(p->w, p->h);
    EXPECT_EQ(p->fnv, b.fnv);
    EXPECT_EQ(p->fnv, puzzleFnv(p->w, p->h, p->solution));
    EXPECT_TRUE(fnvs.insert(p->fnv).second);
    // The id names the size: mini = 5x5, midi = 7x7, maxi = 9x9.
    const std::string id = b.id;
    const int side = id.rfind("mini-", 0) == 0 ? 5 : id.rfind("midi-", 0) == 0 ? 7 : id.rfind("maxi-", 0) == 0 ? 9 : 0;
    EXPECT_EQ(p->w, side);
    bySize[p->w]++;
    for (int e = 0; e < p->entryCount; e++) {
      EXPECT_GT(p->entries[e].clueLen, 0);
      EXPECT_LE(p->entries[e].clueLen, 160);
      EXPECT_GE(p->entries[e].len, 3) << "every entry is 3 letters or more";
      for (const char* c = p->clue(e); *c; c++) EXPECT_TRUE(*c >= 0x20 && *c < 0x7F) << "ASCII clues";
    }
    clues += p->entryCount;
    // Every square is in an Across and a Down entry (no unchecked squares in the built-ins).
    for (int c = 0; c < p->cells(); c++) {
      if (!p->isBlock(c)) EXPECT_TRUE(p->entryAt[ACROSS][c] != NO_ENTRY && p->entryAt[DOWN][c] != NO_ENTRY) << c;
    }
    EXPECT_EQ(findBuiltin(b.id), static_cast<int>(i));
  }
  EXPECT_EQ(bySize[5], 30);
  EXPECT_EQ(bySize[7], 12);
  EXPECT_EQ(bySize[9], 6);
  EXPECT_EQ(clues, 678);
  EXPECT_EQ(findBuiltin("no-such-puzzle"), -1);
  EXPECT_EQ(findBuiltin(nullptr), -1);
}

TEST(CrosswordBuiltins, PinnedFingerprints) {
  // Progress files and the solved list key on a puzzle's fnv: a grid changed under an id starts it
  // fresh and unsolved on every reader. These pins move only on purpose.
  struct Pin {
    const char* id;
    uint32_t fnv;
    const char* solution;
  };
  const Pin pins[] = {
      {"mini-001", 0xCC2BF4EEu, "##MOOSTORMLANCEOCEANTOY##"},
      {"maxi-006", 0x433C051Bu, "#OPT#CODEABLE#ANEWNEON#TUNETYPECASTS###MOM###RENEGADESICON#REDOSHUT#AMIDEONS#NOT#"},
  };
  for (const Pin& pin : pins) {
    SCOPED_TRACE(pin.id);
    const int i = findBuiltin(pin.id);
    ASSERT_GE(i, 0);
    EXPECT_EQ(builtinPuzzle(static_cast<size_t>(i)).fnv, pin.fnv);
    const auto p = parseText(builtinPuzzle(static_cast<size_t>(i)).text);
    EXPECT_EQ(p->fnv, pin.fnv);
    EXPECT_EQ(solutionText(*p), pin.solution);
  }
}

// The picker's and the game's paths over the whole set: every key fits, the solved count and the
// "next unsolved" walk read the solved list as CrosswordPickerActivity / CrosswordActivity do.
TEST(CrosswordBuiltins, SolvedCountAndNextUnsolvedOverTheSet) {
  const int count = static_cast<int>(builtinCount());
  ASSERT_LE(count, MAX_FOLDER_FILES);  // the picker's per-row status array
  std::vector<std::string> keys;
  for (int i = 0; i < count; i++) {
    keys.push_back(std::string(BUILTIN_KEY_PREFIX) + builtinPuzzle(static_cast<size_t>(i)).id);
    EXPECT_LE(keys.back().size(), MAX_SOURCE_KEY);
    EXPECT_STREQ(builtinIdFromKey(keys.back().c_str()), builtinPuzzle(static_cast<size_t>(i)).id);
  }
  // Solved: every third one, plus a line for mini-002 under an older grid (its fnv changed: not solved).
  std::string list;
  char line[SOLVED_LINE_MAX + 2];
  int expectedSolved = 0;
  for (int i = 0; i < count; i += 3) {
    const size_t n = formatSolvedLine(builtinPuzzle(static_cast<size_t>(i)).fnv, keys[i].c_str(), line, sizeof(line));
    ASSERT_GT(n, 0u);
    list.append(line, n);
    expectedSolved++;
  }
  const int stale = findBuiltin("mini-002");
  ASSERT_EQ(stale % 3, 1);
  const size_t n = formatSolvedLine(0x3A9804F4u, keys[stale].c_str(), line, sizeof(line));
  list.append(line, n);

  struct Ctx {
    const std::string* list;
    const std::vector<std::string>* keys;
  } ctx{&list, &keys};
  const IsSolved isSolved = [](void* c, const int index) {
    const auto* x = static_cast<const Ctx*>(c);
    return solvedListHas(x->list->data(), x->list->size(), (*x->keys)[index], builtinPuzzle(index).fnv);
  };
  int solvedCount = 0;
  for (int i = 0; i < count; i++) solvedCount += isSolved(&ctx, i) ? 1 : 0;
  EXPECT_EQ(solvedCount, expectedSolved);
  EXPECT_EQ(solvedCount, 16);
  EXPECT_FALSE(isSolved(&ctx, stale));
  // The first unsolved (a first run opens it), the next after a solved one, and the wrap at the end.
  EXPECT_EQ(nextUnsolved(count, -1, isSolved, &ctx), 1);
  EXPECT_EQ(nextUnsolved(count, 2, isSolved, &ctx), 4);
  EXPECT_EQ(nextUnsolved(count, count - 1, isSolved, &ctx), 1);  // maxi-006 (index 47) wraps to the start
  // All solved: -1 ("All solved here").
  list.clear();
  for (int i = 0; i < count; i++) {
    list.append(line, formatSolvedLine(builtinPuzzle(static_cast<size_t>(i)).fnv, keys[i].c_str(), line, sizeof(line)));
  }
  EXPECT_EQ(nextUnsolved(count, 10, isSolved, &ctx), -1);
}

// A 7x7 sample with two squares only a Down entry covers: they play like any other.
TEST(CrosswordTextFormat, SevenBySevenSampleHasUncheckedSquares) {
  const auto p = parseText(cw_samples::OASIS);
  expectConsistent(*p);
  EXPECT_EQ(p->w, 7);
  int unchecked = 0;
  for (int c = 0; c < p->cells(); c++) {
    if (!p->isBlock(c) && (p->entryAt[ACROSS][c] == NO_ENTRY || p->entryAt[DOWN][c] == NO_ENTRY)) unchecked++;
  }
  EXPECT_EQ(unchecked, 2);
  EXPECT_EQ(p->entryAt[ACROSS][p->index(2, 6)], NO_ENTRY);
  EXPECT_EQ(p->entryAt[ACROSS][p->index(4, 0)], NO_ENTRY);
}

TEST(CrosswordTextFormat, EverySampleParses) {
  for (const char* text : {cw_samples::SHELTER, cw_samples::KEEN, cw_samples::OASIS}) {
    const auto p = parseText(text);
    expectConsistent(*p);
  }
  EXPECT_STREQ(parseText(cw_samples::KEEN)->title, "Keen");
}

TEST(CrosswordTextFormat, CommentsBlanksAndRowsStartingWithABlock) {
  const char text[] =
      "# a comment\n\n#\n=== a-1 |  Title here  \r\n##HUT\n# not a row: a comment\n#CASE\nRAVEN\nAGED#\nPEN##\n"
      "A 1 One\nA 4 Four\n\nA 5 Five\nA 6 Six\nA 7 Seven\nD 1 One\nD 2 Two\nD 3 Three\nD 4 Four\nD 5 Five\n";
  const auto p = parseText(text);
  EXPECT_STREQ(p->title, "Title here");
  EXPECT_STREQ(p->sourceKey, "builtin:a-1");
  EXPECT_EQ(solutionText(*p), "##HUT#CASERAVENAGED#PEN##");
}

TEST(CrosswordTextFormat, Errors) {
  struct Case {
    const char* text;
    Error error;
    int line;
  };
  const char clues[] = "A 1 a\nA 4 b\nA 5 c\nA 6 d\nA 7 e\nD 1 f\nD 2 g\nD 3 h\nD 4 i\nD 5 j\n";
  const std::string grid = "##HUT\n#CASE\nRAVEN\nAGED#\nPEN##\n";
  const std::string head = "=== t | T\n";
  const std::string good = head + grid + clues;
  const std::string cases[] = {
      grid + clues,                                          // no header
      "=== Bad Id | T\n" + grid + clues,                     // id
      "=== t |\n" + grid + clues,                            // empty title
      "=== t T\n" + grid + clues,                            // no bar
      head + "##HUT\n#CAS\nRAVEN\nAGED#\nPEN##\n" + clues,   // ragged
      head + "##HUT\n#CA1E\nRAVEN\nAGED#\nPEN##\n" + clues,  // a digit: not a row, not a clue
      head + grid + "A 1 a\nA 4 b\nA 5 c\nA 6 d\nA 7 e\nD 1 f\nD 2 g\nD 3 h\nD 4 i\n",  // missing 5D
      head + grid + clues + "A 2 extra\n",                                              // no 2-Across
      head + grid + clues + "D 3 again\n",                                              // 3-Down twice
      head + grid + "A x a\n" + clues,                                                  // bad number
      head + grid + "A 1    \n" + clues,                                                // empty clue
      head + grid + clues + head,                                                       // a second block
      head + std::string(16, 'A') + "\n" + clues,                                       // too wide
      head + "AB\nCD\n" + clues,                                                        // too small
      head + grid + "#note\n" + clues,                              // a comment needs "# ": not a row, not a clue
      head + grid + clues + "#####\n",                              // a row of blocks after the clues is no comment
      head + "##HUT\n#####\n#CASE\nRAVEN\nAGED#\nPEN##\n" + clues,  // ... and a row in the grid
  };
  const Error expected[] = {
      Error::Damaged,      Error::Damaged,      Error::Damaged,      Error::Damaged, Error::Damaged,     Error::Damaged,
      Error::ClueMismatch, Error::ClueMismatch, Error::ClueMismatch, Error::Damaged, Error::Damaged,     Error::Damaged,
      Error::TooBig,       Error::TooSmall,     Error::Damaged,      Error::Damaged, Error::ClueMismatch};
  static_assert(sizeof(expected) / sizeof(expected[0]) == sizeof(cases) / sizeof(cases[0]), "one per case");
  auto p = newPuzzle();
  ASSERT_EQ(parseTextPuzzle(good.data(), good.size(), *p).error, Error::None);
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const TextStatus st = parseTextPuzzle(cases[i].data(), cases[i].size(), *p);
    EXPECT_EQ(st.error, expected[i]) << "case " << i;
  }
  // The line of the problem is reported.
  const std::string extra = head + grid + clues + "A 2 extra\n";
  EXPECT_EQ(parseTextPuzzle(extra.data(), extra.size(), *p).line, 17);
  EXPECT_EQ(parseTextPuzzle(nullptr, 0, *p).error, Error::Damaged);
}

TEST(CrosswordTextFormat, ClueTextIsCleanedAndCut) {
  std::string text = "=== t | T\n##HUT\n#CASE\nRAVEN\nAGED#\nPEN##\nA 1   spaced    out  \nA 4 " +
                     std::string(500, 'x') + "\nA 5 c\nA 6 d\nA 7 e\nD 1 f\nD 2 g\nD 3 h\nD 4 i\nD 5 j\n";
  auto p = newPuzzle();
  ASSERT_EQ(parseTextPuzzle(text.data(), text.size(), *p).error, Error::None);
  EXPECT_EQ(clueOf(*p, ACROSS, 1), "spaced out");
  EXPECT_EQ(clueOf(*p, ACROSS, 4).size(), MAX_CLUE_BYTES);
}

TEST(CrosswordTextFormat, CluePoolOverflowIsTooManyClues) {
  auto p = gridOf({"ABC", "DEF", "GHI"});
  const std::string big(MAX_CLUE_BYTES, 'q');
  int stored = 0;
  Error e = Error::None;
  for (int i = 0; i < 100 && e == Error::None; i++) {
    e = setClue(*p, i % p->entryCount, big.data(), big.size(), 0);
    if (e == Error::None) stored++;
  }
  EXPECT_EQ(e, Error::TooManyClues);
  EXPECT_EQ(stored, static_cast<int>((CLUE_POOL - 1) / (MAX_CLUE_BYTES + 1)));
}

// ---- ipuz ---------------------------------------------------------------------------------------

TEST(CrosswordIpuz, PlainStringClues) {
  auto p = newPuzzle();
  const LoadStatus st = loadIpuz(fixture("strings.ipuz"), *p);
  ASSERT_EQ(st.error, Error::None);
  expectConsistent(*p);
  EXPECT_EQ(solutionText(*p), "##HUT#CASERAVENAGED#PEN##");  // lower-case answers folded
  EXPECT_EQ(p->fnv, parseText(SHELTER)->fnv);
  EXPECT_EQ(clueOf(*p, ACROSS, 1), "Simple shelter in the woods");
  EXPECT_EQ(clueOf(*p, DOWN, 5), "Knock on a door");
  EXPECT_STREQ(p->title, "Shelter");
  EXPECT_STREQ(p->author, "CrossPoint tests");
  EXPECT_STREQ(p->copyright, "Public domain test data");
  EXPECT_STREQ(p->sourceKey, "/Puzzles/Crossword/test.ipuz");
  EXPECT_EQ(st.width, 5);
  EXPECT_EQ(st.truncatedClues, 0);
}

TEST(CrosswordIpuz, ByteOrderMarkAndJsonpWrapper) {
  auto p = newPuzzle();
  const std::string json = fixture("strings.ipuz");
  for (const std::string& wrapped : {"\xEF\xBB\xBF" + json, "ipuz(" + json + ")", " \n ipuz (" + json + ");\n",
                                     "\xEF\xBB\xBF"
                                     "ipuz(" +
                                         json + ")",
                                     "  \r\n" + json}) {
    const LoadStatus st = loadIpuz(wrapped, *p);
    EXPECT_EQ(st.error, Error::None) << wrapped.substr(0, 8);
    EXPECT_EQ(clueOf(*p, ACROSS, 1), "Simple shelter in the woods");
  }
  // Not JSON after the prefix is still refused.
  EXPECT_NE(loadIpuz("ipuz(nope", *p).error, Error::None);
  EXPECT_NE(loadIpuz("\xEF\xBB", *p).error, Error::None);
}

TEST(CrosswordIpuz, AllNullGridIsDiagramless) {
  // A diagramless file as the ipuz spec allows it: no squares in the puzzle, letters in the solution.
  auto p = newPuzzle();
  const std::string json =
      R"({"version":"http://ipuz.org/v2","kind":["http://ipuz.org/crossword#1"],"dimensions":{"width":3,"height":3},)"
      R"("puzzle":[[null,null,null],[null,null,null],[null,null,null]],)"
      R"("solution":[["C","A","T"],["A","G","E"],["T","E","N"]],"clues":{"Across":[],"Down":[]}})";
  EXPECT_EQ(loadIpuz(json, *p).error, Error::Diagramless);
}

TEST(CrosswordIpuz, PairCluesCirclesAndHtmlTitle) {
  auto p = newPuzzle();
  ASSERT_EQ(loadIpuz(fixture("pairs.ipuz"), *p).error, Error::None);
  expectConsistent(*p);
  EXPECT_STREQ(p->title, "Keen & eager");
  EXPECT_EQ(clueOf(*p, ACROSS, 6), "Keen to get started");
  int circles = 0;
  for (int c = 0; c < p->cells(); c++) circles += p->isCircled(c) ? 1 : 0;
  EXPECT_EQ(circles, 2);
  EXPECT_TRUE(p->isCircled(p->index(1, 1)));
  EXPECT_TRUE(p->isCircled(p->index(2, 2)));
}

TEST(CrosswordIpuz, ObjectCluesLabelledListsAndObjectCells) {
  auto p = newPuzzle();
  ASSERT_EQ(loadIpuz(fixture("objects.ipuz"), *p).error, Error::None);
  expectConsistent(*p);
  EXPECT_EQ(clueOf(*p, DOWN, 4), "Home for a pet hamster");
  EXPECT_TRUE(p->isCircled(p->index(2, 2)));
  EXPECT_EQ(p->fnv, parseText(SHELTER)->fnv);
}

TEST(CrosswordIpuz, NullCellsAndBlockOverride) {
  auto p = newPuzzle();
  ASSERT_EQ(loadIpuz(fixture("nulls.ipuz"), *p).error, Error::None);
  EXPECT_EQ(solutionText(*p), "#PAY#MERITEAGERTRULY#LED#");
  ASSERT_EQ(loadIpuz(fixture("block_override.ipuz"), *p).error, Error::None);
  EXPECT_EQ(solutionText(*p), "##HUT#CASERAVENAGED#PEN##");
}

TEST(CrosswordIpuz, HtmlAndEntitiesInClues) {
  auto p = newPuzzle();
  ASSERT_EQ(loadIpuz(fixture("html.ipuz"), *p).error, Error::None);
  EXPECT_STREQ(p->title, "Keen & HTML");
  EXPECT_EQ(clueOf(*p, ACROSS, 1), "Settle the bill & tip");
  EXPECT_EQ(clueOf(*p, ACROSS, 4), "Deserve 'praise', \"a lot\"");
  EXPECT_EQ(clueOf(*p, DOWN, 2), "Disagree <loudly>... it's caf\xc3\xa9 talk");
  EXPECT_EQ(clueOf(*p, DOWN, 3), "Give way - at a \"traffic\" sign");
}

TEST(CrosswordIpuz, LongClueIsCutNotRefused) {
  auto p = newPuzzle();
  const LoadStatus st = loadIpuz(fixture("long_clue.ipuz"), *p);
  ASSERT_EQ(st.error, Error::None);
  EXPECT_EQ(st.truncatedClues, 1);
  const std::string clue = clueOf(*p, ACROSS, 8);
  EXPECT_LE(clue.size(), MAX_CLUE_BYTES);
  EXPECT_GT(clue.size(), MAX_CLUE_BYTES - 20);
  EXPECT_EQ(clue.rfind("Was in front and kept going", 0), 0u);
}

TEST(CrosswordIpuz, LargeGridsLoad) {
  auto p = newPuzzle();
  ASSERT_EQ(loadIpuz(fixture("fifteen.ipuz"), *p).error, Error::None);
  expectConsistent(*p);
  EXPECT_EQ(p->w, 15);
  EXPECT_EQ(p->h, 15);
  auto q = newPuzzle();
  ASSERT_EQ(loadPuz(fixture("fifteen.puz"), *q).error, Error::None);
  EXPECT_EQ(q->fnv, p->fnv);
  ASSERT_EQ(q->entryCount, p->entryCount);
  for (int e = 0; e < p->entryCount; e++) EXPECT_STREQ(q->clue(e), p->clue(e));
  ASSERT_EQ(loadIpuz(fixture("nine.ipuz"), *p).error, Error::None);
  EXPECT_EQ(p->w, 9);
}

TEST(CrosswordIpuz, Refusals) {
  struct Case {
    const char* file;
    Error error;
  };
  const Case cases[] = {
      {"no_solution.ipuz", Error::NoSolution},
      {"rebus.ipuz", Error::Rebus},
      {"barred.ipuz", Error::Barred},
      {"oversize.ipuz", Error::TooBig},
      {"bad_numbering.ipuz", Error::BadNumbering},
      {"not_crossword.ipuz", Error::NotCrossword},
      {"truncated.ipuz", Error::Damaged},
      {"missing_clue.ipuz", Error::ClueMismatch},
  };
  auto p = newPuzzle();
  for (const Case& c : cases) {
    const LoadStatus st = loadIpuz(fixture(c.file), *p);
    EXPECT_EQ(st.error, c.error) << c.file;
    EXPECT_EQ(p->w, 0) << c.file;  // a refusal leaves an empty puzzle
  }
  const LoadStatus big = loadIpuz(fixture("oversize.ipuz"), *p);
  EXPECT_EQ(big.width, 17);
  EXPECT_EQ(big.height, 17);
  EXPECT_EQ(loadIpuz("", *p).error, Error::Damaged);
  EXPECT_EQ(loadIpuz("not json", *p).error, Error::Damaged);
  EXPECT_EQ(loadIpuz("[1, 2]", *p).error, Error::NotCrossword);
  EXPECT_EQ(loadIpuz("{\"kind\": [\"http://ipuz.org/crossword#1\"]}", *p).error, Error::Damaged);
  EXPECT_EQ(loadIpuz("{\"kind\": [\"http://ipuz.org/crossword/diagramless#1\"]}", *p).error, Error::Diagramless);
}

namespace {

// A heap that counts (and can refuse past a budget), for the JSON document.
struct CountingHeap {
  static size_t live;
  static size_t peak;
  static size_t budget;
  static void* allocate(const size_t n) {
    if (live + n > budget) return nullptr;
    void* p = std::malloc(n + sizeof(size_t));
    if (!p) return nullptr;
    *static_cast<size_t*>(p) = n;
    live += n;
    if (live > peak) peak = live;
    return static_cast<size_t*>(p) + 1;
  }
  static void deallocate(void* p) {
    if (!p) return;
    size_t* base = static_cast<size_t*>(p) - 1;
    live -= *base;
    std::free(base);
  }
  static void* reallocate(void* p, const size_t n) {
    if (!p) return allocate(n);
    const size_t old = *(static_cast<size_t*>(p) - 1);
    void* q = allocate(n);
    if (!q) return nullptr;
    std::memcpy(q, p, old < n ? old : n);
    deallocate(p);
    return q;
  }
};
size_t CountingHeap::live = 0;
size_t CountingHeap::peak = 0;
size_t CountingHeap::budget = 0;

}  // namespace

TEST(CrosswordIpuz, UsesTheGivenHeapAndSurvivesRunningOut) {
  const Allocator heap{CountingHeap::allocate, CountingHeap::deallocate, CountingHeap::reallocate};
  auto p = newPuzzle();
  CountingHeap::budget = SIZE_MAX;
  CountingHeap::peak = 0;
  ASSERT_EQ(loadIpuz(fixture("fifteen.ipuz"), *p, &heap).error, Error::None);
  EXPECT_EQ(CountingHeap::live, 0u);
  EXPECT_GT(CountingHeap::peak, 1000u);
  EXPECT_LT(CountingHeap::peak, 64u * 1024u);  // the filtered DOM of a 15x15
  std::printf("ipuz 15x15: JSON heap peak %zu bytes\n", CountingHeap::peak);
  CountingHeap::budget = 2048;
  EXPECT_EQ(loadIpuz(fixture("fifteen.ipuz"), *p, &heap).error, Error::OutOfMemory);
  EXPECT_EQ(CountingHeap::live, 0u);
}

TEST(CrosswordIpuz, RandomMutationsNeverCrash) {
  const std::string base = fixture("objects.ipuz");
  std::mt19937 rng(1234);
  auto p = newPuzzle();
  int loaded = 0;
  for (int i = 0; i < 400; i++) {
    std::string s = base;
    const int edits = 1 + static_cast<int>(rng() % 4);
    for (int k = 0; k < edits; k++) {
      const size_t at = rng() % s.size();
      switch (rng() % 3) {
        case 0:
          s[at] = static_cast<char>(rng() & 0xFF);
          break;
        case 1:
          s.erase(at, 1 + rng() % 8);
          break;
        default:
          s.resize(at);
          break;
      }
      if (s.empty()) break;
    }
    if (loadIpuz(s, *p).error == Error::None) {
      loaded++;
      expectConsistent(*p);
    }
  }
  EXPECT_GT(loaded, 0);
}

// ---- puz ----------------------------------------------------------------------------------------

TEST(CrosswordPuz, Version13IsLatin1) {
  auto p = newPuzzle();
  const LoadStatus st = loadPuz(fixture("v13_latin1.puz"), *p);
  ASSERT_EQ(st.error, Error::None);
  expectConsistent(*p);
  EXPECT_EQ(st.checksumWarnings, 0);
  EXPECT_STREQ(p->title, "Caf\xc3\xa9 Keen");
  EXPECT_EQ(clueOf(*p, ACROSS, 7), "In fact; na\xc3\xafvely honest");
  EXPECT_EQ(clueOf(*p, DOWN, 2), "Disagree at the caf\xc3\xa9");
  EXPECT_EQ(clueOf(*p, DOWN, 5), "Give it a shot");
  EXPECT_EQ(solutionText(*p), "#PAY#MERITEAGERTRULY#LED#");
  EXPECT_STREQ(p->sourceKey, "/Puzzles/Crossword/test.puz");
}

TEST(CrosswordPuz, Version20IsUtf8) {
  auto p = newPuzzle();
  const LoadStatus st = loadPuz(fixture("v20_utf8.puz"), *p);
  ASSERT_EQ(st.error, Error::None);
  EXPECT_EQ(st.checksumWarnings, 0);  // notes are summed from version 1.3 on
  EXPECT_STREQ(p->title, "Shelter - UTF-8");
  EXPECT_EQ(clueOf(*p, ACROSS, 4), "Detective's puzzle - crack it");
  EXPECT_EQ(clueOf(*p, DOWN, 2), "Secondhand, like a caf\xc3\xa9 chair");
  EXPECT_EQ(p->fnv, parseText(SHELTER)->fnv);
}

TEST(CrosswordPuz, ExtensionsCirclesAndRebus) {
  auto p = newPuzzle();
  LoadStatus st = loadPuz(fixture("gext_circles.puz"), *p);
  ASSERT_EQ(st.error, Error::None);
  EXPECT_EQ(st.checksumWarnings, 0);
  EXPECT_TRUE(p->isCircled(p->index(1, 1)));
  EXPECT_TRUE(p->isCircled(p->index(2, 2)));
  EXPECT_FALSE(p->isCircled(p->index(0, 1)));
  EXPECT_EQ(loadPuz(fixture("grbs_rebus.puz"), *p).error, Error::Rebus);
  st = loadPuz(fixture("grbs_empty.puz"), *p);
  EXPECT_EQ(st.error, Error::None);  // a rebus table with no rebus square
}

TEST(CrosswordPuz, Refusals) {
  auto p = newPuzzle();
  EXPECT_EQ(loadPuz(fixture("scrambled.puz"), *p).error, Error::Locked);
  EXPECT_EQ(loadPuz(fixture("diagramless.puz"), *p).error, Error::Diagramless);
  const LoadStatus big = loadPuz(fixture("oversize.puz"), *p);
  EXPECT_EQ(big.error, Error::TooBig);
  EXPECT_EQ(big.width, 17);
  EXPECT_EQ(big.height, 17);
  EXPECT_EQ(loadPuz(fixture("truncated.puz"), *p).error, Error::Damaged);
  EXPECT_EQ(p->w, 0);
  EXPECT_EQ(loadPuz("", *p).error, Error::NotCrossword);
  EXPECT_EQ(loadPuz(fixture("strings.ipuz"), *p).error, Error::NotCrossword);
  std::string headerOnly = fixture("v20_utf8.puz").substr(0, 0x34);
  EXPECT_EQ(loadPuz(headerOnly, *p).error, Error::Damaged);
}

TEST(CrosswordPuz, WrongChecksumsStillLoad) {
  auto p = newPuzzle();
  const LoadStatus st = loadPuz(fixture("bad_checksums.puz"), *p);
  ASSERT_EQ(st.error, Error::None);
  EXPECT_EQ(st.checksumWarnings, 3);  // CIB, global, masked
  EXPECT_EQ(clueOf(*p, ACROSS, 5), "Large black bird with a croak");
}

TEST(CrosswordPuz, ShortTailsAndCutExtensionsStillLoad) {
  auto p = newPuzzle();
  // A trailing CR LF or a few NULs after the notes: complete grid and clues, one warning.
  for (const std::string tail : {std::string("\r\n"), std::string(3, '\0'), std::string(7, '\0')}) {
    const LoadStatus st = loadPuz(fixture("v13_latin1.puz") + tail, *p);
    EXPECT_EQ(st.error, Error::None) << tail.size();
    EXPECT_GE(st.checksumWarnings, 1);
  }
  // Bytes after the last extension, and a GEXT cut short.
  const std::string circles = fixture("gext_circles.puz");
  EXPECT_EQ(loadPuz(circles + "abc", *p).error, Error::None);
  EXPECT_EQ(loadPuz(circles.substr(0, circles.size() - 4), *p).error, Error::None);
  // A GRBS cut short still refuses: a rebus square might be missed.
  const std::string rebusTable = fixture("grbs_empty.puz");
  EXPECT_EQ(loadPuz(rebusTable.substr(0, rebusTable.size() - 4), *p).error, Error::Damaged);
}

TEST(CrosswordPuz, ChecksumStep) {
  const uint8_t data[] = {'A', 'B', 'C'};
  // By hand: 0 -> 0x41; 0x41 rotates to 0x8020, + 'B' = 0x8062; rotates to 0x4031, + 'C' = 0x4074.
  EXPECT_EQ(puzChecksum(data, 3, 0), 0x4074);
}

TEST(CrosswordPuz, RandomMutationsNeverCrash) {
  const std::string base = fixture("gext_circles.puz");
  std::mt19937 rng(99);
  auto p = newPuzzle();
  for (int i = 0; i < 1000; i++) {
    std::string s = base;
    const size_t at = rng() % s.size();
    if (rng() % 2) {
      s[at] = static_cast<char>(rng() & 0xFF);
    } else {
      s.resize(at);
    }
    if (loadPuz(s, *p).error == Error::None) expectConsistent(*p);
  }
}

// ---- card sources -------------------------------------------------------------------------------

TEST(CrosswordSource, FileFormatsAndNames) {
  EXPECT_EQ(formatOf("daily.ipuz"), Format::Ipuz);
  EXPECT_EQ(formatOf("DAILY.IPUZ"), Format::Ipuz);
  EXPECT_EQ(formatOf("old.puz"), Format::Puz);
  EXPECT_EQ(formatOf("old.Puz"), Format::Puz);
  EXPECT_EQ(formatOf(".hidden.puz"), Format::None);
  EXPECT_EQ(formatOf("notes.txt"), Format::None);
  EXPECT_EQ(formatOf("puz"), Format::None);
  EXPECT_EQ(formatOf(".puz"), Format::None);
  EXPECT_EQ(formatOf("x.jpz"), Format::None);
  EXPECT_EQ(formatOf(nullptr), Format::None);
  char name[16];
  EXPECT_EQ(displayName("Sunday mini.ipuz", name, sizeof(name)), 11u);
  EXPECT_STREQ(name, "Sunday mini");
  displayName("caf\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9.puz", name, 9);
  EXPECT_STREQ(name, "caf\xc3\xa9\xc3\xa9");  // cut on a code point
  EXPECT_TRUE(isPackFolder("Sunday"));
  EXPECT_FALSE(isPackFolder(".Trash"));
  EXPECT_FALSE(isPackFolder(""));
}

TEST(CrosswordSource, NextUnsolvedWraps) {
  bool solved[4] = {true, false, true, false};
  const IsSolved isSolved = [](void* ctx, const int i) { return static_cast<bool*>(ctx)[i]; };
  EXPECT_EQ(nextUnsolved(4, 1, isSolved, solved), 3);
  EXPECT_EQ(nextUnsolved(4, 3, isSolved, solved), 1);  // wraps
  EXPECT_EQ(nextUnsolved(4, -1, isSolved, solved), 1);
  solved[1] = true;
  EXPECT_EQ(nextUnsolved(4, 3, isSolved, solved), 3);  // itself, last
  solved[3] = true;
  EXPECT_EQ(nextUnsolved(4, 3, isSolved, solved), -1);  // all solved here
  EXPECT_EQ(nextUnsolved(0, 0, isSolved, solved), -1);
}

TEST(CrosswordSource, BuiltinKeys) {
  EXPECT_STREQ(builtinIdFromKey("builtin:mini-001"), "mini-001");
  EXPECT_EQ(builtinIdFromKey("builtin:"), nullptr);
  EXPECT_EQ(builtinIdFromKey("/Puzzles/Crossword/a.puz"), nullptr);
  EXPECT_EQ(builtinIdFromKey(nullptr), nullptr);
}

// ---- play rules ---------------------------------------------------------------------------------

namespace {

struct Game {
  std::unique_ptr<Puzzle> p = parseText(SHELTER);
  std::unique_ptr<Progress> prog = newProgress();
  Game() { resetProgress(*p, *prog); }
  int at(const int r, const int c) const { return p->index(r, c); }
  void put(const int r, const int c, const char ch) { prog->fill[at(r, c)] = ch; }
  // Fills every white square with its answer except the listed ones.
  void fillAllBut(std::initializer_list<std::pair<int, int>> blanks) {
    for (int i = 0; i < p->cells(); i++) {
      if (!p->isBlock(i)) prog->fill[i] = p->solution[i];
    }
    for (const auto& b : blanks) put(b.first, b.second, EMPTY);
  }
  void cursor(const int r, const int c, const uint8_t dir) {
    prog->cursor = static_cast<uint8_t>(at(r, c));
    prog->dir = dir;
  }
  std::string row(const int r) const {
    std::string s;
    for (int c = 0; c < p->w; c++) s += prog->fill[at(r, c)];
    return s;
  }
};

}  // namespace

TEST(CrosswordNav, FreshProgress) {
  Game g;
  EXPECT_EQ(g.prog->cursor, g.at(0, 2));
  EXPECT_EQ(g.prog->dir, ACROSS);
  EXPECT_EQ(g.row(0), "##   ");
  EXPECT_TRUE(progressMatches(*g.p, *g.prog));
  EXPECT_EQ(g.prog->fnv, g.p->fnv);
  EXPECT_STREQ(g.prog->sourceKey, "builtin:sample-shelter");
}

TEST(CrosswordNav, TypingAdvancesThenJumpsToTheNextClue) {
  Game g;
  Change ch = typeLetter(*g.p, *g.prog, 'h', true);
  EXPECT_TRUE(ch.changed);
  EXPECT_TRUE(ch.edited);
  EXPECT_FALSE(ch.wordChanged);
  EXPECT_EQ(g.prog->cursor, g.at(0, 3));
  typeLetter(*g.p, *g.prog, 'U', true);
  ch = typeLetter(*g.p, *g.prog, 'T', true);
  EXPECT_TRUE(ch.wordChanged);  // HUT is full: on to 4-Across's first blank
  EXPECT_EQ(g.row(0), "##HUT");
  EXPECT_EQ(g.prog->cursor, g.at(1, 1));
  EXPECT_EQ(g.prog->dir, ACROSS);
}

TEST(CrosswordNav, SkipFilledOnAndOff) {
  Game g;
  g.put(1, 2, 'A');
  g.cursor(1, 1, ACROSS);
  typeLetter(*g.p, *g.prog, 'C', true);
  EXPECT_EQ(g.prog->cursor, g.at(1, 3));  // skips the filled A
  g.cursor(1, 1, ACROSS);
  typeLetter(*g.p, *g.prog, 'C', false);
  EXPECT_EQ(g.prog->cursor, g.at(1, 2));  // the next square, filled or not
}

TEST(CrosswordNav, EndOfWordGoesToItsFirstBlank) {
  Game g;
  g.cursor(1, 3, ACROSS);
  typeLetter(*g.p, *g.prog, 'S', true);
  EXPECT_EQ(g.prog->cursor, g.at(1, 4));
  const Change ch = typeLetter(*g.p, *g.prog, 'E', true);
  EXPECT_FALSE(ch.wordChanged);
  EXPECT_EQ(g.prog->cursor, g.at(1, 1));  // back to the word's first blank
  // Skip filled off: past the end the same rule applies.
  g.cursor(1, 4, ACROSS);
  typeLetter(*g.p, *g.prog, 'E', false);
  EXPECT_EQ(g.prog->cursor, g.at(1, 1));
}

TEST(CrosswordNav, OverwritingAFullWordStaysInIt) {
  // 4-Across filled wrong as CAVE, the rest empty: retyping CASE fixes it in place.
  Game g;
  g.put(1, 1, 'C');
  g.put(1, 2, 'A');
  g.put(1, 3, 'V');
  g.put(1, 4, 'E');
  g.cursor(1, 1, ACROSS);
  for (const char c : std::string("CASE")) {
    typeLetter(*g.p, *g.prog, c, true);
  }
  EXPECT_EQ(g.row(1), "#CASE");
  EXPECT_EQ(g.row(2), "     ");
  // Past the word's last square the end-of-word rule applies: the next clue with a blank (5-Across).
  EXPECT_EQ(g.prog->cursor, g.at(2, 0));
  EXPECT_EQ(g.prog->dir, ACROSS);
}

TEST(CrosswordNav, OverwritingInAFullGridSteps) {
  // A full grid with 5-Across typed as RAXXN: a tap on (2,2) and V, E fix it square by square.
  Game g;
  g.fillAllBut({});
  g.put(2, 2, 'X');
  g.put(2, 3, 'X');
  g.cursor(2, 2, ACROSS);
  Change ch = typeLetter(*g.p, *g.prog, 'V', true);
  EXPECT_EQ(g.prog->cursor, g.at(2, 3));
  EXPECT_TRUE(ch.fullWrong);
  ch = typeLetter(*g.p, *g.prog, 'E', true);
  EXPECT_EQ(g.row(2), "RAVEN");
  EXPECT_TRUE(ch.solvedNow);
}

TEST(CrosswordNav, NextClueWrapsFromDownToAcross) {
  Game g;
  g.fillAllBut({{0, 2}, {4, 0}});
  g.cursor(4, 0, DOWN);  // the last square of 5-Down (RAP), the last clue
  const Change ch = typeLetter(*g.p, *g.prog, 'P', true);
  EXPECT_TRUE(ch.wordChanged);
  EXPECT_EQ(g.prog->cursor, g.at(0, 2));  // 1-Across, the only blank left
  EXPECT_EQ(g.prog->dir, ACROSS);
}

TEST(CrosswordNav, CompletingTheGridSolvesOrSaysNotQuite) {
  Game g;
  g.fillAllBut({{2, 4}});
  g.cursor(2, 4, ACROSS);
  Change ch = typeLetter(*g.p, *g.prog, 'X', true);
  EXPECT_TRUE(ch.fullWrong);
  EXPECT_EQ(ch.wrong, 1);
  EXPECT_FALSE(ch.solvedNow);
  EXPECT_FALSE(g.prog->solved);
  EXPECT_EQ(g.prog->cursor, g.at(2, 4));      // a full grid stays put
  ch = typeLetter(*g.p, *g.prog, 'N', true);  // the cursor stayed: N replaces the X
  EXPECT_TRUE(ch.solvedNow);
  EXPECT_TRUE(g.prog->solved);
  // Solved: edits do nothing, moving still works.
  EXPECT_FALSE(typeLetter(*g.p, *g.prog, 'Q', true).changed);
  EXPECT_FALSE(deleteLetter(*g.p, *g.prog).changed);
  EXPECT_FALSE(check(*g.p, *g.prog, Scope::Puzzle).changed);
  EXPECT_TRUE(stepClue(*g.p, *g.prog, 1).changed);
}

TEST(CrosswordNav, SolvingReportsOnce) {
  Game g;
  g.fillAllBut({{4, 2}});
  g.cursor(4, 2, ACROSS);
  const Change ch = typeLetter(*g.p, *g.prog, 'N', true);
  EXPECT_TRUE(ch.solvedNow);
  EXPECT_FALSE(ch.fullWrong);
  EXPECT_TRUE(g.prog->solved);
  EXPECT_TRUE(tally(*g.p, *g.prog).correct());
}

TEST(CrosswordNav, DeleteCases) {
  Game g;
  g.cursor(1, 2, ACROSS);
  g.put(1, 1, 'C');
  g.put(1, 2, 'A');
  // A filled square: cleared, the cursor stays.
  Change ch = deleteLetter(*g.p, *g.prog);
  EXPECT_TRUE(ch.edited);
  EXPECT_EQ(g.row(1), "#C   ");
  EXPECT_EQ(g.prog->cursor, g.at(1, 2));
  // An empty square: back one and clear that.
  ch = deleteLetter(*g.p, *g.prog);
  EXPECT_EQ(g.row(1), "#    ");
  EXPECT_EQ(g.prog->cursor, g.at(1, 1));
  // At the start of the word: nothing.
  ch = deleteLetter(*g.p, *g.prog);
  EXPECT_FALSE(ch.changed);
  EXPECT_EQ(g.prog->cursor, g.at(1, 1));
  // A revealed square behind: step onto it, keep it.
  g.cursor(1, 1, ACROSS);
  reveal(*g.p, *g.prog, Scope::Letter);
  g.cursor(1, 2, ACROSS);
  ch = deleteLetter(*g.p, *g.prog);
  EXPECT_EQ(g.prog->cursor, g.at(1, 1));
  EXPECT_EQ(g.row(1), "#C   ");
  EXPECT_FALSE(ch.edited);
  // The cursor on a revealed square: it is locked, so Del steps back (here: the word's start).
  ch = deleteLetter(*g.p, *g.prog);
  EXPECT_EQ(g.row(1), "#C   ");
  // Delete clears a WRONG mark with the letter.
  g.cursor(1, 3, ACROSS);
  g.put(1, 3, 'X');
  check(*g.p, *g.prog, Scope::Letter);
  EXPECT_EQ(g.prog->flags[g.at(1, 3)], FLAG_WRONG);
  deleteLetter(*g.p, *g.prog);
  EXPECT_EQ(g.prog->flags[g.at(1, 3)], 0);
}

TEST(CrosswordNav, ClearWordKeepsRevealedSquares) {
  Game g;
  g.cursor(2, 0, ACROSS);
  for (const char c : std::string("RAVEN")) typeLetter(*g.p, *g.prog, c, true);
  g.cursor(2, 2, ACROSS);
  g.put(2, 2, 'X');
  reveal(*g.p, *g.prog, Scope::Letter);
  const Change ch = clearWord(*g.p, *g.prog);
  EXPECT_TRUE(ch.edited);
  EXPECT_EQ(g.row(2), "  V  ");
  EXPECT_EQ(g.prog->flags[g.at(2, 2)], FLAG_REVEALED);
}

TEST(CrosswordNav, RevealedSquaresDropTypedLetters) {
  Game g;
  g.cursor(1, 2, ACROSS);
  reveal(*g.p, *g.prog, Scope::Letter);
  EXPECT_EQ(g.prog->fill[g.at(1, 2)], 'A');
  EXPECT_EQ(g.prog->reveals, 1);
  const Change ch = typeLetter(*g.p, *g.prog, 'Z', false);
  EXPECT_FALSE(ch.edited);
  EXPECT_EQ(g.prog->fill[g.at(1, 2)], 'A');
  EXPECT_EQ(g.prog->cursor, g.at(1, 3));
}

TEST(CrosswordNav, TapsAndDirection) {
  Game g;
  // Tapping the cursor square toggles (it has both entries).
  Change ch = tapCell(*g.p, *g.prog, g.at(0, 2));
  EXPECT_EQ(g.prog->dir, DOWN);
  EXPECT_TRUE(ch.wordChanged);
  // Another square keeps the direction; a block does nothing.
  tapCell(*g.p, *g.prog, g.at(2, 0));
  EXPECT_EQ(g.prog->dir, DOWN);
  EXPECT_EQ(g.prog->cursor, g.at(2, 0));
  EXPECT_FALSE(tapCell(*g.p, *g.prog, g.at(0, 0)).changed);
  EXPECT_FALSE(tapCell(*g.p, *g.prog, -1).changed);
  // A square with only one entry switches to it, and does not toggle.
  auto p = gridOf({"ABC", "#D#", "EFG"});
  auto prog = newProgress();
  resetProgress(*p, *prog);
  prog->cursor = 2;
  prog->dir = DOWN;
  tapCell(*p, *prog, 0);  // A: Across only
  EXPECT_EQ(prog->dir, ACROSS);
  EXPECT_FALSE(canToggle(*p, *prog));
  EXPECT_FALSE(tapCell(*p, *prog, 0).changed);
  tapCell(*p, *prog, 4);  // D: Down only
  EXPECT_EQ(prog->dir, DOWN);
  EXPECT_FALSE(toggleDirection(*p, *prog).changed);
}

TEST(CrosswordNav, ClueStepsWrapAndLandOnFirstBlanks) {
  Game g;
  stepClue(*g.p, *g.prog, -1);
  EXPECT_EQ(currentEntry(*g.p, *g.prog), findEntry(*g.p, DOWN, 5));
  EXPECT_EQ(g.prog->cursor, g.at(2, 0));
  stepClue(*g.p, *g.prog, 1);
  EXPECT_EQ(currentEntry(*g.p, *g.prog), findEntry(*g.p, ACROSS, 1));
  g.put(1, 1, 'C');
  stepClue(*g.p, *g.prog, 1);
  EXPECT_EQ(g.prog->cursor, g.at(1, 2));  // 4-Across's first blank
  g.put(1, 2, 'A');
  g.put(1, 3, 'S');
  g.put(1, 4, 'E');
  gotoEntry(*g.p, *g.prog, findEntry(*g.p, ACROSS, 4));
  EXPECT_EQ(g.prog->cursor, g.at(1, 1));  // full: its first square
  char pattern[16];
  EXPECT_EQ(entryPattern(*g.p, *g.prog, findEntry(*g.p, DOWN, 1), pattern, sizeof(pattern)), 5u);
  EXPECT_STREQ(pattern, "_A___");
  EXPECT_TRUE(entryFull(*g.p, *g.prog, findEntry(*g.p, ACROSS, 4)));
  EXPECT_EQ(entryPattern(*g.p, *g.prog, 0, pattern, 3), 0u);
}

TEST(CrosswordNav, CheckMarksOnlyWrongFilledSquares) {
  Game g;
  g.put(2, 0, 'R');
  g.put(2, 1, 'X');  // wrong
  g.put(1, 1, 'Q');  // wrong, another word
  g.cursor(2, 0, ACROSS);
  Change ch = check(*g.p, *g.prog, Scope::Letter);
  EXPECT_EQ(ch.count, 0);
  EXPECT_EQ(g.prog->checks, 1);
  ch = check(*g.p, *g.prog, Scope::Word);
  EXPECT_EQ(ch.count, 1);
  EXPECT_EQ(g.prog->flags[g.at(2, 1)], FLAG_WRONG);
  EXPECT_EQ(g.prog->flags[g.at(1, 1)], 0);
  EXPECT_EQ(g.prog->flags[g.at(2, 2)], 0);  // empty: not marked
  ch = check(*g.p, *g.prog, Scope::Puzzle);
  EXPECT_EQ(ch.count, 2);
  EXPECT_EQ(g.prog->checks, 3);
  EXPECT_EQ(g.prog->fill[g.at(2, 1)], 'X');  // never fixed
  EXPECT_EQ(tally(*g.p, *g.prog).marked, 2);
  // Typing over a marked square clears its mark.
  g.cursor(2, 1, ACROSS);
  typeLetter(*g.p, *g.prog, 'A', true);
  EXPECT_EQ(g.prog->flags[g.at(2, 1)], 0);
}

TEST(CrosswordNav, RevealWordAndPuzzle) {
  Game g;
  g.cursor(3, 0, ACROSS);
  g.put(3, 0, 'A');  // right already: not revealed
  g.put(3, 1, 'X');
  Change ch = reveal(*g.p, *g.prog, Scope::Word);
  EXPECT_EQ(ch.count, 3);
  EXPECT_EQ(g.row(3), "AGED#");
  EXPECT_EQ(g.prog->flags[g.at(3, 0)], 0);
  EXPECT_EQ(g.prog->flags[g.at(3, 1)], FLAG_REVEALED);
  EXPECT_EQ(g.prog->reveals, 1);
  ch = reveal(*g.p, *g.prog, Scope::Word);  // nothing left to reveal: not counted
  EXPECT_EQ(ch.count, 0);
  EXPECT_EQ(g.prog->reveals, 1);
  ch = reveal(*g.p, *g.prog, Scope::Puzzle);
  EXPECT_TRUE(ch.solvedNow);
  EXPECT_TRUE(g.prog->solved);
  EXPECT_EQ(g.prog->reveals, 2);
  EXPECT_TRUE(progressMatches(*g.p, *g.prog));
}

TEST(CrosswordNav, ClearPuzzleStartsOver) {
  Game g;
  g.fillAllBut({});
  g.prog->elapsed = 99;
  reveal(*g.p, *g.prog, Scope::Puzzle);
  g.prog->checks = 4;
  const Change ch = clearPuzzle(*g.p, *g.prog);
  EXPECT_TRUE(ch.changed);
  EXPECT_FALSE(g.prog->solved);
  EXPECT_EQ(g.prog->elapsed, 0u);
  EXPECT_EQ(g.prog->checks, 0);
  EXPECT_EQ(tally(*g.p, *g.prog).filled, 0);
  EXPECT_EQ(g.row(0), "##   ");
}

TEST(CrosswordNav, SetCursorForTheBench) {
  Game g;
  setCursor(*g.p, *g.prog, g.at(3, 3), DOWN);
  EXPECT_EQ(g.prog->cursor, g.at(3, 3));
  EXPECT_EQ(g.prog->dir, DOWN);
  EXPECT_FALSE(setCursor(*g.p, *g.prog, g.at(4, 4), ACROSS).changed);  // a block
}

// ---- keyboard -----------------------------------------------------------------------------------

TEST(CrosswordKeyboard, EveryKeyCentreHitsItsKey) {
  std::set<char> letters;
  for (int i = 0; i < KEY_COUNT; i++) {
    const Key& k = keyboardKey(i);
    EXPECT_EQ(keyAt(k.rect.x + k.rect.w / 2, k.rect.y + k.rect.h / 2), i) << i;
    EXPECT_EQ(keyAt(k.rect.x, k.rect.y), i);
    EXPECT_EQ(keyAt(k.rect.right() - 1, k.rect.bottom() - 1), i);
    EXPECT_GE(k.rect.x, GRID_LEFT);
    EXPECT_LE(k.rect.right(), GRID_RIGHT);
    EXPECT_EQ(k.rect.h, KEY_ROW_H);
    if (k.kind == KeyKind::Letter) {
      EXPECT_TRUE(letters.insert(k.letter).second);
      EXPECT_GE(k.rect.w, 44);
      EXPECT_LE(k.rect.w, 45);
      EXPECT_EQ(keyForLetter(k.letter), i);
      EXPECT_EQ(keyForLetter(static_cast<char>(k.letter - 'A' + 'a')), i);
    } else {
      EXPECT_EQ(k.rect.w, SIDE_KEY_W - 2);
    }
  }
  EXPECT_EQ(letters.size(), 26u);
  EXPECT_EQ(keyboardKey(0).letter, 'Q');
  EXPECT_EQ(keyboardKey(9).letter, 'P');
  EXPECT_EQ(keyboardKey(10).letter, 'A');
  EXPECT_EQ(keyboardKey(20).letter, 'Z');
  EXPECT_EQ(keyboardKey(26).letter, 'M');
  EXPECT_EQ(keyboardKey(menuKey()).kind, KeyKind::Menu);
  EXPECT_EQ(keyboardKey(delKey()).kind, KeyKind::Del);
  EXPECT_EQ(keyboardKey(0).rect.y, 632);
  EXPECT_EQ(keyboardKey(10).rect.y, 688);
  EXPECT_EQ(keyboardKey(19).rect.y, 744);
  EXPECT_EQ(keyboardKey(19).rect.bottom(), 796);
  EXPECT_EQ(keyForLetter('1'), -1);
}

TEST(CrosswordKeyboard, GapsGoToTheNearerKey) {
  // Drawn 2 px apart; the gap's pixels belong to the key on their side of the pitch boundary.
  for (int i = 0; i < KEY_COUNT - 1; i++) {
    const Key& a = keyboardKey(i);
    const Key& b = keyboardKey(i + 1);
    if (a.rect.y != b.rect.y) continue;
    EXPECT_EQ(b.rect.x - a.rect.right(), 2) << i;
    EXPECT_EQ(keyAt(a.rect.right(), a.rect.y + 10), i) << i;
    EXPECT_EQ(keyAt(b.rect.x - 1, a.rect.y + 10), i + 1) << i;
  }
  // The 4 px row gaps (y 684..687 and 740..743) split in half between the rows.
  for (const int x : {60, 120, 240, 400}) {
    const int r0 = keyAt(x, 660);
    const int r1 = keyAt(x, 714);
    const int r2 = keyAt(x, 770);
    EXPECT_EQ(keyAt(x, 684), r0) << x;
    EXPECT_EQ(keyAt(x, 685), r0) << x;
    EXPECT_EQ(keyAt(x, 686), r1) << x;
    EXPECT_EQ(keyAt(x, 687), r1) << x;
    EXPECT_EQ(keyAt(x, 741), r1) << x;
    EXPECT_EQ(keyAt(x, 742), r2) << x;
  }
  // Every pixel of the keyboard band (inside the margins) types a key.
  for (int y = KEYBOARD_TOP; y < KEYBOARD_BOTTOM; y++) {
    for (int x = GRID_LEFT; x < GRID_RIGHT; x++) ASSERT_GE(keyAt(x, y), 0) << x << "," << y;
  }
  EXPECT_EQ(keyAt(240, 631), -1);
  EXPECT_EQ(keyAt(240, 796), -1);
  EXPECT_EQ(keyAt(GRID_LEFT, 640), 0);  // Q's 1 px inset
  EXPECT_EQ(keyAt(5, 700), -1);         // the bezel
  // Row 2's half-key margins belong to A and L.
  EXPECT_EQ(keyAt(GRID_LEFT, 700), 10);
  EXPECT_EQ(keyAt(GRID_RIGHT - 1, 700), 18);
  EXPECT_EQ(keyAt(GRID_RIGHT, 700), -1);
}

// ---- layout -------------------------------------------------------------------------------------

TEST(CrosswordLayout, EverySizeFits) {
  for (int w = MIN_SIDE; w <= MAX_SIDE; w++) {
    for (int h = MIN_SIDE; h <= MAX_SIDE; h++) {
      SCOPED_TRACE(std::to_string(w) + "x" + std::to_string(h));
      const ScreenLayout l = computeLayout(w, h);
      int cell = (GRID_RIGHT - GRID_LEFT) / w;
      if ((GRID_BOTTOM - GRID_TOP) / h < cell) cell = (GRID_BOTTOM - GRID_TOP) / h;
      if (cell > MAX_CELL_PX) cell = MAX_CELL_PX;
      ASSERT_EQ(l.cell, cell);
      EXPECT_GE(l.cell, 29);
      EXPECT_GE(l.grid.x, GRID_LEFT);
      EXPECT_LE(l.grid.right(), GRID_RIGHT);
      EXPECT_GE(l.grid.y, GRID_TOP);
      EXPECT_LE(l.grid.bottom(), GRID_BOTTOM);
      EXPECT_LE(std::abs((l.grid.x - GRID_LEFT) - (GRID_RIGHT - l.grid.right())), 1);
      EXPECT_LE(std::abs((l.grid.y - GRID_TOP) - (GRID_BOTTOM - l.grid.bottom())), 1);
      EXPECT_LT(l.grid.bottom(), l.clueBar.y);
      // A number and a letter share a square without touching (nominal metrics).
      const CellStyle& s = l.style;
      const int numberBottom = s.numberY + (s.tinyDigits ? DIGIT_H : SMALL_DIGIT_PX);
      const int letterTop = s.letterBottom - LETTER_CAP_PX[s.letterIndex] + 1;
      EXPECT_LT(numberBottom, letterTop);
      EXPECT_LE(LETTER_W_PX[s.letterIndex], l.cell - 2 * INNER_LINE);
      EXPECT_EQ(s.tinyDigits, l.cell < TINY_DIGIT_CELL);
      // The last square's centre is a Cell target; so is the margin out to the bezel.
      const Rect last = l.cellRect(w * h - 1);
      const Target t = targetAt(l, false, last.x + last.w / 2, last.y + last.h / 2);
      EXPECT_EQ(t.kind, TargetKind::Cell);
      EXPECT_EQ(t.index, w * h - 1);
    }
  }
  EXPECT_EQ(computeLayout(5, 5).cell, 72);
  EXPECT_EQ(computeLayout(7, 7).cell, 62);
  EXPECT_EQ(computeLayout(9, 9).cell, 48);
  EXPECT_EQ(computeLayout(15, 15).cell, 29);
  EXPECT_EQ(computeLayout(5, 5).style.letterPoints, 18);
  EXPECT_EQ(computeLayout(9, 9).style.letterPoints, 16);
  EXPECT_EQ(computeLayout(11, 11).style.letterPoints, 14);
  EXPECT_EQ(computeLayout(15, 15).style.letterPoints, 12);
  EXPECT_TRUE(computeLayout(15, 15).style.tinyDigits);
  EXPECT_FALSE(computeLayout(11, 11).style.tinyDigits);
}

TEST(CrosswordLayout, BarAndBannerTargets) {
  const ScreenLayout l = computeLayout(5, 5);
  EXPECT_EQ(l.prevButton.x, 7);
  EXPECT_EQ(l.prevButton.right(), 55);
  EXPECT_EQ(l.nextButton.x, 425);
  EXPECT_EQ(l.nextButton.right(), 473);
  EXPECT_EQ(l.clueText.x, 61);
  EXPECT_EQ(l.clueText.right(), 419);
  EXPECT_EQ(l.clueBar.y, 556);
  EXPECT_EQ(l.clueBar.bottom(), 628);
  EXPECT_EQ(targetAt(l, false, 30, 590).kind, TargetKind::PrevClue);
  EXPECT_EQ(targetAt(l, false, 450, 590).kind, TargetKind::NextClue);
  EXPECT_EQ(targetAt(l, false, 240, 590).kind, TargetKind::ClueText);
  EXPECT_EQ(targetAt(l, false, 58, 590).kind, TargetKind::None);
  const Key& q = keyboardKey(0);
  Target t = targetAt(l, false, q.rect.x + 5, q.rect.y + 5);
  EXPECT_EQ(t.kind, TargetKind::Key);
  EXPECT_EQ(t.index, 0);
  // The banner hides the bar and the keys.
  EXPECT_EQ(targetAt(l, true, q.rect.x + 5, q.rect.y + 5).kind, TargetKind::None);
  EXPECT_EQ(targetAt(l, true, 30, 590).kind, TargetKind::None);
  const Rect& b = l.bannerButton;
  EXPECT_EQ(targetAt(l, true, b.x + 1, b.y + 1).kind, TargetKind::BannerButton);
  EXPECT_EQ(targetAt(l, false, b.x + 1, b.y + 1).kind, TargetKind::Key);
  EXPECT_GE(b.y, l.banner.y);
  EXPECT_LE(b.bottom(), l.banner.bottom());
  // A tap needs both ends on the same target.
  t = tapTarget(l, false, q.rect.x + 2, q.rect.y + 2, q.rect.x + 30, q.rect.y + 40);
  EXPECT_EQ(t.kind, TargetKind::Key);
  const Key& w = keyboardKey(1);
  EXPECT_EQ(tapTarget(l, false, q.rect.x + 2, q.rect.y + 2, w.rect.x + 2, w.rect.y + 2).kind, TargetKind::None);
  int c0 = -1;
  ASSERT_TRUE(l.cellAt(l.grid.x + 1, l.grid.y + 1, c0));
  EXPECT_EQ(c0, 0);
  ASSERT_TRUE(l.cellNear(l.grid.x - l.cell / 2, l.grid.y + 1, c0));
  EXPECT_EQ(c0, 0);
  EXPECT_FALSE(l.cellNear(l.grid.x - l.cell / 2 - 1, l.grid.y + 1, c0));
}

// ---- digits -------------------------------------------------------------------------------------

TEST(CrosswordDigits, FiveBySevenTable) {
  std::set<std::vector<uint8_t>> shapes;
  for (int d = 0; d < 10; d++) {
    std::vector<uint8_t> rows(DIGITS_5X7[d], DIGITS_5X7[d] + DIGIT_H);
    EXPECT_TRUE(shapes.insert(rows).second) << d;
    bool top = false;
    bool bottom = false;
    for (int x = 0; x < DIGIT_W; x++) {
      top = top || digitPixel(d, x, 0);
      bottom = bottom || digitPixel(d, x, DIGIT_H - 1);
    }
    EXPECT_TRUE(top && bottom) << d;  // every digit spans the full 7 rows
    for (int y = 0; y < DIGIT_H; y++) EXPECT_EQ(DIGITS_5X7[d][y] & ~0x1F, 0);
  }
  EXPECT_FALSE(digitPixel(10, 0, 0));
  EXPECT_FALSE(digitPixel(1, 5, 0));
  EXPECT_EQ(numberWidth(7), 5);
  EXPECT_EQ(numberWidth(42), 11);
  EXPECT_EQ(numberWidth(100), 17);
  EXPECT_EQ(numberWidth(0), 0);
  uint8_t out[3];
  ASSERT_EQ(numberDigits(207, out), 3);
  EXPECT_EQ(out[0], 2);
  EXPECT_EQ(out[1], 0);
  EXPECT_EQ(out[2], 7);
  EXPECT_EQ(numberDigits(0, out), 0);
  // The widest 15x15 number fits a 29 px square beside the inset.
  EXPECT_LE(computeLayout(15, 15).style.numberX + numberWidth(99), 29 - 2);
}

// ---- saves --------------------------------------------------------------------------------------

TEST(CrosswordSave, PrefsRoundTripAndDefaults) {
  Prefs prefs;
  std::snprintf(prefs.current, sizeof(prefs.current), "%s", "/Puzzles/Crossword/Sunday/one.ipuz");
  prefs.skipFilled = false;
  std::snprintf(prefs.collection, sizeof(prefs.collection), "%s", "pack:Sunday");
  prefs.seq = 4000000000u;
  char text[PREFS_TEXT_MAX];
  const size_t n = formatPrefs(prefs, text, sizeof(text));
  ASSERT_GT(n, 0u);
  Prefs back;
  ASSERT_TRUE(parsePrefs(text, n, back));
  EXPECT_STREQ(back.current, prefs.current);
  EXPECT_FALSE(back.skipFilled);
  EXPECT_STREQ(back.collection, "pack:Sunday");
  EXPECT_EQ(back.seq, 4000000000u);
  Prefs fresh;
  resetPrefs(fresh);
  const size_t m = formatPrefs(fresh, text, sizeof(text));
  EXPECT_EQ(std::string(text, m), "CP1\ncurrent -\nskip 1\ncollection -\nseq 0\nend\n");
  ASSERT_TRUE(parsePrefs(text, m, back));
  EXPECT_EQ(back.current[0], '\0');
  EXPECT_TRUE(back.skipFilled);
  // Every truncation is "no save", and leaves the defaults.
  const size_t full = formatPrefs(prefs, text, sizeof(text));
  for (size_t cut = 0; cut + 1 < full; cut++) {
    EXPECT_FALSE(parsePrefs(text, cut, back)) << cut;
    EXPECT_TRUE(back.skipFilled);
  }
  EXPECT_FALSE(parsePrefs("CP1\ncurrent -\nskip 2\ncollection -\nseq 0\nend\n", 44, back));
  EXPECT_FALSE(parsePrefs(nullptr, 0, back));
}

TEST(CrosswordSave, ProgressRoundTrip) {
  Game g;
  g.cursor(2, 0, ACROSS);
  for (const char c : std::string("RXV")) typeLetter(*g.p, *g.prog, c, true);
  check(*g.p, *g.prog, Scope::Word);
  g.cursor(0, 4, DOWN);
  reveal(*g.p, *g.prog, Scope::Letter);
  g.prog->elapsed = 754;
  g.prog->seq = 17;
  char text[PROGRESS_TEXT_MAX];
  const size_t n = formatProgress(*g.prog, text, sizeof(text));
  ASSERT_GT(n, 0u);
  EXPECT_NE(std::string(text, n).find("row RXV..\n"), std::string::npos);
  EXPECT_NE(std::string(text, n).find("flags .w...\n"), std::string::npos);
  EXPECT_NE(std::string(text, n).find("cursor 0 4 D\n"), std::string::npos);
  auto back = newProgress();
  ASSERT_TRUE(parseProgress(text, n, *back));
  EXPECT_EQ(std::memcmp(back->fill, g.prog->fill, MAX_CELLS), 0);
  EXPECT_EQ(std::memcmp(back->flags, g.prog->flags, MAX_CELLS), 0);
  EXPECT_EQ(back->cursor, g.prog->cursor);
  EXPECT_EQ(back->dir, DOWN);
  EXPECT_EQ(back->elapsed, 754u);
  EXPECT_EQ(back->checks, 1);
  EXPECT_EQ(back->reveals, 1);
  EXPECT_EQ(back->seq, 17u);
  EXPECT_FALSE(back->solved);
  EXPECT_STREQ(back->sourceKey, "builtin:sample-shelter");
  EXPECT_TRUE(progressMatches(*g.p, *back));
}

TEST(CrosswordSave, LargestProgressFits) {
  auto p = newPuzzle();
  ASSERT_EQ(loadIpuz(fixture("fifteen.ipuz"), *p).error, Error::None);
  std::snprintf(p->sourceKey, sizeof(p->sourceKey), "%s", std::string(MAX_SOURCE_KEY, 'k').c_str());
  auto prog = newProgress();
  resetProgress(*p, *prog);
  for (int i = 0; i < p->cells(); i++) {
    if (!p->isBlock(i)) prog->fill[i] = 'W';
  }
  prog->elapsed = ELAPSED_MAX;
  prog->checks = 65535;
  prog->reveals = 65535;
  prog->seq = 0xFFFFFFFFu;
  char text[PROGRESS_TEXT_MAX];
  EXPECT_GT(formatProgress(*prog, text, sizeof(text)), 0u);
}

TEST(CrosswordSave, ProgressTruncationAndCorruption) {
  Game g;
  g.cursor(1, 1, ACROSS);
  typeLetter(*g.p, *g.prog, 'C', true);
  char text[PROGRESS_TEXT_MAX];
  const size_t n = formatProgress(*g.prog, text, sizeof(text));
  ASSERT_GT(n, 0u);
  auto back = newProgress();
  for (size_t cut = 0; cut + 1 < n; cut++) EXPECT_FALSE(parseProgress(text, cut, *back)) << cut;
  const std::string good(text, n);
  const auto broken = [&](const std::string& from, const std::string& to) {
    std::string s = good;
    const size_t at = s.find(from);
    if (at == std::string::npos) {
      ADD_FAILURE() << "no " << from;
      return false;
    }
    s.replace(at, from.size(), to);
    return !parseProgress(s.data(), s.size(), *back);
  };
  EXPECT_TRUE(broken("CW1", "CW2"));
  EXPECT_TRUE(broken("size 5 5", "size 5 16"));
  EXPECT_TRUE(broken("cursor 1 2 A", "cursor 0 0 A"));  // a block
  EXPECT_TRUE(broken("cursor 1 2 A", "cursor 1 2 X"));
  EXPECT_TRUE(broken("row #C...", "row #c..."));
  EXPECT_TRUE(broken("flags .....\nflags .....\n", "flags w....\nflags .....\n"));  // a flag on a block
  EXPECT_TRUE(broken("flags .....\nflags .....\n", "flags .....\nflags ..w..\n"));  // a flag on an empty
  EXPECT_TRUE(broken("end\n", "end\nmore\n"));
  EXPECT_TRUE(broken("fnv ", "fnv x"));
  EXPECT_FALSE(broken("elapsed 0", "elapsed 12"));
}

TEST(CrosswordSave, ProgressRandomMutations) {
  Game g;
  g.fillAllBut({{0, 2}});
  g.prog->cursor = static_cast<uint8_t>(g.at(2, 2));
  check(*g.p, *g.prog, Scope::Puzzle);
  char text[PROGRESS_TEXT_MAX];
  const size_t n = formatProgress(*g.prog, text, sizeof(text));
  ASSERT_GT(n, 0u);
  std::mt19937 rng(7);
  auto back = newProgress();
  char again[PROGRESS_TEXT_MAX];
  for (int i = 0; i < 3000; i++) {
    std::string s(text, n);
    s[rng() % s.size()] = static_cast<char>(rng() & 0xFF);
    if (!parseProgress(s.data(), s.size(), *back) || s.find('\r') != std::string::npos) continue;
    // Anything that parses re-formats to itself (a canonical, self-consistent save).
    const size_t m = formatProgress(*back, again, sizeof(again));
    ASSERT_GT(m, 0u);
    EXPECT_EQ(std::string(again, m), s);
  }
}

TEST(CrosswordSave, ProgressMustMatchItsPuzzle) {
  Game g;
  auto other = newProgress();
  *other = *g.prog;
  EXPECT_TRUE(progressMatches(*g.p, *other));
  other->fnv ^= 1;
  EXPECT_FALSE(progressMatches(*g.p, *other));
  *other = *g.prog;
  other->w = 7;
  EXPECT_FALSE(progressMatches(*g.p, *other));
  *other = *g.prog;
  other->fill[g.at(0, 0)] = EMPTY;  // a block that is not one
  EXPECT_FALSE(progressMatches(*g.p, *other));
  *other = *g.prog;
  other->fill[g.at(1, 1)] = 'Q';
  other->flags[g.at(1, 1)] = FLAG_REVEALED;  // revealed but not the answer
  EXPECT_FALSE(progressMatches(*g.p, *other));
  *other = *g.prog;
  other->cursor = static_cast<uint8_t>(g.at(0, 0));
  EXPECT_FALSE(progressMatches(*g.p, *other));
}

TEST(CrosswordSave, PathsAndPruning) {
  char path[64];
  ASSERT_GT(progressPath(0x0badf00du, path, sizeof(path)), 0u);
  EXPECT_STREQ(path, "/.crosspoint/crossword/progress/0badf00d.dat");
  EXPECT_EQ(progressPath(1, path, 10), 0u);
  uint32_t fnv = 0;
  EXPECT_TRUE(progressFileFnv("0badf00d.dat", fnv));
  EXPECT_EQ(fnv, 0x0badf00du);
  EXPECT_FALSE(progressFileFnv("0BADF00D.dat", fnv));
  EXPECT_FALSE(progressFileFnv("0badf00d.tmp", fnv));
  EXPECT_FALSE(progressFileFnv("badf00d.dat", fnv));
  EXPECT_FALSE(progressFileFnv(nullptr, fnv));

  std::vector<SavedProgress> saved;
  for (int i = 0; i < MAX_PROGRESS_FILES; i++) saved.push_back({static_cast<uint32_t>(1000 + i), 50u + i});
  EXPECT_EQ(pruneVictim(saved.data(), static_cast<int>(saved.size()), 0), -1);
  saved.push_back({7, 10});                                                    // the current puzzle, oldest seq
  EXPECT_EQ(pruneVictim(saved.data(), static_cast<int>(saved.size()), 7), 0);  // seq 50, not the current
  EXPECT_EQ(pruneVictim(saved.data(), static_cast<int>(saved.size()), 1000), 40);
}

TEST(CrosswordSave, SolvedList) {
  char line[SOLVED_LINE_MAX + 1];
  const size_t n = formatSolvedLine(0x3a9804f4u, "builtin:mini-001", line, sizeof(line));
  EXPECT_EQ(std::string(line, n), "3a9804f4 builtin:mini-001\n");
  EXPECT_EQ(formatSolvedLine(1, "", line, sizeof(line)), 0u);
  EXPECT_EQ(formatSolvedLine(1, "a\nb", line, sizeof(line)), 0u);
  EXPECT_GT(formatSolvedLine(1, std::string(MAX_SOURCE_KEY, 'k').c_str(), line, sizeof(line)), 0u);
  uint32_t fnv = 0;
  std::string_view key;
  ASSERT_TRUE(parseSolvedLine("3a9804f4 builtin:mini-001\r", fnv, key));
  EXPECT_EQ(fnv, 0x3a9804f4u);
  EXPECT_EQ(key, "builtin:mini-001");
  EXPECT_FALSE(parseSolvedLine("3a9804f4", fnv, key));
  EXPECT_FALSE(parseSolvedLine("3a9804fg x", fnv, key));
  const std::string list = "00000001 builtin:a\ngarbage\n00000002 /Puzzles/Crossword/b.puz\n";
  EXPECT_TRUE(solvedListHas(list.data(), list.size(), "builtin:a"));
  EXPECT_TRUE(solvedListHas(list.data(), list.size(), "/Puzzles/Crossword/b.puz"));
  EXPECT_FALSE(solvedListHas(list.data(), list.size(), "builtin:b"));
  EXPECT_TRUE(solvedListHas(list.data(), list.size(), "", 2));
  EXPECT_FALSE(solvedListHas(list.data(), list.size(), "", 3));
  EXPECT_FALSE(solvedListHas(list.data(), list.size(), "", 0));
  // Key and fnv together: a built-in id given a different puzzle is not solved by the old line.
  EXPECT_TRUE(solvedListHas(list.data(), list.size(), "builtin:a", 1));
  EXPECT_FALSE(solvedListHas(list.data(), list.size(), "builtin:a", 7));
  EXPECT_FALSE(solvedListHas(list.data(), list.size(), "builtin:b", 2));
  EXPECT_EQ(solvedKeepOffset(list.data(), list.size(), 10), 0u);
  EXPECT_EQ(list.substr(solvedKeepOffset(list.data(), list.size(), 2)), "garbage\n00000002 /Puzzles/Crossword/b.puz\n");
  EXPECT_EQ(list.substr(solvedKeepOffset(list.data(), list.size(), 1)), "00000002 /Puzzles/Crossword/b.puz\n");
}
