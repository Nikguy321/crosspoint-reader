// Crossword touch contacts (CwContact): a tap starts and ends on one target, lift drift is
// forgiven, sliding off cancels, holding Del fires once.
#include <gtest/gtest.h>

#include "Crossword.h"

using namespace cw;

namespace {

struct Point {
  int x;
  int y;
};

Point keyCentre(const int index) {
  const Rect& r = keyboardKey(index).rect;
  return Point{r.x + r.w / 2, r.y + r.h / 2};
}

// Frames of one contact: held at each point in turn (one frame every stepMs), then the release.
ContactEvent run(Contact& c, const ScreenLayout& l, const std::initializer_list<Point> points, const uint32_t stepMs,
                 const bool banner = false, uint32_t* now = nullptr) {
  uint32_t t = now ? *now : 1000;
  ContactEvent last;
  for (const Point& p : points) {
    last = trackContact(c, l, banner, true, p.x, p.y, false, t);
    t += stepMs;
  }
  last = trackContact(c, l, banner, false, 0, 0, true, t);
  if (now) *now = t;
  return last;
}

}  // namespace

TEST(CrosswordContact, TapOnAKey) {
  const ScreenLayout l = computeLayout(5, 5);
  Contact c;
  const int q = keyForLetter('Q');
  const Point p = keyCentre(q);
  ContactEvent ev = trackContact(c, l, false, true, p.x, p.y, false, 1000);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Began);
  EXPECT_TRUE(c.active);
  ev = trackContact(c, l, false, false, 0, 0, true, 1090);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(ev.target.kind, TargetKind::Key);
  EXPECT_EQ(ev.target.index, q);
  EXPECT_TRUE(ev.ended);
  EXPECT_FALSE(c.active);
}

TEST(CrosswordContact, SlidingOffCancels) {
  const ScreenLayout l = computeLayout(5, 5);
  Contact c;
  // Rests on Q, slides to W and rests there before lifting.
  const ContactEvent ev = run(c, l, {keyCentre(0), keyCentre(0), keyCentre(1), keyCentre(1), keyCentre(1)}, 50);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
  EXPECT_TRUE(ev.ended);
  // The cancelled contact still began on a key: the activity keeps its end from reading as Back.
  EXPECT_EQ(c.start.kind, TargetKind::Key);
  EXPECT_FALSE(c.active);
}

TEST(CrosswordContact, LiftDriftStillTaps) {
  const ScreenLayout l = computeLayout(5, 5);
  Contact c;
  uint32_t now = 1000;
  // On E for 120 ms, then the last sample (20 ms before the release) has rolled onto R.
  ContactEvent ev = run(c, l, {keyCentre(2), keyCentre(2), keyCentre(2), keyCentre(3)}, 40, false, &now);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(ev.target.index, 2);
  // Two moves (E -> R -> T) are a slide, not drift.
  ev = run(c, l, {keyCentre(2), keyCentre(3), keyCentre(4)}, 20, false, &now);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
  // A move that settled more than SETTLE_MS before the end is the player's.
  ev = run(c, l, {keyCentre(2), keyCentre(3), keyCentre(3), keyCentre(3)}, 50, false, &now);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
}

TEST(CrosswordContact, HoldingDelFiresOnce) {
  const ScreenLayout l = computeLayout(5, 5);
  Contact c;
  const Point del = keyCentre(delKey());
  EXPECT_EQ(trackContact(c, l, false, true, del.x, del.y, false, 0).kind, ContactEvent::Kind::Began);
  EXPECT_EQ(trackContact(c, l, false, true, del.x, del.y, false, DEL_HOLD_MS - 1).kind, ContactEvent::Kind::None);
  ContactEvent ev = trackContact(c, l, false, true, del.x, del.y, false, DEL_HOLD_MS);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Hold);
  EXPECT_EQ(ev.target.index, delKey());
  EXPECT_EQ(trackContact(c, l, false, true, del.x, del.y, false, DEL_HOLD_MS + 500).kind, ContactEvent::Kind::None);
  ev = trackContact(c, l, false, false, 0, 0, true, DEL_HOLD_MS + 600);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);  // the release after a hold is not a tap
  EXPECT_TRUE(ev.ended);
  // A short press of Del is a tap; a long press of a letter is still a tap.
  EXPECT_EQ(run(c, l, {del, del}, 50).kind, ContactEvent::Kind::Tap);
  const Point a = keyCentre(keyForLetter('A'));
  const ContactEvent longA = run(c, l, {a, a, a}, 500);
  EXPECT_EQ(longA.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(longA.target.index, keyForLetter('A'));
}

TEST(CrosswordContact, GapsCellsAndTheBanner) {
  const ScreenLayout l = computeLayout(5, 5);
  Contact c;
  // Between Q and W (the 2 px gap): the nearer key.
  const Rect& q = keyboardKey(0).rect;
  ContactEvent gap = run(c, l, {{q.right(), q.y + 20}, {q.right(), q.y + 20}}, 40);
  EXPECT_EQ(gap.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(gap.target.index, 0);
  gap = run(c, l, {{q.right() + 1, q.y + 20}, {q.right() + 1, q.y + 20}}, 40);
  EXPECT_EQ(gap.target.index, 1);
  // Above the keyboard, between the clue bar and the first row: nothing.
  EXPECT_EQ(run(c, l, {{240, KEYBOARD_TOP - 2}}, 40).kind, ContactEvent::Kind::None);
  // A square, and the margin half a square left of the grid.
  ContactEvent ev = run(c, l, {{l.cellX(2) + 10, l.cellY(3) + 10}}, 40);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(ev.target.kind, TargetKind::Cell);
  EXPECT_EQ(ev.target.index, 3 * 5 + 2);
  ev = run(c, l, {{l.grid.x - 10, l.cellY(1) + 5}}, 40);
  EXPECT_EQ(ev.target.index, 5);
  // The clue bar.
  EXPECT_EQ(run(c, l, {{31, 592}}, 40).target.kind, TargetKind::PrevClue);
  EXPECT_EQ(run(c, l, {{449, 592}}, 40).target.kind, TargetKind::NextClue);
  EXPECT_EQ(run(c, l, {{240, 592}}, 40).target.kind, TargetKind::ClueText);
  // With the banner up the keys are gone; its button taps.
  const Point w = keyCentre(1);
  EXPECT_EQ(run(c, l, {w, w}, 40, true).kind, ContactEvent::Kind::None);
  const Point b{l.bannerButton.x + l.bannerButton.w / 2, l.bannerButton.y + l.bannerButton.h / 2};
  EXPECT_EQ(run(c, l, {b, b}, 40, true).target.kind, TargetKind::BannerButton);
}

TEST(CrosswordContact, EndsWithoutTheReleaseEdge) {
  const ScreenLayout l = computeLayout(5, 5);
  Contact c;
  const Point m = keyCentre(keyForLetter('M'));
  trackContact(c, l, false, true, m.x, m.y, false, 0);
  // A second finger: the single-contact query stops reporting it before any release edge.
  ContactEvent ev = trackContact(c, l, false, false, 0, 0, false, 40);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::Tap);
  EXPECT_EQ(ev.target.index, keyForLetter('M'));
  // The late release edge is no second tap.
  ev = trackContact(c, l, false, false, 0, 0, true, 80);
  EXPECT_EQ(ev.kind, ContactEvent::Kind::None);
  EXPECT_FALSE(ev.ended);
}
