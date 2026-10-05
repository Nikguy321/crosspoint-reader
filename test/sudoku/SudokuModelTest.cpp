// Sudoku play rules on the host: square-first entry and the digit lock, givens and revealed
// squares, the double-tap guard, notes and their auto-removal, clashes, check / reveal / hint
// (a hint never writes a wrong digit, on every tier), restart, and the undo ring: random action
// sequences undo to the exact earlier states, peers' notes included, and an overflow evicts whole
// groups.
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "Sudoku.h"

using namespace sd;

namespace {

constexpr int rc(const int r, const int c) { return r * 9 + c; }

std::unique_ptr<Model> newModel(const int tier = Easy, const uint32_t number = 1) {
  auto m = std::make_unique<Model>();
  Generated gen;
  EXPECT_TRUE(generate(puzzleSeed(tier, number), tier, gen));
  startGame(m->game, gen, number, puzzleSeed(tier, number));
  m->resetSession();
  return m;
}

int firstEmpty(const Game& g, const int from = 0) {
  for (int i = from; i < CELLS; i++) {
    if (!g.value[i]) return i;
  }
  return -1;
}

int firstGiven(const Game& g) {
  for (int i = 0; i < CELLS; i++) {
    if (g.givens[i]) return i;
  }
  return -1;
}

// A digit that is wrong for the square and clashes with nothing shown.
int wrongQuietDigit(const Game& g, const int cell) {
  const uint16_t c = candidatesOf(g, cell);
  for (int d = 1; d <= 9; d++) {
    if ((c & digitBit(d)) && d != g.solution[cell]) return d;
  }
  return 0;
}

struct Snapshot {
  uint8_t value[CELLS];
  uint16_t notes[CELLS];
  uint8_t flags[CELLS];
  explicit Snapshot(const Game& g) {
    std::memcpy(value, g.value, sizeof(value));
    std::memcpy(notes, g.notes, sizeof(notes));
    std::memcpy(flags, g.flags, sizeof(flags));
  }
  bool operator==(const Snapshot& o) const {
    return std::memcmp(value, o.value, sizeof(value)) == 0 && std::memcmp(notes, o.notes, sizeof(notes)) == 0 &&
           std::memcmp(flags, o.flags, sizeof(flags)) == 0;
  }
};

// The state saves and loads back (validGame is part of parseGame).
bool roundTrips(const Game& g) {
  char text[PUZZLE_TEXT_MAX];
  const size_t len = formatGame(g, text, sizeof(text));
  Game back;
  return len > 0 && parseGame(text, len, back);
}

}  // namespace

// ---- entry ------------------------------------------------------------------------------------------

TEST(SudokuModel, StartsClean) {
  auto m = newModel();
  const Game& g = m->game;
  EXPECT_TRUE(validGame(g));
  EXPECT_EQ(g.cursor, NO_CELL);
  EXPECT_EQ(g.lock, LOCK_NONE);
  EXPECT_FALSE(g.notesMode);
  EXPECT_EQ(g.number, 1u);
  EXPECT_EQ(g.tier, Easy);
  EXPECT_EQ(g.fnv, givensFnv(g.givens));
  EXPECT_EQ(emptyCount(g), CELLS - 36);
  int left = 0;
  for (int d = 1; d <= 9; d++) left += remaining(g, d);
  EXPECT_EQ(left, emptyCount(g));
  EXPECT_EQ(m->undo.size(), 0);
}

TEST(SudokuModel, SquareFirstWritesAndOverwrites) {
  auto m = newModel();
  Game& g = m->game;
  const int cell = firstEmpty(g);
  Change c = tapCell(*m, cell, 1000);
  EXPECT_TRUE(c.changed);
  EXPECT_FALSE(c.edited);
  EXPECT_EQ(g.cursor, cell);
  // Tapping the cursor square again does nothing.
  EXPECT_FALSE(tapCell(*m, cell, 2000).changed);
  c = tapDigit(*m, 3, 3000);
  EXPECT_TRUE(c.edited);
  EXPECT_EQ(g.value[cell], 3);
  EXPECT_EQ(g.cursor, cell);  // the cursor stays for a quick correction
  EXPECT_EQ(m->undo.size(), 1);
  // The same digit again changes nothing (no toggle-to-clear), even quickly.
  c = tapDigit(*m, 3, 3100);
  EXPECT_FALSE(c.changed);
  EXPECT_EQ(g.value[cell], 3);
  EXPECT_EQ(m->undo.size(), 1);
  // Another digit overwrites as one step.
  tapDigit(*m, 4, 3200);
  EXPECT_EQ(g.value[cell], 4);
  EXPECT_EQ(m->undo.size(), 2);
  undo(*m);
  EXPECT_EQ(g.value[cell], 3);
  undo(*m);
  EXPECT_EQ(g.value[cell], 0);
  const Change none = undo(*m);
  EXPECT_EQ(none.msg, Msg::NothingToUndo);
  EXPECT_FALSE(none.edited);
}

