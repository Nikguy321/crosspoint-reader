#include <gtest/gtest.h>

#include <cstdint>

#include "util/LiveSleepPolicy.h"

using namespace live_sleep;

namespace {
constexpr uint8_t DARK = 0;
constexpr uint8_t CUSTOM = 2;
constexpr uint8_t QUICK_RESUME = 6;
constexpr uint8_t TRANSPARENT_CUSTOM = 7;
constexpr uint8_t NOW_READING = 8;
constexpr uint8_t SHUFFLE = 14;
constexpr uint8_t WEATHER = 15;
}  // namespace

TEST(LiveSleep, ExternalPowerIsTheChargerLineOrAComputer) {
  EXPECT_FALSE(externalPower(false, false));
  EXPECT_TRUE(externalPower(true, false));  // a wall charger, charging
  EXPECT_TRUE(externalPower(false, true));  // a computer at full charge (STAT dropped)
  EXPECT_TRUE(externalPower(true, true));
}

TEST(LiveSleep, CardModesAreNowReadingThroughWeather) {
  for (uint8_t mode = 0; mode < 20; mode++) {
    EXPECT_EQ(isCardMode(mode), mode >= NOW_READING && mode <= WEATHER) << int(mode);
  }
  EXPECT_TRUE(liveEligible(true, true, SHUFFLE, false));
}

TEST(LiveSleep, LiveOnlyOnTheX4ProOnPowerWithACardOrTheCycle) {
  EXPECT_TRUE(liveEligible(true, true, NOW_READING, false));
  EXPECT_TRUE(liveEligible(true, true, SHUFFLE, false));
  // Unplugged: today's deep sleep, whatever the settings.
  EXPECT_FALSE(liveEligible(true, false, SHUFFLE, true));
  // Other boards never.
  EXPECT_FALSE(liveEligible(false, true, SHUFFLE, true));
  // A classic screen has nothing to update, unless the cycle deals cards.
  EXPECT_FALSE(liveEligible(true, true, DARK, false));
  EXPECT_FALSE(liveEligible(true, true, CUSTOM, false));
  EXPECT_FALSE(liveEligible(true, true, TRANSPARENT_CUSTOM, false));
  EXPECT_TRUE(liveEligible(true, true, DARK, true));
  EXPECT_TRUE(liveEligible(true, true, CUSTOM, true));
  // Quick Resume as the sleep screen: live only with the cycle.
  EXPECT_FALSE(liveEligible(true, true, QUICK_RESUME, false));
  EXPECT_TRUE(liveEligible(true, true, QUICK_RESUME, true));
}

TEST(LiveSleep, UnplugNeedsTwentySecondsWithoutABreak) {
  UnplugDebounce d;
  d.start(1000);
  EXPECT_FALSE(d.unplugged(1000));
  d.note(true, 2000);
  EXPECT_FALSE(d.unplugged(50000));
  d.note(false, 3000);
  EXPECT_FALSE(d.unplugged(3000));
  EXPECT_EQ(d.absentMs(10000), 7000u);
  d.note(false, 22999);
  EXPECT_FALSE(d.unplugged(22999));
  EXPECT_TRUE(d.unplugged(23000));
  EXPECT_EQ(UNPLUG_DEBOUNCE_MS, 20000u);
}

TEST(LiveSleep, AnyPresentSampleRestartsTheCount) {
  UnplugDebounce d;
  d.start(0);
  d.note(false, 1000);
  d.note(false, 15000);
  d.note(true, 15050);  // STAT blipped back (a top-up), or the computer answered
  EXPECT_EQ(d.absentMs(15050), 0u);
  d.note(false, 15100);
  EXPECT_FALSE(d.unplugged(30000));
  EXPECT_FALSE(d.unplugged(35099));
  EXPECT_TRUE(d.unplugged(35100));
}

TEST(LiveSleep, DebounceSurvivesTheMillisWrap) {
  UnplugDebounce d;
  d.start(0xFFFFF000u);
  d.note(false, 0xFFFFF000u);
  EXPECT_FALSE(d.unplugged(0x00001000u));                      // 8 s across the wrap
  EXPECT_TRUE(d.unplugged(0xFFFFF000u + UNPLUG_DEBOUNCE_MS));  // wraps to a small number
}

TEST(LiveSleep, IntervalTableIsAppendOnlyWithATwoMinuteDefault) {
  EXPECT_EQ(intervalMinutes(0), 1);
  EXPECT_EQ(intervalMinutes(1), 2);
  EXPECT_EQ(intervalMinutes(2), 5);
  EXPECT_EQ(intervalMinutes(3), 10);
  EXPECT_EQ(intervalMinutes(4), 15);
  EXPECT_EQ(INTERVAL_COUNT, 5);
  EXPECT_EQ(DEFAULT_INTERVAL_INDEX, 1);
  EXPECT_EQ(intervalMinutes(5), 2);  // an index from a newer firmware: the default
  EXPECT_EQ(intervalMinutes(255), 2);
  // Every interval divides a day, so the minute boundaries line up across midnight.
  for (uint8_t i = 0; i < INTERVAL_COUNT; i++) EXPECT_EQ(1440 % INTERVAL_MINUTES[i], 0) << int(i);
}

