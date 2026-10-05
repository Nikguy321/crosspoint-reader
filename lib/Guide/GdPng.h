#pragma once

// Survival guide: a figure's size from its PNG header (the first 24 bytes), for the layout. Pure.

#include <cstddef>
#include <cstdint>

namespace gd {

constexpr size_t PNG_HEADER_BYTES = 24;  // signature + IHDR length/type + width + height

// False unless data starts with the PNG signature and an IHDR chunk with a size of 1..65535 a side.
bool pngSize(const uint8_t* data, size_t len, int& w, int& h);

}  // namespace gd
