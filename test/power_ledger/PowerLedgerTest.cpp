#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "util/PowerLedger.h"
#include "util/SleepLedger.h"

using namespace power_ledger;

TEST(PowerLedger, FormatsOneWindow) {
  Window w;
  w.unixTime = 1759100000;
  w.uptimeS = 1800;
  w.socPercent = 83;
  w.millivolts = 3987;
  w.cpuMhz = 80;
  w.frontlight = 0;
  w.renders = 7;
  w.lightSleepPermille = 912;
  w.lightSleeps = 5470;
  w.lightSleepEnabled = true;
  char buf[LINE_CAP];
  const size_t n = formatLine(buf, sizeof buf, w);
  EXPECT_EQ(std::string(buf),
            "1759100000 pwr up=1800 soc=83 mv=3987 mhz=80 fl=0 wifi=0 usb=0 host=0 rnd=7 ls=912 lsn=5470 lse=1 live=0 "
            "heap=0 blk=0\n");
  EXPECT_EQ(n, strlen(buf));
}

TEST(PowerLedger, FlagsAndLight) {
  Window w;
  w.cpuMhz = 240;
  w.frontlight = 35;
  w.wifi = true;
  w.usb = true;
  w.host = true;
  w.live = true;
  w.heapFree = 61234;
  w.heapLargest = 40960;
  char buf[LINE_CAP];
  ASSERT_GT(formatLine(buf, sizeof buf, w), 0u);
  EXPECT_EQ(std::string(buf),
            "0 pwr up=0 soc=0 mv=0 mhz=240 fl=35 wifi=1 usb=1 host=1 rnd=0 ls=0 lsn=0 lse=0 live=1 heap=61234 "
            "blk=40960\n");
}

TEST(PowerLedger, WidestLineFitsBothCaps) {
  Window w;
  w.unixTime = 4294967295u;
  w.uptimeS = 4294967295u;
  w.socPercent = 100;
  w.millivolts = 65535;
  w.cpuMhz = 240;
  w.frontlight = 100;
  w.wifi = w.usb = w.host = w.lightSleepEnabled = w.live = true;
  w.heapFree = 4294967295u;
  w.heapLargest = 4294967295u;
  w.renders = 4294967295u;
  w.lightSleepPermille = 1000;
  w.lightSleeps = 4294967295u;
  char buf[LINE_CAP];
  const size_t n = formatLine(buf, sizeof buf, w);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(buf[n - 1], '\n');
  EXPECT_LT(n, LINE_CAP);
  // CAT on the bench cuts lines far beyond this; the ledger's own lines stay short too.
  EXPECT_LE(n, 2 * sleep_ledger::LINE_CAP);
}

TEST(PowerLedger, RefusesATooSmallBuffer) {
  Window w;
  char buf[16];
  EXPECT_EQ(formatLine(buf, sizeof buf, w), 0u);
}

TEST(PowerLedger, Permille) {
  EXPECT_EQ(permille(0, 0), 0u);
  EXPECT_EQ(permille(0, 300000000ULL), 0u);
  EXPECT_EQ(permille(150000000ULL, 300000000ULL), 500u);
  EXPECT_EQ(permille(299999999ULL, 300000000ULL), 1000u);  // rounds
  EXPECT_EQ(permille(300000000ULL, 300000000ULL), 1000u);
  EXPECT_EQ(permille(400000000ULL, 300000000ULL), 1000u);  // clamped
  EXPECT_EQ(permille(273600000ULL, 300000000ULL), 912u);
}

TEST(PowerLedger, WindowIsFiveMinutes) { EXPECT_EQ(WINDOW_MS, 300000u); }

// A window lit for all but its last instant is a lit window: the analysis
// filter (fl=0 wifi=0 usb=0) must not admit it.
TEST(PowerLedger, LatchKeepsTheWholeWindow) {
  WindowLatch latch;
  latch.note(35, false, false, false);
  latch.note(0, true, false, false);
  latch.note(10, false, true, false);
  latch.note(0, false, false, true);
  latch.note(0, false, false, false);  // the instant the line is written
  Window w;
  latch.applyTo(w);
  EXPECT_EQ(w.frontlight, 35u);
  EXPECT_TRUE(w.wifi);
  EXPECT_TRUE(w.usb);
  EXPECT_TRUE(w.host);
}

TEST(PowerLedger, FreshLatchIsAQuietWindow) {
  WindowLatch latch;
  latch.note(0, false, false, false);
  Window w;
  w.frontlight = 99;
  w.wifi = w.usb = w.host = true;
  latch.applyTo(w);
  EXPECT_EQ(w.frontlight, 0u);
  EXPECT_FALSE(w.wifi);
  EXPECT_FALSE(w.usb);
  EXPECT_FALSE(w.host);
  latch.note(20, false, false, false);
  latch = WindowLatch{};  // the next window starts clean
  latch.applyTo(w);
  EXPECT_EQ(w.frontlight, 0u);
}

TEST(PowerLedger, LatchHoldsLiveForTheWholeWindow) {
  WindowLatch latch;
  latch.note(0, false, true, false);
  latch.note(0, true, true, false, true);
  latch.note(0, false, false, false, false);
  Window w;
  latch.applyTo(w);
  EXPECT_TRUE(w.live);
  EXPECT_TRUE(w.wifi);
  EXPECT_TRUE(w.usb);
  WindowLatch quiet;
  quiet.note(0, false, false, false);
  quiet.applyTo(w);
  EXPECT_FALSE(w.live);
}
