#pragma once

#include <cstdint>

namespace sleepcards {

// The sleep-screen cards. Pictures is the existing CUSTOM sleep screen (the
// picture frame), listed so Shuffle can include it. Values index the shuffle
// mask and the registry; append only.
enum class CardId : uint8_t {
  None = 0,
  NowReading = 1,
  Day = 2,
  Calendar = 3,
  Quote = 4,
  Owner = 5,
  Sky = 6,
  Pictures = 7,
  Shuffle = 8,
  Count = 9,
};

constexpr uint16_t cardBit(const CardId id) { return static_cast<uint16_t>(1u << static_cast<uint8_t>(id)); }

}  // namespace sleepcards
