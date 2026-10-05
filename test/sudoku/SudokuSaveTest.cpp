// Sudoku saves on the host: the SD1 puzzle codec round trip, truncation at every length, every
// single-byte mutation (refused, or accepted only as a self-consistent game), hand-made
// corruptions, the .tmp fallback through every power cut of tmp -> remove -> rename, the prefs and
// the solved list.
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "Sudoku.h"

using namespace sd;

namespace {

std::unique_ptr<Model> playedModel() {
  auto m = std::make_unique<Model>();
  Generated gen;
  EXPECT_TRUE(generate(puzzleSeed(Hard, 14), Hard, gen));
  startGame(m->game, gen, 14, puzzleSeed(Hard, 14));
  Game& g = m->game;
  fillAllNotes(*m);
  int placed = 0, wrong = -1, revealed = -1;
  for (int i = 0; i < CELLS && placed < 6; i++) {
    if (g.value[i]) continue;
    putDigit(*m, i, g.solution[i]);
    placed++;
  }
  for (int i = 0; i < CELLS; i++) {
    if (g.value[i]) continue;
    const uint16_t c = candidatesOf(g, i);
    for (int d = 1; d <= 9 && wrong < 0; d++) {
      if ((c & digitBit(d)) && d != g.solution[i]) {
        putDigit(*m, i, d);
        wrong = i;
      }
    }
    if (wrong >= 0) break;
  }
  check(*m, Scope::Puzzle);
  revealed = 80;
  while (g.value[revealed]) revealed--;
  setCursor(*m, revealed);
  reveal(*m, Scope::Square);
  hint(*m);
  setCursor(*m, 40);
  g.notesMode = true;
  g.lock = LOCK_ERASE;
  g.elapsed = 754;
  EXPECT_TRUE(validGame(g));
  return m;
}

std::string format(const Game& g) {
  char buf[PUZZLE_TEXT_MAX];
  const size_t n = formatGame(g, buf, sizeof(buf));
  EXPECT_GT(n, 0u);
  return std::string(buf, n);
}

bool sameGame(const Game& a, const Game& b) {
  return a.tier == b.tier && a.number == b.number && a.seed == b.seed && a.fnv == b.fnv &&
         std::memcmp(a.givens, b.givens, CELLS) == 0 && std::memcmp(a.solution, b.solution, CELLS) == 0 &&
         std::memcmp(a.value, b.value, CELLS) == 0 && std::memcmp(a.notes, b.notes, sizeof(a.notes)) == 0 &&
         std::memcmp(a.flags, b.flags, CELLS) == 0 && a.cursor == b.cursor && a.notesMode == b.notesMode &&
         a.lock == b.lock && a.elapsed == b.elapsed && a.checks == b.checks && a.hints == b.hints &&
         a.reveals == b.reveals && a.solved == b.solved;
}

// Replaces the rest of the line after "<key> " (the first occurrence).
std::string withField(std::string text, const std::string& key, const std::string& value) {
  const size_t at = text.find("\n" + key + " ");
  EXPECT_NE(at, std::string::npos) << key;
  const size_t start = at + 1 + key.size() + 1;
  const size_t end = text.find('\n', start);
  return text.replace(start, end - start, value);
}

std::string lineOf(const std::string& text, const std::string& key) {
  const size_t at = text.find("\n" + key + " ");
  const size_t start = at + 1 + key.size() + 1;
  return text.substr(start, text.find('\n', start) - start);
}

bool parses(const std::string& text) {
  auto g = std::make_unique<Game>();
  return parseGame(text.data(), text.size(), *g);
}

}  // namespace

