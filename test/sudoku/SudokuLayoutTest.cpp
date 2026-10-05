// Sudoku screen geometry, touch targets, the touch tracker (a copy of Crossword's, any digit key
// or Erase can hold) and the 7x11 notes digits.
#include <gtest/gtest.h>

#include <initializer_list>
#include <set>
#include <string>

#include "Sudoku.h"

using namespace sd;

namespace {

struct Point {
  int x;
  int y;
};

Point centre(const Rect& r) { return Point{r.x + r.w / 2, r.y + r.h / 2}; }

int targetId(const Target& t) { return static_cast<int>(t.kind) * 100 + t.index; }

// Frames of one contact: held at each point in turn (one frame every stepMs), then the release.
ContactEvent run(Contact& c, const std::initializer_list<Point> points, const uint32_t stepMs,
                 const bool banner = false, uint32_t* now = nullptr) {
  uint32_t t = now ? *now : 1000;
  ContactEvent last;
  for (const Point& p : points) {
    last = trackContact(c, banner, true, p.x, p.y, false, t);
    t += stepMs;
  }
  last = trackContact(c, banner, false, 0, 0, true, t);
  if (now) *now = t;
  return last;
}

}  // namespace

// ---- geometry ---------------------------------------------------------------------------------------

TEST(SudokuLayout, GridGeometry) {
  const int lefts[9] = {13, 63, 113, 165, 215, 265, 317, 367, 417};
  const int tops[9] = {119, 169, 219, 271, 321, 371, 423, 473, 523};
  for (int k = 0; k < 9; k++) {
    EXPECT_EQ(cellLeft(k), lefts[k]);
    EXPECT_EQ(cellTop(k), tops[k]);
  }
  EXPECT_EQ(GRID_PX, 459);
  EXPECT_EQ(gridRect().right(), 469);  // x 10..468
  EXPECT_EQ(GRID_BOTTOM, 575);         // y 116..574
  EXPECT_EQ(cellLeft(8) + CELL_PX + BOX_LINE, gridRect().right());
  EXPECT_EQ(cellTop(8) + CELL_PX + BOX_LINE, GRID_BOTTOM);
  EXPECT_GT(GRID_Y, 112);  // below the light panel's swipe band
  // Notes fit inside a square with a margin on every side.
  for (int k = 0; k < 3; k++) {
    EXPECT_GE(NOTE_X[k] - 1, 2);
    EXPECT_LE(NOTE_X[k] + NOTE_DIGIT_W + 1, CELL_PX - 2);
    EXPECT_GE(NOTE_Y[k] - 1, 2);
    EXPECT_LE(NOTE_Y[k] + NOTE_DIGIT_H + 1, CELL_PX - 2);
  }
}

TEST(SudokuLayout, KeysAreDrawnWhereTheSpecSays) {
  for (int d = 1; d <= 9; d++) {
    const Rect r = digitKeyRect(d);
    EXPECT_EQ(r.y, 614);
    EXPECT_EQ(r.bottom(), 702);  // y 614..701
    EXPECT_EQ(r.w, 45);
    EXPECT_EQ(r.x, cellLeft(d - 1) + 2);  // under its column
    EXPECT_TRUE(digitKeyHit(d).contains(r.x, r.y));
    EXPECT_TRUE(digitKeyHit(d).contains(r.right() - 1, r.bottom() - 1));
  }
  int lastRight = TARGET_LEFT;
  for (int k = 0; k < TOOL_COUNT; k++) {
    const Rect r = toolKeyRect(static_cast<Tool>(k));
    EXPECT_EQ(r.y, 710);
    EXPECT_EQ(r.bottom(), 796);  // y 710..795
    EXPECT_EQ(r.w, 112);
    EXPECT_GT(r.x, lastRight);  // keys never touch
    lastRight = r.right();
    const Rect h = toolKeyHit(static_cast<Tool>(k));
    EXPECT_TRUE(h.contains(r.x, r.y));
    EXPECT_TRUE(h.contains(r.right() - 1, r.bottom() - 1));
  }
  EXPECT_LT(lastRight, TARGET_RIGHT);
  EXPECT_EQ(statusRect().y, 578);
  EXPECT_EQ(statusRect().bottom(), 609);
  EXPECT_TRUE(bannerRect().contains(bannerButtonRect().x, bannerButtonRect().y));
  EXPECT_TRUE(bannerRect().contains(bannerButtonRect().right() - 1, bannerButtonRect().bottom() - 1));
  EXPECT_EQ(bannerRect().y, 578);
}

