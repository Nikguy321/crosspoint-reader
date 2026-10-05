// Word Search's pure half on the host: PRNG, generator, themes, theme files, selection,
// layout, the save codec and the downloadable theme packs (packs/wordsearch). The drawing and
// its real-font checks are in test/word_search_preview.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "WordSearch.h"

using namespace ws;

namespace {

constexpr Difficulty ALL_DIFFICULTIES[] = {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard};

std::vector<std::string_view> themeWords(const size_t index, ThemeWords& out) {
  EXPECT_TRUE(loadBuiltinTheme(builtinTheme(index), out));
  return out.words;
}

std::unique_ptr<Puzzle> generated(const size_t themeIndex, const Difficulty d, const uint32_t seed) {
  ThemeWords theme;
  const auto words = themeWords(themeIndex, theme);
  auto p = std::make_unique<Puzzle>();
  if (!generatePuzzle(words.data(), words.size(), d, seed, *p)) return nullptr;
  std::snprintf(p->themeKey, sizeof(p->themeKey), "%s", builtinTheme(themeIndex).key);
  std::snprintf(p->themeTitle, sizeof(p->themeTitle), "%s", builtinTheme(themeIndex).title);
  return p;
}

std::string gridText(const Puzzle& p) {
  std::string s;
  for (int r = 0; r < p.size; r++) s.append(p.grid + r * MAX_GRID, p.size);
  return s;
}

std::string reversed(std::string s) {
  std::reverse(s.begin(), s.end());
  return s;
}

// Checks every rule a generated puzzle must keep.
void expectValidPuzzle(const Puzzle& p, const Difficulty d) {
  const DifficultySpec& spec = specFor(d);
  ASSERT_EQ(p.size, spec.size);
  EXPECT_GE(p.wordCount, minimumWords(d));
  EXPECT_LE(p.wordCount, spec.words);
  for (int r = 0; r < p.size; r++) {
    for (int c = 0; c < p.size; c++) {
      ASSERT_GE(p.at(r, c), 'A');
      ASSERT_LE(p.at(r, c), 'Z');
    }
  }
  for (int k = 0; k < p.wordCount; k++) {
    const PuzzleWord& w = p.words[k];
    const int n = static_cast<int>(std::strlen(w.letters));
    EXPECT_GE(n, spec.minLetters) << w.display;
    EXPECT_LE(n, spec.maxLetters) << w.display;
    EXPECT_EQ(w.place.len, n);
    EXPECT_TRUE(spec.dirMask & (1u << w.place.dir)) << "direction " << int(w.place.dir) << " on " << int(d);
    for (int i = 0; i < n; i++) {
      EXPECT_EQ(p.at(w.place.row + DIR_DR[w.place.dir] * i, w.place.col + DIR_DC[w.place.dir] * i), w.letters[i]);
    }
    EXPECT_EQ(countOccurrences(p, w.letters), 1) << w.display;
    EXPECT_FALSE(w.found);
    if (k > 0) {
      EXPECT_LT(std::strcmp(p.words[k - 1].display, w.display), 0) << "listed A-Z";
    }
    for (int j = 0; j < p.wordCount; j++) {
      if (j == k) continue;
      const std::string a = w.letters;
      const std::string b = p.words[j].letters;
      EXPECT_EQ(b.find(a), std::string::npos) << a << " inside " << b;
      EXPECT_EQ(reversed(b).find(a), std::string::npos) << a << " inside reversed " << b;
    }
  }
  EXPECT_TRUE(validatePuzzle(p));
}

}  // namespace

// ---- model / PRNG --------------------------------------------------------------------------------

TEST(WordSearchModel, DirectionsAreReversible) {
  for (int d = 0; d < 8; d++) {
    EXPECT_EQ(DIR_DR[d ^ 4], -DIR_DR[d]);
    EXPECT_EQ(DIR_DC[d ^ 4], -DIR_DC[d]);
    EXPECT_EQ(dirIndex(DIR_DR[d], DIR_DC[d]), d);
  }
  EXPECT_EQ(dirIndex(0, 0), -1);
  EXPECT_EQ(dirIndex(2, 0), -1);
}

TEST(WordSearchModel, AlignedAndLineCells) {
  EXPECT_TRUE(aligned(makeCell(0, 0), makeCell(0, 5)));
  EXPECT_TRUE(aligned(makeCell(3, 3), makeCell(0, 0)));
  EXPECT_TRUE(aligned(makeCell(3, 3), makeCell(6, 0)));
  EXPECT_FALSE(aligned(makeCell(0, 0), makeCell(1, 2)));
  EXPECT_FALSE(aligned(makeCell(2, 2), makeCell(2, 2)));
  EXPECT_FALSE(aligned(Cell{}, makeCell(2, 2)));
  EXPECT_EQ(lineCells(makeCell(1, 1), makeCell(1, 1)), 1);
  EXPECT_EQ(lineCells(makeCell(0, 0), makeCell(4, 4)), 5);
  EXPECT_EQ(lineCells(makeCell(0, 0), makeCell(1, 2)), 0);
}

TEST(WordSearchModel, GridLettersDropsSeparators) {
  char out[16];
  EXPECT_EQ(gridLetters("POLAR BEAR", 10, out, sizeof(out)), 9u);
  EXPECT_STREQ(out, "POLARBEAR");
  EXPECT_EQ(gridLetters("S'MORES", 7, out, sizeof(out)), 6u);
  EXPECT_STREQ(out, "SMORES");
  EXPECT_EQ(gridLetters("JACK-IN-THE-BOX", 15, out, sizeof(out)), 12u);
  EXPECT_EQ(gridLetters("bear", 4, out, sizeof(out)), 0u) << "lower case is not normalised here";
  EXPECT_EQ(gridLetters("R2D2", 4, out, sizeof(out)), 0u);
  EXPECT_EQ(gridLetters("ABCDEFGHIJKLMNOP", 16, out, sizeof(out)), 0u) << "does not fit";
}

TEST(WordSearchModel, FormatElapsed) {
  char buf[16];
  EXPECT_EQ(formatElapsed(0, buf, sizeof(buf)), 4u);
  EXPECT_STREQ(buf, "0:00");
  formatElapsed(392, buf, sizeof(buf));
  EXPECT_STREQ(buf, "6:32");
  formatElapsed(3723, buf, sizeof(buf));
  EXPECT_STREQ(buf, "1:02:03");
  EXPECT_EQ(formatElapsed(392, buf, 4), 0u);
}

TEST(WordSearchRandom, SplitMix32IsDeterministicAndBounded) {
  uint32_t a = 0;
  uint32_t b = 0;
  for (int i = 0; i < 100; i++) EXPECT_EQ(nextRandom(a), nextRandom(b));
  uint32_t s = 0;
  const uint32_t first = nextRandom(s);
  EXPECT_NE(first, 0u) << "seed 0 is well mixed";
  uint32_t r = 12345;
  for (int i = 0; i < 1000; i++) EXPECT_LT(randomBelow(r, 7), 7u);
  EXPECT_EQ(randomBelow(r, 0), 0u);
}

// ---- built-in themes -----------------------------------------------------------------------------

TEST(WordSearchThemes, AtLeast24ThemesOf30WordsEachAllValid) {
  ASSERT_GE(builtinThemeCount(), 24u);
  std::set<std::string> keys;
  for (size_t i = 0; i < builtinThemeCount(); i++) {
    const BuiltinTheme& t = builtinTheme(i);
    SCOPED_TRACE(t.key);
    EXPECT_TRUE(keys.insert(t.key).second) << "duplicate key";
    EXPECT_EQ(findBuiltinTheme(t.key), static_cast<int>(i));
    EXPECT_LE(std::strlen(t.key), static_cast<size_t>(MAX_THEME_KEY));
    EXPECT_STRNE(t.key, RANDOM_CHOICE);
    EXPECT_EQ(fileNameFromKey(t.key), nullptr);
    EXPECT_GT(std::strlen(t.title), 0u);
    EXPECT_LE(std::strlen(t.title), static_cast<size_t>(MAX_TITLE_LEN));
    ThemeWords theme;
    ASSERT_TRUE(loadBuiltinTheme(t, theme));
    EXPECT_STREQ(theme.title, t.title);
    EXPECT_GE(theme.words.size(), 30u);
    std::set<std::string> letters;
    for (const auto w : theme.words) {
      char norm[MAX_DISPLAY_LEN + 1];
      const size_t n = normalizeWordLine(w.data(), w.size(), norm, sizeof(norm));
      EXPECT_EQ(std::string_view(norm, n), w) << "not a valid, normalised word: " << w;
      char grid[MAX_WORD_LETTERS + 1];
      ASSERT_GT(gridLetters(w.data(), w.size(), grid, sizeof(grid)), 0u) << w;
      EXPECT_TRUE(letters.insert(grid).second) << "duplicate " << w;
    }
  }
  EXPECT_EQ(findBuiltinTheme("nope"), -1);
}