TEST(LiveSleep, RedrawLandsTwoSecondsPastTheMinute) {
  const auto at = [](int h, int m, int s) { return static_cast<uint32_t>(h * 3600 + m * 60 + s); };
  // 2-minute interval: 14:17:30 -> 14:18:02.
  EXPECT_EQ(nextRedrawDelayMs(true, at(14, 17, 30), 2), 32000u);
  // Just drawn at 14:18:02: the next one is 14:20:02, not now again.
  EXPECT_EQ(nextRedrawDelayMs(true, at(14, 18, 2), 2), 120000u);
  // 14:18:01 is still before this minute's slot.
  EXPECT_EQ(nextRedrawDelayMs(true, at(14, 18, 1), 2), 1000u);
  EXPECT_EQ(nextRedrawDelayMs(true, at(14, 18, 0), 2), 2000u);
  // 1 minute: every minute.
  EXPECT_EQ(nextRedrawDelayMs(true, at(9, 0, 59), 1), 3000u);
  // 15 minutes: the quarter hours.
  EXPECT_EQ(nextRedrawDelayMs(true, at(9, 7, 0), 15), (8 * 60 + 2) * 1000u);
  EXPECT_EQ(nextRedrawDelayMs(true, at(9, 45, 2), 15), 15 * 60 * 1000u);
}

TEST(LiveSleep, RedrawCrossesMidnight) {
  // 23:59:30 with 2 min: 00:00:02 the next day.
  EXPECT_EQ(nextRedrawDelayMs(true, 23 * 3600 + 59 * 60 + 30, 2), 32000u);
  // 23:55:10 with 10 min: 00:00:02.
  EXPECT_EQ(nextRedrawDelayMs(true, 23 * 3600 + 55 * 60 + 10, 10), (4 * 60 + 52) * 1000u);
  // A seconds-of-day value past midnight folds back.
  EXPECT_EQ(nextRedrawDelayMs(true, 86400 + 30, 2), 92000u);
}

TEST(LiveSleep, WithoutAClockAPlainInterval) {
  EXPECT_EQ(nextRedrawDelayMs(false, 12345, 2), 120000u);
  EXPECT_EQ(nextRedrawDelayMs(false, 0, 15), 900000u);
  EXPECT_EQ(nextRedrawDelayMs(false, 0, 0), 60000u);  // never a zero interval
}

TEST(LiveSleep, NeverMoreThanOneIntervalAhead) {
  for (uint8_t i = 0; i < INTERVAL_COUNT; i++) {
    const uint8_t minutes = INTERVAL_MINUTES[i];
    for (uint32_t t = 0; t < 86400; t += 7) {
      const uint32_t ms = nextRedrawDelayMs(true, t, minutes);
      ASSERT_GT(ms, 0u) << t;
      ASSERT_LE(ms, minutes * 60000u) << t;
      // It lands REDRAW_PAST_MINUTE_S past a multiple of the interval.
      const uint32_t landing = (t + ms / 1000) % 86400;
      ASSERT_EQ(landing % (minutes * 60u), REDRAW_PAST_MINUTE_S) << t << " every " << int(minutes);
    }
  }
}

TEST(LiveSleep, HalfOnAChangeAfterAPictureAndEveryFifth) {
  EXPECT_TRUE(halfRefresh(true, false, 0));    // a new card
  EXPECT_TRUE(halfRefresh(false, true, 0));    // after a picture
  EXPECT_FALSE(halfRefresh(false, false, 0));  // the same card: FAST
  // Same card each time: H F F F F H ...
  uint8_t fast = 0;
  int halves = 0;
  for (int redraw = 0; redraw < 20; redraw++) {
    if (halfRefresh(false, false, fast)) {
      halves++;
      fast = 0;
    } else {
      fast++;
    }
  }
  EXPECT_EQ(halves, 20 / HALF_EVERY);
  EXPECT_EQ(HALF_EVERY, 5);
  EXPECT_TRUE(halfRefresh(false, false, HALF_EVERY - 1));
}

TEST(LiveSleep, TheFinalFrameIsAlwaysHalf) {
  // The frame left on the unpowered panel is a clean pass, whatever the count says.
  for (uint8_t fast = 0; fast < HALF_EVERY; fast++) EXPECT_TRUE(halfRefresh(false, false, fast, true)) << int(fast);
}

TEST(LiveSleep, WakeWaitsForTheKeysButNotForever) {
  EXPECT_TRUE(wakeNow(false, 0));  // every key up: wake
  EXPECT_FALSE(wakeNow(true, 0));  // still held: wait
  EXPECT_FALSE(wakeNow(true, WAKE_RELEASE_WAIT_MS - 1));
  EXPECT_TRUE(wakeNow(true, WAKE_RELEASE_WAIT_MS));  // held too long (a case, a bag): wake anyway
  EXPECT_LE(WAKE_RELEASE_WAIT_MS, 2000u);
}