// Every pixel: the bezel is never a target, the grid / digit / tool bands have no gaps, every
// target is reachable, and every drawn square and key is its own target.
TEST(SudokuLayout, EveryTargetCoveredNoneInTheBezel) {
  for (const bool banner : {false, true}) {
    std::set<int> seen;
    for (int y = 0; y < SCREEN_H; y++) {
      for (int x = 0; x < SCREEN_W; x++) {
        const Target t = targetAt(banner, x, y);
        if (x < 7 || x > 473) {
          ASSERT_EQ(t.kind, TargetKind::None) << x << "," << y;
          continue;
        }
        if (t.kind != TargetKind::None) seen.insert(targetId(t));
        if (y >= GRID_Y && y < GRID_BOTTOM) {
          ASSERT_EQ(t.kind, TargetKind::Cell) << x << "," << y;
        } else if (!banner && y >= DIGIT_HIT_TOP && y < DIGIT_HIT_BOTTOM) {
          ASSERT_EQ(t.kind, TargetKind::Digit) << x << "," << y;
        } else if (!banner && y >= TOOL_HIT_TOP && y < TOOL_HIT_BOTTOM) {
          ASSERT_EQ(t.kind, TargetKind::Tool) << x << "," << y;
        } else if (banner && bannerButtonRect().contains(x, y)) {
          ASSERT_EQ(t.kind, TargetKind::BannerButton);
        } else {
          ASSERT_EQ(t.kind, TargetKind::None) << x << "," << y;  // header, status line, below the keys
        }
      }
    }
    EXPECT_EQ(seen.size(), banner ? 82u : 81u + 9u + 4u);
  }
  // Each drawn square and key belongs wholly to its own target.
  for (int i = 0; i < CELLS; i++) {
    const Rect r = cellRect(i);
    for (int y = r.y; y < r.bottom(); y++) {
      for (int x = r.x; x < r.right(); x++) {
        const Target t = targetAt(false, x, y);
        ASSERT_EQ(t.kind, TargetKind::Cell);
        ASSERT_EQ(t.index, i);
      }
    }
  }
  for (int d = 1; d <= 9; d++) {
    const Rect r = digitKeyRect(d);
    for (int y = r.y; y < r.bottom(); y++) {
      for (int x = r.x; x < r.right(); x++) {
        const Target t = targetAt(false, x, y);
        ASSERT_EQ(t.kind, TargetKind::Digit);
        ASSERT_EQ(t.index, d);
      }
    }
  }
  for (int k = 0; k < TOOL_COUNT; k++) {
    const Rect r = toolKeyRect(static_cast<Tool>(k));
    for (int y = r.y; y < r.bottom(); y++) {
      for (int x = r.x; x < r.right(); x++) {
        const Target t = targetAt(false, x, y);
        ASSERT_EQ(t.kind, TargetKind::Tool);
        ASSERT_EQ(t.index, k);
      }
    }
  }
  // Lines split between their neighbours: a thin line goes left / up, a box line 2 + 1.
  EXPECT_EQ(targetAt(false, cellLeft(1) - 1, 200).index, 9 * 1 + 0);
  EXPECT_EQ(targetAt(false, cellLeft(3) - 2, 200).index, 9 * 1 + 2);
  EXPECT_EQ(targetAt(false, cellLeft(3) - 1, 200).index, 9 * 1 + 3);
  EXPECT_EQ(targetAt(false, 7, 140).index, 0);    // the gutter left of the grid
  EXPECT_EQ(targetAt(false, 473, 140).index, 8);  // and right of it
}

// ---- the touch tracker --------------------------------------------------------------------------------

TEST(SudokuContact, TapOnADigitKey) {
  Contact c;
  const Point p = centre(digitKeyRect(7));
  ContactEvent ev = trackContact(c, false, true, p.x, p.y, false, 1000);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Began);
  EXPECT_TRUE(c.active);
  ev = trackContact(c, false, false, 0, 0, true, 1090);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(ev.target.kind, TargetKind::Digit);
  EXPECT_EQ(ev.target.index, 7);
  EXPECT_TRUE(ev.ended);
  EXPECT_FALSE(c.active);
}

TEST(SudokuContact, SlidingOffCancels) {
  Contact c;
  const Point a = centre(digitKeyRect(1));
  const Point b = centre(digitKeyRect(2));
  const ContactEvent ev = run(c, {a, a, b, b, b}, 50);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
  EXPECT_TRUE(ev.ended);
  // The cancelled contact still began on a key: the activity keeps its end from reading as Back.
  EXPECT_EQ(c.start.kind, TargetKind::Digit);
}

TEST(SudokuContact, LiftDriftStillTaps) {
  Contact c;
  uint32_t now = 1000;
  const Point a = centre(cellRect(40));
  const Point b = centre(cellRect(41));
  const Point d = centre(cellRect(42));
  ContactEvent ev = run(c, {a, a, a, b}, 40, false, &now);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(ev.target.index, 40);
  ev = run(c, {a, b, d}, 20, false, &now);  // two moves: a slide
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
  ev = run(c, {a, b, b, b}, 50, false, &now);  // settled on the new square: the player's
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
}