TEST(SudokuModel, GivensNeverChange) {
  auto m = newModel();
  Game& g = m->game;
  const int given = firstGiven(g);
  const uint8_t v = g.value[given];
  tapCell(*m, given, 1000);
  EXPECT_EQ(g.cursor, given);  // a given can be selected (it shows where its digit is)
  Change c = tapDigit(*m, v == 9 ? 1 : v + 1, 2000);
  EXPECT_EQ(c.msg, Msg::Given);
  EXPECT_FALSE(c.edited);
  EXPECT_EQ(g.value[given], v);
  EXPECT_EQ(tapErase(*m, 3000).msg, Msg::Given);
  EXPECT_EQ(g.value[given], v);
  toggleNotesMode(*m, 4000);
  EXPECT_EQ(tapDigit(*m, 1, 5000).msg, Msg::Given);
  EXPECT_EQ(g.notes[given], 0);
  // A locked digit tapped onto a given.
  toggleNotesMode(*m, 6000);
  holdDigit(*m, 2, 7000);
  EXPECT_EQ(tapCell(*m, given, 8000).msg, Msg::Given);
  EXPECT_EQ(g.value[given], v);
  EXPECT_EQ(eraseSquare(*m, given).msg, Msg::Given);
  EXPECT_EQ(m->undo.size(), 0);
}

TEST(SudokuModel, DigitLock) {
  auto m = newModel();
  Game& g = m->game;
  // Nothing selected: a tap locks the digit.
  Change c = tapDigit(*m, 5, 1000);
  EXPECT_TRUE(c.modeChanged);
  EXPECT_EQ(g.lock, 5);
  // A quick second tap on the same key (the e-ink double tap) does not unlock it.
  EXPECT_FALSE(tapDigit(*m, 5, 1000 + TOGGLE_GUARD_MS - 1).changed);
  EXPECT_EQ(g.lock, 5);
  // Each square tapped gets the digit; givens refuse; the cursor stays hidden.
  const int a = firstEmpty(g);
  const int b = firstEmpty(g, a + 1);
  tapCell(*m, a, 2000);
  tapCell(*m, b, 2100);
  EXPECT_EQ(g.value[a], 5);
  EXPECT_EQ(g.value[b], 5);
  EXPECT_EQ(g.cursor, NO_CELL);
  EXPECT_EQ(m->undo.size(), 2);
  // Another key switches the lock; a quick second tap on it does not unlock.
  tapDigit(*m, 6, 3000);
  EXPECT_EQ(g.lock, 6);
  EXPECT_FALSE(tapDigit(*m, 6, 3100).changed);
  EXPECT_EQ(g.lock, 6);
  // The locked digit overwrites a non-given entry as one undo step.
  tapCell(*m, a, 4000);
  EXPECT_EQ(g.value[a], 6);
  EXPECT_EQ(m->undo.size(), 3);
  undo(*m);
  EXPECT_EQ(g.value[a], 5);
  // The locked key tapped again (after the guard) unlocks; nothing is selected after.
  c = tapDigit(*m, 6, 5000);
  EXPECT_TRUE(c.modeChanged);
  EXPECT_EQ(g.lock, LOCK_NONE);
  EXPECT_EQ(g.cursor, NO_CELL);
  // With a square selected, holding a digit locks it and hides the cursor.
  tapCell(*m, a, 6000);
  EXPECT_EQ(g.cursor, a);
  c = holdDigit(*m, 2, 6600);
  EXPECT_TRUE(c.modeChanged);
  EXPECT_EQ(g.lock, 2);
  EXPECT_EQ(g.cursor, NO_CELL);
  // Holding the locked key keeps it locked.
  EXPECT_FALSE(holdDigit(*m, 2, 8000).changed);
  EXPECT_EQ(g.lock, 2);
}

