#pragma once

// Crossword: the .ipuz reader (ipuz is a trademark of Puzzazz, Inc., used with permission).
//
// The file is streamed through ArduinoJson with a filter that keeps only what is read here
// (kind, dimensions, puzzle, solution, clues, title, author, copyright, block, empty), so there
// is no whole-file buffer. Accepted:
//   kind      a string starting "http://ipuz.org/crossword" (a ".../diagramless" kind: refused)
//   puzzle    numbers, "#" (or the file's "block"), null = omitted -> block, 0 (or "empty") =
//             white, label strings = white, objects {cell, style}; style.shapebg "circle" ->
//             circled; any style with "barred" -> refused
//   solution  REQUIRED; one letter a square (case-folded; {"value": ...} objects too); anything
//             longer or not A-Z -> rebus, refused
//   clues     "Across" / "Down" (or "Across:<label>"), each clue a string (in order), a
//             [number, text] pair or {number, clue}
//   numbers   when the file numbers any square, every number must match CwNumbering
// Titles and clues may hold HTML: tags are stripped and entities decoded.

#include "CwModel.h"
#include "CwSource.h"

namespace cw {

// Parses an .ipuz file into p (complete: numbered, clued, fnv set; sourceKey copied in).
// alloc = the JSON document's heap (nullptr = malloc).
LoadStatus parseIpuz(ByteReader& in, const char* sourceKey, Puzzle& p, const Allocator* alloc = nullptr);

}  // namespace cw
