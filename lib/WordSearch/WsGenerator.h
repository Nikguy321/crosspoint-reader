#pragma once

// Word Search puzzle generation and matching. Pure and deterministic: one seed gives one
// puzzle, on the host and on the reader.
//
// generatePuzzle():
//   - words are picked at random from the theme (grid letters within the difficulty's range,
//     no duplicates, none that is a substring of another chosen word or of its reverse);
//   - the longest is placed first, at a random start, each word preferring the next direction
//     dealt from a shuffled deck of the difficulty's directions (an even mix), crossing other words only on equal
//     letters and never running along another word's line; PLACE_TRIES_PER_WORD tries a word, then it is skipped and a
//     further word from the theme is tried instead;
//   - the rest is filled with letters weighted by English frequency, and filler cells are
//     re-rolled until no chosen word appears anywhere else in any of the 8 directions;
//   - a try that places fewer than ~75 % of the target (or cannot clear an extra occurrence)
//     is retried with the next seed, GENERATE_SEED_TRIES times at most.
// Matching accepts a line in any of the 8 directions, forwards or backwards, on every
// difficulty: a player who spots a word gets it.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "WsModel.h"

namespace ws {

constexpr int GENERATE_SEED_TRIES = 8;
constexpr int PLACE_TRIES_PER_WORD = 300;
constexpr int FILLER_FIX_ROUNDS = 64;

// The fewest words a puzzle may have: ~75 % of the target, rounded up.
constexpr int minimumWords(const Difficulty d) { return (DIFFICULTY_SPECS[static_cast<uint8_t>(d)].words * 3 + 3) / 4; }

// Builds a fresh puzzle from the theme's display texts. Sets everything except themeKey and
// themeTitle (the caller's); words are listed in display order (A-Z). out.seed = seed. False
// (out unusable) when no try reached minimumWords().
bool generatePuzzle(const std::string_view* words, size_t count, Difficulty difficulty, uint32_t seed, Puzzle& out);

// Distinct runs of cells that spell `letters` in any of the 8 directions. A run read
// backwards is the same run, so a palindrome placed once counts 1.
int countOccurrences(const Puzzle& p, const char* letters);

enum class MatchKind : uint8_t { NoMatch, Found, AlreadyFound };
struct MatchResult {
  MatchKind kind = MatchKind::NoMatch;
  int8_t word = -1;  // the word index for Found / AlreadyFound
};
// Does the line (aligned, 2+ cells) spell a word forwards or backwards?
MatchResult matchLine(const Puzzle& p, Line line);

// Marks a word found along the player's line; clears the hint when it was the hinted word.
void markFound(Puzzle& p, int word, Line line);

// A random unfound word to hint at, or -1 when all are found.
int pickHintWord(const Puzzle& p, uint32_t& rng);

}  // namespace ws
