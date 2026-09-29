#pragma once

#include <cstddef>
#include <cstdint>

// Host stand-in: the Paper Mono PMIC path is never taken on the X4 Pro.
namespace freeink::m5pm1 {
constexpr uint8_t REG_GPIO_DRV = 0;
constexpr uint8_t REG_GPIO_FUNC0 = 0;
constexpr uint8_t REG_PWM_FREQ_L = 0;
constexpr uint8_t REG_PWM0_DUTY_L = 0;
inline bool beginBus() { return true; }
inline bool updateReg(uint8_t, uint8_t, uint8_t) { return true; }
inline bool writeReg16(uint8_t, uint16_t) { return true; }
inline bool writeBytes(uint8_t, const uint8_t*, size_t) { return true; }
}  // namespace freeink::m5pm1