TEST(WordSearchThemes, EveryThemeFillsEveryDifficulty) {
  for (size_t i = 0; i < builtinThemeCount(); i++) {
    for (const Difficulty d : ALL_DIFFICULTIES) {
      int full = 0;
      for (uint32_t seed = 1; seed <= 12; seed++) {
        auto p = generated(i, d, seed * 7919u);
        ASSERT_NE(p, nullptr) << builtinTheme(i).key << " difficulty " << int(d) << " seed " << seed;
        expectValidPuzzle(*p, d);
        full += p->wordCount == specFor(d).words ? 1 : 0;
        if (d == Difficulty::Easy) EXPECT_EQ(p->wordCount, 8) << builtinTheme(i).key << " Easy places all 8";
      }
      EXPECT_GE(full, 10) << builtinTheme(i).key << " difficulty " << int(d) << ": mostly full puzzles";
    }
  }
}

// ---- generator -----------------------------------------------------------------------------------

TEST(WordSearchGenerator, DeterministicPerSeed) {
  for (const Difficulty d : ALL_DIFFICULTIES) {
    auto a = generated(0, d, 42);
    auto b = generated(0, d, 42);
    auto c = generated(0, d, 43);
    ASSERT_TRUE(a && b && c);
    EXPECT_EQ(gridText(*a), gridText(*b));
    ASSERT_EQ(a->wordCount, b->wordCount);
    for (int k = 0; k < a->wordCount; k++) {
      EXPECT_STREQ(a->words[k].display, b->words[k].display);
      EXPECT_EQ(a->words[k].place.row, b->words[k].place.row);
      EXPECT_EQ(a->words[k].place.col, b->words[k].place.col);
      EXPECT_EQ(a->words[k].place.dir, b->words[k].place.dir);
    }
    EXPECT_NE(gridText(*a), gridText(*c)) << "another seed, another puzzle";
    EXPECT_EQ(a->seed, 42u);
  }
}

TEST(WordSearchGenerator, PinnedPuzzleDoesNotDrift) {
  // A fixed seed must give the same puzzle on every build and on the device (the bench's
  // "WS new <seed>" relies on it): no std::random, no unstable sort order. If the generator
  // or the Animals list changes on purpose, update this fingerprint (FNV-1a of the grid).
  auto p = generated(static_cast<size_t>(findBuiltinTheme("animals")), Difficulty::Medium, 1234);
  ASSERT_NE(p, nullptr);
  uint32_t h = 2166136261u;
  for (const char c : gridText(*p)) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  EXPECT_EQ(h, 691261517u);
}

TEST(WordSearchGenerator, PlacementDirectionsPerDifficulty) {
  // Over many puzzles every allowed direction shows up and no other does.
  for (const Difficulty d : ALL_DIFFICULTIES) {
    uint8_t seen = 0;
    for (uint32_t seed = 0; seed < 40; seed++) {
      auto p = generated(seed % builtinThemeCount(), d, seed);
      ASSERT_NE(p, nullptr);
      for (int k = 0; k < p->wordCount; k++) seen |= static_cast<uint8_t>(1u << p->words[k].place.dir);
    }
    EXPECT_EQ(seen, specFor(d).dirMask) << "difficulty " << int(d);
  }
}

TEST(WordSearchGenerator, ManySeedsStayValid) {
  for (uint32_t seed = 0; seed < 150; seed++) {
    const Difficulty d = ALL_DIFFICULTIES[seed % 3];
    auto p = generated((seed * 5) % builtinThemeCount(), d, seed * 2654435761u);
    ASSERT_NE(p, nullptr) << seed;
    expectValidPuzzle(*p, d);
  }
}

TEST(WordSearchGenerator, CrossingsShareLettersAndNeverRunAlongAWord) {
  int crossings = 0;
  for (uint32_t seed = 0; seed < 60; seed++) {
    auto p = generated(seed % builtinThemeCount(), Difficulty::Hard, seed);
    ASSERT_NE(p, nullptr);
    std::vector<int> owner(MAX_GRID * MAX_GRID, -1);
    for (int k = 0; k < p->wordCount; k++) {
      const Placement& pl = p->words[k].place;
      for (int i = 0; i < pl.len; i++) {
        const int cell = (pl.row + DIR_DR[pl.dir] * i) * MAX_GRID + pl.col + DIR_DC[pl.dir] * i;
        if (owner[cell] >= 0) {
          crossings++;
          EXPECT_NE(p->words[owner[cell]].place.dir % 4, pl.dir % 4) << "two words on one line";
        }
        owner[cell] = k;
      }
    }
  }
  EXPECT_GT(crossings, 0) << "words do cross";
}

TEST(WordSearchGenerator, DropsSubstringsAndDuplicates) {
  const std::string_view words[] = {"CAT",  "CATERPILLAR", "TAC",   "DOG",   "DOG",  "BIRD", "FISH", "FROG",
                                    "TOAD", "HORSE",       "MOUSE", "SNAKE", "GOAT", "PIG",  "HEN",  "COW"};
  auto p = std::make_unique<Puzzle>();
  for (uint32_t seed = 0; seed < 30; seed++) {
    ASSERT_TRUE(generatePuzzle(words, std::size(words), Difficulty::Easy, seed, *p));
    std::set<std::string> letters;
    for (int k = 0; k < p->wordCount; k++) EXPECT_TRUE(letters.insert(p->words[k].letters).second);
    const bool cat = letters.count("CAT") > 0;
    EXPECT_FALSE(cat && letters.count("CATERPILLAR")) << "CAT is inside CATERPILLAR";
    EXPECT_FALSE(cat && letters.count("TAC")) << "TAC is CAT reversed";
  }
}

TEST(WordSearchGenerator, PalindromesCountOnce) {
  const std::string_view words[] = {"KAYAK", "LEVEL", "RADAR", "CIVIC", "REFER", "ROTOR", "MADAM", "SOLOS", "STATS"};
  auto p = std::make_unique<Puzzle>();
  ASSERT_TRUE(generatePuzzle(words, std::size(words), Difficulty::Easy, 7, *p));
  for (int k = 0; k < p->wordCount; k++) EXPECT_EQ(countOccurrences(*p, p->words[k].letters), 1);
}

TEST(WordSearchGenerator, TooFewWordsFails) {
  const std::string_view words[] = {"CAT", "DOG", "COW"};
  auto p = std::make_unique<Puzzle>();
  EXPECT_FALSE(generatePuzzle(words, std::size(words), Difficulty::Easy, 1, *p));
  EXPECT_FALSE(generatePuzzle(nullptr, 0, Difficulty::Easy, 1, *p));
  // Hard wants 4+ letters: three-letter words do not count.
  const std::string_view shortWords[] = {"CAT", "DOG", "COW", "PIG", "HEN", "EEL", "ANT", "BEE",
                                         "OWL", "YAK", "ELK", "EMU", "APE", "BAT", "RAM", "FOX"};
  EXPECT_FALSE(generatePuzzle(shortWords, std::size(shortWords), Difficulty::Hard, 1, *p));
  EXPECT_EQ(minimumWords(Difficulty::Easy), 6);
  EXPECT_EQ(minimumWords(Difficulty::Medium), 9);
  EXPECT_EQ(minimumWords(Difficulty::Hard), 12);
}

TEST(WordSearchGenerator, IgnoresUnusableEntries) {
  const std::string_view words[] = {"ab",     "TOOLONGFORANYGRIDATALL",
                                    "A1B2",   "MOOSE",
                                    "BISON",  "OTTER",
                                    "SKUNK",  "BADGER",
                                    "RABBIT", "BEAVER",
                                    "MARMOT", "WEASEL"};
  auto p = std::make_unique<Puzzle>();
  ASSERT_TRUE(generatePuzzle(words, std::size(words), Difficulty::Easy, 3, *p));
  for (int k = 0; k < p->wordCount; k++) {
    EXPECT_NE(std::string(p->words[k].display), "ab");
    EXPECT_NE(std::string(p->words[k].display), "A1B2");
  }
}