TEST(SudokuModel, EraseKeyAndEraseLock) {
  auto m = newModel();
  Game& g = m->game;
  const int a = firstEmpty(g);
  putDigit(*m, a, 7);
  EXPECT_EQ(g.cursor, a);
  // A tap clears the square's digit, then (with no digit) its notes.
  EXPECT_TRUE(tapErase(*m, 1000).edited);
  EXPECT_EQ(g.value[a], 0);
  toggleNote(*m, a, 1);
  toggleNote(*m, a, 2);
  EXPECT_EQ(g.notes[a], digitBit(1) | digitBit(2));
  EXPECT_TRUE(tapErase(*m, 2000).edited);
  EXPECT_EQ(g.notes[a], 0);
  EXPECT_FALSE(tapErase(*m, 3000).changed);  // nothing left to erase
  // Erase with nothing selected locks it; each square tapped is cleared.
  const int b = firstEmpty(g, a + 1);
  putDigit(*m, a, 1);
  putDigit(*m, b, 2);
  setCursor(*m, NO_CELL);
  EXPECT_TRUE(tapErase(*m, 4000).modeChanged);
  EXPECT_EQ(g.lock, LOCK_ERASE);
  tapCell(*m, a, 4500);
  tapCell(*m, b, 4600);
  EXPECT_EQ(g.value[a], 0);
  EXPECT_EQ(g.value[b], 0);
  // Erase tapped again (after the guard) unlocks; a digit tap switches the lock instead.
  holdErase(*m, 5000);
  tapDigit(*m, 4, 5100);
  EXPECT_EQ(g.lock, 4);
  tapErase(*m, 6000);
  EXPECT_EQ(g.lock, LOCK_ERASE);
  tapErase(*m, 7000);
  EXPECT_EQ(g.lock, LOCK_NONE);
}

TEST(SudokuModel, NotesModeAndGuard) {
  auto m = newModel();
  Game& g = m->game;
  const int a = firstEmpty(g);
  tapCell(*m, a, 0);
  EXPECT_TRUE(toggleNotesMode(*m, 1000).modeChanged);
  EXPECT_TRUE(g.notesMode);
  // The Notes key's double tap is ignored.
  EXPECT_FALSE(toggleNotesMode(*m, 1200).changed);
  EXPECT_TRUE(g.notesMode);
  tapDigit(*m, 4, 2000);
  EXPECT_EQ(g.notes[a], digitBit(4));
  EXPECT_EQ(g.value[a], 0);
  // A double tap on the same digit is one toggle; a later tap toggles back.
  EXPECT_FALSE(tapDigit(*m, 4, 2000 + TOGGLE_GUARD_MS - 1).changed);
  EXPECT_EQ(g.notes[a], digitBit(4));
  tapDigit(*m, 4, 2000 + TOGGLE_GUARD_MS);
  EXPECT_EQ(g.notes[a], 0);
  // Two different digits quickly are two toggles.
  tapDigit(*m, 1, 3000);
  tapDigit(*m, 2, 3050);
  EXPECT_EQ(g.notes[a], digitBit(1) | digitBit(2));
  // Notes with a locked digit: each square tap toggles it; the same square quickly is ignored.
  const int b = firstEmpty(g, a + 1);
  holdDigit(*m, 9, 4000);
  tapCell(*m, b, 5000);
  EXPECT_EQ(g.notes[b], digitBit(9));
  EXPECT_FALSE(tapCell(*m, b, 5100).changed);
  tapCell(*m, b, 6000);
  EXPECT_EQ(g.notes[b], 0);
  // A note on a filled square: "Erase the 5 first".
  toggleNotesMode(*m, 7000);
  putDigit(*m, b, 5);
  toggleNotesMode(*m, 8000);
  const Change c = tapCell(*m, b, 9000);
  EXPECT_EQ(c.msg, Msg::EraseFirst);
  EXPECT_EQ(c.digit, 5);
  EXPECT_EQ(g.notes[b], 0);
}

// The guard is one key on one square: the same digit quickly on the next square is a new note.
TEST(SudokuModel, NotesGuardIsPerSquare) {
  auto m = newModel();
  Game& g = m->game;
  const int a = firstEmpty(g);
  const int b = firstEmpty(g, a + 1);
  toggleNotesMode(*m, 0);
  tapCell(*m, a, 1000);
  EXPECT_TRUE(tapDigit(*m, 3, 1100).changed);
  tapCell(*m, b, 1250);
  EXPECT_TRUE(tapDigit(*m, 3, 1400).changed);
  EXPECT_EQ(g.notes[a], digitBit(3));
  EXPECT_EQ(g.notes[b], digitBit(3));
  // Tapping the square already under the cursor keeps the guard.
  tapCell(*m, b, 1450);
  EXPECT_FALSE(tapDigit(*m, 3, 1500).changed);
  EXPECT_EQ(g.notes[b], digitBit(3));
}

