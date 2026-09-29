#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include "FakeLedc.h"
#include "HalFrontlight.h"

namespace {

constexpr int COOL = 8;
constexpr int WARM = 9;

// The SDK perceptual duty per percent: total 10-bit duty of the X4 Pro
// frontlight (gamma-1.6554 table, FrontlightManager.cpp perceptualDuty()).
constexpr uint16_t TOTAL_DUTY[101] = {
    0,   1,   2,   3,   5,   7,   10,  13,  16,  19,  23,  26,  31,  35,  39,  44,   49,  54,  60,  65,  71,
    77,  83,  90,  96,  103, 110, 117, 124, 132, 139, 147, 155, 163, 172, 180, 189,  197, 206, 215, 224, 234,
    243, 253, 263, 273, 283, 293, 304, 314, 325, 336, 347, 358, 369, 380, 392, 403,  415, 427, 439, 451, 464,
    476, 489, 501, 514, 527, 540, 553, 567, 580, 594, 608, 621, 635, 650, 664, 678,  692, 707, 722, 737, 751,
    767, 782, 797, 812, 828, 844, 859, 875, 891, 907, 923, 940, 956, 973, 989, 1006, 1023};

class FrontlightPwm : public ::testing::Test {
 protected:
  void SetUp() override {
    BoardConfig::ACTIVE = {BoardConfig::Board::XteinkX4Pro, BoardConfig::X4_PRO_FRONTLIGHT};
    fakeLedc.clockSource = LEDC_USE_XTAL_CLK;  // the Arduino 3.x LEDC default on the S3
    Frontlight.begin(50, 50, false);           // returns any hold an earlier test left
    ASSERT_EQ(fakeLedc.xtalRefs, 0);
    fakeLedc.clearLog();
  }
  void TearDown() override { EXPECT_GE(fakeLedc.xtalRefs, 0); }

  static uint32_t cool() { return fakeLedc.duty[COOL]; }
  static uint32_t warm() { return fakeLedc.duty[WARM]; }
};

}  // namespace

// The fake BoardConfig carries the X4 Pro frontlight line the SDK profile has.
TEST(FrontlightProfile, FakeMatchesTheSdkX4ProProfile) {
  std::ifstream f(BOARDCONFIG_H_PATH);
  ASSERT_TRUE(f.good()) << BOARDCONFIG_H_PATH;
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string text = ss.str();
  const size_t start = text.find("constexpr BoardProfile XTEINK_X4_PRO = {");
  ASSERT_NE(start, std::string::npos);
  const size_t end = text.find("constexpr BoardProfile", start + 1);
  const std::string profile = text.substr(start, end - start);
  EXPECT_NE(profile.find("{8, 25000, 10, true, 9},"), std::string::npos);
}

// The LEDC timer divisor as the IDF driver computes it (ledc.c
// ledc_calculate_divisor(), 8 fractional bits; valid when > 255). The Arduino
// core already clocks the frontlight from the 40 MHz crystal, so keeping that
// clock through light sleep changes nothing about the waveform. RC_FAST (the
// SDK's FREEINK_FRONTLIGHT_LS clock) cannot make 25 kHz at 10 bits at all.
TEST(FrontlightProfile, CrystalMakes25kHzAt10Bits) {
  const auto divisor = [](uint64_t srcHz, uint64_t freqHz, uint64_t steps) {
    return ((srcHz << 8) + freqHz * steps / 2) / (freqHz * steps);
  };
  EXPECT_EQ(divisor(40000000, 25000, 1024), 400u);  // 1.5625: exactly 25 000 Hz
  EXPECT_EQ(40000000ull * 256 / (400 * 1024), 25000u);
  EXPECT_LE(divisor(17500000, 25000, 1024), 255u);  // RC_FAST: no valid divisor
}

