#pragma once

// Crossword: the Across Lite .puz reader (binary, little-endian).
//
//   0x00 global checksum     0x02 "ACROSS&DOWN\0"    0x0E CIB checksum   0x10/0x14 masked sums
//   0x18 version "1.3\0"     0x2C width  0x2D height  0x2E clue count     0x30 type (0x0401 =
//   diagramless: refused)    0x32 scrambled state (non-zero: refused as locked)
// then the solution grid ('.' = block), the player grid (ignored: progress is our own), and
// NUL-terminated strings: title, author, copyright, the clues in numbering order (Across before
// Down for the same number), notes. Strings are ISO-8859-1 below version "2.0", UTF-8 from it.
// Extensions: GEXT bit 0x80 = circled; a GRBS (rebus) with any non-zero square is refused.
// Checksums are verified but never required: mismatches are only counted.

#include "CwModel.h"
#include "CwSource.h"

namespace cw {

// Parses a .puz file into p (complete: numbered, clued, fnv set; sourceKey copied in).
LoadStatus parsePuz(ByteReader& in, const char* sourceKey, Puzzle& p);

// The .puz checksum step over a byte run (exposed for the tests and fixtures).
uint16_t puzChecksum(const uint8_t* data, size_t len, uint16_t sum);

}  // namespace cw
