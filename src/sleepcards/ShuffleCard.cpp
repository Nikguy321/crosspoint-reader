#include "ShuffleCard.h"

#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

namespace sleepcards {
namespace shuffle {
namespace {

int bitCount(const uint16_t v) { return __builtin_popcount(v); }

// The k-th set bit of mask (k < bitCount(mask)) as a card id.
CardId nthCard(uint16_t mask, uint32_t k) {
  for (uint8_t i = 0; i < 16; i++) {
    if ((mask & (1u << i)) == 0) continue;
    if (k == 0) return static_cast<CardId>(i);
    k--;
  }
  return CardId::None;
}

// A 32-bit finaliser (murmur3 fmix32): a fixed or counting seed still spreads over every card.
uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

bool isPickable(const CardId id) { return (pickableMask() & cardBit(id)) != 0; }

bool timeSet(const CardContext& ctx) { return ctx.timeValid && plausibleTime(ctx.utcNow); }

// The Quote card always has its built-in set to fall back on.
bool quoteUsable(const CardContext&) { return true; }

ShuffleState readState(const CardIo& io) {
  ShuffleState state;
  char buf[STATE_TEXT_CAP] = {};
  const int32_t got = io.readFileAt(STATE_PATH, 0, buf, sizeof(buf) - 1);
  if (got > 0) parseState(buf, static_cast<size_t>(got), state);
  return state;
}

}  // namespace

CardId chooseCard(uint16_t usable, ShuffleState& state, const uint32_t seed) {
  usable &= pickableMask();
  if (usable == 0) return CardId::None;
  const uint16_t lastBit = isPickable(state.last) ? cardBit(state.last) : 0;
  uint16_t pool = usable & static_cast<uint16_t>(~state.dealt) & static_cast<uint16_t>(~lastBit);
  if (pool == 0) {
    // Every usable card has been dealt this round: a new round, not opening with the last card.
    state.dealt = 0;
    pool = usable & static_cast<uint16_t>(~lastBit);
  }
  if (pool == 0) pool = usable;  // only one usable card, and it was the last one
  const CardId pick = nthCard(pool, mix(seed) % static_cast<uint32_t>(bitCount(pool)));
  state.last = pick;
  state.dealt = static_cast<uint16_t>((state.dealt | cardBit(pick)) & usable);
  return pick;
}

bool parseState(const char* text, const size_t len, ShuffleState& out) {
  out = ShuffleState{};
  if (text == nullptr || len == 0 || len >= STATE_TEXT_CAP) return false;
  char buf[STATE_TEXT_CAP];
  std::memcpy(buf, text, len);
  buf[len] = '\0';
  if (std::strlen(buf) != len) return false;  // an embedded NUL
  unsigned last = 0;
  unsigned dealt = 0;
  int end = 0;
  if (std::sscanf(buf, "S1 %u %x%n", &last, &dealt, &end) != 2) return false;
  // Nothing but line endings after the mask.
  for (size_t i = static_cast<size_t>(end); i < len; i++) {
    if (buf[i] != '\n' && buf[i] != '\r') return false;
  }
  if (last > 0xFF || dealt > 0xFFFF) return false;
  const auto id = static_cast<CardId>(last);
  if (id != CardId::None && !isPickable(id)) return false;
  if ((dealt & ~static_cast<unsigned>(pickableMask())) != 0) return false;
  out.last = id;
  out.dealt = static_cast<uint16_t>(dealt);
  return true;
}

size_t formatState(const ShuffleState& state, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  const int n = std::snprintf(out, cap, "S1 %u %04x\n", static_cast<unsigned>(state.last),
                              static_cast<unsigned>(state.dealt & pickableMask()));
  if (n <= 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

bool hasInk(const char* s, const size_t cap) {
  if (s == nullptr) return false;
  for (size_t i = 0; i < cap && s[i] != '\0'; i++) {
    const char c = s[i];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return true;
  }
  return false;
}

uint16_t shuffleUsableMask(const CardContext& ctx, const uint16_t mask) {
  uint16_t usable = 0;
  const auto keep = [&](const CardId id, const bool ok) {
    if (shuffleIncludes(mask, id) && ok) usable |= cardBit(id);
  };
  const SleepCardSettings& s = ctx.settings;
  // Cheapest checks first; the file checks run only for ticked cards.
  keep(CardId::Day, timeSet(ctx));
  keep(CardId::Calendar, timeSet(ctx));
  keep(CardId::Sky, timeSet(ctx));
  keep(CardId::Owner, hasInk(s.ownerName, sizeof(s.ownerName)) || hasInk(s.ownerContact1, sizeof(s.ownerContact1)) ||
                          hasInk(s.ownerContact2, sizeof(s.ownerContact2)));
  keep(CardId::Pictures, true);
  if (ctx.io == nullptr) return usable;
  if (shuffleIncludes(mask, CardId::NowReading)) {
    keep(CardId::NowReading, ctx.bookPath != nullptr && ctx.bookPath[0] != '\0' && ctx.io->fileSize(ctx.bookPath) >= 0);
  }
  if (shuffleIncludes(mask, CardId::Quote)) keep(CardId::Quote, quoteUsable(ctx));
  return usable;
}

}  // namespace shuffle

CardId pickShuffleCardExcluding(const CardContext& ctx, const uint16_t excluded) {
  const uint16_t ticked = ctx.settings.shuffleMask & shuffle::pickableMask() & static_cast<uint16_t>(~excluded);
  if (ticked == 0) return CardId::None;
  const uint16_t usable = shuffle::shuffleUsableMask(ctx, ticked);
  if (usable == 0) return CardId::None;
  if (ctx.io == nullptr) {
    // No storage to remember with: still a fair choice, just without the round.
    shuffle::ShuffleState fresh;
    return shuffle::chooseCard(usable, fresh, ctx.seed);
  }
  shuffle::ShuffleState state = shuffle::readState(*ctx.io);
  const shuffle::ShuffleState before = state;
  const CardId pick = shuffle::chooseCard(usable, state, ctx.seed);
  if (state.last != before.last || state.dealt != before.dealt) {
    char line[shuffle::STATE_TEXT_CAP];
    const size_t n = shuffle::formatState(state, line, sizeof(line));
    if (n == 0 || !ctx.io->writeFile(shuffle::STATE_PATH, line, n)) LOG_ERR("CARD", "shuffle: state not saved");
  }
  LOG_DBG("CARD", "shuffle: usable %04x -> %s", static_cast<unsigned>(usable), cardName(pick));
  return pick;
}

CardId pickShuffleCard(const CardContext& ctx) { return pickShuffleCardExcluding(ctx, 0); }

bool renderCardOrShuffle(CardId requested, const CardContext& ctx, GfxRenderer& renderer, CardId& shown,
                         const uint16_t excluded) {
  const bool shuffling = requested == CardId::Shuffle;
  if (shuffling) requested = pickShuffleCardExcluding(ctx, excluded);
  bool drawn = false;
  uint16_t declined = excluded;
  for (uint8_t attempt = 0; attempt < static_cast<uint8_t>(CardId::Count); attempt++) {
    if (requested == CardId::None || requested == CardId::Pictures) break;
    drawn = renderCard(requested, ctx, renderer);
    if (drawn || !shuffling) break;
    LOG_INF("CARD", "shuffle: %s declined, trying the next", cardName(requested));
    declined |= cardBit(requested);
    requested = pickShuffleCardExcluding(ctx, declined);
  }
  shown = requested;
  return drawn;
}

}  // namespace sleepcards
