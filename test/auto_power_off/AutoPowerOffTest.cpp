#include <gtest/gtest.h>

#include <cstdint>

#include "util/AutoPowerOff.h"

using namespace auto_power_off;

TEST(AutoPowerOff, IndexTableIsStockOrderAndAppendOnly) {
  EXPECT_EQ(minutesForIndex(0), 5);
  EXPECT_EQ(minutesForIndex(1), 10);
  EXPECT_EQ(minutesForIndex(2), 20);
  EXPECT_EQ(minutesForIndex(3), 30);
  EXPECT_EQ(minutesForIndex(4), 60);
  EXPECT_EQ(minutesForIndex(5), 0);  // Never
  EXPECT_EQ(OPTION_COUNT, 6);
  // An out-of-range index (a future option on an older firmware) is Never, not
  // a wild timer.
  EXPECT_EQ(minutesForIndex(6), 0);
  EXPECT_EQ(minutesForIndex(255), 0);
}

TEST(AutoPowerOff, TimerMicrosFollowsTheTable) {
  EXPECT_EQ(timerMicros(0, true), 5ULL * 60 * 1000000);
  EXPECT_EQ(timerMicros(3, true), 30ULL * 60 * 1000000);
  EXPECT_EQ(timerMicros(4, true), 3600ULL * 1000000);
  EXPECT_EQ(timerMicros(5, true), 0u);  // Never = no timer
  EXPECT_EQ(timerMicros(200, true), 0u);
}

TEST(AutoPowerOff, NoTimerWhereTheRailCannotBeCut) {
  for (uint8_t i = 0; i < OPTION_COUNT; ++i) EXPECT_EQ(timerMicros(i, false), 0u) << "index " << int(i);
}

TEST(AutoPowerOff, OnlyTheX4ProWithALatchCanCut) {
  EXPECT_TRUE(canCutRail(true, 1));
  EXPECT_FALSE(canCutRail(true, -1));
  EXPECT_FALSE(canCutRail(false, 1));   // Sticky-style latches are left alone
  EXPECT_FALSE(canCutRail(false, 13));  // C3 X4: GPIO13 already is the power-off
  EXPECT_FALSE(canCutRail(false, -1));
}

TEST(AutoPowerOff, CutsOnlyOnATimerWake) {
  EXPECT_TRUE(shouldCutRailOnWake(true, false, true, false));
  EXPECT_FALSE(shouldCutRailOnWake(false, false, true, false));  // cold boot / USB
  EXPECT_FALSE(shouldCutRailOnWake(false, true, true, false));   // button
  EXPECT_FALSE(shouldCutRailOnWake(true, false, false, false));  // never armed there, and never cut
  EXPECT_FALSE(shouldCutRailOnWake(false, false, false, false));
}

TEST(AutoPowerOff, AButtonPressInTheTimersInstantWins) {
  // Both sources fired: the user is holding the button, so boot instead of
  // powering off under their thumb.
  EXPECT_FALSE(shouldCutRailOnWake(true, true, true, false));
}

TEST(AutoPowerOff, AChargingReaderIsNeverCut) {
  // Plugged in while it slept: the timer wake boots into the live sleep screen.
  EXPECT_FALSE(shouldCutRailOnWake(true, false, true, true));
  EXPECT_FALSE(shouldCutRailOnWake(true, true, true, true));
  // Charging changes nothing about the other wakes.
  EXPECT_FALSE(shouldCutRailOnWake(false, false, true, true));
  EXPECT_FALSE(shouldCutRailOnWake(false, true, true, true));
  EXPECT_FALSE(shouldCutRailOnWake(true, false, false, true));
}