TEST(SudokuSave, RoundTrip) {
  auto m = playedModel();
  const std::string text = format(m->game);
  EXPECT_LT(text.size(), PUZZLE_TEXT_MAX);
  EXPECT_GT(text.size(), 700u);
  EXPECT_EQ(text.rfind("SD1\ntier hard\nnumber 14\n", 0), 0u);
  EXPECT_NE(text.find("\nmode 1 e\n"), std::string::npos);
  EXPECT_NE(text.find("\nelapsed 754\n"), std::string::npos);
  auto back = std::make_unique<Game>();
  ASSERT_TRUE(parseGame(text.data(), text.size(), *back));
  EXPECT_TRUE(sameGame(m->game, *back));
  EXPECT_EQ(format(*back), text);
  // CRLF line ends and a missing final newline still parse.
  std::string crlf;
  for (const char c : text) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  EXPECT_TRUE(parses(crlf));
  EXPECT_TRUE(parses(text.substr(0, text.size() - 1)));
  // A fresh game and a solved one round-trip too (no cursor, digit lock).
  auto fresh = std::make_unique<Model>();
  Generated gen;
  ASSERT_TRUE(generate(puzzleSeed(Easy, 1), Easy, gen));
  startGame(fresh->game, gen, 1, puzzleSeed(Easy, 1));
  fresh->game.lock = 7;
  const std::string freshText = format(fresh->game);
  EXPECT_NE(freshText.find("\ncursor -\n"), std::string::npos);
  EXPECT_NE(freshText.find("\nmode 0 7\n"), std::string::npos);
  ASSERT_TRUE(parseGame(freshText.data(), freshText.size(), *back));
  EXPECT_TRUE(sameGame(fresh->game, *back));
  reveal(*fresh, Scope::Puzzle);
  ASSERT_TRUE(fresh->game.solved);
  const std::string solvedText = format(fresh->game);
  ASSERT_TRUE(parseGame(solvedText.data(), solvedText.size(), *back));
  EXPECT_TRUE(back->solved);
}

TEST(SudokuSave, TruncationIsNoSave) {
  auto m = playedModel();
  const std::string text = format(m->game);
  for (size_t n = 0; n + 1 < text.size(); n++) {
    EXPECT_FALSE(parses(text.substr(0, n))) << n;
  }
  auto g = std::make_unique<Game>();
  EXPECT_FALSE(parseGame(nullptr, 10, *g));
  // Anything after "end" is refused.
  EXPECT_FALSE(parses(text + "x\n"));
  EXPECT_FALSE(parses(text + "\n\n"));
}

// Every single-byte change either fails to parse or yields a game that passes validGame (a
// changed digit in elapsed, say, is a different valid save; a changed given never is).
TEST(SudokuSave, MutationsAreRefusedOrConsistent) {
  auto m = playedModel();
  const std::string text = format(m->game);
  const char subs[] = {'0', '1', '5', '9', '.', 'a', 'f', 'g', 'r', 'w', 'e', '-', ' ', '\n', '\r', 'Z', '\0'};
  int accepted = 0, refused = 0;
  auto g = std::make_unique<Game>();
  for (size_t i = 0; i < text.size(); i++) {
    for (const char c : subs) {
      if (text[i] == c) continue;
      std::string t = text;
      t[i] = c;
      if (parseGame(t.data(), t.size(), *g)) {
        accepted++;
        ASSERT_TRUE(validGame(*g)) << i;
      } else {
        refused++;
      }
    }
  }
  EXPECT_GT(refused, 10000);
  EXPECT_GT(accepted, 0);
  // Changing any given or solution square is always refused (the fnv, the uniqueness and the
  // solution's validity guard them).
  const std::string givens = lineOf(text, "givens");
  const std::string solution = lineOf(text, "solution");
  for (int i = 0; i < CELLS; i++) {
    for (const char c : {'.', '1', '9'}) {
      if (givens[i] == c) continue;
      std::string gv = givens;
      gv[i] = c;
      EXPECT_FALSE(parses(withField(text, "givens", gv))) << "given " << i;
    }
    std::string sv = solution;
    sv[i] = sv[i] == '9' ? '1' : static_cast<char>(sv[i] + 1);
    EXPECT_FALSE(parses(withField(text, "solution", sv))) << "solution " << i;
  }
}

