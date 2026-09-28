#pragma once

// Host stand-in for the Arduino core: just what the renderer, the fonts and
// the sleep cards touch.
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

unsigned long millis();
unsigned long micros();
inline void delay(unsigned long) {}

struct HostEsp {
  [[noreturn]] void restart() { std::abort(); }
  size_t getFreeHeap() const { return 256 * 1024; }
  size_t getMaxAllocHeap() const { return 128 * 1024; }
};
inline HostEsp ESP;

#define PROGMEM
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))
