#pragma once

#include <cstddef>
#include <cstdint>

#include "SleepCard.h"

// SHUFFLE: a different ticked card each sleep. Entry point: sleepcards::pickShuffleCard()
// (declared in SleepCard.h); it has no screen of its own - it names the card to draw.
//
// The choice works like dealing from a deck: every ticked card that can show something right now
// is dealt once, in random order, before any card comes round again, and a new round never starts
// with the card that ended the last one. So with two or more usable cards the same card never
// shows twice in a row, and with five ticked each one shows once every five sleeps.
//
// "Usable right now" is a cheap check made before choosing (see shuffleUsableMask): Owner needs a
// name or a contact line, Now Reading an open book that is still on the card, Day / Calendar / Sky
// a set clock; Quote always (its built-in set is the fallback). Pictures is taken as usable when
// ticked (SleepActivity's picture frame falls back to the logo screen by itself). A card that
// still declines when drawn can be skipped with pickShuffleCardExcluding().
//
// Memory: the last card shown and the cards dealt this round, one short line in STATE_PATH:
//     S1 <last card id> <dealt mask, hex>
// A missing or unreadable file just starts a fresh round.
namespace sleepcards::shuffle {

constexpr const char* STATE_PATH = "/.crosspoint/sleepcards/shuffle.dat";
constexpr size_t STATE_TEXT_CAP = 24;  // "S1 7 00fe\n" fits with room to spare

struct ShuffleState {
  CardId last = CardId::None;  // the card the previous sleep showed
  uint16_t dealt = 0;          // cardBit() of every card shown in the current round
};

// The cards Shuffle may ever pick: Now Reading .. Pictures (never None or Shuffle itself).
constexpr uint16_t pickableMask() {
  uint16_t mask = 0;
  for (uint8_t i = static_cast<uint8_t>(CardId::NowReading); i <= static_cast<uint8_t>(CardId::Pictures); i++) {
    mask |= cardBit(static_cast<CardId>(i));
  }
  return mask;
}

// The pure choice. usable = cardBit() of every card that may be shown now. Returns the card to
// show and advances state; None (state untouched) when nothing is usable. seed is any 32-bit
// randomness (esp_random on the device).
CardId chooseCard(uint16_t usable, ShuffleState& state, uint32_t seed);

// The state line <-> ShuffleState. parseState() accepts only a well-formed "S1" line (a stray
// byte, an unknown id, a mask naming non-pickable cards: false, state reset to the default).
bool parseState(const char* text, size_t len, ShuffleState& out);
// Writes the line with its '\n'; returns its length, or 0 when cap is too small.
size_t formatState(const ShuffleState& state, char* out, size_t cap);

// Of the cards in mask, the ones that can show something right now (one or two small file
// checks through ctx.io; no book is opened and no file is read in full).
uint16_t shuffleUsableMask(const CardContext& ctx, uint16_t mask);

// Is any byte of s (up to cap) other than whitespace? (The Owner card's "has something" test.)
bool hasInk(const char* s, size_t cap);

}  // namespace sleepcards::shuffle

namespace sleepcards {

// pickShuffleCard() with the cards in `excluded` treated as unusable: for a caller that drew the
// pick, saw it decline, and wants the next one. The saved state records the card returned last.
CardId pickShuffleCardExcluding(const CardContext& ctx, uint16_t excluded);

}  // namespace sleepcards
