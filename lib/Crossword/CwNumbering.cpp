#include "CwNumbering.h"

#include <cstring>

namespace cw {

Error numberGrid(Puzzle& p) {
  if (p.w > MAX_SIDE || p.h > MAX_SIDE) return Error::TooBig;
  if (p.w < MIN_SIDE || p.h < MIN_SIDE) return Error::TooSmall;
  std::memset(p.number, 0, sizeof(p.number));
  std::memset(p.entryAt, NO_ENTRY, sizeof(p.entryAt));
  p.entryCount = 0;
  p.acrossCount = 0;
  const auto white = [&p](const int r, const int c) {
    return r >= 0 && c >= 0 && r < p.h && c < p.w && !p.isBlock(p.index(r, c));
  };
  // Pass 1: numbers. Pass 2 (per direction): entries in number order, Across first.
  int next = 1;
  for (int r = 0; r < p.h; r++) {
    for (int c = 0; c < p.w; c++) {
      if (!white(r, c)) continue;
      const bool across = !white(r, c - 1) && white(r, c + 1);
      const bool down = !white(r - 1, c) && white(r + 1, c);
      if (!across && !down) continue;
      if (next > 255) return Error::TooManyClues;
      p.number[p.index(r, c)] = static_cast<uint8_t>(next++);
    }
  }
  for (uint8_t dir = ACROSS; dir <= DOWN; dir++) {
    for (int r = 0; r < p.h; r++) {
      for (int c = 0; c < p.w; c++) {
        const int dr = dir == DOWN ? 1 : 0;
        const int dc = dir == ACROSS ? 1 : 0;
        if (!white(r, c) || white(r - dr, c - dc) || !white(r + dr, c + dc)) continue;
        if (p.entryCount >= MAX_ENTRIES) return Error::TooManyClues;
        Entry& e = p.entries[p.entryCount];
        e = Entry{};
        e.row = static_cast<uint8_t>(r);
        e.col = static_cast<uint8_t>(c);
        e.dir = dir;
        e.number = p.number[p.index(r, c)];
        int len = 0;
        while (white(r + dr * len, c + dc * len)) {
          p.entryAt[dir][p.index(r + dr * len, c + dc * len)] = p.entryCount;
          len++;
        }
        e.len = static_cast<uint8_t>(len);
        p.entryCount++;
      }
    }
    if (dir == ACROSS) p.acrossCount = p.entryCount;
  }
  if (p.entryCount == 0) return Error::NotCrossword;
  for (int i = 0; i < p.cells(); i++) {
    if (!p.isBlock(i) && p.entryAt[ACROSS][i] == NO_ENTRY && p.entryAt[DOWN][i] == NO_ENTRY) {
      return Error::NotCrossword;
    }
  }
  p.fnv = puzzleFnv(p.w, p.h, p.solution);
  return Error::None;
}

int findEntry(const Puzzle& p, const uint8_t dir, const int number) {
  const int lo = dir == ACROSS ? 0 : p.acrossCount;
  const int hi = dir == ACROSS ? p.acrossCount : p.entryCount;
  for (int i = lo; i < hi; i++) {
    if (p.entries[i].number == number) return i;
  }
  return -1;
}

}  // namespace cw
