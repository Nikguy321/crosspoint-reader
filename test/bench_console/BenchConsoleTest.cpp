#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "BenchInjection.h"
#include "BenchProtocol.h"

using namespace bench;

// --- CRC32 / base64 ------------------------------------------------------------

TEST(BenchCrc32, MatchesZlibCheckValue) {
  const char* s = "123456789";
  EXPECT_EQ(crc32Update(0, reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
  EXPECT_EQ(crc32Update(0, nullptr, 0), 0u);
}

TEST(BenchCrc32, IncrementalEqualsOneShot) {
  uint8_t data[1000];
  for (int i = 0; i < 1000; ++i) data[i] = static_cast<uint8_t>(i * 7 + 3);
  const uint32_t whole = crc32Update(0, data, sizeof(data));
  uint32_t running = crc32Update(0, data, 333);
  running = crc32Update(running, data + 333, 667);
  EXPECT_EQ(whole, running);
}

TEST(BenchBase64, Rfc4648Vectors) {
  const char* in[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* out[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (int i = 0; i < 7; ++i) {
    char buf[16];
    const size_t n = base64Encode(reinterpret_cast<const uint8_t*>(in[i]), strlen(in[i]), buf, sizeof(buf));
    EXPECT_EQ(n, strlen(out[i]));
    EXPECT_STREQ(buf, out[i]);
  }
}

TEST(BenchBase64, ShotLineFitsAndRefusesSmallBuffer) {
  uint8_t data[SHOT_BYTES_PER_LINE];
  for (size_t i = 0; i < sizeof(data); ++i) data[i] = static_cast<uint8_t>(0xFF - i);
  char buf[base64Length(SHOT_BYTES_PER_LINE) + 1];
  EXPECT_EQ(base64Encode(data, sizeof(data), buf, sizeof(buf)), 128u);
  EXPECT_EQ(base64Encode(data, sizeof(data), buf, sizeof(buf) - 1), 0u);
}

// --- Tokenizing and validation -------------------------------------------------

TEST(BenchCommand, SplitsVerbAndRest) {
  char line[] = "CMD:put 12 0123 /Books/A Tale of Two.epub  ";
  char* verb = nullptr;
  char* rest = nullptr;
  ASSERT_TRUE(splitCommand(line, verb, rest));
  EXPECT_STREQ(verb, "PUT");
  EXPECT_STREQ(rest, "12 0123 /Books/A Tale of Two.epub");

  char* tok = nullptr;
  ASSERT_TRUE(nextToken(rest, tok));
  EXPECT_STREQ(tok, "12");
  ASSERT_TRUE(nextToken(rest, tok));
  EXPECT_STREQ(tok, "0123");
  EXPECT_STREQ(rest, "/Books/A Tale of Two.epub");  // the path runs to end of line
}

TEST(BenchCommand, BareVerbAndNonCommands) {
  char a[] = "CMD:PING";
  char* verb = nullptr;
  char* rest = nullptr;
  ASSERT_TRUE(splitCommand(a, verb, rest));
  EXPECT_STREQ(verb, "PING");
  EXPECT_STREQ(rest, "");
  char* tok = nullptr;
  EXPECT_FALSE(nextToken(rest, tok));

  char b[] = "[123] [INF] [MEM] Free: 1";
  EXPECT_FALSE(splitCommand(b, verb, rest));
  char c[] = "CMD:";
  ASSERT_TRUE(splitCommand(c, verb, rest));
  EXPECT_STREQ(verb, "");
}

TEST(BenchCommand, NumberParsers) {
  uint32_t u = 0;
  EXPECT_TRUE(parseU32("4294967295", u));
  EXPECT_EQ(u, 4294967295u);
  EXPECT_FALSE(parseU32("4294967296", u));
  EXPECT_FALSE(parseU32("12a", u));
  EXPECT_FALSE(parseU32("", u));
  EXPECT_FALSE(parseU32("-1", u));

  int32_t i = 0;
  EXPECT_TRUE(parseI32("-5", i));
  EXPECT_EQ(i, -5);
  EXPECT_TRUE(parseI32("-2147483648", i));
  EXPECT_EQ(i, INT32_MIN);
  EXPECT_FALSE(parseI32("2147483648", i));

  uint32_t h = 0;
  EXPECT_TRUE(parseHex32("cbf43926", h));
  EXPECT_EQ(h, 0xCBF43926u);
  EXPECT_TRUE(parseHex32("DEADBEEF", h));
  EXPECT_EQ(h, 0xDEADBEEFu);
  EXPECT_FALSE(parseHex32("123456789", h));
  EXPECT_FALSE(parseHex32("xyz", h));
}

TEST(BenchCommand, Md5Hex) {
  char good[] = "D41D8CD98F00B204E9800998ECF8427E";
  EXPECT_TRUE(normalizeMd5Hex(good));
  EXPECT_STREQ(good, "d41d8cd98f00b204e9800998ecf8427e");
  char shortHex[] = "d41d8cd98f00b204e9800998ecf8427";
  EXPECT_FALSE(normalizeMd5Hex(shortHex));
  char bad[] = "g41d8cd98f00b204e9800998ecf8427e";
  EXPECT_FALSE(normalizeMd5Hex(bad));
}

TEST(BenchPath, Validation) {
  EXPECT_TRUE(isValidPath("/Books/A Tale of Two Cities.epub", false));
  EXPECT_TRUE(isValidPath("/.crosspoint", false));
  EXPECT_TRUE(isValidPath("/", true));
  EXPECT_FALSE(isValidPath("/", false));
  EXPECT_FALSE(isValidPath("Books/a.epub", false));
  EXPECT_FALSE(isValidPath("/Books//a.epub", false));
  EXPECT_FALSE(isValidPath("/Books/", false));
  EXPECT_FALSE(isValidPath("/Books/../a.epub", false));
  EXPECT_FALSE(isValidPath("/Books/./a.epub", false));
  EXPECT_FALSE(isValidPath("/a\\b", false));
  EXPECT_FALSE(isValidPath("/a\tb", false));
  EXPECT_FALSE(isValidPath("/name.", false));
  EXPECT_FALSE(isValidPath("/name ", false));
  EXPECT_FALSE(isValidPath(nullptr, true));
  for (const char* bad : {"/a\"b", "/a*b", "/a:b", "/a<b", "/a>b", "/a?b", "/a|b"}) {
    EXPECT_FALSE(isValidPath(bad, false)) << bad;  // FAT-reserved characters
  }
  EXPECT_TRUE(isValidPath("/Books/Émile [2nd ed.] (c) & 'more'.epub", false));
  const std::string longPath = "/" + std::string(MAX_PATH_LEN, 'a');
  EXPECT_FALSE(isValidPath(longPath.c_str(), false));
  EXPECT_TRUE(isValidPath(longPath.substr(0, MAX_PATH_LEN).c_str(), false));
}

TEST(BenchPath, PartSuffixAndSlashes) {
  EXPECT_TRUE(isPartPath("/a.epub.bench-part"));
  EXPECT_TRUE(isPartPath("/a.BENCH-PART"));  // FAT names ignore case
  EXPECT_TRUE(isPartPath("/a.Bench-Part"));
  EXPECT_FALSE(isPartPath("/a.epub"));
  EXPECT_FALSE(isPartPath("-part"));
  char p[] = "/Books///";
  trimTrailingSlashes(p);
  EXPECT_STREQ(p, "/Books");
  char root[] = "/";
  trimTrailingSlashes(root);
  EXPECT_STREQ(root, "/");
}

TEST(BenchCat, ArgsAreAnOptionalWindowThenThePath) {
  uint32_t tail = 0;
  char* path = nullptr;
  char plain[] = "/sleep.log";
  ASSERT_TRUE(parseCatArgs(plain, tail, path));
  EXPECT_EQ(tail, CAT_TAIL_DEFAULT);
  EXPECT_STREQ(path, "/sleep.log");

  char windowed[] = "512  /Books/My Book.txt";
  ASSERT_TRUE(parseCatArgs(windowed, tail, path));
  EXPECT_EQ(tail, 512u);
  EXPECT_STREQ(path, "/Books/My Book.txt");

  char clampedHigh[] = "999999 /a.txt";
  ASSERT_TRUE(parseCatArgs(clampedHigh, tail, path));
  EXPECT_EQ(tail, CAT_TAIL_MAX);
  char clampedLow[] = "0 /a.txt";
  ASSERT_TRUE(parseCatArgs(clampedLow, tail, path));
  EXPECT_EQ(tail, 1u);

  // Digits alone are a path candidate (isValidPath then rejects them: no
  // leading '/'), never a window without a path.
  char digitsOnly[] = "4096";
  ASSERT_TRUE(parseCatArgs(digitsOnly, tail, path));
  EXPECT_EQ(tail, CAT_TAIL_DEFAULT);
  EXPECT_STREQ(path, "4096");
  EXPECT_FALSE(isValidPath(path, false));
  char empty[] = "";
  EXPECT_FALSE(parseCatArgs(empty, tail, path));
  EXPECT_FALSE(parseCatArgs(nullptr, tail, path));
  char numericName[] = "123abc";  // digits not followed by a space: the whole thing is the path
  ASSERT_TRUE(parseCatArgs(numericName, tail, path));
  EXPECT_EQ(tail, CAT_TAIL_DEFAULT);
  EXPECT_STREQ(path, "123abc");
}

TEST(BenchChunkHeader, Parses) {
  uint32_t len = 0;
  uint32_t crc = 0;
  EXPECT_TRUE(parseChunkHeader("4096 cbf43926", 4096, len, crc));
  EXPECT_EQ(len, 4096u);
  EXPECT_EQ(crc, 0xCBF43926u);
  EXPECT_TRUE(parseChunkHeader("1 0", 10, len, crc));
  EXPECT_FALSE(parseChunkHeader("4097 0", 4096, len, crc));
  EXPECT_FALSE(parseChunkHeader("0 0", 4096, len, crc));
  EXPECT_FALSE(parseChunkHeader("12", 4096, len, crc));
  EXPECT_FALSE(parseChunkHeader("12 zz", 4096, len, crc));
  EXPECT_FALSE(parseChunkHeader("12 00 extra", 4096, len, crc));
  EXPECT_FALSE(parseChunkHeader("", 4096, len, crc));
  EXPECT_FALSE(parseChunkHeader("PK\x03\x04 binary garbage that is long enough to overflow", 4096, len, crc));
}

TEST(BenchCommand, Nonce) {
  EXPECT_TRUE(isValidNonce("a1B2_-"));
  EXPECT_TRUE(isValidNonce("0123456789abcdef"));
  EXPECT_FALSE(isValidNonce("0123456789abcdefg"));  // 17 chars
  EXPECT_FALSE(isValidNonce(""));
  EXPECT_FALSE(isValidNonce(nullptr));
  EXPECT_FALSE(isValidNonce("a b"));
  EXPECT_FALSE(isValidNonce("a/b"));
}

// --- PutSession ----------------------------------------------------------------

namespace {
struct Chunk {
  std::string header;
  std::string payload;
};
Chunk makeChunk(const std::string& payload) {
  char h[32];
  snprintf(h, sizeof(h), "%zu %08x", payload.size(),
           crc32Update(0, reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
  return {h, payload};
}
PutSession::Action feed(PutSession& s, const Chunk& c, size_t got = SIZE_MAX) {
  const auto a = s.onHeader(c.header.c_str(), true);
  if (a != PutSession::Action::ReadPayload) return a;
  const size_t n = got == SIZE_MAX ? c.payload.size() : got;
  return s.onPayload(reinterpret_cast<const uint8_t*>(c.payload.data()), n);
}
}  // namespace

TEST(BenchPutSession, AckPathAndLastChunkLimit) {
  PutSession s(10);
  EXPECT_FALSE(s.complete());
  EXPECT_EQ(feed(s, makeChunk("abcdef")), PutSession::Action::Write);
  EXPECT_EQ(s.total(), 0u);  // counts only once written
  s.committed();
  EXPECT_EQ(s.total(), 6u);
  // Longer than the 4 bytes still owed: refused as a bad header.
  EXPECT_EQ(feed(s, makeChunk("ghijk")), PutSession::Action::Nak);
  EXPECT_STREQ(s.reason(), "hdr");
  EXPECT_EQ(feed(s, makeChunk("ghij")), PutSession::Action::Write);
  s.committed();
  EXPECT_TRUE(s.complete());
  EXPECT_EQ(s.total(), 10u);
}

TEST(BenchPutSession, NakReasonsAndStreakReset) {
  PutSession s(8192);
  const Chunk good = makeChunk(std::string(100, 'x'));
  EXPECT_EQ(feed(s, good, 50), PutSession::Action::Nak);
  EXPECT_STREQ(s.reason(), "short");
  Chunk bad = good;
  bad.payload[3] = 'y';
  EXPECT_EQ(feed(s, bad), PutSession::Action::Nak);
  EXPECT_STREQ(s.reason(), "crc");
  EXPECT_EQ(s.onHeader("garbage", true), PutSession::Action::Nak);
  EXPECT_STREQ(s.reason(), "hdr");
  EXPECT_EQ(s.onHeader("100 0", false), PutSession::Action::Nak);  // overflowed line
  // A good chunk ends the streak: four more NAKs are allowed again.
  EXPECT_EQ(feed(s, good), PutSession::Action::Write);
  s.committed();
  for (int i = 0; i < PutSession::MAX_NAKS - 1; ++i) EXPECT_EQ(feed(s, bad), PutSession::Action::Nak);
  EXPECT_EQ(feed(s, bad), PutSession::Action::Abort);
  EXPECT_STREQ(s.reason(), "naks");
  EXPECT_EQ(s.total(), 100u);
}

TEST(BenchPutSession, NewCommandAbortsTheUpload) {
  PutSession s(100);
  EXPECT_EQ(s.onHeader("CMD:PING", true), PutSession::Action::Abort);
  EXPECT_STREQ(s.reason(), "interrupted");
}

TEST(BenchKeys, LookupAndClamp) {
  KeyDef k{};
  ASSERT_TRUE(lookupKey("Power", k));
  EXPECT_EQ(k.kind, KeyKind::Button);
  EXPECT_EQ(k.buttonIndex, 6);
  ASSERT_TRUE(lookupKey("up", k));
  EXPECT_EQ(k.buttonIndex, 4);
  ASSERT_TRUE(lookupKey("home", k));
  EXPECT_EQ(k.kind, KeyKind::Home);
  EXPECT_LT(k.defaultHoldMs, TouchInjector::HOME_LONG_PRESS_MS);
  ASSERT_TRUE(lookupKey("HOMELONG", k));
  EXPECT_GT(k.defaultHoldMs, TouchInjector::HOME_LONG_PRESS_MS);
  ASSERT_TRUE(lookupKey("powerdouble", k));
  EXPECT_EQ(k.buttonIndex, 6);
  EXPECT_EQ(k.presses, 2);
  ASSERT_TRUE(lookupKey("homedouble", k));
  EXPECT_EQ(k.kind, KeyKind::Home);
  EXPECT_EQ(k.presses, 2);
  ASSERT_TRUE(lookupKey("confirm", k));
  EXPECT_EQ(k.presses, 1);
  EXPECT_FALSE(lookupKey("hom", k));
  EXPECT_FALSE(lookupKey("homelongg", k));
  EXPECT_EQ(clampHoldMs(0), HOLD_MIN_MS);
  EXPECT_EQ(clampHoldMs(500), 500u);
  EXPECT_EQ(clampHoldMs(999999), HOLD_MAX_MS);
}

// --- KeyInjector against a model of InputManager's debounce ----------------------

namespace {
// InputManager::update() for a digital board: a raw change restarts the 5 ms
// debounce, and the state commits when it has been stable for longer.
struct DebounceModel {
  uint8_t last = 0, current = 0, pressed = 0, released = 0;
  uint32_t lastChange = 0;
  void update(uint32_t now, uint8_t raw) {
    pressed = released = 0;
    if (raw != last) {
      lastChange = now;
      last = raw;
    }
    if (now - lastChange > 5 && raw != current) {
      pressed = raw & ~current;
      released = current & ~raw;
      current = raw;
    }
  }
};
}  // namespace

TEST(BenchKeyInjector, PressHoldReleaseThenDone) {
  KeyInjector key;
  DebounceModel im;
  key.start(4, 60);
  uint32_t now = 1000;
  int pressFrame = -1, releaseFrame = -1, doneFrame = -1;
  uint32_t pressAt = 0, releaseAt = 0;
  for (int frame = 0; frame < 40 && !key.done(); ++frame, now += 10) {
    im.update(now, key.mask());  // the hook is sampled inside update()
    key.onUpdate(now, im.current & (1 << 4));
    if (im.pressed & (1 << 4)) {
      pressFrame = frame;
      pressAt = now;
    }
    if (im.released & (1 << 4)) {
      releaseFrame = frame;
      releaseAt = now;
    }
    if (key.done()) doneFrame = frame;
  }
  ASSERT_GE(pressFrame, 1);  // the first sampling frame only starts the debounce
  ASSERT_GT(releaseFrame, pressFrame);
  EXPECT_GE(releaseAt - pressAt, 60u);
  EXPECT_LE(releaseAt - pressAt, 90u);
  EXPECT_EQ(doneFrame, releaseFrame + 1);  // one full frame after the release edge
  EXPECT_TRUE(key.pressDelivered());
  EXPECT_EQ(key.mask(), 0);
}

TEST(BenchKeyInjector, OredWithPhysicalButtonsAndTimesOut) {
  KeyInjector key;
  key.start(6, 60);
  EXPECT_EQ(key.mask(), 1 << 6);  // asserted from start, before any update
  uint32_t now = 0;
  // Press never commits (e.g. a board without that input path): gives up.
  for (int i = 0; i < 200 && !key.done(); ++i, now += 10) key.onUpdate(now, false);
  EXPECT_TRUE(key.done());
  EXPECT_FALSE(key.pressDelivered());
}

TEST(BenchKeyInjector, PhysicallyHeldButtonDoesNotHang) {
  KeyInjector key;
  key.start(5, 50);
  uint32_t now = 0;
  for (int i = 0; i < 400 && !key.done(); ++i, now += 10) key.onUpdate(now, true);
  EXPECT_TRUE(key.done());
  EXPECT_TRUE(key.pressDelivered());
}

// --- TouchInjector ------------------------------------------------------------------

namespace {
struct Frame {
  bool down, released, tap, longPress, swipe, candidate, held, activity;
  float nx, ny;
};

Frame sample(const TouchInjector& t, uint32_t now) {
  Frame f{};
  float x = 0, y = 0, x2 = 0, y2 = 0;
  unsigned long heldMs = 0;
  f.down = t.down(x, y);
  f.released = t.released();
  f.tap = t.tap(f.nx, f.ny);
  f.longPress = t.longPress(x, y);
  f.swipe = t.swipe(x, y, x2, y2);
  f.candidate = t.tapCandidate(now, x, y, heldMs);
  f.held = t.heldAt(x, y);
  f.activity = t.activity();
  return f;
}

// GfxRenderer::tapToLogical, copied for the round-trip check.
void tapToLogical(int orientation, float nx, float ny, int panelW, int panelH, int& outX, int& outY) {
  int phyX = static_cast<int>(nx * panelW);
  int phyY = static_cast<int>(ny * panelH);
  if (phyX > panelW - 1) phyX = panelW - 1;
  if (phyY > panelH - 1) phyY = panelH - 1;
  switch (orientation) {
    case 0:
      outX = panelH - 1 - phyY;
      outY = phyX;
      break;
    case 2:
      outX = phyY;
      outY = panelW - 1 - phyX;
      break;
    case 1:
      outX = panelW - 1 - phyX;
      outY = panelH - 1 - phyY;
      break;
    default:
      outX = phyX;
      outY = phyY;
      break;
  }
}
}  // namespace

TEST(BenchTouchInjector, TapDeliversDownCandidateThenTapOnRelease) {
  TouchInjector t;
  t.setPanelSize(800, 480);
  t.startContact(123, 45, 123, 45, 80);
  uint32_t now = 5000;
  int frames = 0, taps = 0, downs = 0, releases = 0, activity = 0;
  bool sawCandidate = false;
  while (!t.done() && frames < 50) {
    t.onUpdate(now);
    const Frame f = sample(t, now);
    downs += f.down;
    taps += f.tap;
    releases += f.released;
    activity += f.activity;
    sawCandidate |= f.candidate;
    EXPECT_FALSE(f.swipe);
    EXPECT_FALSE(f.longPress);
    if (f.tap) {
      EXPECT_EQ(static_cast<int>(f.nx * 800), 123);
      EXPECT_EQ(static_cast<int>(f.ny * 480), 45);
      EXPECT_GE(t.lastHeldMs(), 80u);
    }
    now += 10;
    ++frames;
  }
  EXPECT_TRUE(t.done());
  EXPECT_EQ(downs, 1);
  EXPECT_EQ(taps, 1);
  EXPECT_EQ(releases, 1);
  EXPECT_EQ(activity, 2);  // the down frame and the release frame
  EXPECT_TRUE(sawCandidate);
  t.onUpdate(now);
  EXPECT_FALSE(t.busy());
}

TEST(BenchTouchInjector, LongPressFiresOnceAndSuppressionEatsTheLift) {
  TouchInjector t;
  t.startContact(400, 240, 400, 240, 800);
  uint32_t now = 0;
  uint32_t downAt = 0;
  int longs = 0;
  bool sawTapOrRelease = false;
  bool first = true;
  while (!t.done()) {
    t.onUpdate(now);
    if (first) {
      downAt = now;
      first = false;
    }
    float x = 0, y = 0;
    if (t.longPress(x, y)) {
      ++longs;
      EXPECT_GE(now - downAt, TouchInjector::LONG_PRESS_MS);
      t.suppress();  // what MappedInputManager::wasScreenLongPress does
    }
    const Frame f = sample(t, now);
    sawTapOrRelease |= f.tap || f.released;
    now += 10;
  }
  EXPECT_EQ(longs, 1);
  EXPECT_FALSE(sawTapOrRelease);
  // The latch clears once the contact is over: a new contact is delivered normally.
  t.startContact(10, 10, 10, 10, 40);
  int taps = 0;
  while (!t.done()) {
    t.onUpdate(now);
    float x = 0, y = 0;
    taps += t.tap(x, y);
    now += 10;
  }
  EXPECT_EQ(taps, 1);
}

TEST(BenchTouchInjector, FastMoveIsASwipeNotATap) {
  TouchInjector t;
  t.startContact(100, 200, 400, 210, 250);
  uint32_t now = 0;
  int swipes = 0, taps = 0, longs = 0;
  while (!t.done()) {
    t.onUpdate(now);
    float sx = 0, sy = 0, ex = 0, ey = 0;
    if (t.swipe(sx, sy, ex, ey)) {
      ++swipes;
      EXPECT_EQ(static_cast<int>(sx * 800), 100);
      EXPECT_EQ(static_cast<int>(ex * 800), 400);
      EXPECT_EQ(static_cast<int>(ey * 480), 210);
    }
    float x = 0, y = 0;
    taps += t.tap(x, y);
    longs += t.longPress(x, y);
    now += 10;
  }
  EXPECT_EQ(swipes, 1);
  EXPECT_EQ(taps, 0);
  EXPECT_EQ(longs, 0);
}

TEST(BenchTouchInjector, SlowDragIsNeitherSwipeNorTap) {
  TouchInjector t;
  t.startContact(100, 200, 400, 200, 1200);
  uint32_t now = 0;
  int swipes = 0, taps = 0, releases = 0;
  while (!t.done()) {
    t.onUpdate(now);
    float a = 0, b = 0, c = 0, d = 0;
    swipes += t.swipe(a, b, c, d);
    taps += t.tap(a, b);
    releases += t.released();
    now += 10;
  }
  EXPECT_EQ(swipes, 0);
  EXPECT_EQ(taps, 0);
  EXPECT_EQ(releases, 1);  // the raw release edge still arrives
}

TEST(BenchTouchInjector, SmallRollStaysATapButLeavesCandidacy) {
  TouchInjector t;
  t.startContact(300, 300, 340, 300, 100);  // 40 px: past the 28 px hold slop, under the 59 px release slop
  uint32_t now = 0;
  int taps = 0;
  bool candidateAtEnd = true;
  while (!t.done()) {
    t.onUpdate(now);
    float x = 0, y = 0;
    unsigned long held = 0;
    if (t.contactActive()) candidateAtEnd = t.tapCandidate(now, x, y, held);
    taps += t.tap(x, y);
    now += 10;
  }
  EXPECT_EQ(taps, 1);
  EXPECT_FALSE(candidateAtEnd);
}

TEST(BenchTouchInjector, HomeTapAndHomeLongPress) {
  TouchInjector t;
  t.startHomeKey(100);
  uint32_t now = 0;
  int presses = 0, taps = 0, longs = 0, activity = 0;
  while (!t.done()) {
    t.onUpdate(now);
    presses += t.homePressed();
    taps += t.homeTapped();
    longs += t.homeLongPressed();
    activity += t.activity();
    now += 10;
  }
  EXPECT_EQ(presses, 1);
  EXPECT_EQ(taps, 1);
  EXPECT_EQ(longs, 0);
  EXPECT_EQ(activity, 2);

  t.startHomeKey(1000);
  presses = taps = longs = 0;
  uint32_t start = now;
  uint32_t longAt = 0;
  while (!t.done()) {
    t.onUpdate(now);
    presses += t.homePressed();
    taps += t.homeTapped();
    if (t.homeLongPressed()) {
      ++longs;
      longAt = now;
    }
    now += 10;
  }
  EXPECT_EQ(presses, 1);
  EXPECT_EQ(taps, 0);  // the long press suppresses the release tap
  EXPECT_EQ(longs, 1);
  EXPECT_GE(longAt - start, TouchInjector::HOME_LONG_PRESS_MS);
}

TEST(BenchOrientation, LogicalToPanelInvertsTapToLogical) {
  const int pw = 800, ph = 480;
  for (int o = 0; o < 4; ++o) {
    const bool portrait = o == 0 || o == 2;
    const int w = portrait ? ph : pw;
    const int h = portrait ? pw : ph;
    const int xs[] = {0, 1, w / 2, w - 2, w - 1};
    const int ys[] = {0, 1, h / 3, h - 2, h - 1};
    for (int x : xs) {
      for (int y : ys) {
        int px = -1, py = -1;
        logicalToPanel(o, x, y, pw, ph, px, py);
        ASSERT_GE(px, 0);
        ASSERT_LT(px, pw);
        ASSERT_GE(py, 0);
        ASSERT_LT(py, ph);
        TouchInjector t;
        t.setPanelSize(pw, ph);
        t.startContact(px, py, px, py, 40);
        t.onUpdate(0);
        float nx = 0, ny = 0;
        ASSERT_TRUE(t.down(nx, ny));
        int lx = -1, ly = -1;
        tapToLogical(o, nx, ny, pw, ph, lx, ly);
        EXPECT_EQ(lx, x) << "orientation " << o;
        EXPECT_EQ(ly, y) << "orientation " << o;
      }
    }
  }
}

// --- HostPresence -------------------------------------------------------------------

TEST(BenchHostPresence, DebouncesSofGlitches) {
  HostPresence h;
  h.update(0, false);
  EXPECT_FALSE(h.present());
  h.update(100, true);
  EXPECT_TRUE(h.present());
  h.update(1000, false);  // glitch
  h.update(2099, false);
  EXPECT_TRUE(h.present());
  h.update(2100, true);  // back before the window closed
  h.update(4000, false);
  h.update(5000, false);
  EXPECT_TRUE(h.present());
  h.update(5999, false);
  EXPECT_TRUE(h.present());
  h.update(6000, false);  // 2 s of continuous absence, 4 readings
  EXPECT_FALSE(h.present());
  h.update(6100, true);
  EXPECT_TRUE(h.present());
}

TEST(BenchHostPresence, OneReadingAfterALongStallKeepsTheHost) {
  HostPresence h;
  h.update(0, true);
  // The main loop was blocked for 30 s; the first reading afterwards is a flap.
  h.update(30000, false);
  EXPECT_TRUE(h.present());
  h.update(30010, true);
  EXPECT_TRUE(h.present());
  // Two readings 3 s apart are still not enough: three are needed.
  h.update(40000, false);
  h.update(43000, false);
  EXPECT_TRUE(h.present());
  h.update(43010, false);
  EXPECT_FALSE(h.present());
}

// --- Double presses, finishSoon ---------------------------------------------------

TEST(BenchKeyInjector, DoubleClickPlaysTwoPressesAndReportsHeld) {
  KeyInjector key;
  DebounceModel im;
  key.start(6, 60, 2, 100);
  EXPECT_EQ(key.buttonIndex(), 6);
  uint32_t now = 0;
  std::vector<uint32_t> pressAt, releaseAt;
  for (int frame = 0; frame < 200 && !key.done(); ++frame, now += 10) {
    im.update(now, key.mask());
    key.onUpdate(now, im.current & (1 << 6));
    if (im.pressed & (1 << 6)) pressAt.push_back(now);
    if (im.released & (1 << 6)) releaseAt.push_back(now);
  }
  ASSERT_TRUE(key.done());
  ASSERT_EQ(pressAt.size(), 2u);
  ASSERT_EQ(releaseAt.size(), 2u);
  EXPECT_TRUE(key.pressDelivered());
  EXPECT_GE(pressAt[1] - releaseAt[0], 100u);
  EXPECT_LT(releaseAt[1] - releaseAt[0], 500u);  // inside the power double-click window
  EXPECT_EQ(key.heldMs(), releaseAt[1] - pressAt[1]);
}

TEST(BenchKeyInjector, FinishSoonReleasesAHeldPress) {
  KeyInjector key;
  DebounceModel im;
  key.start(5, 5000);
  uint32_t now = 0;
  for (int i = 0; i < 5; ++i, now += 10) {
    im.update(now, key.mask());
    key.onUpdate(now, im.current & (1 << 5));
  }
  ASSERT_TRUE(key.anyDelivered());
  key.finishSoon();
  EXPECT_EQ(key.mask(), 0);
  bool released = false;
  for (int i = 0; i < 10 && !key.done(); ++i, now += 10) {
    im.update(now, key.mask());
    key.onUpdate(now, im.current & (1 << 5));
    released |= (im.released & (1 << 5)) != 0;
  }
  EXPECT_TRUE(released);
  EXPECT_TRUE(key.done());
}

TEST(BenchTouchInjector, HomeDoubleTapGivesTwoTapsInsideTheWindow) {
  TouchInjector t;
  t.startHomeKey(60, 2, 100);
  uint32_t now = 0;
  std::vector<uint32_t> tapAt;
  int presses = 0;
  while (!t.done() && now < 5000) {
    t.onUpdate(now);
    presses += t.homePressed();
    if (t.homeTapped()) tapAt.push_back(now);
    now += 10;
  }
  EXPECT_EQ(presses, 2);
  ASSERT_EQ(tapAt.size(), 2u);
  EXPECT_LE(tapAt[1] - tapAt[0], 350u);  // HomeButtonInput::DOUBLE_TAP_MS
  EXPECT_EQ(t.heldMs(), 60u);
}

TEST(BenchTouchInjector, FinishSoonLiftsTheFingerWithARealRelease) {
  TouchInjector t;
  t.setPanelSize(800, 480);
  t.startContact(100, 100, 100, 100, 5000);
  uint32_t now = 0;
  t.onUpdate(now);
  EXPECT_TRUE(t.delivered());
  now += 10;
  t.onUpdate(now);
  t.finishSoon();
  now += 10;
  t.onUpdate(now);
  EXPECT_TRUE(t.released());  // a release frame, not a vanished contact
  EXPECT_FALSE(t.contactActive());
  now += 10;
  t.onUpdate(now);
  EXPECT_TRUE(t.done());

  // Not started yet: dropped without any frame.
  t.startContact(1, 1, 1, 1, 80);
  EXPECT_FALSE(t.delivered());
  t.finishSoon();
  EXPECT_FALSE(t.busy());
  t.onUpdate(now);
  float x = 0, y = 0;
  EXPECT_FALSE(t.down(x, y));
}

// --- InputWaiter ---------------------------------------------------------------------

TEST(BenchInputWaiter, OkAfterDoneAndSettle) {
  InputWaiter w;
  w.start(1000, 60, 400, true);
  EXPECT_EQ(w.poll(1050, false, false), InputWaiter::Verdict::Wait);
  EXPECT_EQ(w.poll(1100, true, true), InputWaiter::Verdict::Wait);  // settle starts
  EXPECT_EQ(w.poll(1499, true, true), InputWaiter::Verdict::Wait);
  EXPECT_EQ(w.poll(1500, true, true), InputWaiter::Verdict::Ok);
  EXPECT_FALSE(w.late());
}

TEST(BenchInputWaiter, NoEdgeOnlyForKeys) {
  InputWaiter w;
  w.start(0, 60, 0, true);
  EXPECT_EQ(w.poll(100, true, false), InputWaiter::Verdict::ErrNoEdge);
  w.start(0, 60, 0, false);
  EXPECT_EQ(w.poll(100, true, false), InputWaiter::Verdict::Ok);
}

TEST(BenchInputWaiter, BlockedLoopIsLateNotAnError) {
  InputWaiter w;
  w.start(0, 800, 0, false);
  // The loop was blocked for 20 s: the release frame is being delivered now.
  EXPECT_EQ(w.poll(20000, false, false), InputWaiter::Verdict::Wait);
  EXPECT_TRUE(w.late());
  EXPECT_EQ(w.poll(20010, true, false), InputWaiter::Verdict::Ok);
  EXPECT_TRUE(w.late());
}

TEST(BenchInputWaiter, StuckNeedsPassesAndTime) {
  InputWaiter w;
  w.start(0, 60, 0, false);
  uint32_t now = 3060;
  InputWaiter::Verdict v = InputWaiter::Verdict::Wait;
  // Many passes but little time: still waiting.
  for (int i = 0; i < 100; ++i, now += 1) v = w.poll(now, false, false);
  EXPECT_EQ(v, InputWaiter::Verdict::Wait);
  // Enough time too: an error.
  now += InputWaiter::STUCK_MS;
  EXPECT_EQ(w.poll(now, false, false), InputWaiter::Verdict::ErrTimeout);
}

TEST(BenchSleep, PlainOrDeep) {
  SleepKind kind = SleepKind::Deep;
  EXPECT_TRUE(parseSleepArgs("", kind));
  EXPECT_EQ(kind, SleepKind::Default);
  EXPECT_TRUE(parseSleepArgs(nullptr, kind));
  EXPECT_EQ(kind, SleepKind::Default);
  EXPECT_TRUE(parseSleepArgs("deep", kind));
  EXPECT_EQ(kind, SleepKind::Deep);
  EXPECT_TRUE(parseSleepArgs(" DEEP ", kind));
  EXPECT_EQ(kind, SleepKind::Deep);
  EXPECT_FALSE(parseSleepArgs("deeper", kind));
  EXPECT_FALSE(parseSleepArgs("live", kind));
  EXPECT_FALSE(parseSleepArgs("dee", kind));
}

TEST(BenchWifiLast, SsidIsTheRestOfTheLine) {
  EXPECT_TRUE(isValidSsid("Home"));
  EXPECT_TRUE(isValidSsid("Cafe Guest 5G"));
  EXPECT_TRUE(isValidSsid("12345678901234567890123456789012"));    // 32 bytes
  EXPECT_FALSE(isValidSsid("123456789012345678901234567890123"));  // 33
  EXPECT_FALSE(isValidSsid(""));
  EXPECT_FALSE(isValidSsid(nullptr));
  EXPECT_FALSE(isValidSsid("tab\there"));
  EXPECT_FALSE(isValidSsid("del\x7f"));
}

TEST(BenchWeather, ShowFetchOrClear) {
  WeatherOp op = WeatherOp::Fetch;
  EXPECT_TRUE(parseWeatherArgs("", op));
  EXPECT_EQ(op, WeatherOp::Show);
  EXPECT_TRUE(parseWeatherArgs(nullptr, op));
  EXPECT_EQ(op, WeatherOp::Show);
  EXPECT_TRUE(parseWeatherArgs(" FETCH ", op));
  EXPECT_EQ(op, WeatherOp::Fetch);
  EXPECT_TRUE(parseWeatherArgs("clear", op));
  EXPECT_EQ(op, WeatherOp::Clear);
  EXPECT_TRUE(parseWeatherArgs("Show", op));
  EXPECT_EQ(op, WeatherOp::Show);
  EXPECT_FALSE(parseWeatherArgs("fetch now", op));
  EXPECT_FALSE(parseWeatherArgs("refresh", op));
  EXPECT_FALSE(parseWeatherArgs("fetchfetch", op));
  EXPECT_FALSE(parseWeatherArgs("fetc", op));
}

TEST(BenchApp, AppsOrWordSearch) {
  AppTarget target = AppTarget::Apps;
  EXPECT_TRUE(parseAppArgs("wordsearch", target));
  EXPECT_EQ(target, AppTarget::WordSearch);
  EXPECT_TRUE(parseAppArgs(" APPS ", target));
  EXPECT_EQ(target, AppTarget::Apps);
  EXPECT_TRUE(parseAppArgs("WordSearch", target));
  EXPECT_EQ(target, AppTarget::WordSearch);
  EXPECT_FALSE(parseAppArgs("", target));
  EXPECT_FALSE(parseAppArgs(nullptr, target));
  EXPECT_FALSE(parseAppArgs("word search", target));
  EXPECT_FALSE(parseAppArgs("crossword", target));
  EXPECT_FALSE(parseAppArgs("apps now", target));
  EXPECT_FALSE(parseAppArgs("wordsearchwordsearch", target));
}

TEST(BenchWs, DumpOrNewPuzzle) {
  WsArgs ws;
  char none[] = "";
  EXPECT_TRUE(parseWsArgs(none, ws));
  EXPECT_FALSE(ws.newPuzzle);
  EXPECT_TRUE(parseWsArgs(nullptr, ws));
  EXPECT_FALSE(ws.newPuzzle);

  char seedOnly[] = "new 1234";
  ASSERT_TRUE(parseWsArgs(seedOnly, ws));
  EXPECT_TRUE(ws.newPuzzle);
  EXPECT_EQ(ws.seed, 1234u);
  EXPECT_EQ(ws.difficulty, -1);
  EXPECT_STREQ(ws.themeKey, "");

  char full[] = "NEW 7 Hard animals";
  ASSERT_TRUE(parseWsArgs(full, ws));
  EXPECT_EQ(ws.seed, 7u);
  EXPECT_EQ(ws.difficulty, 2);
  EXPECT_STREQ(ws.themeKey, "animals");

  char easy[] = "new 0 easy";
  ASSERT_TRUE(parseWsArgs(easy, ws));
  EXPECT_EQ(ws.difficulty, 0);
  EXPECT_STREQ(ws.themeKey, "");

  // No difficulty: the rest of the line is the key, spaces and all.
  char file[] = "new 99 file:Pond Life.words  ";
  ASSERT_TRUE(parseWsArgs(file, ws));
  EXPECT_EQ(ws.difficulty, -1);
  EXPECT_STREQ(ws.themeKey, "file:Pond Life.words");

  char fileWithLevel[] = "new 4294967295 medium file:My Words.words";
  ASSERT_TRUE(parseWsArgs(fileWithLevel, ws));
  EXPECT_EQ(ws.seed, 4294967295u);
  EXPECT_EQ(ws.difficulty, 1);
  EXPECT_STREQ(ws.themeKey, "file:My Words.words");

  // "mediums" is a key, not a difficulty.
  char nearMiss[] = "new 1 mediums";
  ASSERT_TRUE(parseWsArgs(nearMiss, ws));
  EXPECT_EQ(ws.difficulty, -1);
  EXPECT_STREQ(ws.themeKey, "mediums");

  char noSeed[] = "new";
  EXPECT_FALSE(parseWsArgs(noSeed, ws));
  char badSeed[] = "new -3";
  EXPECT_FALSE(parseWsArgs(badSeed, ws));
  char other[] = "dump";
  EXPECT_FALSE(parseWsArgs(other, ws));
  char tooLong[] = "new 1 file:0123456789012345678901234567890123456789.words";
  EXPECT_FALSE(parseWsArgs(tooLong, ws));
  char control[] = "new 1 ani\x01mals";
  EXPECT_FALSE(parseWsArgs(control, ws));
}

TEST(BenchPins, Seconds) {
  uint32_t s = 0;
  EXPECT_TRUE(parsePinsArgs("", s));
  EXPECT_EQ(s, PINS_DEFAULT_SECONDS);
  EXPECT_TRUE(parsePinsArgs(nullptr, s));
  EXPECT_EQ(s, PINS_DEFAULT_SECONDS);
  EXPECT_TRUE(parsePinsArgs(" 30 ", s));
  EXPECT_EQ(s, 30u);
  EXPECT_TRUE(parsePinsArgs("180", s));
  EXPECT_EQ(s, 180u);
  EXPECT_TRUE(parsePinsArgs("1", s));
  EXPECT_EQ(s, 1u);
  EXPECT_FALSE(parsePinsArgs("0", s));
  EXPECT_FALSE(parsePinsArgs("181", s));
  EXPECT_FALSE(parsePinsArgs("ten", s));
  EXPECT_FALSE(parsePinsArgs("10 20", s));
  EXPECT_FALSE(parsePinsArgs("99999999999999", s));
}

TEST(BenchPins, ProbePinsAreFreeAndSafe) {
  for (const uint8_t pin : X4PRO_PROBE_PINS) {
    EXPECT_FALSE(isReservedS3Pin(pin)) << int(pin);
    for (const uint8_t used : X4PRO_ASSIGNED_PINS) EXPECT_NE(pin, used);
  }
  for (const uint8_t pin : X4PRO_WATCH_ASSIGNED_PINS) {
    EXPECT_FALSE(isReservedS3Pin(pin)) << int(pin);
    bool assigned = false;
    for (const uint8_t used : X4PRO_ASSIGNED_PINS) assigned = assigned || used == pin;
    EXPECT_TRUE(assigned) << int(pin);
  }
  // The power latch is never watched or probed.
  for (const uint8_t pin : X4PRO_PROBE_PINS) EXPECT_NE(pin, 1);
  for (const uint8_t pin : X4PRO_WATCH_ASSIGNED_PINS) EXPECT_NE(pin, 1);
  // Every GPIO the S3 has is either assigned, reserved or probed: none was forgotten.
  for (uint8_t pin = 0; pin <= PINS_MAX_GPIO; ++pin) {
    bool known = isReservedS3Pin(pin);
    for (const uint8_t used : X4PRO_ASSIGNED_PINS) known = known || used == pin;
    for (const uint8_t probe : X4PRO_PROBE_PINS) known = known || probe == pin;
    EXPECT_TRUE(known) << int(pin);
  }
  EXPECT_TRUE(isReservedS3Pin(19));
  EXPECT_TRUE(isReservedS3Pin(20));
  EXPECT_TRUE(isReservedS3Pin(26));
  EXPECT_TRUE(isReservedS3Pin(37));
  EXPECT_TRUE(isReservedS3Pin(43));
  EXPECT_TRUE(isReservedS3Pin(44));
  EXPECT_TRUE(isReservedS3Pin(49));
  EXPECT_FALSE(isReservedS3Pin(38));
}

TEST(BenchPins, ChangeBudgetPerSecond) {
  ChangeBudget budget(3);
  EXPECT_TRUE(budget.allow(100));
  EXPECT_TRUE(budget.allow(150));
  EXPECT_TRUE(budget.allow(900));
  EXPECT_FALSE(budget.allow(1000));
  EXPECT_FALSE(budget.allow(1099));
  EXPECT_EQ(budget.dropped(), 2u);
  EXPECT_TRUE(budget.allow(1100));  // a new window
  EXPECT_TRUE(budget.allow(2050));
  EXPECT_EQ(budget.dropped(), 2u);
}