TEST_F(FrontlightPwm, AttachesBothChannelsAt25kHz10Bit) {
  EXPECT_EQ(fakeLedc.attachFreq[COOL], 25000u);
  EXPECT_EQ(fakeLedc.attachBits[COOL], 10);
  EXPECT_EQ(fakeLedc.attachFreq[WARM], 25000u);
  EXPECT_EQ(fakeLedc.attachBits[WARM], 10);
  EXPECT_TRUE(fakeLedc.sleepSelDisabled[COOL]);
  EXPECT_TRUE(fakeLedc.sleepSelDisabled[WARM]);
}

// Every brightness step at every warmth: the same duties as before.
TEST_F(FrontlightPwm, EveryLevelKeepsItsDuty) {
  Frontlight.setOn(true);
  for (int warmth = 0; warmth <= 100; ++warmth) {
    Frontlight.setWarmth(static_cast<uint8_t>(warmth));
    for (int pct = 0; pct <= 100; ++pct) {
      Frontlight.setBrightness(static_cast<uint8_t>(pct));
      const uint32_t total = TOTAL_DUTY[pct];
      const uint32_t w = (total * static_cast<uint32_t>(warmth) + 50u) / 100u;
      ASSERT_EQ(warm(), w) << pct << "% warm " << warmth;
      ASSERT_EQ(cool(), total - w) << pct << "% warm " << warmth;
    }
  }
  EXPECT_EQ(fakeLedc.xtalRefs, 1);
}

// The dim end pinned literally: 1 % is one LSB on one channel.
TEST_F(FrontlightPwm, DimEndSplitsAsBefore) {
  struct Case {
    uint8_t pct, warmth;
    uint32_t cool, warm;
  };
  const Case cases[] = {
      {1, 0, 1, 0},      {1, 25, 1, 0},       {1, 50, 0, 1},       {1, 100, 0, 1},   {2, 25, 1, 1},
      {3, 50, 1, 2},     {5, 50, 3, 4},       {10, 50, 11, 12},    {20, 75, 18, 53}, {50, 25, 244, 81},
      {100, 0, 1023, 0}, {100, 50, 511, 512}, {100, 100, 0, 1023},
  };
  Frontlight.setOn(true);
  for (const auto& c : cases) {
    Frontlight.setWarmth(c.warmth);
    Frontlight.setBrightness(c.pct);
    EXPECT_EQ(cool(), c.cool) << int(c.pct) << "% warm " << int(c.warmth);
    EXPECT_EQ(warm(), c.warm) << int(c.pct) << "% warm " << int(c.warmth);
  }
}

// XTAL is held through light sleep exactly while lit, taken before the first
// lit duty and returned after the dark one.
TEST_F(FrontlightPwm, CrystalHeldExactlyWhileLit) {
  Frontlight.setBrightness(1);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
  EXPECT_TRUE(Frontlight.survivesLightSleep());

  Frontlight.setOn(true);
  ASSERT_GE(fakeLedc.events.size(), 2u);
  EXPECT_EQ(fakeLedc.events.front().kind, FakeEvent::XtalOn);
  EXPECT_EQ(fakeLedc.xtalRefs, 1);
  EXPECT_EQ(cool() + warm(), 1u);
  EXPECT_TRUE(Frontlight.survivesLightSleep());

  Frontlight.setOn(true);  // already on: no second reference
  Frontlight.setBrightness(60);
  Frontlight.setWarmth(80);
  EXPECT_EQ(fakeLedc.xtalRefs, 1);

  fakeLedc.clearLog();
  Frontlight.setOn(false);
  ASSERT_GE(fakeLedc.events.size(), 2u);
  EXPECT_EQ(fakeLedc.events.back().kind, FakeEvent::XtalOff);
  EXPECT_EQ(cool() + warm(), 0u);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);

  Frontlight.setOn(false);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
}

