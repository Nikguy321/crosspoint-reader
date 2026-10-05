#pragma once

// Sudoku on the SD card: the game's own files under sd::SAVE_DIR (the puzzle in progress, the
// prefs, the solved list). Written tmp -> remove -> rename; a load whose main file is missing
// takes the .tmp a power cut left between the last two steps. Device-only: the text formats are
// the pure codecs in lib/Sudoku (SdSave).

#include <Sudoku.h>

namespace sd::store {

// False (prefs = defaults) when there is no valid prefs file.
bool loadPrefs(Prefs& prefs);
bool savePrefs(const Prefs& prefs);

// False when there is no valid saved puzzle (none, or it does not parse and validate): the
// caller starts a fresh one (out is then unusable).
bool loadGame(Game& out);
bool saveGame(const Game& game);

// Adds a solved puzzle (no-op when its fnv is listed); past SOLVED_CAP lines the list is
// rewritten with its newest lines.
bool appendSolved(const Game& game);

}  // namespace sd::store
