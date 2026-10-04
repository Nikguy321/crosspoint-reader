#pragma once

// Crossword play rules, pure: cursor and direction, typing and Del, clue to clue, check, reveal,
// clear and completion. Every action takes the puzzle and the player's Progress and reports what
// changed; the activity redraws, saves and refreshes from that.
//
// The current entry is the cursor square's entry in the current direction (every white square
// is in at least one entry, so a square with only the other one switches direction on arrival).
// Clue order ("<" / ">", end of word): Across 1..n then Down 1..n, wrapping.
//
// Once a puzzle is solved, edits (letters, Del, clear word, check, reveal) do nothing; moving
// the cursor still works, and Clear puzzle starts it over.

#include <cstddef>
#include <cstdint>

#include "CwModel.h"

namespace cw {

enum class Scope : uint8_t { Letter = 0, Word = 1, Puzzle = 2 };

struct Change {
  bool changed = false;      // fill, flags, cursor or direction changed: redraw
  bool edited = false;       // a square's letter or flags changed (ends the bar's "not quite")
  bool wordChanged = false;  // the cursor moved to another entry (a pause point: save, HALF ok)
  bool solvedNow = false;    // this action completed the grid correctly (prog.solved is set)
  bool fullWrong = false;    // after this edit every square is filled but some are wrong
  uint16_t wrong = 0;        // the wrong squares, when fullWrong
  uint16_t count = 0;        // check: squares marked WRONG; reveal: squares revealed
};

struct Tally {
  uint16_t white = 0;
  uint16_t filled = 0;
  uint16_t wrong = 0;   // filled squares that differ from the answer
  uint16_t marked = 0;  // squares carrying FLAG_WRONG
  bool full() const { return white > 0 && filled == white; }
  bool correct() const { return full() && wrong == 0; }
};
Tally tally(const Puzzle& p, const Progress& prog);

// The current entry (always valid on a matching Progress).
int currentEntry(const Puzzle& p, const Progress& prog);
// The letter index of cell inside entry, or -1.
int entryPosition(const Puzzle& p, int entry, int cell);
// The entry's first empty square, or -1 when it is full.
int firstBlank(const Puzzle& p, const Progress& prog, int entry);
bool entryFull(const Puzzle& p, const Progress& prog, int entry);
// The fill pattern for the clue list: letters and '_' for blanks ("CA__S"). Returns the length.
size_t entryPattern(const Puzzle& p, const Progress& prog, int entry, char* out, size_t cap);
// The cursor square is in both an Across and a Down entry.
bool canToggle(const Puzzle& p, const Progress& prog);

// A grid tap: a block does nothing; another white square becomes the cursor (keeping the
// direction when that square has an entry in it, else switching); the cursor square toggles.
Change tapCell(const Puzzle& p, Progress& prog, int cell);
Change toggleDirection(const Puzzle& p, Progress& prog);
// Puts the cursor on an entry: its first blank, or its first square when it is full.
Change gotoEntry(const Puzzle& p, Progress& prog, int entry);
// The previous (-1) or next (+1) clue in clue order, wrapping.
Change stepClue(const Puzzle& p, Progress& prog, int delta);
// Places the cursor (bench): a white square, and dir when that square has an entry that way.
Change setCursor(const Puzzle& p, Progress& prog, int cell, uint8_t dir);

// A letter key ('A'..'Z'; lower case folded). Writes the cursor square (unless REVEALED: the
// letter is dropped), clears its WRONG, then advances: to the next empty square of the word
// (skipFilled) or the next square; past the end of the word, to the word's first blank, else
// the first blank of the next clue that has one; a full grid stays put.
Change typeLetter(const Puzzle& p, Progress& prog, char letter, bool skipFilled);
// Del: clears the cursor square when it holds an unlocked letter (and stays); otherwise steps
// back one square in the word (not past its start) and clears that one when unlocked.
Change deleteLetter(const Puzzle& p, Progress& prog);
// Clears the current word's unlocked squares (Del held, or the menu).
Change clearWord(const Puzzle& p, Progress& prog);
// Starts the puzzle over: every square empty, no flags, counters and time 0, not solved. The
// cursor stays.
Change clearPuzzle(const Puzzle& p, Progress& prog);
// Marks WRONG on the wrong filled unlocked squares in scope (never fixes them; empty squares are
// not marked); counts a check. Change::count = squares marked.
Change check(const Puzzle& p, Progress& prog, Scope scope);
// Writes the answer into each square in scope that does not already hold it, sets REVEALED,
// clears WRONG; counts a reveal when any square changed. Revealing the last squares solves the
// puzzle (with reveals). Change::count = squares revealed.
Change reveal(const Puzzle& p, Progress& prog, Scope scope);

}  // namespace cw
