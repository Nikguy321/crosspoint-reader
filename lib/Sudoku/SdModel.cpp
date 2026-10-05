#include "SdModel.h"

#include <cstring>

namespace sd {

namespace {

// Toggle-guard targets: a square (0..80), a digit key, Erase, Notes.
constexpr int16_t GUARD_DIGIT = 100;  // + digit
constexpr int16_t GUARD_ERASE = 110;
constexpr int16_t GUARD_NOTES = 111;

}  // namespace

void Game::clear() {
  tier = Easy;
  number = 0;
  seed = 0;
  fnv = 0;
  std::memset(givens, 0, sizeof(givens));
  std::memset(solution, 0, sizeof(solution));
  std::memset(value, 0, sizeof(value));
  std::memset(notes, 0, sizeof(notes));
  std::memset(flags, 0, sizeof(flags));
  cursor = NO_CELL;
  notesMode = false;
  lock = LOCK_NONE;
  elapsed = 0;
  checks = 0;
  hints = 0;
  reveals = 0;
  solved = false;
}

void startGame(Game& game, const Generated& gen, const uint32_t number, const uint32_t seed) {
  game.clear();
  game.tier = gen.tier;
  game.number = number;
  game.seed = seed;
  std::memcpy(game.givens, gen.givens, CELLS);
  std::memcpy(game.solution, gen.solution, CELLS);
  std::memcpy(game.value, gen.givens, CELLS);
  game.fnv = givensFnv(game.givens);
}

bool isComplete(const Game& g) { return std::memcmp(g.value, g.solution, CELLS) == 0; }

bool validGame(const Game& g) {
  if (g.tier >= TIER_COUNT) return false;
  // The solution: a complete grid with no clash.
  for (int i = 0; i < CELLS; i++) {
    if (g.solution[i] < 1 || g.solution[i] > 9) return false;
  }
  if (countSolutions(g.solution, 2) != 1) return false;
  for (int i = 0; i < CELLS; i++) {
    const uint8_t given = g.givens[i];
    const uint8_t v = g.value[i];
    if (given > 9 || v > 9) return false;
    if (given && (given != g.solution[i] || v != given)) return false;
    if (g.notes[i] & ~ALL_DIGITS) return false;
    if (v && g.notes[i]) return false;
    const uint8_t f = g.flags[i];
    if (f & ~(FLAG_WRONG | FLAG_REVEALED)) return false;
    if ((f & FLAG_REVEALED) && (given || v != g.solution[i] || (f & FLAG_WRONG))) return false;
    if ((f & FLAG_WRONG) && (given || v == 0 || v == g.solution[i])) return false;
  }
  if (givensFnv(g.givens) != g.fnv) return false;
  uint8_t first[CELLS];
  if (countSolutions(g.givens, 2, first) != 1 || std::memcmp(first, g.solution, CELLS) != 0) return false;
  if (g.cursor != NO_CELL && g.cursor >= CELLS) return false;
  if (g.lock > LOCK_ERASE) return false;
  if (g.elapsed > ELAPSED_MAX) return false;
  if (g.solved != isComplete(g)) return false;
  return true;
}

// ---- undo ---------------------------------------------------------------------------------------

uint64_t packUndo(const UndoRecord& r) {
  return static_cast<uint64_t>(r.cell & 0x7F) | (static_cast<uint64_t>(r.before & 0xF) << 7) |
         (static_cast<uint64_t>(r.after & 0xF) << 11) | (static_cast<uint64_t>(r.notesBefore & ALL_DIGITS) << 15) |
         (static_cast<uint64_t>(r.wrongBefore ? 1 : 0) << 24) | (static_cast<uint64_t>(r.peersLost & 0xFFFFF) << 25) |
         (static_cast<uint64_t>(r.groupStart ? 1 : 0) << 45);
}

UndoRecord unpackUndo(const uint64_t bits) {
  UndoRecord r;
  r.cell = static_cast<uint8_t>(bits & 0x7F);
  r.before = static_cast<uint8_t>((bits >> 7) & 0xF);
  r.after = static_cast<uint8_t>((bits >> 11) & 0xF);
  r.notesBefore = static_cast<uint16_t>((bits >> 15) & ALL_DIGITS);
  r.wrongBefore = ((bits >> 24) & 1) != 0;
  r.peersLost = static_cast<uint32_t>((bits >> 25) & 0xFFFFF);
  r.groupStart = ((bits >> 45) & 1) != 0;
  return r;
}

namespace {
constexpr uint64_t GROUP_BIT = 1ull << 45;
}  // namespace

int UndoRing::groups() const {
  int n = 0;
  for (int k = 0; k < count; k++) {
    if (slots[(head + UNDO_CAPACITY - count + k) % UNDO_CAPACITY] & GROUP_BIT) n++;
  }
  return n;
}

void UndoRing::push(const UndoRecord& r) {
  if (count == UNDO_CAPACITY) {
    // Drop the oldest record, then the rest of its group.
    do {
      count--;
    } while (count > 0 && !(slots[(head + UNDO_CAPACITY - count) % UNDO_CAPACITY] & GROUP_BIT));
  }
  uint64_t bits = packUndo(r);
  if (count == 0) bits |= GROUP_BIT;  // a group never starts mid-way
  slots[head] = bits;
  head = static_cast<uint16_t>((head + 1) % UNDO_CAPACITY);
  count++;
}

bool UndoRing::pop(UndoRecord& out) {
  if (count == 0) return false;
  head = static_cast<uint16_t>((head + UNDO_CAPACITY - 1) % UNDO_CAPACITY);
  count--;
  out = unpackUndo(slots[head]);
  return true;
}

void Model::resetSession() {
  undo.clear();
  hintCell = NO_CELL;
  guardTarget = -1;
  guardMs = 0;
}

// ---- queries ------------------------------------------------------------------------------------

int remaining(const Game& g, const int digit) {
  int n = 0;
  for (const uint8_t v : g.value) n += v == digit;
  return n >= 9 ? 0 : 9 - n;
}

int emptyCount(const Game& g) {
  int n = 0;
  for (const uint8_t v : g.value) n += v == 0;
  return n;
}

uint16_t candidatesOf(const Game& g, const int cell) {
  uint16_t seen = 0;
  for (int k = 0; k < 20; k++) {
    const uint8_t v = g.value[peerOf(cell, k)];
    if (v) seen |= digitBit(v);
  }
  return static_cast<uint16_t>(ALL_DIGITS & ~seen);
}

bool isClash(const Game& g, const int cell) {
  const uint8_t v = g.value[cell];
  if (!v || isGiven(g, cell)) return false;
  for (int k = 0; k < 20; k++) {
    if (g.value[peerOf(cell, k)] == v) return true;
  }
  return false;
}

int clashCount(const Game& g) {
  int n = 0;
  for (int i = 0; i < CELLS; i++) n += isClash(g, i);
  return n;
}

bool firstClash(const Game& g, uint8_t& digit, uint8_t& unit) {
  for (int u = 0; u < 27; u++) {
    uint16_t seen = 0;
    for (int k = 0; k < 9; k++) {
      const uint8_t v = g.value[unitCell(u, k)];
      if (!v) continue;
      if (seen & digitBit(v)) {
        digit = v;
        unit = static_cast<uint8_t>(u);
        return true;
      }
      seen |= digitBit(v);
    }
  }
  return false;
}

// ---- the edits ----------------------------------------------------------------------------------

namespace {

bool validCell(const int cell) { return cell >= 0 && cell < CELLS; }
bool validDigit(const int d) { return d >= 1 && d <= 9; }

void say(Change& c, const Msg msg) {
  c.msg = msg;
  c.changed = true;
}

// A toggle on target: true when it repeats the last toggle on the same target within the guard
// (ignore it); otherwise it becomes the last toggle.
bool repeatedToggle(Model& m, const int16_t target, const uint32_t nowMs) {
  if (m.guardTarget == target && nowMs - m.guardMs < TOGGLE_GUARD_MS) return true;
  m.guardTarget = target;
  m.guardMs = nowMs;
  return false;
}

// Refuses a square that never changes; true when it may change.
bool editable(const Game& g, const int cell, Change& c) {
  if (isGiven(g, cell)) {
    say(c, Msg::Given);
    return false;
  }
  if (isRevealed(g, cell)) {
    say(c, Msg::Locked);
    return false;
  }
  return true;
}

void edited(Model& m, Change& c) {
  c.changed = true;
  c.edited = true;
  m.hintCell = NO_CELL;
}

void checkSolved(Model& m, Change& c) {
  Game& g = m.game;
  if (g.solved || !isComplete(g)) return;
  g.solved = true;
  g.lock = LOCK_NONE;
  g.notesMode = false;
  m.hintCell = NO_CELL;
  c.solvedNow = true;
  c.changed = true;
  c.edited = true;
}

// Removes digit d from the empty peers' notes; the mask of peers that lost it.
uint32_t clearPeerNotes(Game& g, const int cell, const int d) {
  uint32_t lost = 0;
  const uint16_t bit = digitBit(d);
  for (int k = 0; k < 20; k++) {
    const int p = peerOf(cell, k);
    if (g.value[p] == 0 && (g.notes[p] & bit)) {
      g.notes[p] = static_cast<uint16_t>(g.notes[p] & ~bit);
      lost |= 1u << k;
    }
  }
  return lost;
}

// Writes d into a square (one undo group). Same digit: nothing.
Change writeDigit(Model& m, const int cell, const int d) {
  Change c;
  Game& g = m.game;
  if (!editable(g, cell, c) || g.value[cell] == d) return c;
  UndoRecord r;
  r.cell = static_cast<uint8_t>(cell);
  r.before = g.value[cell];
  r.after = static_cast<uint8_t>(d);
  r.notesBefore = g.notes[cell];
  r.wrongBefore = isWrongMarked(g, cell);
  r.groupStart = true;
  g.value[cell] = static_cast<uint8_t>(d);
  g.notes[cell] = 0;
  g.flags[cell] = static_cast<uint8_t>(g.flags[cell] & ~FLAG_WRONG);
  if (m.settings.removeNotes) r.peersLost = clearPeerNotes(g, cell, d);
  m.undo.push(r);
  edited(m, c);
  checkSolved(m, c);
  return c;
}

Change noteToggle(Model& m, const int cell, const int d) {
  Change c;
  Game& g = m.game;
  if (!editable(g, cell, c)) return c;
  if (g.value[cell]) {
    say(c, Msg::EraseFirst);
    c.digit = g.value[cell];
    return c;
  }
  UndoRecord r;
  r.cell = static_cast<uint8_t>(cell);
  r.notesBefore = g.notes[cell];
  r.groupStart = true;
  m.undo.push(r);
  g.notes[cell] = static_cast<uint16_t>(g.notes[cell] ^ digitBit(d));
  edited(m, c);
  return c;
}

// Clears a square's digit, else its notes.
Change erase(Model& m, const int cell) {
  Change c;
  Game& g = m.game;
  if (!editable(g, cell, c)) return c;
  if (!g.value[cell] && !g.notes[cell]) return c;
  UndoRecord r;
  r.cell = static_cast<uint8_t>(cell);
  r.before = g.value[cell];
  r.notesBefore = g.notes[cell];
  r.wrongBefore = isWrongMarked(g, cell);
  r.groupStart = true;
  m.undo.push(r);
  if (g.value[cell]) {
    g.value[cell] = 0;
    g.flags[cell] = static_cast<uint8_t>(g.flags[cell] & ~FLAG_WRONG);
  } else {
    g.notes[cell] = 0;
  }
  edited(m, c);
  return c;
}

Change setLock(Model& m, const uint8_t lock) {
  Change c;
  Game& g = m.game;
  if (g.lock == lock && (lock == LOCK_NONE || g.cursor == NO_CELL)) return c;
  g.lock = lock;
  if (lock != LOCK_NONE) g.cursor = NO_CELL;
  c.changed = true;
  c.modeChanged = true;
  return c;
}

// A short tap on a digit key (1..9) or Erase (LOCK_ERASE).
Change tapKey(Model& m, const uint8_t key, const uint32_t nowMs) {
  Game& g = m.game;
  if (g.solved) return Change{};
  const int16_t target = key == LOCK_ERASE ? GUARD_ERASE : static_cast<int16_t>(GUARD_DIGIT + key);
  if (g.lock != LOCK_NONE) {
    if (g.lock == key) {
      if (repeatedToggle(m, target, nowMs)) return Change{};
      return setLock(m, LOCK_NONE);
    }
    m.guardTarget = target;  // switching is no toggle, but a quick second tap must not unlock
    m.guardMs = nowMs;
    return setLock(m, key);
  }
  if (g.cursor == NO_CELL) {
    if (repeatedToggle(m, target, nowMs)) return Change{};
    return setLock(m, key);
  }
  if (key == LOCK_ERASE) return erase(m, g.cursor);
  if (g.notesMode) {
    if (repeatedToggle(m, target, nowMs)) return Change{};
    return noteToggle(m, g.cursor, key);
  }
  return writeDigit(m, g.cursor, key);
}

Change holdKey(Model& m, const uint8_t key, const uint32_t nowMs) {
  if (m.game.solved) return Change{};
  m.guardTarget = key == LOCK_ERASE ? GUARD_ERASE : static_cast<int16_t>(GUARD_DIGIT + key);
  m.guardMs = nowMs;
  return setLock(m, key);
}

}  // namespace

// ---- input --------------------------------------------------------------------------------------

Change tapCell(Model& m, const int cell, const uint32_t nowMs) {
  Game& g = m.game;
  if (!validCell(cell) || g.solved) return Change{};  // a solved grid draws no cursor
  if (g.lock == LOCK_NONE) {
    // Another square: the same key tapped there is no double tap.
    if (g.cursor != cell) m.guardTarget = -1;
    return setCursor(m, cell);
  }
  if (g.lock == LOCK_ERASE) return erase(m, cell);
  if (g.notesMode) {
    if (repeatedToggle(m, static_cast<int16_t>(cell), nowMs)) return Change{};
    return noteToggle(m, cell, g.lock);
  }
  return writeDigit(m, cell, g.lock);
}

Change tapDigit(Model& m, const int digit, const uint32_t nowMs) {
  if (!validDigit(digit)) return Change{};
  return tapKey(m, static_cast<uint8_t>(digit), nowMs);
}

Change holdDigit(Model& m, const int digit, const uint32_t nowMs) {
  if (!validDigit(digit)) return Change{};
  return holdKey(m, static_cast<uint8_t>(digit), nowMs);
}

Change tapErase(Model& m, const uint32_t nowMs) { return tapKey(m, LOCK_ERASE, nowMs); }

Change holdErase(Model& m, const uint32_t nowMs) { return holdKey(m, LOCK_ERASE, nowMs); }

Change toggleNotesMode(Model& m, const uint32_t nowMs) {
  Change c;
  if (m.game.solved || repeatedToggle(m, GUARD_NOTES, nowMs)) return c;
  m.game.notesMode = !m.game.notesMode;
  c.changed = true;
  c.modeChanged = true;
  return c;
}

Change undo(Model& m) {
  Change c;
  Game& g = m.game;
  if (g.solved) return c;
  UndoRecord r;
  if (!m.undo.pop(r)) {
    say(c, Msg::NothingToUndo);
    return c;
  }
  for (;;) {
    const int cell = r.cell < CELLS ? r.cell : 0;
    if (!isLocked(g, cell)) {  // a reveal since then keeps its square
      g.value[cell] = r.before;
      g.notes[cell] = r.notesBefore;
      g.flags[cell] =
          static_cast<uint8_t>(r.wrongBefore ? (g.flags[cell] | FLAG_WRONG) : (g.flags[cell] & ~FLAG_WRONG));
    }
    if (r.peersLost && validDigit(r.after)) {
      for (int k = 0; k < 20; k++) {
        const int p = peerOf(cell, k);
        if ((r.peersLost & (1u << k)) && g.value[p] == 0)
          g.notes[p] = static_cast<uint16_t>(g.notes[p] | digitBit(r.after));
      }
    }
    if (r.groupStart || !m.undo.pop(r)) break;
  }
  edited(m, c);
  // A reveal since then keeps its square, so undo can complete a grid that was never complete.
  checkSolved(m, c);
  return c;
}

// ---- direct edits -------------------------------------------------------------------------------

Change setCursor(Model& m, const int cell) {
  Change c;
  const uint8_t to = validCell(cell) ? static_cast<uint8_t>(cell) : NO_CELL;
  if (m.game.cursor == to) return c;
  m.game.cursor = to;
  c.changed = true;
  return c;
}

Change putDigit(Model& m, const int cell, const int digit) {
  if (!validCell(cell) || !validDigit(digit) || m.game.solved) return Change{};
  Change c = setCursor(m, cell);
  const Change w = writeDigit(m, cell, digit);
  c.changed |= w.changed;
  c.edited = w.edited;
  c.solvedNow = w.solvedNow;
  c.msg = w.msg;
  return c;
}

Change toggleNote(Model& m, const int cell, const int digit) {
  if (!validCell(cell) || !validDigit(digit) || m.game.solved) return Change{};
  Change c = setCursor(m, cell);
  const Change w = noteToggle(m, cell, digit);
  c.changed |= w.changed;
  c.edited = w.edited;
  c.msg = w.msg;
  c.digit = w.digit;
  return c;
}

Change eraseSquare(Model& m, const int cell) {
  if (!validCell(cell) || m.game.solved) return Change{};
  Change c = setCursor(m, cell);
  const Change w = erase(m, cell);
  c.changed |= w.changed;
  c.edited = w.edited;
  c.msg = w.msg;
  return c;
}

// ---- menu ---------------------------------------------------------------------------------------

namespace {

// Sets every empty square's notes to fill ? its candidates : none, as one undo group.
Change setAllNotes(Model& m, const bool fill) {
  Change c;
  Game& g = m.game;
  if (g.solved) return c;
  bool first = true;
  for (int i = 0; i < CELLS; i++) {
    if (g.value[i]) continue;
    const uint16_t want = fill ? candidatesOf(g, i) : 0;
    if (g.notes[i] == want) continue;
    UndoRecord r;
    r.cell = static_cast<uint8_t>(i);
    r.notesBefore = g.notes[i];
    r.groupStart = first;
    first = false;
    m.undo.push(r);
    g.notes[i] = want;
    c.count++;
  }
  if (c.count) edited(m, c);
  return c;
}

}  // namespace

Change fillAllNotes(Model& m) { return setAllNotes(m, true); }

Change clearAllNotes(Model& m) { return setAllNotes(m, false); }

Change check(Model& m, const Scope scope) {
  Change c;
  Game& g = m.game;
  if (g.solved) return c;
  if (scope == Scope::Square && g.cursor == NO_CELL) {
    say(c, Msg::NoSquare);
    return c;
  }
  const int from = scope == Scope::Square ? g.cursor : 0;
  const int to = scope == Scope::Square ? g.cursor + 1 : CELLS;
  for (int i = from; i < to; i++) {
    if (isLocked(g, i) || !g.value[i] || g.value[i] == g.solution[i]) continue;
    g.flags[i] = static_cast<uint8_t>(g.flags[i] | FLAG_WRONG);
    c.count++;
  }
  if (g.checks < COUNT_MAX) g.checks++;
  m.hintCell = NO_CELL;
  say(c, Msg::Checked);
  c.edited = true;
  return c;
}

namespace {

// Reveals one square (not a given, not already showing its solution); false when nothing to do.
bool revealSquare(Model& m, const int cell) {
  Game& g = m.game;
  if (isGiven(g, cell) || g.value[cell] == g.solution[cell]) return false;
  g.value[cell] = g.solution[cell];
  g.notes[cell] = 0;
  g.flags[cell] = FLAG_REVEALED;
  if (m.settings.removeNotes) clearPeerNotes(g, cell, g.solution[cell]);
  return true;
}

}  // namespace

Change reveal(Model& m, const Scope scope) {
  Change c;
  Game& g = m.game;
  if (g.solved) return c;
  if (scope == Scope::Square && g.cursor == NO_CELL) {
    say(c, Msg::NoSquare);
    return c;
  }
  const int from = scope == Scope::Square ? g.cursor : 0;
  const int to = scope == Scope::Square ? g.cursor + 1 : CELLS;
  for (int i = from; i < to; i++) c.count += revealSquare(m, i);
  say(c, Msg::Revealed);
  if (c.count) {
    if (g.reveals < COUNT_MAX) g.reveals++;
    edited(m, c);
    checkSolved(m, c);
  }
  return c;
}

Change hint(Model& m) {
  Change c;
  Game& g = m.game;
  if (g.solved) return c;
  say(c, Msg::Hint);
  // (3) The same square again: reveal it.
  if (m.hintCell != NO_CELL && g.cursor == m.hintCell) {
    const int cell = m.hintCell;
    m.hintCell = NO_CELL;
    if (revealSquare(m, cell)) {
      if (g.reveals < COUNT_MAX) g.reveals++;
      c.hint.kind = HintKind::Revealed;
      c.hint.cell = static_cast<uint8_t>(cell);
      c.hint.digit = g.solution[cell];
      edited(m, c);
      checkSolved(m, c);
      return c;
    }
  }
  if (g.hints < COUNT_MAX) g.hints++;
  c.edited = true;
  // (1) A wrong entry first: logic run on a wrong grid gives wrong advice.
  int wrong = -1;
  if (g.cursor != NO_CELL && g.value[g.cursor] && g.value[g.cursor] != g.solution[g.cursor]) wrong = g.cursor;
  for (int i = 0; i < CELLS && wrong < 0; i++) {
    if (g.value[i] && g.value[i] != g.solution[i]) wrong = i;
  }
  int cell = -1;
  if (wrong >= 0) {
    g.flags[wrong] = static_cast<uint8_t>(g.flags[wrong] | FLAG_WRONG);
    c.hint.kind = HintKind::Wrong;
    cell = wrong;
  } else {
    // (2) The logic from what the grid shows (every digit is right here).
    NextPlacement next;
    if (findNextPlacement(g.value, next) && next.place.place >= 0 && g.value[next.place.place] == 0 &&
        next.place.digit == g.solution[next.place.place]) {
      cell = next.place.place;
      if (next.singlesOnly) {
        c.hint.kind = HintKind::Single;
        c.hint.digit = next.place.digit;
        c.hint.step = next.place;
      } else {
        c.hint.kind = next.hardest.tech == Trial ? HintKind::Trial : HintKind::Step;
        c.hint.step = next.hardest;
      }
    } else {
      // Stuck (or the ladder disagrees with the solution, which the tests rule out): point at a
      // square; Hint again reveals it.
      cell = g.cursor != NO_CELL && g.value[g.cursor] == 0 ? g.cursor : -1;
      for (int i = 0; i < CELLS && cell < 0; i++) {
        if (g.value[i] == 0) cell = i;
      }
      c.hint.kind = HintKind::Trial;
    }
  }
  if (cell < 0) return c;  // full and right: solved already
  c.hint.cell = static_cast<uint8_t>(cell);
  // A lock would send the next tap to itself, not to the square the hint points at.
  if (g.lock != LOCK_NONE) {
    g.lock = LOCK_NONE;
    c.modeChanged = true;
  }
  g.cursor = static_cast<uint8_t>(cell);
  m.hintCell = static_cast<uint8_t>(cell);
  return c;
}

Change restart(Model& m) {
  Change c;
  Game& g = m.game;
  std::memcpy(g.value, g.givens, CELLS);
  std::memset(g.notes, 0, sizeof(g.notes));
  std::memset(g.flags, 0, sizeof(g.flags));
  g.cursor = NO_CELL;
  g.notesMode = false;
  g.lock = LOCK_NONE;
  g.elapsed = 0;
  g.checks = 0;
  g.hints = 0;
  g.reveals = 0;
  g.solved = false;
  m.resetSession();
  c.changed = true;
  c.edited = true;
  c.modeChanged = true;
  return c;
}

}  // namespace sd
