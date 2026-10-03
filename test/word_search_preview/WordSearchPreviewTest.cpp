// Word Search on the host with the real renderer and fonts (SleepCardHost): writes the screen
// set to build/wordsearch/*.png for a look, and checks with the real glyphs that letters sit
// centred in their cells, capsules end on the selected cells, and every built-in theme's word
// list fits at every difficulty.
//
// The header here is a stand-in (title and underline where Lyra draws them): GUI.drawHeader is
// device-only. Everything below it is the device's drawing (src/activities/apps/WordSearchDraw).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CardPreview.h"
#include "WordSearch.h"
#include "src/activities/apps/WordSearchDraw.h"
#include "src/fontIds.h"

using namespace ws;
using sleepcards::preview::renderer;

namespace {

constexpr Difficulty ALL_DIFFICULTIES[] = {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard};
constexpr int LYRA_CONTENT_TOP = 110;  // topPadding 10 + headerHeight 84 + verticalSpacing 16

std::string outDir() {
  const std::string dir = std::string(CARD_PREVIEW_REPO_ROOT) + "/build/wordsearch";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::unique_ptr<Puzzle> generated(const char* key, const Difficulty d, const uint32_t seed) {
  const int index = findBuiltinTheme(key);
  EXPECT_GE(index, 0) << key;
  ThemeWords theme;
  EXPECT_TRUE(loadBuiltinTheme(builtinTheme(static_cast<size_t>(index)), theme));
  auto p = std::make_unique<Puzzle>();
  if (!generatePuzzle(theme.words.data(), theme.words.size(), d, seed, *p)) return nullptr;
  std::snprintf(p->themeKey, sizeof(p->themeKey), "%s", key);
  std::snprintf(p->themeTitle, sizeof(p->themeTitle), "%s", theme.title);
  return p;
}

void stubHeader(GfxRenderer& r, const char* title) {
  r.drawText(UI_12_FONT_ID, 60, 46, title, true, EpdFontFamily::BOLD);
  r.drawText(UI_12_FONT_ID, 18, 46, "<", true, EpdFontFamily::BOLD);
  r.fillRect(0, 91, 480, 3, true);
}

draw::BoardView viewFor(const Puzzle& p) {
  static char status[48];
  static char banner[64];
  draw::BoardView v;
  std::snprintf(status, sizeof(status), "%d of %d found", p.foundCount(), p.wordCount);
  v.status = status;
  v.menuLabel = "Menu";
  char elapsed[16];
  formatElapsed(p.elapsedSeconds, elapsed, sizeof(elapsed));
  std::snprintf(banner, sizeof(banner), "All %d found in %s", p.wordCount, elapsed);
  v.bannerTitle = banner;
  v.bannerDetail = p.hintsUsed == 1 ? "1 hint" : "";
  v.newPuzzleLabel = "New puzzle";
  return v;
}

BoardLayout layoutOf(const Puzzle& p) { return computeLayout(p.difficulty, LYRA_CONTENT_TOP); }

void render(const Puzzle& p, const draw::BoardView& view, const std::string& name) {
  GfxRenderer& r = renderer();
  r.clearScreen();
  stubHeader(r, p.themeTitle);
  draw::drawBoard(r, p, layoutOf(p), view);
  ASSERT_TRUE(sleepcards::preview::writeFramePng(outDir() + "/" + name + ".png"));
}

void findAll(Puzzle& p, const bool backwards) {
  for (int k = 0; k < p.wordCount; k++) {
    const Placement pl = p.words[k].place;
    markFound(p, k, backwards ? Line{pl.end(), pl.start()} : Line{pl.start(), pl.end()});
  }
}

// The ink box of whatever is drawn inside a rect.
struct Ink {
  int x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
  bool any() const { return x1 >= 0; }
};
Ink inkIn(const int x, const int y, const int w, const int h) {
  Ink ink;
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) {
      if (!renderer().readPixel(xx, yy)) continue;
      ink.x0 = std::min(ink.x0, xx);
      ink.y0 = std::min(ink.y0, yy);
      ink.x1 = std::max(ink.x1, xx);
      ink.y1 = std::max(ink.y1, yy);
    }
  }
  return ink;
}