// ---- matching ------------------------------------------------------------------------------------

TEST(WordSearchMatch, ForwardsBackwardsAndAlreadyFound) {
  auto p = generated(static_cast<size_t>(findBuiltinTheme("birds")), Difficulty::Hard, 99);
  ASSERT_NE(p, nullptr);
  const Placement pl = p->words[0].place;
  const Line forward{pl.start(), pl.end()};
  const Line backward{pl.end(), pl.start()};
  MatchResult m = matchLine(*p, forward);
  EXPECT_EQ(m.kind, MatchKind::Found);
  EXPECT_EQ(m.word, 0);
  m = matchLine(*p, backward);
  EXPECT_EQ(m.kind, MatchKind::Found);
  p->hintWord = 0;
  markFound(*p, 0, backward);
  EXPECT_TRUE(p->words[0].found);
  EXPECT_EQ(p->hintWord, -1) << "finding the hinted word clears the hint";
  EXPECT_EQ(p->words[0].foundLine.a, pl.end());
  m = matchLine(*p, forward);
  EXPECT_EQ(m.kind, MatchKind::AlreadyFound);
  EXPECT_EQ(m.word, 0);
  // One cell short, one cell, and a knight's move do not match.
  const Cell shorter = makeCell(pl.end().row - DIR_DR[pl.dir], pl.end().col - DIR_DC[pl.dir]);
  EXPECT_EQ(matchLine(*p, Line{pl.start(), shorter}).kind, MatchKind::NoMatch);
  EXPECT_EQ(matchLine(*p, Line{pl.start(), pl.start()}).kind, MatchKind::NoMatch);
  EXPECT_EQ(matchLine(*p, Line{makeCell(0, 0), makeCell(1, 2)}).kind, MatchKind::NoMatch);
  EXPECT_TRUE(validatePuzzle(*p));
}

TEST(WordSearchMatch, AnyDirectionCountsOnEasy) {
  // Easy places only right/down, but a player who selects a word backwards still gets it.
  auto p = generated(0, Difficulty::Easy, 5);
  ASSERT_NE(p, nullptr);
  for (int k = 0; k < p->wordCount; k++) {
    const Placement pl = p->words[k].place;
    const MatchResult m = matchLine(*p, Line{pl.end(), pl.start()});
    EXPECT_EQ(m.kind, MatchKind::Found);
    EXPECT_EQ(m.word, k);
  }
}

TEST(WordSearchMatch, HintPicksUnfoundWords) {
  auto p = generated(1, Difficulty::Medium, 8);
  ASSERT_NE(p, nullptr);
  uint32_t rng = 1;
  for (int k = 1; k < p->wordCount; k++) markFound(*p, k, Line{p->words[k].place.start(), p->words[k].place.end()});
  for (int i = 0; i < 20; i++) EXPECT_EQ(pickHintWord(*p, rng), 0);
  markFound(*p, 0, Line{p->words[0].place.start(), p->words[0].place.end()});
  EXPECT_EQ(pickHintWord(*p, rng), -1);
  EXPECT_TRUE(p->complete());
}

// ---- theme files ---------------------------------------------------------------------------------

namespace {
bool parse(std::string text, ThemeWords& out, std::string& storage) {
  storage = std::move(text);
  return parseThemeFile(storage.data(), storage.size(), out);
}
std::vector<std::string> asStrings(const ThemeWords& t) {
  std::vector<std::string> v;
  for (const auto w : t.words) v.emplace_back(w);
  return v;
}
constexpr const char* EIGHT = "alpha\nbravo\ncharlie\ndelta\necho\nfoxtrot\ngolf\nhotel\n";
}  // namespace

TEST(WordSearchThemeFile, TitleCommentsBlanksAndCase) {
  ThemeWords t;
  std::string s;
  ASSERT_TRUE(parse("# my list\n\n  Garden Birds  \n# comment\nrobin\n\nBlue Tit\n" + std::string(EIGHT), t, s));
  EXPECT_STREQ(t.title, "Garden Birds");
  const auto words = asStrings(t);
  ASSERT_EQ(words.size(), 10u);
  EXPECT_EQ(words[0], "ROBIN");
  EXPECT_EQ(words[1], "BLUE TIT");
  EXPECT_EQ(words[2], "ALPHA");
}

TEST(WordSearchThemeFile, BomCrlfTabsAndSpaces) {
  ThemeWords t;
  std::string s;
  ASSERT_TRUE(parse("\xEF\xBB\xBFTitle\r\n\tpolar   bear \r\nsea-lion\r\nit's\r\n" + std::string(EIGHT), t, s));
  EXPECT_STREQ(t.title, "Title");
  const auto words = asStrings(t);
  EXPECT_EQ(words[0], "POLAR BEAR");
  EXPECT_EQ(words[1], "SEA-LION");
  EXPECT_EQ(words[2], "IT'S");
}

TEST(WordSearchThemeFile, SkipsLinesWithOtherCharacters) {
  ThemeWords t;
  std::string s;
  ASSERT_TRUE(parse("T\nr2d2\ncaf\xC3\xA9\nsnow\xE2\x9D\x84\nab\nabcdefghijklmnop\nok word\ntab\there\nhash#tag\n" +
                        std::string(EIGHT),
                    t, s));
  const auto words = asStrings(t);
  ASSERT_EQ(words.size(), 9u);
  EXPECT_EQ(words[0], "OK WORD");
}

TEST(WordSearchThemeFile, LengthLimits) {
  char out[MAX_DISPLAY_LEN + 1];
  EXPECT_EQ(normalizeWordLine("abc", 3, out, sizeof(out)), 3u);
  EXPECT_EQ(normalizeWordLine("ab", 2, out, sizeof(out)), 0u);
  EXPECT_EQ(normalizeWordLine("a b c", 5, out, sizeof(out)), 5u) << "3 letters, spaces kept";
  EXPECT_EQ(normalizeWordLine("abcdefghijklmno", 15, out, sizeof(out)), 15u);
  EXPECT_EQ(normalizeWordLine("abcdefghijklmnop", 16, out, sizeof(out)), 0u);
  EXPECT_EQ(normalizeWordLine("a-b-c-d-e-f-g-h-i-j-k", 21, out, sizeof(out)), 0u) << "display text too long";
  EXPECT_EQ(normalizeWordLine("   ", 3, out, sizeof(out)), 0u);
  EXPECT_EQ(normalizeWordLine("--'", 3, out, sizeof(out)), 0u);
}

TEST(WordSearchThemeFile, FewerThanEightWordsIsNotATheme) {
  ThemeWords t;
  std::string s;
  EXPECT_FALSE(parse("Title\none\ntwo\nthree\nfour\nfive\nsix\nseven\n", t, s));
  EXPECT_TRUE(parse("Title\none\ntwo\nthree\nfour\nfive\nsix\nseven\neight", t, s)) << "no final newline";
  EXPECT_FALSE(parse("", t, s));
  EXPECT_FALSE(parse("# only comments\n\n#\n", t, s));
  EXPECT_FALSE(parse(std::string(EIGHT), t, s)) << "the first line is the title: 7 words left";
}

TEST(WordSearchThemeFile, CapsWordsAndTitle) {
  std::string text = "A title that is very much longer than thirty-two bytes\n";
  for (int i = 0; i < 500; i++) text += "word\n";
  ThemeWords t;
  std::string s;
  ASSERT_TRUE(parse(text, t, s));
  EXPECT_EQ(t.words.size(), MAX_FILE_WORDS);
  EXPECT_EQ(std::strlen(t.title), static_cast<size_t>(MAX_TITLE_LEN));
  // A long UTF-8 title is cut on a code point.
  std::string accents = "T";
  for (int i = 0; i < 20; i++) accents += "\xC3\xA9";
  char title[MAX_TITLE_LEN + 1];
  ASSERT_TRUE(readThemeTitle(accents.data(), accents.size(), title, sizeof(title)));
  EXPECT_EQ(std::strlen(title), 31u);
  EXPECT_NE(static_cast<unsigned char>(title[30]), 0xC3);
}