TEST(SudokuModel, PlacingRemovesPeersNotesAsOneStep) {
  for (const bool removeNotes : {true, false}) {
    auto m = newModel();
    m->settings.removeNotes = removeNotes;
    Game& g = m->game;
    fillAllNotes(*m);
    const Snapshot before(g);
    const int cell = firstEmpty(g);
    const int d = g.solution[cell];
    ASSERT_TRUE(g.notes[cell] & digitBit(d));
    const int sizeBefore = m->undo.size();
    putDigit(*m, cell, d);
    EXPECT_EQ(m->undo.size(), sizeBefore + 1);
    EXPECT_EQ(g.notes[cell], 0);
    int peersWithIt = 0;
    for (int i = 0; i < CELLS; i++) {
      if (i == cell || g.value[i]) continue;
      const bool had = before.notes[i] & digitBit(d);
      const bool has = g.notes[i] & digitBit(d);
      if (sees(i, cell)) {
        peersWithIt += had;
        EXPECT_EQ(has, had && !removeNotes) << i;
      } else {
        EXPECT_EQ(g.notes[i], before.notes[i]) << i;  // non-peers untouched
      }
    }
    EXPECT_GT(peersWithIt, 0);
    undo(*m);
    EXPECT_TRUE(Snapshot(g) == before);
  }
}

TEST(SudokuModel, FillAndClearAllNotes) {
  auto m = newModel(Medium);
  Game& g = m->game;
  Change c = fillAllNotes(*m);
  EXPECT_EQ(c.count, emptyCount(g));
  EXPECT_EQ(m->undo.groups(), 1);
  for (int i = 0; i < CELLS; i++) {
    if (!g.value[i]) {
      EXPECT_EQ(g.notes[i], candidatesOf(g, i));
      EXPECT_TRUE(g.notes[i] & digitBit(g.solution[i]));
    } else {
      EXPECT_EQ(g.notes[i], 0);
    }
  }
  EXPECT_EQ(fillAllNotes(*m).count, 0);  // nothing to change
  c = clearAllNotes(*m);
  EXPECT_EQ(c.count, emptyCount(g));
  EXPECT_EQ(m->undo.groups(), 2);
  undo(*m);
  for (int i = 0; i < CELLS; i++) {
    if (!g.value[i]) EXPECT_EQ(g.notes[i], candidatesOf(g, i));
  }
  undo(*m);
  for (int i = 0; i < CELLS; i++) EXPECT_EQ(g.notes[i], 0);
}

TEST(SudokuModel, ClashesAreLiveAndNeverCounted) {
  auto m = newModel();
  Game& g = m->game;
  EXPECT_EQ(clashCount(g), 0);
  uint8_t digit = 0, unit = 0;
  EXPECT_FALSE(firstClash(g, digit, unit));
  // An entry equal to a given in its row.
  const int given = firstGiven(g);
  int cell = -1;
  for (int c = 0; c < 9 && cell < 0; c++) {
    const int i = rc(given / 9, c);
    if (!g.value[i]) cell = i;
  }
  ASSERT_GE(cell, 0);
  putDigit(*m, cell, g.value[given]);
  EXPECT_TRUE(isClash(g, cell));
  EXPECT_FALSE(isClash(g, given));  // givens are never marked
  EXPECT_EQ(clashCount(g), 1);
  ASSERT_TRUE(firstClash(g, digit, unit));
  EXPECT_EQ(digit, g.value[given]);
  EXPECT_EQ(unit, given / 9);
  EXPECT_EQ(g.checks + g.hints + g.reveals, 0);
  eraseSquare(*m, cell);
  EXPECT_EQ(clashCount(g), 0);
}

TEST(SudokuModel, CheckMarksButNeverFixes) {
  auto m = newModel();
  Game& g = m->game;
  const int a = firstEmpty(g);
  const int b = firstEmpty(g, a + 1);
  const int wrong = wrongQuietDigit(g, a);
  ASSERT_GT(wrong, 0);
  putDigit(*m, a, wrong);
  putDigit(*m, b, g.solution[b]);
  // Square scope on the right one: nothing marked, still a check.
  Change c = check(*m, Scope::Square);
  EXPECT_EQ(c.msg, Msg::Checked);
  EXPECT_EQ(c.count, 0);
  EXPECT_EQ(g.checks, 1);
  c = check(*m, Scope::Puzzle);
  EXPECT_EQ(c.count, 1);
  EXPECT_EQ(g.checks, 2);
  EXPECT_TRUE(isWrongMarked(g, a));
  EXPECT_FALSE(isWrongMarked(g, b));
  EXPECT_EQ(g.value[a], wrong);  // never fixed
  EXPECT_TRUE(validGame(g));
  // Editing the square clears its mark; undo brings mark and digit back.
  putDigit(*m, a, g.solution[a]);
  EXPECT_FALSE(isWrongMarked(g, a));
  undo(*m);
  EXPECT_EQ(g.value[a], wrong);
  EXPECT_TRUE(isWrongMarked(g, a));
  // No square selected.
  setCursor(*m, NO_CELL);
  EXPECT_EQ(check(*m, Scope::Square).msg, Msg::NoSquare);
  EXPECT_EQ(g.checks, 2);
}

