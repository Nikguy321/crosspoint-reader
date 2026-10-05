#include "GdPng.h"

#include <cstring>

namespace gd {

namespace {
uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
}  // namespace

bool pngSize(const uint8_t* data, const size_t len, int& w, int& h) {
  static constexpr uint8_t SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  w = h = 0;
  if (!data || len < PNG_HEADER_BYTES) return false;
  if (std::memcmp(data, SIG, sizeof(SIG)) != 0 || be32(data + 8) != 13 || std::memcmp(data + 12, "IHDR", 4) != 0) {
    return false;
  }
  const uint32_t pw = be32(data + 16), ph = be32(data + 20);
  if (pw == 0 || ph == 0 || pw > 65535 || ph > 65535) return false;
  w = static_cast<int>(pw);
  h = static_cast<int>(ph);
  return true;
}

}  // namespace gd
