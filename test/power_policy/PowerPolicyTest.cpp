#include <gtest/gtest.h>

#include <cstring>

#include "hal/PowerPolicy.h"

using namespace power_policy;

namespace {
// An X4 Pro reading with the light off, nothing attached: the state that sleeps.
SleepInputs idleReader() {
  SleepInputs in;
  in.enabled = true;
  in.boardSupports = true;
  return in;
}
}  // namespace

TEST(PowerPolicy, IdleReaderSleeps) {
  EXPECT_TRUE(shouldLightSleep(idleReader()));
  EXPECT_EQ(lightSleepBlock(idleReader()), Block::None);
}

TEST(PowerPolicy, EveryGuardBlocksOnItsOwn) {
  struct Case {
    void (*set)(SleepInputs&);
    Block want;
  };
  const Case cases[] = {
      {[](SleepInputs& in) { in.enabled = false; }, Block::Disabled},
      {[](SleepInputs& in) { in.boardSupports = false; }, Block::Board},
      {[](SleepInputs& in) { in.powerLockHeld = true; }, Block::Lock},
      {[](SleepInputs& in) { in.radio.radioLocked = true; }, Block::Radio},
      {[](SleepInputs& in) { in.radio.wifiModeOn = true; }, Block::Radio},
      {[](SleepInputs& in) { in.usbHost = true; }, Block::Host},
      {[](SleepInputs& in) { in.charging = true; }, Block::Charging},
      {[](SleepInputs& in) { in.frontlightLit = true; }, Block::Light},
      {[](SleepInputs& in) { in.inputActive = true; }, Block::Input},
      {[](SleepInputs& in) { in.debouncePending = true; }, Block::Input},
      {[](SleepInputs& in) { in.activityBusy = true; }, Block::Activity},
      {[](SleepInputs& in) { in.inPostWakeWindow = true; }, Block::PostWake},
      {[](SleepInputs& in) { in.renderQueued = true; }, Block::RenderQueued},
  };
  for (const auto& c : cases) {
    SleepInputs in = idleReader();
    c.set(in);
    EXPECT_EQ(lightSleepBlock(in), c.want) << blockName(c.want);
    EXPECT_FALSE(shouldLightSleep(in)) << blockName(c.want);
  }
}

// Nick's rule: a radio wins over everything that would otherwise let it sleep,
// and nothing but the radio being off lets the clock drop.
TEST(PowerPolicy, RadioNeverSleepsOrDownclocks) {
  for (const bool locked : {false, true}) {
    for (const bool wifi : {false, true}) {
      const RadioState r{locked, wifi};
      EXPECT_EQ(radioActive(r), locked || wifi);
      EXPECT_EQ(mayDownclock(r), !(locked || wifi));
      SleepInputs in = idleReader();
      in.radio = r;
      EXPECT_EQ(shouldLightSleep(in), !(locked || wifi));
    }
  }
}

TEST(PowerPolicy, RadioIsReportedBeforeTheUsbGuards) {
  // A sync on a charger with a host attached reports the radio, the rule that
  // outlives the cable.
  SleepInputs in = idleReader();
  in.radio.wifiModeOn = true;
  in.usbHost = true;
  in.charging = true;
  EXPECT_EQ(lightSleepBlock(in), Block::Radio);
}

TEST(PowerPolicy, DisabledAndBoardComeFirst) {
  SleepInputs in = idleReader();
  in.radio.radioLocked = true;
  in.enabled = false;
  EXPECT_EQ(lightSleepBlock(in), Block::Disabled);
  in.enabled = true;
  in.boardSupports = false;
  EXPECT_EQ(lightSleepBlock(in), Block::Board);
}

TEST(PowerPolicy, BlockNamesAreDistinctWords) {
  const Block all[] = {Block::None,     Block::Disabled,     Block::Board,  Block::Lock,  Block::Radio,
                       Block::Host,     Block::Charging,     Block::Light,  Block::Input, Block::Activity,
                       Block::PostWake, Block::RenderQueued, Block::Refused};
  for (const Block a : all) {
    ASSERT_NE(blockName(a), nullptr);
    EXPECT_EQ(strchr(blockName(a), ' '), nullptr);
    for (const Block b : all) {
      if (a != b) EXPECT_STRNE(blockName(a), blockName(b));
    }
  }
}

TEST(PowerPolicy, PostWakeWindowIsWrapSafe) {
  EXPECT_TRUE(before(1000, 1000 + POST_WAKE_AWAKE_MS));
  EXPECT_FALSE(before(1000 + POST_WAKE_AWAKE_MS, 1000 + POST_WAKE_AWAKE_MS));
  EXPECT_FALSE(before(5000 + POST_WAKE_AWAKE_MS, 1000 + POST_WAKE_AWAKE_MS));
  // millis() wraps at 2^32 ms (~49.7 days).
  const uint32_t nearWrap = 0xFFFFFF00u;
  const uint32_t until = nearWrap + POST_WAKE_AWAKE_MS;  // wrapped past zero
  EXPECT_TRUE(before(nearWrap, until));
  EXPECT_TRUE(before(0x10u, until));
  EXPECT_FALSE(before(until + 1, until));
}