TEST(SudokuSave, HandMadeCorruptions) {
  auto m = playedModel();
  const Game& g = m->game;
  const std::string text = format(g);
  ASSERT_TRUE(parses(text));
  EXPECT_FALSE(parses("SD2" + text.substr(3)));
  EXPECT_TRUE(parses(withField(text, "tier", "medium")));  // the label is not checked against the grids
  EXPECT_FALSE(parses(withField(text, "tier", "nightmare")));
  EXPECT_FALSE(parses(withField(text, "fnv", "00000000")));
  EXPECT_FALSE(parses(withField(text, "fnv", "ABCDEF01")));  // upper-case hex is not written
  EXPECT_FALSE(parses(withField(text, "cursor", "81")));
  EXPECT_FALSE(parses(withField(text, "mode", "2 0")));
  EXPECT_FALSE(parses(withField(text, "mode", "0 x")));
  EXPECT_FALSE(parses(withField(text, "mode", "0 10")));
  EXPECT_FALSE(parses(withField(text, "elapsed", std::to_string(ELAPSED_MAX + 1))));
  EXPECT_FALSE(parses(withField(text, "counts", "1 2")));
  EXPECT_FALSE(parses(withField(text, "counts", "1 2 3 4")));
  EXPECT_FALSE(parses(withField(text, "counts", "65536 0 0")));
  EXPECT_FALSE(parses(withField(text, "solved", "1")));  // not complete
  // Squares: a given shown in entries, notes on a filled square, flags that lie.
  std::string entries = lineOf(text, "entries");
  std::string notes = lineOf(text, "notes");
  std::string flags = lineOf(text, "flags");
  int given = -1, filled = -1, empty = -1, wrong = -1;
  for (int i = 0; i < CELLS; i++) {
    if (g.givens[i] && given < 0) given = i;
    if (!g.givens[i] && g.value[i] == g.solution[i] && !(g.flags[i] & FLAG_REVEALED) && filled < 0) filled = i;
    if (!g.value[i] && empty < 0) empty = i;
    if (g.flags[i] & FLAG_WRONG) wrong = i;
  }
  ASSERT_GE(given, 0);
  ASSERT_GE(filled, 0);
  ASSERT_GE(empty, 0);
  ASSERT_GE(wrong, 0);
  std::string e = entries;
  e[given] = static_cast<char>('0' + g.givens[given]);
  EXPECT_FALSE(parses(withField(text, "entries", e)));
  std::string n = notes;
  n.replace(filled * 3, 3, "001");
  EXPECT_FALSE(parses(withField(text, "notes", n)));
  n = notes;
  n.replace(empty * 3, 3, "200");  // bit 9: no such digit
  EXPECT_FALSE(parses(withField(text, "notes", n)));
  std::string f = flags;
  f[filled] = 'w';  // WRONG on a right entry
  EXPECT_FALSE(parses(withField(text, "flags", f)));
  f = flags;
  f[empty] = 'r';  // REVEALED on an empty square
  EXPECT_FALSE(parses(withField(text, "flags", f)));
  f = flags;
  f[given] = 'w';
  EXPECT_FALSE(parses(withField(text, "flags", f)));
  f = flags;
  f[wrong] = '.';  // an unmarked wrong entry is fine (the mark comes from a check)
  EXPECT_TRUE(parses(withField(text, "flags", f)));
  // Givens with two solutions are refused even with a matching fnv.
  auto loose = std::make_unique<Game>();
  ASSERT_TRUE(parseGame(text.data(), text.size(), *loose));
  bool ambiguous = false;
  for (int i = 0; i < CELLS && !ambiguous; i++) {
    if (!loose->givens[i]) continue;
    loose->givens[i] = 0;
    loose->value[i] = 0;
    ambiguous = countSolutions(loose->givens, 2) == 2;
  }
  ASSERT_TRUE(ambiguous);
  loose->fnv = givensFnv(loose->givens);
  const std::string t = format(*loose);
  EXPECT_FALSE(parses(t));
}

// ---- the .tmp fallback ------------------------------------------------------------------------------