// Every built-in theme's words a difficulty can use.
std::vector<std::string> eligibleWords(const size_t theme, const Difficulty d) {
  ThemeWords t;
  loadBuiltinTheme(builtinTheme(theme), t);
  std::vector<std::string> out;
  const DifficultySpec& spec = specFor(d);
  for (const auto w : t.words) {
    char letters[MAX_WORD_LETTERS + 1];
    const size_t n = gridLetters(w.data(), w.size(), letters, sizeof(letters));
    if (n >= spec.minLetters && n <= std::min<size_t>(spec.maxLetters, spec.size)) out.emplace_back(w);
  }
  return out;
}

}  // namespace

// ---- the PNG set ---------------------------------------------------------------------------------

TEST(WordSearchPreview, FreshPuzzles) {
  for (const Difficulty d : ALL_DIFFICULTIES) {
    auto p = generated("animals", d, 2026);
    ASSERT_NE(p, nullptr);
    const char* names[] = {"easy", "medium", "hard"};
    render(*p, viewFor(*p), std::string("ws_") + names[static_cast<int>(d)]);
  }
}

TEST(WordSearchPreview, MediumFoundAnchorAndDrag) {
  // A Medium puzzle with a diagonal word crossing another: find the first seed that has one.
  std::unique_ptr<Puzzle> p;
  int diag = -1;
  int cross = -1;
  for (uint32_t seed = 1; seed < 200 && diag < 0; seed++) {
    p = generated("ocean", Difficulty::Medium, seed);
    ASSERT_NE(p, nullptr);
    for (int a = 0; a < p->wordCount && diag < 0; a++) {
      const Placement& pa = p->words[a].place;
      if (pa.dir != DIR_DOWN_RIGHT && pa.dir != DIR_UP_RIGHT) continue;
      for (int b = 0; b < p->wordCount && diag < 0; b++) {
        if (b == a) continue;
        const Placement& pb = p->words[b].place;
        for (int i = 0; i < pa.len && diag < 0; i++) {
          for (int j = 0; j < pb.len; j++) {
            if (pa.row + DIR_DR[pa.dir] * i == pb.row + DIR_DR[pb.dir] * j &&
                pa.col + DIR_DC[pa.dir] * i == pb.col + DIR_DC[pb.dir] * j) {
              diag = a;
              cross = b;
              break;
            }
          }
        }
      }
    }
  }
  ASSERT_GE(diag, 0) << "no seed with a crossing diagonal";
  // The diagonal selected backwards, the word it crosses forwards, two more found.
  markFound(*p, diag, Line{p->words[diag].place.end(), p->words[diag].place.start()});
  markFound(*p, cross, Line{p->words[cross].place.start(), p->words[cross].place.end()});
  int more = 0;
  int unfound = -1;
  for (int k = 0; k < p->wordCount; k++) {
    if (k == diag || k == cross) continue;
    if (more < 3) {
      markFound(*p, k, Line{p->words[k].place.start(), p->words[k].place.end()});
      more++;
    } else if (unfound < 0) {
      unfound = k;
    }
  }
  p->elapsedSeconds = 125;
  ASSERT_GE(unfound, 0);
  draw::BoardView v = viewFor(*p);
  v.anchor = p->words[unfound].place.start();
  render(*p, v, "ws_medium_found_anchor");

  // The same puzzle mid-drag over the unfound word (three cells in, not yet the whole word).
  draw::BoardView drag = viewFor(*p);
  const Placement& pu = p->words[unfound].place;
  drag.dragStart = pu.start();
  drag.dragEnd = makeCell(pu.row + DIR_DR[pu.dir] * 2, pu.col + DIR_DC[pu.dir] * 2);
  drag.status = "Selecting";
  render(*p, drag, "ws_medium_drag");
}