TEST(WordSearchThemeFile, ReadTitleOnly) {
  char title[MAX_TITLE_LEN + 1];
  const std::string text = "\n# header\n  Kitchen Things \nspoon\n";
  ASSERT_TRUE(readThemeTitle(text.data(), text.size(), title, sizeof(title)));
  EXPECT_STREQ(title, "Kitchen Things");
  EXPECT_FALSE(readThemeTitle("# x\n\n", 5, title, sizeof(title)));
  // A truncated first read (the picker reads only the start) still gives the title.
  EXPECT_TRUE(readThemeTitle("Trees\nOA", 8, title, sizeof(title)));
  EXPECT_STREQ(title, "Trees");
}

TEST(WordSearchThemeFile, ParsedFileGeneratesAPuzzle) {
  std::string text = "Garden\n";
  const char* words[] = {"rose",  "tulip", "daisy", "fern", "moss", "hedge", "lawn",   "shed", "trowel",
                         "seeds", "pond",  "bench", "path", "gate", "fence", "shovel", "hose", "weeds"};
  for (const char* w : words) text += std::string(w) + "\n";
  ThemeWords t;
  std::string s;
  ASSERT_TRUE(parse(text, t, s));
  auto p = std::make_unique<Puzzle>();
  for (const Difficulty d : ALL_DIFFICULTIES) {
    ASSERT_TRUE(generatePuzzle(t.words.data(), t.words.size(), d, 11, *p));
    std::snprintf(p->themeKey, sizeof(p->themeKey), "file:garden.words");
    std::snprintf(p->themeTitle, sizeof(p->themeTitle), "%s", t.title);
    expectValidPuzzle(*p, d);
  }
}

TEST(WordSearchThemeFile, FileKeys) {
  char key[MAX_THEME_KEY + 1];
  ASSERT_TRUE(makeFileThemeKey("my words.words", key, sizeof(key)));
  EXPECT_STREQ(key, "file:my words.words");
  EXPECT_STREQ(fileNameFromKey(key), "my words.words");
  EXPECT_EQ(fileNameFromKey("animals"), nullptr);
  EXPECT_EQ(fileNameFromKey("file:"), nullptr);
  EXPECT_FALSE(makeFileThemeKey(std::string(60, 'a').c_str(), key, sizeof(key)));
  EXPECT_FALSE(makeFileThemeKey("", key, sizeof(key)));
}

// ---- snapping ------------------------------------------------------------------------------------

TEST(WordSearchSnap, EightDirectionsExact) {
  const Cell s = makeCell(7, 7);
  constexpr int C = 100;  // px a cell
  for (int d = 0; d < 8; d++) {
    for (int steps = 1; steps <= 5; steps++) {
      const Cell e = snapEnd(15, C, s, DIR_DC[d] * steps * C, DIR_DR[d] * steps * C);
      EXPECT_EQ(e, makeCell(7 + DIR_DR[d] * steps, 7 + DIR_DC[d] * steps)) << "dir " << d << " steps " << steps;
    }
  }
  EXPECT_EQ(snapEnd(15, C, s, 0, 0), s);
}

TEST(WordSearchSnap, OffAngleSnapsToTheNearestDirection) {
  const Cell s = makeCell(7, 7);
  constexpr int C = 100;
  // 20 degrees above horizontal (tan 20 = 0.364): still horizontal.
  EXPECT_EQ(snapEnd(15, C, s, 300, -109), makeCell(7, 10));
  // 25 degrees (tan 25 = 0.466): diagonal, up-right. Projection (300 + 140) / 2 = 220 -> 2 steps.
  EXPECT_EQ(snapEnd(15, C, s, 300, -140), makeCell(5, 9));
  // 70 degrees below horizontal to the left: vertical down.
  EXPECT_EQ(snapEnd(15, C, s, -110, 300), makeCell(10, 7));
  // A wobbly diagonal down-left.
  EXPECT_EQ(snapEnd(15, C, s, -290, 320), makeCell(10, 4));
}

TEST(WordSearchSnap, LengthRoundsToWholeSteps) {
  const Cell s = makeCell(0, 0);
  constexpr int C = 100;
  EXPECT_EQ(snapEnd(15, C, s, 149, 0), makeCell(0, 1));
  EXPECT_EQ(snapEnd(15, C, s, 150, 0), makeCell(0, 2));
  EXPECT_EQ(snapEnd(15, C, s, 40, 0), s) << "under half a cell is no step";
  // Diagonal: a step is sqrt 2 cells; (dx + dy) / 2 = 150 -> 2 steps (rounded half up).
  EXPECT_EQ(snapEnd(15, C, s, 150, 150), makeCell(2, 2));
  EXPECT_EQ(snapEnd(15, C, s, 140, 140), makeCell(1, 1));
}

TEST(WordSearchSnap, ClampsToTheGrid) {
  constexpr int C = 100;
  EXPECT_EQ(snapEnd(10, C, makeCell(5, 5), 2000, 0), makeCell(5, 9));
  EXPECT_EQ(snapEnd(10, C, makeCell(5, 5), -2000, 0), makeCell(5, 0));
  EXPECT_EQ(snapEnd(10, C, makeCell(5, 5), 0, -2000), makeCell(0, 5));
  // Diagonals stop where the first axis runs out, keeping the direction.
  EXPECT_EQ(snapEnd(10, C, makeCell(2, 6), 2000, 2000), makeCell(5, 9));
  EXPECT_EQ(snapEnd(10, C, makeCell(2, 6), -2000, -2000), makeCell(0, 4));
  EXPECT_EQ(snapEnd(10, C, makeCell(2, 6), 2000, -2000), makeCell(0, 8));
  EXPECT_EQ(snapEnd(10, C, makeCell(9, 0), -500, 500), makeCell(9, 0)) << "no room: the start itself";
}

TEST(WordSearchSnap, ThroughTheLayout) {
  const BoardLayout l = computeLayout(Difficulty::Medium);
  const Cell s = makeCell(3, 3);
  EXPECT_EQ(snapEndAt(l, s, l.centerX(8), l.centerY(3) + 5), makeCell(3, 8));
  EXPECT_EQ(snapEndAt(l, s, l.centerX(0) - 30, l.centerY(0) - 30), makeCell(0, 0));
  EXPECT_EQ(snapEndAt(l, s, 2000, l.centerY(3)), makeCell(3, 11)) << "off the grid still clamps";
}

// ---- contact state machine -----------------------------------------------------------------------

namespace {
struct Finger {
  BoardLayout layout = computeLayout(Difficulty::Medium);
  Contact contact;
  Cell anchor;
  uint32_t now = 1000;
  uint32_t stepMs = 100;  // between samples: every end has rested unless a test says otherwise
  ContactEvent held(const Cell c, const int dx = 0, const int dy = 0) {
    return heldAt(layout.centerX(c.col) + dx, layout.centerY(c.row) + dy);
  }
  ContactEvent heldAt(const int x, const int y) {
    now += stepMs;
    return trackContact(contact, anchor, layout, true, x, y, false, now);
  }
  ContactEvent release() {
    now += stepMs;
    return trackContact(contact, anchor, layout, false, 0, 0, true, now);
  }
  ContactEvent lost() {
    now += stepMs;
    return trackContact(contact, anchor, layout, false, 0, 0, false, now);
  }
  ContactEvent tap(const Cell c) {
    held(c);
    held(c, 3, -2);
    return release();
  }
};
using K = ContactEvent::Kind;
}  // namespace

TEST(WordSearchContact, TapSetsThenClearsTheAnchor) {
  Finger f;
  EXPECT_EQ(f.held(makeCell(2, 2)).kind, K::Began);
  EXPECT_TRUE(f.contact.gridActive());
  ContactEvent e = f.release();
  EXPECT_EQ(e.kind, K::Anchored);
  EXPECT_TRUE(e.gridEnded);
  EXPECT_EQ(f.anchor, makeCell(2, 2));
  EXPECT_EQ(f.tap(makeCell(2, 2)).kind, K::AnchorCleared);
  EXPECT_FALSE(f.anchor.valid());
}

TEST(WordSearchContact, AlignedSecondTapEvaluatesTheLine) {
  Finger f;
  f.tap(makeCell(1, 1));
  const ContactEvent e = f.tap(makeCell(5, 5));
  EXPECT_EQ(e.kind, K::Line);
  EXPECT_EQ(e.line.a, makeCell(1, 1));
  EXPECT_EQ(e.line.b, makeCell(5, 5));
  EXPECT_FALSE(f.anchor.valid());
}

TEST(WordSearchContact, UnalignedSecondTapMovesTheAnchor) {
  Finger f;
  f.tap(makeCell(1, 1));
  EXPECT_EQ(f.tap(makeCell(2, 4)).kind, K::Anchored);
  EXPECT_EQ(f.anchor, makeCell(2, 4));
}

