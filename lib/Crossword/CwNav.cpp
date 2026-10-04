#include "CwNav.h"

namespace cw {

namespace {

bool locked(const Progress& prog, const int cell) { return (prog.flags[cell] & FLAG_REVEALED) != 0; }

// Fills in the completion fields after an edit, and marks the puzzle solved.
void settle(const Puzzle& p, Progress& prog, Change& ch) {
  const Tally t = tally(p, prog);
  if (t.correct()) {
    if (!prog.solved) {
      prog.solved = true;
      ch.solvedNow = true;
    }
    return;
  }
  if (t.full()) {
    ch.fullWrong = true;
    ch.wrong = t.wrong;
  }
}

void moveTo(const Puzzle& p, Progress& prog, const int cell, const uint8_t dir, Change& ch) {
  const int before = currentEntry(p, prog);
  if (prog.cursor != cell || prog.dir != dir) ch.changed = true;
  prog.cursor = static_cast<uint8_t>(cell);
  prog.dir = dir;
  if (currentEntry(p, prog) != before) ch.wordChanged = true;
}

// The squares an action covers.
template <typename F>
void forScope(const Puzzle& p, const Progress& prog, const Scope scope, F fn) {
  if (scope == Scope::Letter) {
    fn(static_cast<int>(prog.cursor));
  } else if (scope == Scope::Word) {
    const int e = currentEntry(p, prog);
    for (int i = 0; i < p.entries[e].len; i++) fn(p.entryCell(e, i));
  } else {
    for (int c = 0; c < p.cells(); c++) {
      if (!p.isBlock(c)) fn(c);
    }
  }
}

}  // namespace

Tally tally(const Puzzle& p, const Progress& prog) {
  Tally t;
  for (int c = 0; c < p.cells(); c++) {
    if (p.isBlock(c)) continue;
    t.white++;
    if (prog.fill[c] != EMPTY) {
      t.filled++;
      if (prog.fill[c] != p.solution[c]) t.wrong++;
    }
    if (prog.flags[c] & FLAG_WRONG) t.marked++;
  }
  return t;
}

int currentEntry(const Puzzle& p, const Progress& prog) {
  if (p.entryCount == 0 || prog.cursor >= p.cells()) return 0;
  const uint8_t dir = prog.dir > DOWN ? ACROSS : prog.dir;
  const uint8_t e = p.entryAt[dir][prog.cursor];
  if (e != NO_ENTRY) return e;
  const uint8_t other = p.entryAt[dir ^ 1][prog.cursor];
  return other != NO_ENTRY ? other : 0;
}

int entryPosition(const Puzzle& p, const int entry, const int cell) {
  if (entry < 0 || entry >= p.entryCount) return -1;
  for (int i = 0; i < p.entries[entry].len; i++) {
    if (p.entryCell(entry, i) == cell) return i;
  }
  return -1;
}

int firstBlank(const Puzzle& p, const Progress& prog, const int entry) {
  for (int i = 0; i < p.entries[entry].len; i++) {
    const int c = p.entryCell(entry, i);
    if (prog.fill[c] == EMPTY) return c;
  }
  return -1;
}

bool entryFull(const Puzzle& p, const Progress& prog, const int entry) { return firstBlank(p, prog, entry) < 0; }

size_t entryPattern(const Puzzle& p, const Progress& prog, const int entry, char* out, const size_t cap) {
  if (!out || cap == 0 || entry < 0 || entry >= p.entryCount) return 0;
  const int len = p.entries[entry].len;
  if (static_cast<size_t>(len) + 1 > cap) return 0;
  for (int i = 0; i < len; i++) {
    const char f = prog.fill[p.entryCell(entry, i)];
    out[i] = f == EMPTY ? '_' : f;
  }
  out[len] = '\0';
  return static_cast<size_t>(len);
}

bool canToggle(const Puzzle& p, const Progress& prog) {
  return prog.cursor < p.cells() && p.entryAt[ACROSS][prog.cursor] != NO_ENTRY &&
         p.entryAt[DOWN][prog.cursor] != NO_ENTRY;
}

Change toggleDirection(const Puzzle& p, Progress& prog) {
  Change ch;
  if (!canToggle(p, prog)) return ch;
  moveTo(p, prog, prog.cursor, static_cast<uint8_t>(prog.dir ^ 1), ch);
  return ch;
}

Change tapCell(const Puzzle& p, Progress& prog, const int cell) {
  Change ch;
  if (cell < 0 || cell >= p.cells() || p.isBlock(cell)) return ch;
  if (cell == prog.cursor) return toggleDirection(p, prog);
  const uint8_t dir = p.entryAt[prog.dir][cell] != NO_ENTRY ? prog.dir : static_cast<uint8_t>(prog.dir ^ 1);
  moveTo(p, prog, cell, dir, ch);
  return ch;
}

Change setCursor(const Puzzle& p, Progress& prog, const int cell, const uint8_t dir) {
  Change ch;
  if (cell < 0 || cell >= p.cells() || p.isBlock(cell) || dir > DOWN) return ch;
  moveTo(p, prog, cell, p.entryAt[dir][cell] != NO_ENTRY ? dir : static_cast<uint8_t>(dir ^ 1), ch);
  return ch;
}

Change gotoEntry(const Puzzle& p, Progress& prog, const int entry) {
  Change ch;
  if (entry < 0 || entry >= p.entryCount) return ch;
  const int blank = firstBlank(p, prog, entry);
  moveTo(p, prog, blank >= 0 ? blank : p.entryCell(entry, 0), p.entries[entry].dir, ch);
  return ch;
}

Change stepClue(const Puzzle& p, Progress& prog, const int delta) {
  if (p.entryCount == 0) return Change{};
  const int n = p.entryCount;
  const int e = currentEntry(p, prog);
  return gotoEntry(p, prog, ((e + delta) % n + n) % n);
}

Change typeLetter(const Puzzle& p, Progress& prog, char letter, const bool skipFilled) {
  Change ch;
  if (letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
  if (prog.solved || letter < 'A' || letter > 'Z' || p.entryCount == 0) return ch;
  const int cell = prog.cursor;
  const int e = currentEntry(p, prog);
  // A word that was already full is being overwritten: step square by square (as with Skip filled off), so a
  // correction stays in its word; the end-of-word rule applies only past its last square.
  const bool overwrite = firstBlank(p, prog, e) < 0;
  if (!locked(prog, cell)) {
    if (prog.fill[cell] != letter || prog.flags[cell] != 0) {
      prog.fill[cell] = letter;
      prog.flags[cell] = 0;
      ch.changed = true;
      ch.edited = true;
    }
  }
  // Advance.
  const int len = p.entries[e].len;
  int next = -1;
  for (int j = entryPosition(p, e, cell) + 1; j < len; j++) {
    const int c = p.entryCell(e, j);
    if (!skipFilled || overwrite || prog.fill[c] == EMPTY) {
      next = c;
      break;
    }
  }
  if (next >= 0 || (next = firstBlank(p, prog, e)) >= 0) {
    moveTo(p, prog, next, p.entries[e].dir, ch);
  } else {
    for (int k = 1; k < p.entryCount; k++) {
      const int other = (e + k) % p.entryCount;
      const int blank = firstBlank(p, prog, other);
      if (blank >= 0) {
        moveTo(p, prog, blank, p.entries[other].dir, ch);
        break;
      }
    }
  }
  if (ch.edited) settle(p, prog, ch);
  return ch;
}

Change deleteLetter(const Puzzle& p, Progress& prog) {
  Change ch;
  if (prog.solved || p.entryCount == 0) return ch;
  const int cell = prog.cursor;
  if (prog.fill[cell] != EMPTY && !locked(prog, cell)) {
    prog.fill[cell] = EMPTY;
    prog.flags[cell] = 0;
    ch.changed = true;
    ch.edited = true;
    return ch;
  }
  const int e = currentEntry(p, prog);
  const int pos = entryPosition(p, e, cell);
  if (pos <= 0) return ch;
  const int prev = p.entryCell(e, pos - 1);
  moveTo(p, prog, prev, p.entries[e].dir, ch);
  if (prog.fill[prev] != EMPTY && !locked(prog, prev)) {
    prog.fill[prev] = EMPTY;
    prog.flags[prev] = 0;
    ch.changed = true;
    ch.edited = true;
  }
  return ch;
}

Change clearWord(const Puzzle& p, Progress& prog) {
  Change ch;
  if (prog.solved || p.entryCount == 0) return ch;
  forScope(p, prog, Scope::Word, [&](const int c) {
    if (locked(prog, c) || (prog.fill[c] == EMPTY && prog.flags[c] == 0)) return;
    prog.fill[c] = EMPTY;
    prog.flags[c] = 0;
    ch.changed = true;
    ch.edited = true;
  });
  return ch;
}

Change clearPuzzle(const Puzzle& p, Progress& prog) {
  Change ch;
  for (int c = 0; c < p.cells(); c++) {
    if (p.isBlock(c)) continue;
    if (prog.fill[c] != EMPTY || prog.flags[c] != 0) {
      ch.changed = true;
      ch.edited = true;
    }
    prog.fill[c] = EMPTY;
    prog.flags[c] = 0;
  }
  if (prog.solved || prog.elapsed || prog.checks || prog.reveals) ch.changed = true;
  prog.solved = false;
  prog.elapsed = 0;
  prog.checks = 0;
  prog.reveals = 0;
  return ch;
}

Change check(const Puzzle& p, Progress& prog, const Scope scope) {
  Change ch;
  if (prog.solved || p.entryCount == 0) return ch;
  forScope(p, prog, scope, [&](const int c) {
    if (prog.fill[c] == EMPTY || locked(prog, c) || prog.fill[c] == p.solution[c]) return;
    if (!(prog.flags[c] & FLAG_WRONG)) {
      prog.flags[c] |= FLAG_WRONG;
      ch.changed = true;
    }
    ch.count++;
  });
  if (prog.checks < 0xFFFF) prog.checks++;
  return ch;
}

Change reveal(const Puzzle& p, Progress& prog, const Scope scope) {
  Change ch;
  if (prog.solved || p.entryCount == 0) return ch;
  forScope(p, prog, scope, [&](const int c) {
    if (prog.fill[c] == p.solution[c]) return;
    prog.fill[c] = p.solution[c];
    prog.flags[c] = FLAG_REVEALED;
    ch.count++;
  });
  if (ch.count == 0) return ch;
  ch.changed = true;
  ch.edited = true;
  if (prog.reveals < 0xFFFF) prog.reveals++;
  settle(p, prog, ch);
  return ch;
}

}  // namespace cw