TEST(SudokuModel, RevealLocksAndSurvivesUndo) {
  auto m = newModel();
  Game& g = m->game;
  const int a = firstEmpty(g);
  const int wrong = wrongQuietDigit(g, a);
  putDigit(*m, a, wrong);
  Change c = reveal(*m, Scope::Square);
  EXPECT_EQ(c.count, 1);
  EXPECT_EQ(g.reveals, 1);
  EXPECT_EQ(g.value[a], g.solution[a]);
  EXPECT_TRUE(isRevealed(g, a));
  EXPECT_TRUE(validGame(g));
  // Locked: no digit, note or erase changes it, and undoing the earlier write keeps it.
  EXPECT_EQ(tapDigit(*m, wrong, 10000).msg, Msg::Locked);
  EXPECT_EQ(tapErase(*m, 11000).msg, Msg::Locked);
  undo(*m);
  EXPECT_EQ(g.value[a], g.solution[a]);
  EXPECT_TRUE(isRevealed(g, a));
  // Revealing a square that already shows its answer does nothing (and is not counted).
  const int b = firstEmpty(g);
  putDigit(*m, b, g.solution[b]);
  c = reveal(*m, Scope::Square);
  EXPECT_EQ(c.count, 0);
  EXPECT_EQ(g.reveals, 1);
  EXPECT_FALSE(isRevealed(g, b));
  // Reveal puzzle solves it, with reveals.
  c = reveal(*m, Scope::Puzzle);
  EXPECT_TRUE(c.solvedNow);
  EXPECT_TRUE(g.solved);
  EXPECT_EQ(g.reveals, 2);
  EXPECT_TRUE(validGame(g));
  EXPECT_FALSE(isRevealed(g, b));
}

TEST(SudokuModel, SolvingEndsPlay) {
  auto m = newModel();
  Game& g = m->game;
  holdDigit(*m, 1, 0);
  toggleNotesMode(*m, 1000);
  toggleNotesMode(*m, 2000);
  Change last;
  for (int i = 0; i < CELLS; i++) {
    if (!g.value[i]) last = putDigit(*m, i, g.solution[i]);
  }
  EXPECT_TRUE(last.solvedNow);
  EXPECT_TRUE(g.solved);
  EXPECT_EQ(g.lock, LOCK_NONE);
  EXPECT_FALSE(g.notesMode);
  EXPECT_TRUE(validGame(g));
  // Edits, undo and the cursor do nothing now (a solved grid draws no cursor).
  const uint8_t cursor = g.cursor;
  EXPECT_FALSE(undo(*m).changed);
  EXPECT_FALSE(tapDigit(*m, 3, 50000).changed);
  EXPECT_FALSE(hint(*m).changed);
  EXPECT_FALSE(fillAllNotes(*m).changed);
  EXPECT_FALSE(tapCell(*m, cursor == 40 ? 41 : 40, 60000).changed);
  EXPECT_EQ(g.cursor, cursor);
  // Restart starts it over.
  const Change r = restart(*m);
  EXPECT_TRUE(r.edited);
  EXPECT_FALSE(g.solved);
  EXPECT_EQ(std::memcmp(g.value, g.givens, CELLS), 0);
  EXPECT_EQ(g.cursor, NO_CELL);
  EXPECT_EQ(m->undo.size(), 0);
  EXPECT_TRUE(validGame(g));
}

// ---- hints -----------------------------------------------------------------------------------------

TEST(SudokuModel, HintFindsAWrongEntryFirst) {
  auto m = newModel(Medium);
  Game& g = m->game;
  const int a = firstEmpty(g);
  const int b = firstEmpty(g, a + 1);
  putDigit(*m, a, wrongQuietDigit(g, a));
  putDigit(*m, b, wrongQuietDigit(g, b));
  // The cursor's square first when it is wrong.
  Change c = hint(*m);
  EXPECT_EQ(c.msg, Msg::Hint);
  EXPECT_EQ(c.hint.kind, HintKind::Wrong);
  EXPECT_EQ(c.hint.cell, b);
  EXPECT_EQ(g.cursor, b);
  EXPECT_TRUE(isWrongMarked(g, b));
  EXPECT_EQ(g.hints, 1);
  // Hint again on the same square reveals it.
  c = hint(*m);
  EXPECT_EQ(c.hint.kind, HintKind::Revealed);
  EXPECT_EQ(c.hint.digit, g.solution[b]);
  EXPECT_EQ(g.value[b], g.solution[b]);
  EXPECT_TRUE(isRevealed(g, b));
  EXPECT_EQ(g.reveals, 1);
  EXPECT_EQ(g.hints, 1);
  // Then the other wrong one, wherever the cursor is.
  setCursor(*m, NO_CELL);
  c = hint(*m);
  EXPECT_EQ(c.hint.kind, HintKind::Wrong);
  EXPECT_EQ(c.hint.cell, a);
  EXPECT_EQ(g.hints, 2);
  // Moving away and hinting again recomputes instead of revealing.
  setCursor(*m, firstEmpty(g));
  c = hint(*m);
  EXPECT_EQ(c.hint.kind, HintKind::Wrong);
  EXPECT_EQ(c.hint.cell, a);
  EXPECT_EQ(g.hints, 3);
  // Any edit forgets the pending reveal.
  putDigit(*m, a, g.solution[a]);
  setCursor(*m, a);
  c = hint(*m);
  EXPECT_NE(c.hint.kind, HintKind::Revealed);
  EXPECT_NE(c.hint.kind, HintKind::Wrong);
}

