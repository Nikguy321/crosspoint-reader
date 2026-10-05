#pragma once

// Drawing helpers the Apps games share (Word Search, Crossword, Sudoku): capital letters centred on their
// own ink, and the rounded text button. Free functions over GfxRenderer and the built-in fonts,
// so the host preview tests draw the same pixels as the device.

#include <EpdFontFamily.h>

class GfxRenderer;

namespace appdraw {

// A built-in font's glyph, or nullptr.
const EpdGlyph* glyphFor(const GfxRenderer& r, int fontId, unsigned char c, EpdFontFamily::Style style);

// Height of the capitals above the baseline ('H'), for centring upper-case text.
int capHeight(const GfxRenderer& r, int fontId, EpdFontFamily::Style style);

// The top y for drawText so capitals sit centred on cy.
int capTopFor(const GfxRenderer& r, int fontId, EpdFontFamily::Style style, int cy);

// One bold letter, centred horizontally on its own ink box at cx, its baseline at `baseline`.
// halo: a white (or, on black, a black) outline a pixel wide first, so a line crossing the
// cell stops short of the letter instead of running through it.
void drawLetterOnBaseline(GfxRenderer& r, int fontId, int ascender, char letter, int cx, int baseline, bool black,
                          bool halo);
// The same in another style (Sudoku: givens bold, the player's digits regular).
void drawLetterOnBaseline(GfxRenderer& r, int fontId, int ascender, char letter, int cx, int baseline, bool black,
                          bool halo, EpdFontFamily::Style style);

// The same with the capitals centred vertically on cy (capH = capHeight of the font).
void drawLetter(GfxRenderer& r, int fontId, int capH, int ascender, char letter, int cx, int cy, bool black, bool halo);

// A rounded 2 px outline with a bold label centred in it (UI_12 unless fontId says otherwise).
void drawButton(GfxRenderer& r, int x, int y, int w, int h, const char* label, int fontId = -1);

}  // namespace appdraw