TEST(WordSearchContact, DragEvaluatesAndClearsTheAnchor) {
  Finger f;
  f.tap(makeCell(9, 9));
  ASSERT_TRUE(f.anchor.valid());
  EXPECT_EQ(f.held(makeCell(0, 0)).kind, K::Began);
  EXPECT_EQ(f.held(makeCell(0, 1)).kind, K::None) << "one step is shorter than any word: still the start";
  EXPECT_EQ(f.held(makeCell(0, 2)).kind, K::EndMoved);
  EXPECT_EQ(f.contact.end, makeCell(0, 2));
  EXPECT_EQ(f.held(makeCell(0, 2), 4, 4).kind, K::None) << "same snapped cell: no redraw";
  EXPECT_EQ(f.held(makeCell(1, 4), 10, -12).kind, K::EndMoved) << "off-angle: still the row";
  EXPECT_EQ(f.contact.end, makeCell(0, 4));
  const ContactEvent e = f.release();
  EXPECT_EQ(e.kind, K::Line);
  EXPECT_TRUE(e.gridEnded);
  EXPECT_EQ(e.line.a, makeCell(0, 0));
  EXPECT_EQ(e.line.b, makeCell(0, 4));
  EXPECT_FALSE(f.anchor.valid());
}

TEST(WordSearchContact, DragBackToTheStartIsATap) {
  Finger f;
  f.held(makeCell(4, 4));
  EXPECT_EQ(f.held(makeCell(4, 7)).kind, K::EndMoved);
  EXPECT_EQ(f.held(makeCell(4, 4), 2, 2).kind, K::EndMoved) << "the preview goes away";
  EXPECT_EQ(f.release().kind, K::Anchored);
}

TEST(WordSearchContact, SmallRollIsStillATap) {
  Finger f;
  // Land near the right edge of a cell and roll a few px over the border.
  const int x = f.layout.cellX(3) + f.layout.cell - 3;
  const int y = f.layout.centerY(3);
  f.heldAt(x, y);
  EXPECT_EQ(f.heldAt(x + 8, y + 2).kind, K::None);
  EXPECT_EQ(f.release().kind, K::Anchored);
  EXPECT_EQ(f.anchor, makeCell(3, 3));
}

TEST(WordSearchContact, OneStepDragIsATap) {
  Finger f;
  f.held(makeCell(4, 4));
  EXPECT_EQ(f.held(makeCell(4, 5)).kind, K::None);
  EXPECT_EQ(f.release().kind, K::Anchored);
  EXPECT_EQ(f.anchor, makeCell(4, 4));
}

TEST(WordSearchContact, LiftDriftKeepsATap) {
  // The touch centroid drifts 10-20 px as a finger rolls off; on Hard's 29 px cells that is
  // most of a cell in any direction.
  for (const int drift : {16, 20, 24}) {
    for (int dir = 0; dir < 8; dir++) {
      Finger f;
      f.layout = computeLayout(Difficulty::Hard);
      f.stepMs = 15;
      const Cell s = makeCell(7, 7);
      f.held(s, -6, 5);  // anywhere in the cell
      f.held(s, -6 + DIR_DC[dir] * drift, 5 + DIR_DR[dir] * drift);
      EXPECT_EQ(f.release().kind, K::Anchored) << drift << " px, direction " << dir;
      EXPECT_EQ(f.anchor, s);
    }
  }
}

TEST(WordSearchContact, LiftDriftKeepsTheSecondTapOfALine) {
  Finger f;
  f.layout = computeLayout(Difficulty::Hard);
  f.tap(makeCell(2, 2));
  f.stepMs = 15;
  f.held(makeCell(2, 6));
  f.held(makeCell(2, 6), 18, 4);  // the lift rolls toward the next letter
  const ContactEvent e = f.release();
  EXPECT_EQ(e.kind, K::Line);
  EXPECT_EQ(e.line.a, makeCell(2, 2));
  EXPECT_EQ(e.line.b, makeCell(2, 6));
}

TEST(WordSearchContact, LiftDriftDoesNotStretchADrag) {
  Finger f;
  f.layout = computeLayout(Difficulty::Hard);
  f.held(makeCell(3, 2));
  f.held(makeCell(3, 5));  // rests on the word's last letter (100 ms)
  f.held(makeCell(3, 5), 4, 0);
  EXPECT_EQ(f.contact.end, makeCell(3, 5));
  f.stepMs = 20;
  EXPECT_EQ(f.held(makeCell(3, 5), 16, 0).kind, K::EndMoved) << "the lift's drift, as the panel reports it";
  EXPECT_EQ(f.contact.end, makeCell(3, 6));
  const ContactEvent e = f.release();
  EXPECT_EQ(e.kind, K::Line);
  EXPECT_EQ(e.line.b, makeCell(3, 5)) << "the cell the finger rested on";
}

TEST(WordSearchContact, FlickKeepsItsLastCell) {
  // A quick sweep never rests on a cell: its last cell counts, however young.
  Finger f;
  f.layout = computeLayout(Difficulty::Hard);
  f.held(makeCell(5, 1));
  f.stepMs = 15;
  for (int col = 2; col <= 6; col++) f.held(makeCell(5, col));
  EXPECT_EQ(f.release().line.b, makeCell(5, 6));
  // A deliberate move on after a rest also counts once it has rested itself.
  f.stepMs = 100;
  f.held(makeCell(1, 1));
  f.held(makeCell(1, 4));
  f.held(makeCell(1, 5));
  EXPECT_EQ(f.release().line.b, makeCell(1, 5));
}

TEST(WordSearchContact, MarginBesideTheGridStartsOnTheEdgeCell) {
  Finger f;
  f.layout = computeLayout(Difficulty::Easy);
  const int y = f.layout.centerY(3);
  EXPECT_EQ(f.heldAt(f.layout.grid.x - 4, y).kind, K::Began);
  EXPECT_EQ(f.contact.start, makeCell(3, 0));
  f.heldAt(f.layout.centerX(3), y);
  const ContactEvent e = f.release();
  EXPECT_EQ(e.kind, K::Line);
  EXPECT_TRUE(e.gridEnded) << "so the edge swipe's Back stays unread";
  EXPECT_EQ(e.line.a, makeCell(3, 0));
  EXPECT_EQ(e.line.b, makeCell(3, 3));
  // The right margin too; beyond half a cell it is off the grid.
  f.heldAt(f.layout.grid.right() + f.layout.cell / 2 - 1, y);
  EXPECT_EQ(f.contact.start, makeCell(3, f.layout.size - 1));
  f.release();
  EXPECT_EQ(f.heldAt(f.layout.grid.x - f.layout.cell / 2 - 1, y).kind, K::None);
  EXPECT_EQ(f.release().kind, K::OutsideEnded);
}

TEST(WordSearchContact, LostContactEndsLikeARelease) {
  // A second finger silences isScreenTouchHeld without a release edge.
  Finger f;
  f.held(makeCell(0, 0));
  f.held(makeCell(3, 3));
  const ContactEvent e = f.lost();
  EXPECT_EQ(e.kind, K::Line);
  EXPECT_FALSE(f.contact.active);
  EXPECT_EQ(f.release().kind, K::None) << "the late release edge is ignored";
}

TEST(WordSearchContact, DragLeavingTheGridClamps) {
  Finger f;
  f.held(makeCell(5, 8));
  f.heldAt(470, f.layout.centerY(5));
  EXPECT_EQ(f.contact.end, makeCell(5, 11));
  f.heldAt(f.layout.centerX(8) + 10, 790);  // straight down, past the grid's bottom
  EXPECT_EQ(f.contact.end, makeCell(11, 8));
  f.heldAt(470, 470);  // down-right, off the grid's right edge: the diagonal stops at the edge
  EXPECT_EQ(f.contact.end, makeCell(8, 11));
  EXPECT_EQ(f.release().line.b, makeCell(8, 11));
}