// Undo puts back a right digit after a reveal filled the other last square: that completes the
// grid, so it solves the puzzle (else the save fails validGame and the puzzle is lost).
TEST(SudokuModel, UndoCanSolveAfterAReveal) {
  for (const bool byHint : {false, true}) {  // erase + Hint twice, or overwrite + Reveal square
    auto m = newModel();
    Game& g = m->game;
    const int x = firstEmpty(g);
    const int y = firstEmpty(g, x + 1);
    for (int i = y + 1; i < CELLS; i++) {
      if (!g.value[i]) putDigit(*m, i, g.solution[i]);
    }
    putDigit(*m, x, g.solution[x]);
    if (byHint) {
      eraseSquare(*m, x);
    } else {
      putDigit(*m, x, g.solution[x] % 9 + 1);
    }
    setCursor(*m, y);
    if (byHint) {
      // As if the last Hint pointed at y: Hint again there reveals it.
      m->hintCell = static_cast<uint8_t>(y);
      EXPECT_EQ(hint(*m).hint.kind, HintKind::Revealed);
    } else {
      reveal(*m, Scope::Square);
    }
    ASSERT_TRUE(isRevealed(g, y));
    ASSERT_FALSE(g.solved);
    const Change c = undo(*m);
    EXPECT_EQ(g.value[x], g.solution[x]) << byHint;
    EXPECT_TRUE(c.solvedNow) << byHint;
    EXPECT_TRUE(g.solved) << byHint;
    EXPECT_TRUE(validGame(g)) << byHint;
    EXPECT_TRUE(roundTrips(g)) << byHint;
  }
}

// A hint moves the cursor and clears a lock, so the hinted digit tapped next goes into the square.
TEST(SudokuModel, HintClearsALock) {
  auto m = newModel();
  Game& g = m->game;
  holdDigit(*m, 5, 0);
  ASSERT_EQ(g.lock, 5);
  const Change c = hint(*m);
  ASSERT_EQ(c.hint.kind, HintKind::Single);
  EXPECT_TRUE(c.modeChanged);
  EXPECT_EQ(g.lock, LOCK_NONE);
  EXPECT_EQ(g.cursor, c.hint.cell);
  tapDigit(*m, c.hint.digit, 5000);
  EXPECT_EQ(g.value[c.hint.cell], g.solution[c.hint.cell]);
  // No lock: the hint reports no mode change.
  EXPECT_FALSE(hint(*m).modeChanged);
}

TEST(SudokuModel, HintNamesASingleOrTheStepNeeded) {
  auto easy = newModel(Easy, 3);
  Change c = hint(*easy);
  EXPECT_EQ(c.hint.kind, HintKind::Single);
  EXPECT_TRUE(isSingle(c.hint.step.tech));
  EXPECT_EQ(c.hint.digit, easy->game.solution[c.hint.cell]);
  EXPECT_EQ(easy->game.value[c.hint.cell], 0);
  EXPECT_EQ(easy->game.cursor, c.hint.cell);
  // An Expert puzzle solved as far as the Hard ladder goes: the next placement needs a trial.
  auto expert = newModel(Expert, 1);
  Game& g = expert->game;
  Board b;
  ASSERT_TRUE(b.load(g.givens));
  Step s;
  while (nextStep(b, Hard, s)) {
  }
  for (int i = 0; i < CELLS; i++) {
    if (b.v[i] && !g.value[i]) putDigit(*expert, i, b.v[i]);
  }
  c = hint(*expert);
  EXPECT_EQ(c.hint.kind, HintKind::Trial);
  EXPECT_EQ(c.hint.step.tech, Trial);
  EXPECT_EQ(g.value[c.hint.cell], 0);
}