namespace {

// A card that can lose power after any step of the store's tmp -> remove -> rename.
struct FakeCard {
  std::map<std::string, std::string> files;
  // Writes path atomically, stopping after `steps` of: 1 tmp written, 2 old removed, 3 renamed.
  // partialTmp: the power cut came in the middle of writing the tmp.
  void write(const std::string& path, const std::string& text, const int steps, const bool partialTmp = false) {
    char tmp[64];
    ASSERT_GT(tmpPathOf(path.c_str(), tmp, sizeof(tmp)), 0u);
    if (steps < 1) {
      if (partialTmp) files[tmp] = text.substr(0, text.size() / 2);
      return;
    }
    files[tmp] = text;
    if (steps < 2) return;
    files.erase(path);
    if (steps < 3) return;
    files[path] = files[tmp];
    files.erase(tmp);
  }
  // The store's load: the main file, or the .tmp when it is missing.
  bool load(const std::string& path, Game& out) {
    char tmp[64];
    tmpPathOf(path.c_str(), tmp, sizeof(tmp));
    const LoadFrom from = loadFrom(files.count(path) > 0, files.count(tmp) > 0);
    if (from == LoadFrom::None) return false;
    const std::string& text = files[from == LoadFrom::Main ? path : std::string(tmp)];
    return parseGame(text.data(), text.size(), out);
  }
};

}  // namespace

TEST(SudokuSave, TmpFallbackSurvivesEveryPowerCut) {
  EXPECT_EQ(loadFrom(true, true), LoadFrom::Main);
  EXPECT_EQ(loadFrom(true, false), LoadFrom::Main);
  EXPECT_EQ(loadFrom(false, true), LoadFrom::Tmp);
  EXPECT_EQ(loadFrom(false, false), LoadFrom::None);
  char tmp[64];
  EXPECT_GT(tmpPathOf(PUZZLE_PATH, tmp, sizeof(tmp)), 0u);
  EXPECT_STREQ(tmp, "/.crosspoint/sudoku/puzzle.dat.tmp");
  EXPECT_EQ(tmpPathOf(PUZZLE_PATH, tmp, 10), 0u);

  auto m = playedModel();
  const std::string oldText = format(m->game);
  m->game.elapsed = 900;
  putDigit(*m, 40, m->game.solution[40]);
  const std::string newText = format(m->game);
  ASSERT_NE(oldText, newText);
  auto loaded = std::make_unique<Game>();
  for (int steps = 0; steps <= 3; steps++) {
    for (const bool partial : {false, true}) {
      FakeCard card;
      card.write(PUZZLE_PATH, oldText, 3);
      card.write(PUZZLE_PATH, newText, steps, partial);
      ASSERT_TRUE(card.load(PUZZLE_PATH, *loaded)) << steps;
      const std::string got = format(*loaded);
      // Before the remove the old save loads; after it, the new one (from the .tmp, then the file).
      EXPECT_EQ(got, steps >= 2 ? newText : oldText) << steps;
      // The next save completes and leaves no .tmp.
      card.write(PUZZLE_PATH, newText, 3);
      EXPECT_EQ(card.files.count(tmp), 0u);
      ASSERT_TRUE(card.load(PUZZLE_PATH, *loaded));
      EXPECT_EQ(format(*loaded), newText);
    }
  }
  // The very first save cut before the rename: the .tmp is the save; cut mid-tmp: no save.
  FakeCard first;
  first.write(PUZZLE_PATH, newText, 2);
  EXPECT_TRUE(first.load(PUZZLE_PATH, *loaded));
  FakeCard torn;
  torn.write(PUZZLE_PATH, newText, 0, true);
  EXPECT_FALSE(torn.load(PUZZLE_PATH, *loaded));
}

// ---- prefs ------------------------------------------------------------------------------------------