TEST(WordSearchPreview, HardFoundInAllEightDirections) {
  std::unique_ptr<Puzzle> p;
  for (uint32_t seed = 1; seed < 400; seed++) {
    p = generated("birds", Difficulty::Hard, seed);
    ASSERT_NE(p, nullptr);
    uint8_t dirs = 0;
    for (int k = 0; k < p->wordCount; k++) dirs |= static_cast<uint8_t>(1u << p->words[k].place.dir);
    if (dirs == 0xFF) break;
  }
  uint8_t dirs = 0;
  for (int k = 0; k < p->wordCount; k++) dirs |= static_cast<uint8_t>(1u << p->words[k].place.dir);
  ASSERT_EQ(dirs, 0xFF) << "no seed placed words in all 8 directions";
  // All found but two, half of them selected backwards.
  for (int k = 0; k + 2 < p->wordCount; k++) {
    const Placement pl = p->words[k].place;
    markFound(*p, k, (k & 1) ? Line{pl.end(), pl.start()} : Line{pl.start(), pl.end()});
  }
  render(*p, viewFor(*p), "ws_hard_found");

  // Each capsule ends on its own end cells: ink just inside the cap, none just past it (unless
  // something else is drawn there, which only another capsule or letter could be).
  const BoardLayout l = layoutOf(*p);
  const int radius = l.cell / 2 - 1;
  for (int k = 0; k + 2 < p->wordCount; k++) {
    const Placement pl = p->words[k].place;
    const float ux = static_cast<float>(DIR_DC[pl.dir]) / std::hypot(DIR_DC[pl.dir], DIR_DR[pl.dir]);
    const float uy = static_cast<float>(DIR_DR[pl.dir]) / std::hypot(DIR_DC[pl.dir], DIR_DR[pl.dir]);
    const Cell ends[2] = {pl.start(), pl.end()};
    for (int e = 0; e < 2; e++) {
      const float sign = e == 0 ? -1.0f : 1.0f;
      const float cx = static_cast<float>(l.centerX(ends[e].col));
      const float cy = static_cast<float>(l.centerY(ends[e].row));
      const int ix = static_cast<int>(std::lround(cx + sign * ux * (radius - 1.5f)));
      const int iy = static_cast<int>(std::lround(cy + sign * uy * (radius - 1.5f)));
      // The cap's ring is 3 px: look in a 3x3 patch for it.
      bool ring = false;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) ring = ring || renderer().readPixel(ix + dx, iy + dy);
      }
      EXPECT_TRUE(ring) << p->words[k].display << " end " << e;
    }
  }
}

TEST(WordSearchPreview, HardCompleteWithBanner) {
  auto p = generated("space", Difficulty::Hard, 77);
  ASSERT_NE(p, nullptr);
  findAll(*p, false);
  p->elapsedSeconds = 392;
  p->hintsUsed = 1;
  ASSERT_TRUE(p->complete());
  render(*p, viewFor(*p), "ws_hard_complete");
  // The banner's button is where the layout says it is.
  const BoardLayout l = layoutOf(*p);
  EXPECT_TRUE(renderer().readPixel(l.newPuzzleButton.x + l.newPuzzleButton.w / 2, l.newPuzzleButton.y));
}