TEST(SudokuContact, DigitsAndEraseHoldOthersTap) {
  Contact c;
  for (int d = 1; d <= 9; d++) {
    const Point p = centre(digitKeyRect(d));
    EXPECT_EQ(trackContact(c, false, true, p.x, p.y, false, 0).kind, ContactEvent::Kind::Began);
    EXPECT_EQ(trackContact(c, false, true, p.x, p.y, false, HOLD_MS - 1).kind, ContactEvent::Kind::None);
    ContactEvent ev = trackContact(c, false, true, p.x, p.y, false, HOLD_MS);
    EXPECT_EQ(ev.kind, ContactEvent::Kind::Hold);
    EXPECT_EQ(ev.target.index, d);
    EXPECT_EQ(trackContact(c, false, true, p.x, p.y, false, HOLD_MS + 500).kind, ContactEvent::Kind::None);
    ev = trackContact(c, false, false, 0, 0, true, HOLD_MS + 600);
    EXPECT_EQ(ev.kind, ContactEvent::Kind::None);  // the release after a hold is not a tap
    EXPECT_TRUE(ev.ended);
  }
  const Point erase = centre(toolKeyRect(Tool::Erase));
  trackContact(c, false, true, erase.x, erase.y, false, 0);
  const ContactEvent ev = trackContact(c, false, true, erase.x, erase.y, false, HOLD_MS);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Hold);
  EXPECT_EQ(ev.target.kind, TargetKind::Tool);
  EXPECT_EQ(ev.target.index, static_cast<int>(Tool::Erase));
  trackContact(c, false, false, 0, 0, true, HOLD_MS + 10);
  // Notes, Undo, Menu and squares never hold: a long press is a tap.
  for (const Point p : {centre(toolKeyRect(Tool::Notes)), centre(toolKeyRect(Tool::Undo)),
                        centre(toolKeyRect(Tool::Menu)), centre(cellRect(10))}) {
    EXPECT_EQ(run(c, {p, p, p}, 500).kind, ContactEvent::Kind::Tap);
  }
  // A hold that slid off its key does not fire.
  const Point one = centre(digitKeyRect(1));
  const Point two = centre(digitKeyRect(2));
  trackContact(c, false, true, one.x, one.y, false, 0);
  trackContact(c, false, true, two.x, two.y, false, 100);
  EXPECT_EQ(trackContact(c, false, true, two.x, two.y, false, HOLD_MS + 100).kind, ContactEvent::Kind::None);
  trackContact(c, false, false, 0, 0, true, HOLD_MS + 200);
}

TEST(SudokuContact, BannerAndBezel) {
  Contact c;
  // With the banner up the keys are gone; its button taps; squares still tap.
  const Point key = centre(digitKeyRect(5));
  EXPECT_EQ(run(c, {key, key}, 40, true).kind, ContactEvent::Kind::None);
  const Point b = centre(bannerButtonRect());
  EXPECT_EQ(run(c, {b, b}, 40, true).target.kind, TargetKind::BannerButton);
  EXPECT_EQ(run(c, {centre(cellRect(3))}, 40, true).target.kind, TargetKind::Cell);
  // A contact from the bezel (the Back swipe) starts on no target and never taps.
  const ContactEvent ev = run(c, {{2, 300}, {60, 300}, {150, 300}}, 30);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
  EXPECT_TRUE(ev.ended);
  EXPECT_EQ(c.start.kind, TargetKind::None);
}

TEST(SudokuContact, EndsWithoutTheReleaseEdge) {
  Contact c;
  const Point m = centre(toolKeyRect(Tool::Undo));
  trackContact(c, false, true, m.x, m.y, false, 0);
  ContactEvent ev = trackContact(c, false, false, 0, 0, false, 40);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(ev.target.index, static_cast<int>(Tool::Undo));
  ev = trackContact(c, false, false, 0, 0, true, 80);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
  EXPECT_FALSE(ev.ended);
}

// ---- notes digits -------------------------------------------------------------------------------------

TEST(SudokuNotesFont, NinetyNineBytesNineDistinctDigits) {
  EXPECT_EQ(sizeof(NOTE_DIGITS_7X11), 99u);
  std::set<std::string> shapes;
  for (int d = 1; d <= 9; d++) {
    std::string shape;
    int ink = 0;
    for (int y = 0; y < NOTE_DIGIT_H; y++) {
      EXPECT_EQ(NOTE_DIGITS_7X11[d - 1][y] & 0x80, 0);  // 7 bits a row
      for (int x = 0; x < NOTE_DIGIT_W; x++) {
        const bool on = notePixel(d, x, y);
        ink += on;
        shape += on ? '#' : '.';
      }
    }
    EXPECT_GT(ink, 15) << d;
    shapes.insert(shape);
    // Ink touches the top and bottom rows (full 11 px height).
    bool top = false, bottom = false;
    for (int x = 0; x < NOTE_DIGIT_W; x++) {
      top |= notePixel(d, x, 0);
      bottom |= notePixel(d, x, NOTE_DIGIT_H - 1);
    }
    EXPECT_TRUE(top && bottom) << d;
  }
  EXPECT_EQ(shapes.size(), 9u);
  EXPECT_FALSE(notePixel(0, 3, 3));
  EXPECT_FALSE(notePixel(10, 3, 3));
  EXPECT_FALSE(notePixel(8, 7, 0));
  EXPECT_FALSE(notePixel(8, 0, 11));
}
