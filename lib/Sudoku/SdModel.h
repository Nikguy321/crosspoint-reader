#pragma once

// Sudoku play state and rules, pure: the game (saved), the undo ring (not saved), and every
// action as a function that reports what changed in a Change. The activity maps touches, keys and
// menu rows onto these, then redraws, saves and refreshes from the Change.
//
// Entry is square first, with a digit lock:
//   nothing selected   square tap = select it          digit / Erase tap = lock it
//   square selected    square tap = move the cursor    digit tap = write it (Notes on: toggle the
//                                                       note); Erase tap = clear the square
//   key locked         square tap = place the locked digit (Notes on: toggle its note) or erase
//                      (Erase locked); the locked key tapped again unlocks; another key switches
// Holding a digit or Erase (600 ms, SdContact) locks it at any time; locking hides the cursor.
// A locked digit overwrites a non-given entry as one undo step; writing the digit a square
// already shows changes nothing; givens and revealed squares never change. The toggles (a note,
// the Notes key, locking and unlocking) ignore a second tap on the same target within
// TOGGLE_GUARD_MS (the e-ink double tap). Placing a digit clears the square's notes and, with
// Settings::removeNotes, that digit's notes in its 20 peers (one undo step). Once solved, nothing
// changes but restart (a solved grid shows no cursor).
//
// Undo: a ring of UNDO_CAPACITY 8-byte records grouped per action; an overflow evicts the oldest
// WHOLE group; no redo. Undo restores digits, notes and WRONG flags (never a revealed square: a
// reveal, check or hint is not undoable). Clashes are computed live and never counted.

#include <cstddef>
#include <cstdint>

#include "SdEngine.h"

namespace sd {

constexpr uint8_t NO_CELL = 0xFF;
constexpr uint8_t LOCK_NONE = 0;
constexpr uint8_t LOCK_ERASE = 10;    // Game::lock: 1..9 = that digit, LOCK_ERASE = the Erase key
constexpr uint8_t FLAG_WRONG = 1;     // marked by a check or a hint; cleared when the square changes
constexpr uint8_t FLAG_REVEALED = 2;  // written by a reveal (or a second hint); locked
constexpr uint32_t TOGGLE_GUARD_MS = 350;
constexpr uint32_t ELAPSED_MAX = 99u * 3600u;
constexpr uint16_t COUNT_MAX = 0xFFFF;

// The saved game (~0.5 KB; a Model holds it).
struct Game {
  uint8_t tier = Easy;  // the REAL tier
  uint32_t number = 0;  // "Medium 14"
  uint32_t seed = 0;    // what generated it (the bench shows it; the save keeps the grids anyway)
  uint32_t fnv = 0;     // givensFnv(givens)
  uint8_t givens[CELLS] = {};
  uint8_t solution[CELLS] = {};
  uint8_t value[CELLS] = {};   // what the square shows: its given, the player's digit, or 0
  uint16_t notes[CELLS] = {};  // pencil marks (bit d - 1); only on empty squares
  uint8_t flags[CELLS] = {};
  uint8_t cursor = NO_CELL;
  bool notesMode = false;
  uint8_t lock = LOCK_NONE;
  uint32_t elapsed = 0;  // seconds
  uint16_t checks = 0;
  uint16_t hints = 0;
  uint16_t reveals = 0;
  bool solved = false;

  // Back to all zero in place (no ~0.5 KB temporary on the stack).
  void clear();
};

// A fresh game from a generated puzzle: givens shown, nothing else, cursor none, counts 0.
void startGame(Game& game, const Generated& gen, uint32_t number, uint32_t seed);

// Self-consistency (what a save must satisfy): the solution is a complete valid grid, the givens
// are part of it and unique, fnv matches, every given shows, digits 1-9, notes only on empty
// squares, REVEALED only on a correct non-given, WRONG only on a wrong non-given entry, cursor and
// lock in range, solved exactly when every square matches the solution.
bool validGame(const Game& game);

// ---- undo -----------------------------------------------------------------------------------------

constexpr int UNDO_CAPACITY = 512;

struct UndoRecord {
  uint8_t cell = 0;
  uint8_t before = 0;  // the square's digit before (0 = empty)
  uint8_t after = 0;   // and after (the digit removed from peers' notes)
  uint16_t notesBefore = 0;
  bool wrongBefore = false;
  uint32_t peersLost = 0;   // bit k: peerOf(cell, k) lost the note `after`
  bool groupStart = false;  // the first record of an action
};
uint64_t packUndo(const UndoRecord& r);
UndoRecord unpackUndo(uint64_t bits);

class UndoRing {
 public:
  void clear() {
    head = 0;
    count = 0;
  }
  int size() const { return count; }
  int groups() const;
  // Adds a record; when full, first evicts the oldest whole group.
  void push(const UndoRecord& r);
  // The newest record; false when empty.
  bool pop(UndoRecord& out);

 private:
  uint64_t slots[UNDO_CAPACITY] = {};
  uint16_t head = 0;  // the next slot to write
  uint16_t count = 0;
};

// ---- the model ------------------------------------------------------------------------------------

struct Settings {
  bool removeNotes = true;  // placing a digit removes it from its peers' notes
};

// Everything the game screen plays on (~4.6 KB: allocate it once, makeUniqueNoThrow).
struct Model {
  Game game;
  UndoRing undo;
  Settings settings;
  uint8_t hintCell = NO_CELL;  // the square the last hint pointed at: Hint there again reveals it
  int16_t guardTarget = -1;    // the last toggle's target (see guardTargetFor*)
  uint32_t guardMs = 0;