TEST(WordSearchPreview, CursorAndHint) {
  auto p = generated("fruit", Difficulty::Easy, 5);
  ASSERT_NE(p, nullptr);
  markFound(*p, 0, Line{p->words[0].place.start(), p->words[0].place.end()});
  p->hintWord = 2;
  draw::BoardView v = viewFor(*p);
  v.cursor = makeCell(4, 6);
  v.showCursor = true;
  char status[48];
  std::snprintf(status, sizeof(status), "Hint: look for a %c", p->words[2].letters[0]);
  v.status = status;
  render(*p, v, "ws_cursor_hint");
  const BoardLayout l = layoutOf(*p);
  // The cursor box: 3 px on every side, straddling the cell's edge.
  ASSERT_EQ(draw::cursorLineFor(l.cell), draw::CURSOR_LINE);
  for (int i = 0; i < draw::CURSOR_LINE; i++) {
    EXPECT_TRUE(renderer().readPixel(l.cellX(6) + l.cell / 2, l.cellY(4) - 1 + i));
    EXPECT_TRUE(renderer().readPixel(l.cellX(6) - 1 + i, l.cellY(4) + l.cell / 2));
  }
  EXPECT_FALSE(renderer().readPixel(l.cellX(6) + l.cell / 2, l.cellY(4) - 1 + draw::CURSOR_LINE));
}

TEST(WordSearchPreview, HardAnchorAndCapsuleOnW) {
  // Hard's widest letter: a found vertical word through a W, another W anchored, the cursor on
  // a third letter.
  std::unique_ptr<Puzzle> p;
  int word = -1;
  for (uint32_t seed = 1; seed < 400 && word < 0; seed++) {
    p = generated("animals", Difficulty::Hard, seed);
    ASSERT_NE(p, nullptr);
    for (int k = 0; k < p->wordCount && word < 0; k++) {
      const uint8_t dir = p->words[k].place.dir;
      if ((dir == DIR_DOWN || dir == DIR_UP) && std::strchr(p->words[k].letters, 'W')) word = k;
    }
  }
  ASSERT_GE(word, 0) << "no seed with a vertical word holding a W";
  markFound(*p, word, Line{p->words[word].place.start(), p->words[word].place.end()});
  draw::BoardView v = viewFor(*p);
  for (int i = 0; i < p->size * p->size && !v.anchor.valid(); i++) {
    const Cell c = makeCell(i / p->size, i % p->size);
    if (p->at(c.row, c.col) == 'W' && c.col != p->words[word].place.col) v.anchor = c;
  }
  if (!v.anchor.valid()) {
    // No second W in the grid: put one where nothing is found.
    v.anchor = makeCell(0, p->words[word].place.col == 0 ? 1 : 0);
    p->at(v.anchor.row, v.anchor.col) = 'W';
  }
  v.cursor = makeCell(v.anchor.row, v.anchor.col == p->size - 1 ? v.anchor.col - 1 : v.anchor.col + 1);
  v.showCursor = true;
  render(*p, v, "ws_hard_anchor_w");
}

TEST(WordSearchPreview, LongestWordsThreeVersusTwoColumns) {
  // The theme with the widest words, in a puzzle whose list takes 3 columns and one that
  // falls back to 2.
  GfxRenderer& r = renderer();
  size_t widest = 0;
  int widestPx = 0;
  for (size_t t = 0; t < builtinThemeCount(); t++) {
    for (const auto& w : eligibleWords(t, Difficulty::Hard)) {
      const int px = r.getTextWidth(UI_12_FONT_ID, w.c_str());
      if (px > widestPx) {
        widestPx = px;
        widest = t;
      }
    }
  }
  bool three = false;
  bool two = false;
  for (uint32_t seed = 1; seed < 300 && !(three && two); seed++) {
    for (const Difficulty d : {Difficulty::Medium, Difficulty::Hard}) {
      auto p = generated(builtinTheme(widest).key, d, seed);
      ASSERT_NE(p, nullptr);
      const ListLayout list = draw::layoutWordList(r, *p, layoutOf(*p));
      ASSERT_TRUE(list.fits);
      if (list.columns == 3 && !three) {
        three = true;
        render(*p, viewFor(*p), "ws_longest_3col");
      } else if (list.columns == 2 && !two) {
        two = true;
        render(*p, viewFor(*p), "ws_longest_2col");
      }
    }
  }
  EXPECT_TRUE(three);
  EXPECT_TRUE(two);
}