TEST(WordSearchContact, OffGridContactsGoToTheButtons) {
  Finger f;
  const Rect m = f.layout.menuButton;
  EXPECT_EQ(f.heldAt(m.x + 10, m.y + 10).kind, K::None);
  EXPECT_FALSE(f.contact.gridActive());
  f.heldAt(m.x + 14, m.y + 12);
  const ContactEvent e = f.release();
  EXPECT_EQ(e.kind, K::OutsideEnded);
  EXPECT_FALSE(e.gridEnded);
  EXPECT_EQ(hitButton(f.layout, false, e.x0, e.y0, e.x1, e.y1), ButtonHit::Menu);
  // Dragging onto the grid from outside does not select.
  f.heldAt(240, 100);
  EXPECT_EQ(f.held(makeCell(4, 4)).kind, K::None);
  EXPECT_EQ(f.release().kind, K::OutsideEnded);
  EXPECT_FALSE(f.anchor.valid());
}

TEST(WordSearchContact, NoInputNoEvents) {
  Finger f;
  EXPECT_EQ(f.release().kind, K::None);
  EXPECT_EQ(f.lost().kind, K::None);
}

TEST(WordSearchCursor, StepAndMove) {
  EXPECT_EQ(stepCursor(makeCell(0, 0), 1, 10), makeCell(0, 1));
  EXPECT_EQ(stepCursor(makeCell(0, 9), 1, 10), makeCell(1, 0));
  EXPECT_EQ(stepCursor(makeCell(9, 9), 1, 10), makeCell(0, 0));
  EXPECT_EQ(stepCursor(makeCell(0, 0), -1, 10), makeCell(9, 9));
  EXPECT_EQ(moveCursor(makeCell(0, 0), -1, 0, 10), makeCell(0, 0));
  EXPECT_EQ(moveCursor(makeCell(5, 5), 1, 1, 10), makeCell(6, 6));
  EXPECT_EQ(moveCursor(makeCell(9, 9), 0, 1, 10), makeCell(9, 9));
  Cell anchor;
  EXPECT_EQ(tapCell(anchor, makeCell(0, 0)).kind, K::Anchored);
  EXPECT_EQ(tapCell(anchor, makeCell(0, 4)).kind, K::Line);
}

// ---- layout --------------------------------------------------------------------------------------

TEST(WordSearchLayout, GeometryStaysInBounds) {
  for (const Difficulty d : ALL_DIFFICULTIES) {
    for (const int contentTop : {0, 99, 110, 116}) {
      const BoardLayout l = computeLayout(d, contentTop);
      SCOPED_TRACE(int(d));
      EXPECT_EQ(l.size, specFor(d).size);
      EXPECT_GE(l.grid.y, GRID_TOP_MIN) << "no grid drag can start in the light-panel band";
      EXPECT_GT(l.grid.y, 112);
      EXPECT_GE(l.grid.x, SIDE_INSET);
      EXPECT_LE(l.grid.right(), SCREEN_W - SIDE_INSET);
      EXPECT_GE(l.list.y, l.grid.bottom());
      EXPECT_LE(l.list.bottom(), l.bar.y);
      EXPECT_GE(l.list.h, 150);
      EXPECT_GE(l.menuButton.w, 96);
      EXPECT_GE(l.menuButton.h, 44);
      EXPECT_LE(l.menuButton.right(), SCREEN_W - SIDE_INSET);
      EXPECT_LE(l.menuButton.bottom(), SCREEN_H - 3);
      EXPECT_GE(l.newPuzzleButton.w, 96);
      EXPECT_GE(l.newPuzzleButton.h, 44);
      EXPECT_GE(l.newPuzzleButton.y, l.list.y);
      EXPECT_LE(l.newPuzzleButton.bottom(), l.list.bottom());
      EXPECT_LE(l.status.right(), l.menuButton.x);
    }
  }
  EXPECT_EQ(computeLayout(Difficulty::Easy).cell, 44);
  EXPECT_EQ(computeLayout(Difficulty::Medium).cell, 38);
  EXPECT_EQ(computeLayout(Difficulty::Hard).cell, 29);
}

TEST(WordSearchLayout, CellHitTest) {
  const BoardLayout l = computeLayout(Difficulty::Hard);
  Cell c;
  ASSERT_TRUE(l.cellAt(l.grid.x, l.grid.y, c));
  EXPECT_EQ(c, makeCell(0, 0));
  ASSERT_TRUE(l.cellAt(l.grid.right() - 1, l.grid.bottom() - 1, c));
  EXPECT_EQ(c, makeCell(14, 14));
  EXPECT_FALSE(l.cellAt(l.grid.x - 1, l.grid.y, c));
  EXPECT_FALSE(l.cellAt(l.grid.x, l.grid.bottom(), c));
  ASSERT_TRUE(l.cellAt(l.centerX(6), l.centerY(9), c));
  EXPECT_EQ(c, makeCell(9, 6));
}

TEST(WordSearchLayout, ButtonsNeedStartAndEnd) {
  const BoardLayout l = computeLayout(Difficulty::Easy);
  const Rect n = l.newPuzzleButton;
  EXPECT_EQ(hitButton(l, true, n.x + 5, n.y + 5, n.x + 20, n.y + 9), ButtonHit::NewPuzzle);
  EXPECT_EQ(hitButton(l, false, n.x + 5, n.y + 5, n.x + 20, n.y + 9), ButtonHit::None) << "only when complete";
  EXPECT_EQ(hitButton(l, true, n.x + 5, n.y + 5, 5, 5), ButtonHit::None) << "slid off";
}

namespace {
// 10 px per character for the large font, 8 for the small one.
int fakeMeasure(void*, const ListFont font, const char* text) {
  return static_cast<int>(std::strlen(text)) * (font == ListFont::Large ? 10 : 8);
}
}  // namespace

TEST(WordSearchLayout, WordListPicksColumnsThenFont) {
  const Rect area{12, 600, 456, 140};
  const int pitch[LIST_FONT_COUNT] = {24, 20};
  const char* shortWords[12] = {"OAK", "ELM", "FIR", "ASH", "YEW", "PINE", "PALM", "LIME", "BAY", "BOX", "TEA", "FIG"};
  ListLayout l = layoutWordList(shortWords, 12, area, pitch, fakeMeasure, nullptr);
  EXPECT_TRUE(l.fits);
  EXPECT_EQ(l.columns, 3);
  EXPECT_EQ(l.font, ListFont::Large);
  EXPECT_EQ(l.rows, 4);
  // Columns are as wide as their widest word: one long word still leaves room for 3.
  const char* oneLong[6] = {"ABCDEFGHIJKLMNO", "A", "B", "C", "D", "E"};
  l = layoutWordList(oneLong, 6, area, pitch, fakeMeasure, nullptr);
  EXPECT_TRUE(l.fits);
  EXPECT_EQ(l.columns, 3);
  // Six 15-character words: 3 x 150 + gaps > 456 -> 2 columns of the large font.
  const char* sixLong[6];
  for (auto& w : sixLong) w = "ABCDEFGHIJKLMNO";
  l = layoutWordList(sixLong, 6, area, pitch, fakeMeasure, nullptr);
  EXPECT_TRUE(l.fits);
  EXPECT_EQ(l.columns, 2);
  EXPECT_EQ(l.font, ListFont::Large);
  EXPECT_GE(l.colX[1], l.colX[0] + 150 + LIST_COL_GAP);
  EXPECT_LE(l.colX[1] + 150, area.right());
  // Sixteen 15-character words: 8 rows of 24 px overflow, the small font's 3 x 120 fits.
  const char* many[16];
  for (auto& w : many) w = "ABCDEFGHIJKLMNO";
  l = layoutWordList(many, 16, area, pitch, fakeMeasure, nullptr);
  EXPECT_TRUE(l.fits);
  EXPECT_EQ(l.columns, 3);
  EXPECT_EQ(l.font, ListFont::Small);
  // Sixteen 20-character words fit nowhere: 2 equal small columns, truncated by the drawing.
  const char* huge[16];
  for (auto& w : huge) w = "ABCDEFGHIJKLMNOPQRST";
  l = layoutWordList(huge, 16, area, pitch, fakeMeasure, nullptr);
  EXPECT_FALSE(l.fits);
  EXPECT_EQ(l.columns, 2);
  EXPECT_EQ(l.font, ListFont::Small);
  // 16 short words: 3 columns of 6 rows; large font needs 144 > 140 -> small font.
  const char* sixteen[16];
  for (auto& w : sixteen) w = "OAK";
  l = layoutWordList(sixteen, 16, area, pitch, fakeMeasure, nullptr);
  EXPECT_TRUE(l.fits);
  EXPECT_EQ(l.columns, 3);
  EXPECT_EQ(l.font, ListFont::Small);
  int x = 0;
  int y = 0;
  listItemOrigin(l, 0, x, y);
  EXPECT_EQ(x, l.colX[0]);
  EXPECT_GE(x, area.x);
  EXPECT_EQ(y, l.top);
  listItemOrigin(l, 6, x, y);  // column-major: item 6 starts column 2
  EXPECT_EQ(x, l.colX[1]);
  EXPECT_EQ(y, l.top);
  listItemOrigin(l, 15, x, y);
  EXPECT_EQ(x, l.colX[2]);
  EXPECT_EQ(y, l.top + 3 * l.rowPitch);
  EXPECT_LE(l.colX[2] + l.colW[2], area.right());
  EXPECT_GE(l.top, area.y);
}