// Following the hints alone finishes every tier's puzzles, and no hint ever names or writes a
// digit other than the solution's, from the givens or from a random part-solved grid.
TEST(SudokuModel, HintNeverPlacesAWrongDigit) {
  std::mt19937 rng(7);
  for (int tier = 0; tier < TIER_COUNT; tier++) {
    int kinds[6] = {};
    for (uint32_t n = 1; n <= 20; n++) {
      auto m = newModel(tier, n);
      Game& g = m->game;
      if (n % 2 == 0) {  // half of them from a random part-solved grid
        for (int i = 0; i < CELLS; i++) {
          if (!g.value[i] && rng() % 3 == 0) putDigit(*m, i, g.solution[i]);
        }
      }
      int guard = 0;
      while (!g.solved && guard++ < 3 * CELLS) {
        const Change c = hint(*m);
        ASSERT_EQ(c.msg, Msg::Hint);
        kinds[static_cast<int>(c.hint.kind)]++;
        ASSERT_LT(c.hint.cell, CELLS);
        switch (c.hint.kind) {
          case HintKind::Single:
            ASSERT_EQ(c.hint.digit, g.solution[c.hint.cell]) << tierKey(tier) << " " << n;
            ASSERT_EQ(g.value[c.hint.cell], 0);
            putDigit(*m, c.hint.cell, c.hint.digit);  // the player follows it
            break;
          case HintKind::Step:
          case HintKind::Trial: {
            ASSERT_EQ(g.value[c.hint.cell], 0);
            const Change r = hint(*m);  // again: reveal
            ASSERT_EQ(r.hint.kind, HintKind::Revealed);
            ASSERT_EQ(g.value[c.hint.cell], g.solution[c.hint.cell]);
            break;
          }
          default:
            FAIL() << "unexpected hint " << static_cast<int>(c.hint.kind);
        }
        for (int i = 0; i < CELLS; i++) {
          if (g.value[i]) ASSERT_EQ(g.value[i], g.solution[i]);
        }
      }
      EXPECT_TRUE(g.solved) << tierKey(tier) << " " << n;
      EXPECT_TRUE(validGame(g));
    }
    EXPECT_GT(kinds[static_cast<int>(HintKind::Single)], 0);
    if (tier >= Medium)
      EXPECT_GT(kinds[static_cast<int>(HintKind::Step)] + kinds[static_cast<int>(HintKind::Trial)], 0);
    if (tier == Expert) EXPECT_GT(kinds[static_cast<int>(HintKind::Trial)], 0);
  }
}

// ---- undo -------------------------------------------------------------------------------------------

TEST(SudokuUndo, RecordPacksIntoEightBytes) {
  UndoRecord r;
  r.cell = 80;
  r.before = 9;
  r.after = 7;
  r.notesBefore = 0x1FF;
  r.wrongBefore = true;
  r.peersLost = 0xFFFFF;
  r.groupStart = true;
  const UndoRecord back = unpackUndo(packUndo(r));
  EXPECT_EQ(back.cell, 80);
  EXPECT_EQ(back.before, 9);
  EXPECT_EQ(back.after, 7);
  EXPECT_EQ(back.notesBefore, 0x1FF);
  EXPECT_TRUE(back.wrongBefore);
  EXPECT_EQ(back.peersLost, 0xFFFFFu);
  EXPECT_TRUE(back.groupStart);
  EXPECT_LT(packUndo(r), 1ull << 46);
  EXPECT_EQ(sizeof(UndoRing), UNDO_CAPACITY * 8 + 8);
}

