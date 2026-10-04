#pragma once

// Crossword numbering: the standard American rule, the one source of entry numbers for every
// format. Row-major, a white square gets the next number when it starts an Across run (no white
// square to its left, a white square to its right) or a Down run (the same above and below).
// Runs are 2 or more squares; a square may be in only one run (unchecked), but not in none.

#include "CwModel.h"

namespace cw {

// From p.w, p.h and p.solution: fills number[], entries[] (clues all empty), entryAt[],
// entryCount and acrossCount, and fnv. Errors: TooSmall/TooBig for the size, NotCrossword when
// there is no entry or a white square is in no entry, TooManyClues past MAX_ENTRIES.
Error numberGrid(Puzzle& p);

// The entry with this number and direction, or -1.
int findEntry(const Puzzle& p, uint8_t dir, int number);

}  // namespace cw