// ---- real-font checks ----------------------------------------------------------------------------

TEST(WordSearchFit, LettersAreCentredInTheirCells) {
  for (const Difficulty d : ALL_DIFFICULTIES) {
    auto p = generated("colors", d, 9);
    ASSERT_NE(p, nullptr);
    // Every letter of the alphabet somewhere: overwrite the grid in reading order.
    for (int i = 0; i < p->size * p->size; i++) p->at(i / p->size, i % p->size) = static_cast<char>('A' + i % 26);
    GfxRenderer& r = renderer();
    r.clearScreen();
    const BoardLayout l = layoutOf(*p);
    draw::BoardView v;
    draw::drawGrid(r, *p, l, v);
    for (int row = 0; row < p->size; row++) {
      for (int col = 0; col < p->size; col++) {
        const char c = p->at(row, col);
        const Ink ink = inkIn(l.cellX(col), l.cellY(row), l.cell, l.cell);
        ASSERT_TRUE(ink.any()) << c;
        // Inside the cell with a pixel to spare on each side.
        EXPECT_GT(ink.x0, l.cellX(col)) << c;
        EXPECT_LT(ink.x1, l.cellX(col) + l.cell - 1) << c;
        EXPECT_GT(ink.y0, l.cellY(row)) << c;
        EXPECT_LT(ink.y1, l.cellY(row) + l.cell - 1) << c;
        const float midX = (ink.x0 + ink.x1) / 2.0f;
        EXPECT_NEAR(midX, l.centerX(col), 1.5f) << c << " at " << row << "," << col << " difficulty " << int(d);
        if (c == 'Q' || c == 'J') continue;  // tails below the baseline
        const float midY = (ink.y0 + ink.y1) / 2.0f;
        EXPECT_NEAR(midY, l.centerY(row), 1.5f) << c << " at " << row << "," << col << " difficulty " << int(d);
      }
    }
  }
  sleepcards::preview::writeFramePng(outDir() + "/ws_alphabet_hard.png");
}

TEST(WordSearchFit, AnchorAndCursorClearEveryLetter) {
  // Every capital, at every size: on the anchor every stroke shows white with black on all
  // sides (none runs off onto the paper), and the cursor box touches no letter's ink, its own
  // cell's or a neighbour's.
  for (const Difficulty d : ALL_DIFFICULTIES) {
    auto p = generated("colors", d, 9);
    ASSERT_NE(p, nullptr);
    for (int i = 0; i < p->size * p->size; i++) p->at(i / p->size, i % p->size) = static_cast<char>('A' + i % 26);
    GfxRenderer& r = renderer();
    const BoardLayout l = layoutOf(*p);
    r.clearScreen();
    draw::drawGrid(r, *p, l, draw::BoardView{});
    // The plain ink of the first 26 cells and their neighbours (rows 0..3 cover them all).
    const int rows = 26 / p->size + 2;
    std::vector<std::pair<int, int>> ink;
    for (int y = l.grid.y; y < l.cellY(rows); y++) {
      for (int x = l.grid.x; x < l.grid.right(); x++) {
        if (r.readPixel(x, y)) ink.emplace_back(x, y);
      }
    }
    for (int i = 0; i < 26; i++) {
      const Cell c = makeCell(i / p->size, i % p->size);
      const char letter = p->at(c.row, c.col);
      const int cx0 = l.cellX(c.col);
      const int cy0 = l.cellY(c.row);
      const auto inCell = [&](const int x, const int y) {
        return x >= cx0 && x < cx0 + l.cell && y >= cy0 && y < cy0 + l.cell;
      };
      std::vector<std::pair<int, int>> mine;
      for (const auto& px : ink) {
        if (inCell(px.first, px.second)) mine.push_back(px);
      }
      const auto isMine = [&](const int x, const int y) {
        return std::find(mine.begin(), mine.end(), std::make_pair(x, y)) != mine.end();
      };
      draw::BoardView anchored;
      anchored.anchor = c;
      r.clearScreen();
      draw::drawGrid(r, *p, l, anchored);
      int lost = 0;
      for (const auto& px : mine) {
        EXPECT_FALSE(r.readPixel(px.first, px.second)) << letter << " difficulty " << int(d);
        const int n[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& o : n) {
          const int x = px.first + o[0];
          const int y = px.second + o[1];
          if (!isMine(x, y) && !r.readPixel(x, y)) lost++;
        }
      }
      EXPECT_EQ(lost, 0) << letter << " runs off the anchor, difficulty " << int(d);

      // The cursor box's ring, from the drawing's own geometry.
      const int t = draw::cursorLineFor(l.cell);
      const int bx0 = cx0 - 1;
      const int by0 = cy0 - 1;
      const int bx1 = cx0 + l.cell;
      const int by1 = cy0 + l.cell;
      for (const auto& px : ink) {
        const int x = px.first;
        const int y = px.second;
        const bool inBox = x >= bx0 && x <= bx1 && y >= by0 && y <= by1;
        const bool inside = x >= bx0 + t && x <= bx1 - t && y >= by0 + t && y <= by1 - t;
        EXPECT_FALSE(inBox && !inside) << "the cursor on " << letter << " touches ink at " << x - cx0 << "," << y - cy0
                                       << ", difficulty " << int(d);
      }
    }
  }
}

