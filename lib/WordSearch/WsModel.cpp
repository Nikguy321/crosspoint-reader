#include "WsModel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ws {

int dirIndex(const int dr, const int dc) {
  for (int i = 0; i < 8; i++) {
    if (DIR_DR[i] == dr && DIR_DC[i] == dc) return i;
  }
  return -1;
}

bool aligned(const Cell a, const Cell b) {
  if (!a.valid() || !b.valid() || a == b) return false;
  const int dr = std::abs(b.row - a.row);
  const int dc = std::abs(b.col - a.col);
  return dr == 0 || dc == 0 || dr == dc;
}

int lineCells(const Cell a, const Cell b) {
  if (!a.valid() || !b.valid()) return 0;
  if (a == b) return 1;
  if (!aligned(a, b)) return 0;
  const int dr = std::abs(b.row - a.row);
  const int dc = std::abs(b.col - a.col);
  return (dr > dc ? dr : dc) + 1;
}

void Puzzle::reset() {
  difficulty = Difficulty::Medium;
  size = 0;
  seed = 0;
  std::memset(themeKey, 0, sizeof(themeKey));
  std::memset(themeTitle, 0, sizeof(themeTitle));
  std::memset(grid, 0, sizeof(grid));
  for (PuzzleWord& w : words) {
    std::memset(w.display, 0, sizeof(w.display));
    std::memset(w.letters, 0, sizeof(w.letters));
    w.place = Placement{};
    w.found = false;
    w.foundLine = Line{};
  }
  wordCount = 0;
  hintWord = -1;
  hintsUsed = 0;
  elapsedSeconds = 0;
  cursor = makeCell(0, 0);
}

int Puzzle::foundCount() const {
  int n = 0;
  for (int i = 0; i < wordCount; i++) n += words[i].found ? 1 : 0;
  return n;
}

size_t gridLetters(const char* display, const size_t displayLen, char* out, const size_t cap) {
  if (!display || !out || cap == 0) return 0;
  size_t n = 0;
  for (size_t i = 0; i < displayLen; i++) {
    const char c = display[i];
    if (c == ' ' || c == '-' || c == '\'') continue;
    if (c < 'A' || c > 'Z') return 0;
    if (n + 1 >= cap) return 0;
    out[n++] = c;
  }
  out[n] = '\0';
  return n;
}

size_t lettersOnLine(const Puzzle& p, const Line line, char* out, const size_t cap) {
  const int n = lineCells(line.a, line.b);
  if (n == 0 || !inGrid(line.a, p.size) || !inGrid(line.b, p.size) || static_cast<size_t>(n) + 1 > cap) return 0;
  const int dr = (line.b.row > line.a.row) - (line.b.row < line.a.row);
  const int dc = (line.b.col > line.a.col) - (line.b.col < line.a.col);
  for (int i = 0; i < n; i++) out[i] = p.at(line.a.row + dr * i, line.a.col + dc * i);
  out[n] = '\0';
  return static_cast<size_t>(n);
}

size_t formatElapsed(const uint32_t seconds, char* out, const size_t cap) {
  const uint32_t h = seconds / 3600;
  const uint32_t m = (seconds / 60) % 60;
  const uint32_t s = seconds % 60;
  const int n =
      h > 0 ? std::snprintf(out, cap, "%lu:%02lu:%02lu", static_cast<unsigned long>(h), static_cast<unsigned long>(m),
                            static_cast<unsigned long>(s))
            : std::snprintf(out, cap, "%lu:%02lu", static_cast<unsigned long>(m), static_cast<unsigned long>(s));
  return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

}  // namespace ws