// ---- save codec ----------------------------------------------------------------------------------

namespace {
std::unique_ptr<Puzzle> playedPuzzle() {
  auto p = generated(static_cast<size_t>(findBuiltinTheme("ocean")), Difficulty::Hard, 2024);
  EXPECT_NE(p, nullptr);
  markFound(*p, 1, Line{p->words[1].place.end(), p->words[1].place.start()});
  markFound(*p, 3, Line{p->words[3].place.start(), p->words[3].place.end()});
  p->hintWord = 5;
  p->hintsUsed = 2;
  p->elapsedSeconds = 392;
  p->cursor = makeCell(4, 13);
  std::snprintf(p->themeKey, sizeof(p->themeKey), "file:my sea words.words");
  std::snprintf(p->themeTitle, sizeof(p->themeTitle), "Sea \xC3\xA9 Shore");
  return p;
}
std::string formatted(const Puzzle& p) {
  std::string s(PUZZLE_TEXT_MAX, '\0');
  const size_t n = formatPuzzle(p, s.data(), s.size());
  EXPECT_GT(n, 0u);
  s.resize(n);
  return s;
}
}  // namespace

TEST(WordSearchSave, PuzzleRoundTrip) {
  auto p = playedPuzzle();
  const std::string text = formatted(*p);
  auto q = std::make_unique<Puzzle>();
  ASSERT_TRUE(parsePuzzle(text.data(), text.size(), *q)) << text;
  EXPECT_EQ(q->difficulty, p->difficulty);
  EXPECT_EQ(q->size, p->size);
  EXPECT_EQ(q->seed, p->seed);
  EXPECT_STREQ(q->themeKey, p->themeKey);
  EXPECT_STREQ(q->themeTitle, p->themeTitle);
  EXPECT_EQ(gridText(*q), gridText(*p));
  ASSERT_EQ(q->wordCount, p->wordCount);
  for (int k = 0; k < p->wordCount; k++) {
    EXPECT_STREQ(q->words[k].display, p->words[k].display);
    EXPECT_STREQ(q->words[k].letters, p->words[k].letters);
    EXPECT_EQ(q->words[k].place.row, p->words[k].place.row);
    EXPECT_EQ(q->words[k].place.col, p->words[k].place.col);
    EXPECT_EQ(q->words[k].place.dir, p->words[k].place.dir);
    EXPECT_EQ(q->words[k].place.len, p->words[k].place.len);
    EXPECT_EQ(q->words[k].found, p->words[k].found);
    if (p->words[k].found) {
      EXPECT_EQ(q->words[k].foundLine.a, p->words[k].foundLine.a);
      EXPECT_EQ(q->words[k].foundLine.b, p->words[k].foundLine.b);
    }
  }
  EXPECT_EQ(q->hintWord, 5);
  EXPECT_EQ(q->hintsUsed, 2);
  EXPECT_EQ(q->elapsedSeconds, 392u);
  EXPECT_EQ(q->cursor, makeCell(4, 13));
  EXPECT_EQ(formatted(*q), text) << "stable";
  EXPECT_LT(text.size(), PUZZLE_TEXT_MAX);
  // CRLF line ends (a card edited elsewhere) still read.
  std::string crlf;
  for (const char c : text) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  EXPECT_TRUE(parsePuzzle(crlf.data(), crlf.size(), *q));
}

TEST(WordSearchSave, EveryTruncationIsNoSave) {
  auto p = playedPuzzle();
  const std::string text = formatted(*p);
  auto q = std::make_unique<Puzzle>();
  // Everything short of "end" (the final newline is optional).
  for (size_t n = 0; n + 1 < text.size(); n++) {
    EXPECT_FALSE(parsePuzzle(text.data(), n, *q)) << "accepted a save cut at byte " << n;
  }
  EXPECT_TRUE(parsePuzzle(text.data(), text.size() - 1, *q));
}

TEST(WordSearchSave, CorruptSavesAreRejected) {
  auto p = playedPuzzle();
  const std::string good = formatted(*p);
  auto q = std::make_unique<Puzzle>();
  auto rejects = [&](std::string text, const char* why) {
    EXPECT_FALSE(parsePuzzle(text.data(), text.size(), *q)) << why;
  };
  auto replaceLine = [&](const std::string& prefix, const std::string& with) {
    std::string s = good;
    const size_t at = s.find("\n" + prefix);
    EXPECT_NE(at, std::string::npos) << prefix;
    const size_t end = s.find('\n', at + 1);
    s.replace(at + 1, end - at - 1, with);
    return s;
  };
  rejects("WS2" + good.substr(3), "unknown version");
  rejects(replaceLine("difficulty", "difficulty 3"), "bad difficulty");
  rejects(replaceLine("difficulty", "difficulty 1"), "rows do not match the size");
  rejects(replaceLine("cursor", "cursor 15 0"), "cursor off the grid");
  rejects(replaceLine("hint", "hint 1 1"), "hint on a found word");
  rejects(replaceLine("hint", "hint 1 16"), "hint out of range");
  rejects(replaceLine("theme", "theme"), "no theme key");
  rejects(replaceLine("elapsed", "elapsed 9999999"), "elapsed out of range");
  rejects(replaceLine("seed", "seed -4"), "negative seed");
  rejects(replaceLine("row", "row " + std::string(15, 'a')), "lower-case letters");
  rejects(replaceLine("row", "row " + std::string(14, 'A')), "short row");
  rejects(good + "junk\n", "data after end");
  // A grid letter under a word changed: the placement no longer matches.
  std::string s = good;
  const PuzzleWord& w0 = p->words[0];
  const size_t rowAt = [&] {
    size_t at = 0;
    for (int r = 0; r <= w0.place.row; r++) at = s.find("\nrow ", at + 1);
    return at;
  }();
  char& cell = s[rowAt + 5 + w0.place.col];
  cell = cell == 'Q' ? 'Z' : 'Q';
  rejects(s, "placement does not match the grid");
  // A found line that does not spell its word (one cell short), a bad direction, a bad display.
  const Placement& pl = w0.place;
  const Cell shortEnd = makeCell(pl.end().row - DIR_DR[pl.dir], pl.end().col - DIR_DC[pl.dir]);
  char line[96];
  std::snprintf(line, sizeof(line), "word %d %d %u %u 1 %d %d %d %d %s", pl.row, pl.col, pl.dir, pl.len, pl.row, pl.col,
                shortEnd.row, shortEnd.col, w0.display);
  rejects(replaceLine("word", line), "found line does not spell it");
  std::snprintf(line, sizeof(line), "word %d %d 9 %u 0 -1 -1 -1 -1 %s", pl.row, pl.col, pl.len, w0.display);
  rejects(replaceLine("word", line), "bad direction");
  std::snprintf(line, sizeof(line), "word %d %d %u %u 0 -1 -1 -1 -1 %s1", pl.row, pl.col, pl.dir, pl.len, w0.display);
  rejects(replaceLine("word", line), "bad display text");
  // The same line, written back unchanged, is accepted (the edits above are what fail).
  std::snprintf(line, sizeof(line), "word %d %d %u %u 0 -1 -1 -1 -1 %s", pl.row, pl.col, pl.dir, pl.len, w0.display);
  const std::string same = replaceLine("word", line);
  EXPECT_TRUE(parsePuzzle(same.data(), same.size(), *q));
}

TEST(WordSearchSave, RandomMutationsNeverCrash) {
  auto p = playedPuzzle();
  const std::string good = formatted(*p);
  auto q = std::make_unique<Puzzle>();
  uint32_t rng = 77;
  int accepted = 0;
  for (int i = 0; i < 3000; i++) {
    std::string s = good;
    const int edits = 1 + static_cast<int>(randomBelow(rng, 3));
    for (int e = 0; e < edits; e++) {
      s[randomBelow(rng, static_cast<uint32_t>(s.size()))] = static_cast<char>(randomBelow(rng, 256));
    }
    if (parsePuzzle(s.data(), s.size(), *q)) {
      accepted++;
      EXPECT_TRUE(validatePuzzle(*q));
    }
  }
  EXPECT_LT(accepted, 3000);
}