TEST(PowerPolicy, HostSeenHoldsThroughBlips) {
  HostSeen h;
  EXPECT_FALSE(h.present(0));
  h.update(100, false);
  EXPECT_FALSE(h.present(100));
  h.update(200, true);
  EXPECT_TRUE(h.present(200));
  h.update(300, false);  // one false reading: still attached
  EXPECT_TRUE(h.present(300));
  EXPECT_TRUE(h.present(200 + HostSeen::ABSENT_MS - 1));
  EXPECT_FALSE(h.present(200 + HostSeen::ABSENT_MS));
  h.update(5000, true);
  EXPECT_TRUE(h.present(5000));
}

// The radio lock is counted with the Locks; a Wi-Fi session alone must still
// report as the radio (STATE lsblk=radio), and any other Lock as a Lock.
TEST(PowerPolicy, OtherLockExcludesTheRadioLock) {
  EXPECT_FALSE(otherLockHeld(0, false));
  EXPECT_TRUE(otherLockHeld(1, false));
  EXPECT_FALSE(otherLockHeld(1, true));  // only the radio lock
  EXPECT_TRUE(otherLockHeld(2, true));   // radio + a render
  SleepInputs in = idleReader();
  in.powerLockHeld = otherLockHeld(1, true);
  in.radio.radioLocked = true;
  EXPECT_EQ(lightSleepBlock(in), Block::Radio);
}

// 1 s only where the idle loop light-sleeps (X4 Pro); the C3 boards keep 3 s.
TEST(PowerPolicy, IdleThresholdByBoard) {
  EXPECT_EQ(idlePowerSavingMs(true), 1000u);
  EXPECT_EQ(idlePowerSavingMs(false), 3000u);
}

TEST(PowerPolicy, RefusedIsNotASleep) { EXPECT_STREQ(blockName(Block::Refused), "refused"); }

// A lit frontlight stops the nap only when its PWM would stop with it.
TEST(PowerPolicy, LitLightNapsWhenItsPwmSurvivesSleep) {
  SleepInputs in = idleReader();
  in.frontlightLit = true;
  EXPECT_EQ(lightSleepBlock(in), Block::Light);
  in.frontlightSurvivesSleep = true;
  EXPECT_EQ(lightSleepBlock(in), Block::None);
  in.frontlightLit = false;
  EXPECT_EQ(lightSleepBlock(in), Block::None);
  in.frontlightSurvivesSleep = false;
  EXPECT_EQ(lightSleepBlock(in), Block::None);
}

// The bench window lifts the USB-host and charger guards and nothing else.
TEST(PowerPolicy, BenchForceLiftsOnlyTheUsbGuards) {
  SleepInputs in = idleReader();
  in.benchForced = true;
  in.usbHost = true;
  in.charging = true;
  EXPECT_EQ(lightSleepBlock(in), Block::None);

  struct Case {
    void (*set)(SleepInputs&);
    Block want;
  };
  const Case cases[] = {
      {[](SleepInputs& s) { s.enabled = false; }, Block::Disabled},
      {[](SleepInputs& s) { s.boardSupports = false; }, Block::Board},
      {[](SleepInputs& s) { s.powerLockHeld = true; }, Block::Lock},
      {[](SleepInputs& s) { s.radio.wifiModeOn = true; }, Block::Radio},
      {[](SleepInputs& s) { s.frontlightLit = true; }, Block::Light},
      {[](SleepInputs& s) { s.inputActive = true; }, Block::Input},
      {[](SleepInputs& s) { s.activityBusy = true; }, Block::Activity},
      {[](SleepInputs& s) { s.inPostWakeWindow = true; }, Block::PostWake},
      {[](SleepInputs& s) { s.renderQueued = true; }, Block::RenderQueued},
  };
  for (const auto& c : cases) {
    SleepInputs forced = in;
    c.set(forced);
    EXPECT_EQ(lightSleepBlock(forced), c.want) << blockName(c.want);
  }
}

TEST(PowerPolicy, BenchForceIsBoundedAndLeavesTimeToEnumerate) {
  constexpr uint32_t SHORTEST_AUTO_SLEEP_MS = 60000;  // CrossPointSettings MIN_SLEEP_TIMEOUT_MINUTES
  EXPECT_LT(BENCH_FORCE_MAX_S * 1000 + BENCH_FORCE_TAIL_MS, SHORTEST_AUTO_SLEEP_MS);
  EXPECT_GE(BENCH_FORCE_TAIL_MS, POST_WAKE_AWAKE_MS);
}