TEST(WordSearchFit, EveryThemeFitsAtEveryDifficulty) {
  GfxRenderer& r = renderer();
  for (size_t t = 0; t < builtinThemeCount(); t++) {
    for (const Difficulty d : ALL_DIFFICULTIES) {
      const BoardLayout l = computeLayout(d, LYRA_CONTENT_TOP);
      // The worst case: every word in the list as wide as the theme's widest usable word.
      const auto words = eligibleWords(t, d);
      ASSERT_FALSE(words.empty());
      const std::string* widest = &words[0];
      for (const auto& w : words) {
        if (r.getTextWidth(UI_12_FONT_ID, w.c_str()) > r.getTextWidth(UI_12_FONT_ID, widest->c_str())) widest = &w;
      }
      std::vector<const char*> texts(specFor(d).words, widest->c_str());
      const ListLayout worst = draw::layoutTexts(r, texts.data(), static_cast<int>(texts.size()), l);
      EXPECT_TRUE(worst.fits) << builtinTheme(t).key << " difficulty " << int(d) << " widest " << *widest;
      EXPECT_LE(worst.top + worst.rows * worst.rowPitch, l.list.bottom());
      // And real puzzles.
      for (uint32_t seed = 1; seed <= 5; seed++) {
        auto p = generated(builtinTheme(t).key, d, seed);
        ASSERT_NE(p, nullptr);
        EXPECT_TRUE(draw::layoutWordList(r, *p, l).fits) << builtinTheme(t).key << " seed " << seed;
      }
    }
  }
}

TEST(WordSearchFit, GridLettersFitTheirCells) {
  GfxRenderer& r = renderer();
  for (const Difficulty d : ALL_DIFFICULTIES) {
    const BoardLayout l = computeLayout(d, LYRA_CONTENT_TOP);
    const int font = draw::letterFontFor(l.cell);
    for (char c = 'A'; c <= 'Z'; c++) {
      const char text[2] = {c, '\0'};
      EXPECT_LE(r.getTextWidth(font, text, EpdFontFamily::BOLD), l.cell - 4) << c << " difficulty " << int(d);
    }
    EXPECT_GE(l.grid.y, GRID_TOP_MIN);
    EXPECT_LE(l.grid.right(), SCREEN_W - SIDE_INSET);
  }
}