TEST(WordSearchSave, PrefsRoundTripAndDefaults) {
  Prefs p;
  EXPECT_TRUE(p.randomChoice());
  EXPECT_EQ(p.difficulty, Difficulty::Medium);
  p.difficulty = Difficulty::Hard;
  std::snprintf(p.choice, sizeof(p.choice), "file:knots.words");
  rememberTheme(p, "trees");
  rememberTheme(p, "space");
  char buf[PREFS_TEXT_MAX];
  const size_t n = formatPrefs(p, buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  Prefs q;
  ASSERT_TRUE(parsePrefs(buf, n, q)) << buf;
  EXPECT_EQ(q.difficulty, Difficulty::Hard);
  EXPECT_STREQ(q.choice, "file:knots.words");
  EXPECT_STREQ(q.recent[0], "space");
  EXPECT_STREQ(q.recent[1], "trees");
  EXPECT_STREQ(q.recent[2], "");
  // Random round trip.
  Prefs r;
  const size_t rn = formatPrefs(r, buf, sizeof(buf));
  ASSERT_TRUE(parsePrefs(buf, rn, q));
  EXPECT_TRUE(q.randomChoice());
  // Corrupt or truncated -> defaults.
  for (size_t cut = 0; cut + 1 < n; cut++) {
    formatPrefs(p, buf, sizeof(buf));
    EXPECT_FALSE(parsePrefs(buf, cut, q)) << cut;
    EXPECT_EQ(q.difficulty, Difficulty::Medium);
    EXPECT_TRUE(q.randomChoice());
  }
  const std::string bad = "WP1\ndifficulty 7\nchoice random\nend\n";
  EXPECT_FALSE(parsePrefs(bad.data(), bad.size(), q));
  const std::string tooMany = "WP1\ndifficulty 0\nchoice random\nrecent a\nrecent b\nrecent c\nrecent d\nend\n";
  EXPECT_FALSE(parsePrefs(tooMany.data(), tooMany.size(), q));
  EXPECT_EQ(formatPrefs(p, buf, 10), 0u) << "cap too small";
}

TEST(WordSearchSave, RecentThemesAndRandomPick) {
  Prefs p;
  rememberTheme(p, "a");
  rememberTheme(p, "b");
  rememberTheme(p, "c");
  rememberTheme(p, "b");  // moves to the front, no duplicate
  EXPECT_STREQ(p.recent[0], "b");
  EXPECT_STREQ(p.recent[1], "c");
  EXPECT_STREQ(p.recent[2], "a");
  rememberTheme(p, "d");
  EXPECT_STREQ(p.recent[0], "d");
  EXPECT_STREQ(p.recent[2], "c");
  EXPECT_FALSE(isRecentTheme(p, "a"));

  const std::string_view keys[] = {"a", "b", "c", "d", "e"};
  uint32_t rng = 9;
  std::set<int> picked;
  for (int i = 0; i < 200; i++) {
    const int k = pickRandomTheme(rng, keys, 5, p);
    ASSERT_GE(k, 0);
    EXPECT_FALSE(isRecentTheme(p, keys[k])) << keys[k];
    picked.insert(k);
  }
  EXPECT_EQ(picked, (std::set<int>{0, 4}));
  // Every theme recent: any of them.
  const std::string_view few[] = {"b", "c"};
  EXPECT_GE(pickRandomTheme(rng, few, 2, p), 0);
  EXPECT_EQ(pickRandomTheme(rng, few, 0, p), -1);
}

// ---- the downloadable theme packs (packs/wordsearch/*.words) --------------------------------------

namespace {

struct PackTheme {
  std::string name;  // "dog-breeds.words"
  std::string text;
};

std::vector<PackTheme> packThemes() {
  namespace fs = std::filesystem;
  std::vector<PackTheme> out;
  const fs::path root(WORD_SEARCH_PACKS);
  if (!fs::is_directory(root)) return out;
  for (const auto& file : fs::directory_iterator(root)) {
    const std::string name = file.path().filename().string();
    if (!file.is_regular_file() || name.size() <= std::strlen(THEME_EXT) ||
        name.compare(name.size() - std::strlen(THEME_EXT), std::string::npos, THEME_EXT) != 0) {
      continue;
    }
    std::ifstream in(file.path(), std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    out.push_back({name, s.str()});
  }
  std::sort(out.begin(), out.end(), [](const PackTheme& a, const PackTheme& b) { return a.name < b.name; });
  return out;
}

// The lines a theme file means as words: neither blank nor a '#' comment, after the title.
std::vector<std::string> wordLines(const std::string& text) {
  std::vector<std::string> lines;
  std::stringstream s(text);
  std::string line;
  bool title = false;
  while (std::getline(s, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find_first_not_of(" \t") == std::string::npos || line[0] == '#') continue;
    if (!title) {
      title = true;
      continue;
    }
    lines.push_back(line);
  }
  return lines;
}

}  // namespace

TEST(WordSearchPacks, EveryPackThemeParsesWhole) {
  const std::vector<PackTheme> themes = packThemes();
  ASSERT_FALSE(themes.empty()) << "no theme files under " << WORD_SEARCH_PACKS;
  EXPECT_LE(themes.size(), MAX_THEME_FILES) << "the reader lists at most this many theme files";
  std::set<std::string> titles;
  for (size_t i = 0; i < builtinThemeCount(); i++) titles.insert(builtinTheme(i).title);
  for (const PackTheme& t : themes) {
    SCOPED_TRACE(t.name);
    EXPECT_LE(t.text.size(), MAX_FILE_BYTES);
    char key[MAX_THEME_KEY + 1];
    EXPECT_TRUE(makeFileThemeKey(t.name.c_str(), key, sizeof(key)));
    char title[MAX_TITLE_LEN + 1];
    ASSERT_TRUE(readThemeTitle(t.text.data(), t.text.size(), title, sizeof(title)));

    std::string storage = t.text;
    ThemeWords theme;
    ASSERT_TRUE(parseThemeFile(storage.data(), storage.size(), theme));
    EXPECT_STREQ(theme.title, title);
    EXPECT_TRUE(titles.insert(theme.title).second) << "a title already used: " << theme.title;
    // Every word line is kept (none skipped as unusable), and no two words share grid letters.
    const std::vector<std::string> lines = wordLines(t.text);
    EXPECT_EQ(theme.words.size(), lines.size()) << "a word line was skipped as unusable";
    EXPECT_GE(theme.words.size(), 30u);
    std::set<std::string> letters;
    for (const auto w : theme.words) {
      char grid[MAX_WORD_LETTERS + 1];
      ASSERT_GT(gridLetters(w.data(), w.size(), grid, sizeof(grid)), 0u) << w;
      EXPECT_TRUE(letters.insert(grid).second) << "duplicate " << w;
    }
  }
}

TEST(WordSearchPacks, EveryPackThemeFillsEveryDifficulty) {
  const std::vector<PackTheme> themes = packThemes();
  ASSERT_FALSE(themes.empty());
  auto p = std::make_unique<Puzzle>();
  for (const PackTheme& t : themes) {
    SCOPED_TRACE(t.name);
    std::string storage = t.text;
    ThemeWords theme;
    ASSERT_TRUE(parseThemeFile(storage.data(), storage.size(), theme));
    for (const Difficulty d : ALL_DIFFICULTIES) {
      int full = 0;
      for (uint32_t seed = 1; seed <= 12; seed++) {
        ASSERT_TRUE(generatePuzzle(theme.words.data(), theme.words.size(), d, seed * 7919u, *p))
            << "difficulty " << int(d) << " seed " << seed;
        std::snprintf(p->themeKey, sizeof(p->themeKey), "%s%s", FILE_KEY_PREFIX, t.name.c_str());
        std::snprintf(p->themeTitle, sizeof(p->themeTitle), "%s", theme.title);
        expectValidPuzzle(*p, d);
        full += p->wordCount == specFor(d).words ? 1 : 0;
        if (d == Difficulty::Easy) EXPECT_EQ(p->wordCount, 8) << "Easy places all 8";
      }
      EXPECT_GE(full, 10) << "difficulty " << int(d) << ": mostly full puzzles";
    }
  }
}