  // After a load or a new game: no undo history, no pending hint, no guard.
  void resetSession();
};

// ---- queries ----------------------------------------------------------------------------------------

inline bool isGiven(const Game& g, const int cell) { return g.givens[cell] != 0; }
inline bool isRevealed(const Game& g, const int cell) { return (g.flags[cell] & FLAG_REVEALED) != 0; }
// A given or a revealed square: never changes.
inline bool isLocked(const Game& g, const int cell) { return isGiven(g, cell) || isRevealed(g, cell); }
inline bool isWrongMarked(const Game& g, const int cell) { return (g.flags[cell] & FLAG_WRONG) != 0; }
// How many of a digit are still to place (9 minus those shown, never below 0).
int remaining(const Game& g, int digit);
int emptyCount(const Game& g);
// The digits no peer shows (what "Fill all notes" writes).
uint16_t candidatesOf(const Game& g, int cell);
// An entry (not a given) whose digit a peer also shows: the clash bar.
bool isClash(const Game& g, int cell);
// Entries with a clash bar.
int clashCount(const Game& g);
// The first unit (rows, then columns, then boxes) showing a digit twice: "Two 7s in row 1".
bool firstClash(const Game& g, uint8_t& digit, uint8_t& unit);
// Every square shows its solution.
bool isComplete(const Game& g);

// ---- what an action did -----------------------------------------------------------------------------

enum class Scope : uint8_t { Square = 0, Puzzle = 1 };

enum class Msg : uint8_t {
  None = 0,
  Given,          // "That one is given"
  Locked,         // a revealed square: "That one is revealed"
  EraseFirst,     // a note on a filled square: "Erase the 5 first" (Change::digit)
  NothingToUndo,  // "Nothing to undo"
  NoSquare,       // a square action with no square selected: "Tap a square first"
  Hint,           // Change::hint
  Checked,        // Change::count = wrong entries in scope (0: "No mistakes")
  Revealed,       // Change::count = squares revealed
};

enum class HintKind : uint8_t {
  None = 0,
  Wrong,     // "This square is wrong" (flagged, counted as a hint)
  Single,    // "Only 7 fits here (box 2)": digit, step = the single (step.unit, NO_UNIT for a naked single)
  Step,      // "Needs an X-wing on 7s first": step = the hardest step needed before the placement
  Trial,     // "No step short of a trial"
  Revealed,  // a second Hint on the same square: digit written, locked, counted as a reveal
};

struct Hint {
  HintKind kind = HintKind::None;
  uint8_t cell = NO_CELL;  // the cursor moved here
  uint8_t digit = 0;
  Step step;
};

struct Change {
  bool changed = false;      // something on screen changed (a message too): redraw
  bool edited = false;       // digits, notes, flags or counts changed: save at the next pause
  bool modeChanged = false;  // Notes toggled or a key locked / unlocked: a pause point
  bool solvedNow = false;    // this action completed the grid (Game::solved is set)
  Msg msg = Msg::None;
  uint8_t digit = 0;   // Msg::EraseFirst
  uint16_t count = 0;  // Msg::Checked / Msg::Revealed; fillAllNotes / clearAllNotes: squares changed
  Hint hint;           // Msg::Hint
};

// ---- input (the activity maps contact taps / holds and the edge keys here) --------------------------

Change tapCell(Model& m, int cell, uint32_t nowMs);
Change tapDigit(Model& m, int digit, uint32_t nowMs);   // 1..9
Change holdDigit(Model& m, int digit, uint32_t nowMs);  // locks it
Change tapErase(Model& m, uint32_t nowMs);
Change holdErase(Model& m, uint32_t nowMs);  // locks Erase
Change toggleNotesMode(Model& m, uint32_t nowMs);
Change undo(Model& m);

// ---- direct edits (the bench: SU put / note / erase; no guard, the cursor moves there) --------------

Change setCursor(Model& m, int cell);  // NO_CELL hides it
Change putDigit(Model& m, int cell, int digit);
Change toggleNote(Model& m, int cell, int digit);
Change eraseSquare(Model& m, int cell);

// ---- menu --------------------------------------------------------------------------------------------

// Every empty square's notes become its candidates (one undo step; count = squares changed).
Change fillAllNotes(Model& m);
Change clearAllNotes(Model& m);
// Marks WRONG on the wrong entries in scope (never fixes; empty squares are not marked); counts a
// check. Square scope uses the cursor (Msg::NoSquare without one).
Change check(Model& m, Scope scope);
// Writes the solution into each non-given square in scope that does not show it: REVEALED,
// locked, notes cleared (and the digit from peers' notes, with removeNotes); counts a reveal when
// any square changed. Revealing the last squares solves the puzzle (with reveals).
Change reveal(Model& m, Scope scope);
// (1) a wrong entry (the cursor's first, else the first in reading order): flag it, cursor there,
// count a hint; (2) else the logic from the givens and the player's digits (notes ignored) to the
// first placement: cursor there, Single / Step / Trial, count a hint; (3) Hint again while the
// cursor is still on that square, with nothing edited since: reveal it. Moving the cursor clears a
// lock (modeChanged), so the next digit tap goes into the hinted square. A hint never writes a
// digit other than the solution's.
Change hint(Model& m);
// Starts the puzzle over: every entry, note and flag gone, counts and time 0, not solved, no
// cursor, lock or Notes mode, no undo history.
Change restart(Model& m);

}  // namespace sd
