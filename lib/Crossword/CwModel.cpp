#include "CwModel.h"

#include <cstdio>
#include <cstring>

namespace cw {

void Puzzle::reset() {
  w = 0;
  h = 0;
  std::memset(solution, 0, sizeof(solution));
  std::memset(number, 0, sizeof(number));
  std::memset(circled, 0, sizeof(circled));
  for (Entry& e : entries) e = Entry{};
  entryCount = 0;
  acrossCount = 0;
  std::memset(entryAt, NO_ENTRY, sizeof(entryAt));
  clueUsed = 1;  // clues[0] is the empty clue every entry starts with
  clues[0] = '\0';
  std::memset(title, 0, sizeof(title));
  std::memset(author, 0, sizeof(author));
  std::memset(copyright, 0, sizeof(copyright));
  std::memset(sourceKey, 0, sizeof(sourceKey));
  fnv = 0;
}

int Puzzle::whiteCount() const {
  int n = 0;
  for (int i = 0; i < cells(); i++) n += isBlock(i) ? 0 : 1;
  return n;
}

void Progress::clear() {
  std::memset(sourceKey, 0, sizeof(sourceKey));
  fnv = 0;
  w = 0;
  h = 0;
  std::memset(fill, 0, sizeof(fill));
  std::memset(flags, 0, sizeof(flags));
  cursor = 0;
  dir = ACROSS;
  elapsed = 0;
  checks = 0;
  reveals = 0;
  solved = false;
  seq = 0;
}

uint32_t fnv1a(const char* data, const size_t len, uint32_t h) {
  for (size_t i = 0; i < len; i++) {
    h ^= static_cast<uint8_t>(data[i]);
    h *= 16777619u;
  }
  return h;
}

uint32_t puzzleFnv(const int w, const int h, const char* solution) {
  char head[16];
  const int n = std::snprintf(head, sizeof(head), "%dx%d:", w, h);
  const uint32_t v = fnv1a(head, n > 0 ? static_cast<size_t>(n) : 0);
  return fnv1a(solution, static_cast<size_t>(w > 0 && h > 0 ? w * h : 0), v);
}

void resetProgress(const Puzzle& p, Progress& out) {
  std::snprintf(out.sourceKey, sizeof(out.sourceKey), "%s", p.sourceKey);
  out.fnv = p.fnv;
  out.w = p.w;
  out.h = p.h;
  std::memset(out.fill, 0, sizeof(out.fill));
  std::memset(out.flags, 0, sizeof(out.flags));
  for (int i = 0; i < p.cells(); i++) out.fill[i] = p.isBlock(i) ? BLOCK : EMPTY;
  out.cursor = 0;
  out.dir = ACROSS;
  if (p.entryCount > 0) {
    out.cursor = static_cast<uint8_t>(p.entryCell(0, 0));
    out.dir = p.entries[0].dir;
  }
  out.elapsed = 0;
  out.checks = 0;
  out.reveals = 0;
  out.solved = false;
  out.seq = 0;
}

bool progressMatches(const Puzzle& p, const Progress& prog) {
  if (p.w == 0 || prog.fnv != p.fnv || prog.w != p.w || prog.h != p.h) return false;
  for (int i = 0; i < p.cells(); i++) {
    const char f = prog.fill[i];
    const uint8_t flags = prog.flags[i];
    if (p.isBlock(i)) {
      if (f != BLOCK || flags != 0) return false;
      continue;
    }
    if (f != EMPTY && (f < 'A' || f > 'Z')) return false;
    if (flags & ~(FLAG_WRONG | FLAG_REVEALED)) return false;
    if ((flags & FLAG_REVEALED) && (f != p.solution[i] || (flags & FLAG_WRONG))) return false;
    if ((flags & FLAG_WRONG) && f == EMPTY) return false;
  }
  if (prog.cursor >= p.cells() || p.isBlock(prog.cursor) || prog.dir > DOWN) return false;
  return true;
}

size_t formatElapsed(const uint32_t seconds, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  const uint32_t h = seconds / 3600;
  const uint32_t m = (seconds / 60) % 60;
  const uint32_t s = seconds % 60;
  const int n =
      h > 0 ? std::snprintf(out, cap, "%lu:%02lu:%02lu", static_cast<unsigned long>(h), static_cast<unsigned long>(m),
                            static_cast<unsigned long>(s))
            : std::snprintf(out, cap, "%lu:%02lu", static_cast<unsigned long>(m), static_cast<unsigned long>(s));
  return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

size_t formatClueLabel(const Puzzle& p, const int entry, char* out, const size_t cap) {
  if (!out || cap == 0 || entry < 0 || entry >= p.entryCount) return 0;
  const Entry& e = p.entries[entry];
  const int n = std::snprintf(out, cap, "%u%c", static_cast<unsigned>(e.number), e.dir == ACROSS ? 'A' : 'D');
  return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

}  // namespace cw