// Restore Light on Wake: begin() lit takes the reference before lighting.
TEST_F(FrontlightPwm, BeginLitHoldsTheCrystal) {
  Frontlight.begin(6, 50, true);
  EXPECT_EQ(fakeLedc.xtalRefs, 1);
  EXPECT_TRUE(Frontlight.survivesLightSleep());
  size_t firstLit = fakeLedc.events.size();
  size_t firstOn = fakeLedc.events.size();
  for (size_t i = 0; i < fakeLedc.events.size(); ++i) {
    const auto& e = fakeLedc.events[i];
    if (e.kind == FakeEvent::Write && e.duty != 0 && firstLit == fakeLedc.events.size()) firstLit = i;
    if (e.kind == FakeEvent::XtalOn && firstOn == fakeLedc.events.size()) firstOn = i;
  }
  EXPECT_LT(firstOn, firstLit);
  EXPECT_EQ(cool() + warm(), TOTAL_DUTY[6]);
  Frontlight.begin(6, 50, false);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
}

// A core that clocks LEDC from APB would freeze in light sleep: no hold, and a
// lit light keeps blocking the naps.
TEST_F(FrontlightPwm, ApbClockedLedcDoesNotSurvive) {
  fakeLedc.clockSource = LEDC_USE_APB_CLK;
  Frontlight.begin(6, 50, true);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
  EXPECT_FALSE(Frontlight.survivesLightSleep());
  EXPECT_EQ(cool() + warm(), TOTAL_DUTY[6]);
  Frontlight.setOn(false);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
}

TEST_F(FrontlightPwm, OtherBoardsAreUntouched) {
  BoardConfig::ACTIVE.board = BoardConfig::Board::XteinkX4;
  const int forceDisables = fakeLedc.xtalForceDisables;
  Frontlight.begin(6, 50, true);
  EXPECT_EQ(fakeLedc.xtalForceDisables, forceDisables);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
  EXPECT_EQ(Frontlight.sleepClockRequests(), 0);
  EXPECT_FALSE(Frontlight.survivesLightSleep());
  Frontlight.setOn(false);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
}

// Deep sleep while lit: startDeepSleep() returns the request, the light stays
// logically on (Restore Light on Wake reads it before sleeping).
TEST_F(FrontlightPwm, DeepSleepReturnsTheRequest) {
  Frontlight.setOn(true);
  ASSERT_EQ(Frontlight.sleepClockRequests(), 1);
  Frontlight.releaseForDeepSleep();
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
  EXPECT_EQ(Frontlight.sleepClockRequests(), 0);
  EXPECT_TRUE(Frontlight.isOn());
  Frontlight.releaseForDeepSleep();  // the ghost-wake re-sleep path calls it again
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
}

// The count survives a deep-sleep wake (RTC memory) while the HAL's own flag
// starts false: begin() starts from zero whatever an earlier boot left, so a
// dark wake holds nothing and a lit one holds exactly one request.
TEST_F(FrontlightPwm, DeepSleepWakeStartsFromZero) {
  fakeLedc.xtalRefs = 2;  // left by a sleep that never returned its requests
  Frontlight.begin(6, 50, false);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
  EXPECT_TRUE(Frontlight.survivesLightSleep());

  fakeLedc.xtalRefs = 1;
  Frontlight.begin(6, 50, true);
  EXPECT_EQ(fakeLedc.xtalRefs, 1);
  Frontlight.setOn(false);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
}

// On at brightness 0 is duty 0: no request, and the naps still run.
TEST_F(FrontlightPwm, OnAtZeroHoldsNothing) {
  Frontlight.setBrightness(0);
  Frontlight.setOn(true);
  EXPECT_EQ(cool() + warm(), 0u);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
  EXPECT_TRUE(Frontlight.survivesLightSleep());

  fakeLedc.clearLog();
  Frontlight.setBrightness(1);
  ASSERT_GE(fakeLedc.events.size(), 2u);
  EXPECT_EQ(fakeLedc.events.front().kind, FakeEvent::XtalOn);
  EXPECT_EQ(cool() + warm(), 1u);
  EXPECT_EQ(fakeLedc.xtalRefs, 1);

  fakeLedc.clearLog();
  Frontlight.setBrightness(0);
  ASSERT_GE(fakeLedc.events.size(), 2u);
  EXPECT_EQ(fakeLedc.events.back().kind, FakeEvent::XtalOff);
  EXPECT_EQ(fakeLedc.xtalRefs, 0);
  EXPECT_TRUE(Frontlight.survivesLightSleep());
}
