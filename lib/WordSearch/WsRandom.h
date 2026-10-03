#pragma once

// The Word Search PRNG: splitmix32, the same mixer as the Quote card's (QuoteCard.cpp), copied
// so the game does not depend on a sleep card. Every seed (0 included) gives a well-mixed
// sequence; the device seeds it from esp_random(), the tests from fixed values, so a seed
// always yields the same puzzle on the host and on the reader.

#include <cstdint>

namespace ws {

inline uint32_t nextRandom(uint32_t& state) {
  state += 0x9E3779B9u;
  uint32_t z = state;
  z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
  z = (z ^ (z >> 13)) * 0xC2B2AE35u;
  return z ^ (z >> 16);
}

// Uniform-enough in [0, n) by multiply-shift; 0 when n is 0.
inline uint32_t randomBelow(uint32_t& state, const uint32_t n) {
  if (n == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(nextRandom(state)) * n) >> 32);
}

}  // namespace ws