TEST(SudokuSave, Prefs) {
  Prefs p;
  EXPECT_EQ(p.tier, Easy);
  EXPECT_EQ(takeNumber(p, Easy), 1u);  // first run: Easy 1
  EXPECT_EQ(takeNumber(p, Easy), 2u);
  EXPECT_EQ(takeNumber(p, Expert), 1u);
  EXPECT_EQ(takeNumber(p, 7), 0u);
  p.tier = Hard;
  p.next[Medium] = 14;
  p.removeNotes = false;
  char buf[PREFS_TEXT_MAX];
  const size_t n = formatPrefs(p, buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(std::string(buf, n), "SP1\ntier hard\nnext 3 14 1 2\nremovenotes 0\nend\n");
  Prefs back;
  ASSERT_TRUE(parsePrefs(buf, n, back));
  EXPECT_EQ(back.tier, Hard);
  EXPECT_EQ(back.next[Easy], 3u);
  EXPECT_EQ(back.next[Medium], 14u);
  EXPECT_EQ(back.next[Expert], 2u);
  EXPECT_FALSE(back.removeNotes);
  EXPECT_EQ(formatPrefs(p, buf, 10), 0u);
  // Anything else: defaults.
  for (const char* bad :
       {"", "SP1\n", "SP1\ntier hard\nnext 1 1 1\nremovenotes 1\nend\n",
        "SP1\ntier hard\nnext 1 0 1 1\nremovenotes 1\nend\n", "SP1\ntier hard\nnext 1 1 1 1\nremovenotes 2\nend\n",
        "SP1\ntier hardest\nnext 1 1 1 1\nremovenotes 1\nend\n",
        "SP1\ntier hard\nnext 1 1 1 1\nremovenotes 1\nend\nmore\n"}) {
    Prefs q;
    q.tier = Expert;
    EXPECT_FALSE(parsePrefs(bad, std::strlen(bad), q)) << bad;
    EXPECT_EQ(q.tier, Easy);
    EXPECT_TRUE(q.removeNotes);
    EXPECT_EQ(q.next[Hard], 1u);
  }
}

// ---- solved list ------------------------------------------------------------------------------------

TEST(SudokuSave, SolvedList) {
  auto m = playedModel();
  const SolvedEntry e = solvedEntryOf(m->game);
  EXPECT_EQ(e.tier, Hard);
  EXPECT_EQ(e.number, 14u);
  EXPECT_EQ(e.elapsed, 754u);
  char line[SOLVED_LINE_MAX];
  const size_t n = formatSolvedLine(e, line, sizeof(line));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(line[n - 1], '\n');
  char expect[SOLVED_LINE_MAX];
  std::snprintf(expect, sizeof(expect), "%08lx hard 14 754 %u %u %u\n", static_cast<unsigned long>(m->game.fnv),
                m->game.checks, m->game.hints, m->game.reveals);
  EXPECT_STREQ(line, expect);
  SolvedEntry back;
  ASSERT_TRUE(parseSolvedLine(std::string_view(line, n - 1), back));
  EXPECT_EQ(back.fnv, e.fnv);
  EXPECT_EQ(back.tier, e.tier);
  EXPECT_EQ(back.number, e.number);
  EXPECT_EQ(back.elapsed, e.elapsed);
  EXPECT_EQ(back.checks, e.checks);
  EXPECT_EQ(back.hints, e.hints);
  EXPECT_EQ(back.reveals, e.reveals);
  EXPECT_TRUE(parseSolvedLine("0badf00d easy 1 60 0 0 0\r", back));
  for (const char* bad : {"", "0badf00d easy 1 60 0 0", "0badf00d easy 1 60 0 0 0 0", "0BADF00D easy 1 60 0 0 0",
                          "0badf00d simple 1 60 0 0 0", "0badf00d easy -1 60 0 0 0", "0badf00d easy 1 60 0 0 70000"}) {
    EXPECT_FALSE(parseSolvedLine(bad, back)) << bad;
  }
  const std::string list = std::string("garbage\n") + line + "0badf00d easy 1 60 0 0 0\n";
  EXPECT_TRUE(solvedListHas(list.data(), list.size(), e.fnv));
  EXPECT_TRUE(solvedListHas(list.data(), list.size(), 0x0badf00du));
  EXPECT_FALSE(solvedListHas(list.data(), list.size(), 0x12345678u));
  EXPECT_EQ(solvedKeepOffset(list.data(), list.size(), 1), list.size() - std::strlen("0badf00d easy 1 60 0 0 0\n"));
  EXPECT_EQ(solvedKeepOffset(list.data(), list.size(), 3), 0u);
  EXPECT_EQ(solvedKeepOffset(list.data(), list.size(), 9), 0u);
}