// Random undoable actions (writes, overwrites, erases, notes, locks, fills), then undo all the
// way: each undo lands exactly on the state before its action, peers' notes included. (Check,
// reveal and hint are not undoable; CheckMarksButNeverFixes and RevealLocksAndSurvivesUndo cover
// how undo treats their marks.)
TEST(SudokuUndo, RandomSequencesRestoreExactStates) {
  std::mt19937 rng(20261004);
  int undone = 0;
  for (int round = 0; round < 40; round++) {
    auto m = newModel(round % TIER_COUNT, 1 + round / TIER_COUNT);
    m->settings.removeNotes = round % 3 != 0;
    Game& g = m->game;
    std::vector<Snapshot> stack;
    uint32_t now = 0;
    for (int step = 0; step < 120 && !g.solved; step++) {
      now += 400;  // past the toggle guard
      const Snapshot before(g);
      const int sizeBefore = m->undo.size();
      const int groupsBefore = m->undo.groups();
      const int cell = static_cast<int>(rng() % CELLS);
      const int digit = 1 + static_cast<int>(rng() % 9);
      switch (rng() % 12) {
        case 0:
        case 1:
        case 2:
          tapCell(*m, cell, now);
          break;
        case 3:
        case 4:
          tapDigit(*m, digit, now);
          break;
        case 5:
          holdDigit(*m, digit, now);
          break;
        case 6:
          tapErase(*m, now);
          break;
        case 7:
          toggleNotesMode(*m, now);
          break;
        case 8:
          putDigit(*m, cell, digit);
          break;
        case 9:
          toggleNote(*m, cell, digit);
          break;
        case 10:
          if (rng() % 4 == 0) {
            fillAllNotes(*m);
          } else {
            eraseSquare(*m, cell);
          }
          break;
        default:
          clearAllNotes(*m);
          break;
      }
      ASSERT_TRUE(roundTrips(g)) << "round " << round << " step " << step;
      if (m->undo.size() > sizeBefore) {
        ASSERT_EQ(m->undo.groups(), groupsBefore + 1);  // one action, one group
        stack.push_back(before);
      } else {
        ASSERT_EQ(m->undo.size(), sizeBefore);
      }
    }
    if (g.solved) continue;
    while (!stack.empty()) {
      const Change c = undo(*m);
      ASSERT_TRUE(c.edited);
      ASSERT_TRUE(Snapshot(g) == stack.back()) << "round " << round << " depth " << stack.size();
      ASSERT_TRUE(roundTrips(g));
      stack.pop_back();
      undone++;
    }
    EXPECT_EQ(undo(*m).msg, Msg::NothingToUndo);
  }
  EXPECT_GT(undone, 1000);
}

TEST(SudokuUndo, OverflowEvictsWholeGroups) {
  // Big groups (fill / clear all notes, ~50 records each) between single note toggles, until the
  // ring has overflowed several times: what is left is whole groups, each undoing exactly.
  auto m = newModel(Expert, 2);
  Game& g = m->game;
  std::vector<Snapshot> stack;
  for (int k = 0; k < 300; k++) {
    stack.push_back(Snapshot(g));
    Change c;
    if (k % 10 == 0) {
      c = fillAllNotes(*m);
    } else if (k % 10 == 5) {
      c = clearAllNotes(*m);
    } else {
      c = toggleNote(*m, firstEmpty(g, k % 40), 1 + k % 9);
    }
    if (!c.edited) stack.pop_back();  // nothing pushed
    ASSERT_LE(m->undo.size(), UNDO_CAPACITY);
  }
  const int groups = m->undo.groups();
  EXPECT_GT(groups, 0);
  EXPECT_LT(groups, static_cast<int>(stack.size()));  // older groups were evicted
  for (int k = 0; k < groups; k++) {
    ASSERT_TRUE(undo(*m).edited);
    ASSERT_TRUE(Snapshot(g) == stack[stack.size() - 1 - k]) << k;
  }
  EXPECT_EQ(m->undo.size(), 0);
  EXPECT_EQ(undo(*m).msg, Msg::NothingToUndo);
}

TEST(SudokuUndo, OverflowLandsOnTheOldestKeptState) {
  // Single-record groups only: after 600 writes the ring keeps the newest 512.
  auto m = newModel(Expert, 3);
  Game& g = m->game;
  const int a = firstEmpty(g);
  std::vector<Snapshot> states;
  for (int k = 0; k < 600; k++) {
    states.push_back(Snapshot(g));
    putDigit(*m, a, 1 + (k % 2 == 0 ? 3 : 5));
  }
  EXPECT_EQ(m->undo.size(), UNDO_CAPACITY);
  EXPECT_EQ(m->undo.groups(), UNDO_CAPACITY);
  for (int k = 0; k < UNDO_CAPACITY; k++) undo(*m);
  EXPECT_TRUE(Snapshot(g) == states[600 - UNDO_CAPACITY]);
  EXPECT_EQ(undo(*m).msg, Msg::NothingToUndo);
  // A group bigger than what is left evicts several old groups at once and stays whole.
  UndoRing ring;
  for (int k = 0; k < UNDO_CAPACITY; k++) {
    UndoRecord r;
    r.cell = static_cast<uint8_t>(k % CELLS);
    r.groupStart = true;
    ring.push(r);
  }
  for (int k = 0; k < 81; k++) {
    UndoRecord r;
    r.cell = static_cast<uint8_t>(k);
    r.groupStart = k == 0;
    ring.push(r);
  }
  EXPECT_EQ(ring.size(), UNDO_CAPACITY);
  EXPECT_EQ(ring.groups(), UNDO_CAPACITY - 81 + 1);
  UndoRecord r;
  int inGroup = 0;
  while (ring.pop(r)) {
    inGroup++;
    if (r.groupStart) break;
  }
  EXPECT_EQ(inGroup, 81);
}
