#pragma once

// Sudoku, the pure half (no Arduino): the engine (counter, filler, logic ladder, generator, hint
// search), the play model and undo ring, the save codecs, the screen layout and touch targets,
// the touch tracker and the notes digits. The activities (src/activities/apps) and the host tests
// include this.

#include "SdContact.h"
#include "SdEngine.h"
#include "SdLayout.h"
#include "SdModel.h"
#include "SdNotesFont.h"
#include "SdSave.h"
