#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "util/SleepLedger.h"

using namespace sleep_ledger;

TEST(SleepLedger, FormatsOneLinePerEvent) {
  Entry e;
  e.unixTime = 1759100000;
  e.event = "sleep";
  e.uptimeS = 1234;
  e.resetReason = 5;
  e.wakeCause = 7;
  e.extra = 1;
  e.socPercent = 83;
  e.millivolts = 3987;
  char buf[LINE_CAP];
  const size_t n = formatLine(buf, sizeof buf, e);
  EXPECT_EQ(std::string(buf), "1759100000 sleep up=1234 rst=5 wake=7 x=1 soc=83 mv=3987\n");
  EXPECT_EQ(n, strlen(buf));
}

TEST(SleepLedger, ZeroFieldsWhenNothingIsKnown) {
  Entry e;
  e.event = "boot";
  char buf[LINE_CAP];
  ASSERT_GT(formatLine(buf, sizeof buf, e), 0u);
  EXPECT_EQ(std::string(buf), "0 boot up=0 rst=0 wake=0 x=0 soc=0 mv=0\n");
}

TEST(SleepLedger, WorstCaseFitsTheLineCap) {
  Entry e;
  e.unixTime = 0xFFFFFFFFu;
  e.event = "resleep";
  e.uptimeS = 0xFFFFFFFFu;
  e.resetReason = -2147483647;
  e.wakeCause = -2147483647;
  e.extra = -2147483647;
  e.socPercent = 4294967295u;
  e.millivolts = 4294967295u;
  char buf[LINE_CAP];
  const size_t n = formatLine(buf, sizeof buf, e);
  EXPECT_GT(n, 0u);
  EXPECT_LT(n, LINE_CAP);
  EXPECT_EQ(buf[n - 1], '\n');
}

TEST(SleepLedger, RefusesABufferThatIsTooSmall) {
  Entry e;
  e.event = "cut";
  char buf[8];
  EXPECT_EQ(formatLine(buf, sizeof buf, e), 0u);
}

TEST(SleepLedger, PathIsAtTheCardRoot) {
  EXPECT_STREQ(PATH, "/sleep.log");
  EXPECT_STREQ(ROTATED_PATH, "/sleep.log.1");
}

TEST(SleepLedger, RotatesOnlyPastTheCap) {
  EXPECT_FALSE(shouldRotate(0));
  EXPECT_FALSE(shouldRotate(ROTATE_BYTES));
  EXPECT_TRUE(shouldRotate(ROTATE_BYTES + 1));
  // A day of ghost wakes (~170 KB) rolls over; a quiet month (~60 KB) does not.
  EXPECT_TRUE(shouldRotate(170 * 1024));
  EXPECT_FALSE(shouldRotate(60 * 1024));
}
