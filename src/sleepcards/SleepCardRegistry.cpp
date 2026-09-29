#include <GfxRenderer.h>

#include <cstring>

#include "CardDraw.h"
#include "SleepCard.h"

namespace sleepcards {

bool CardIo::hasBookmarks() const {
  CardSnippet one;
  return loadBookmarks(&one, 1) > 0;
}

namespace {

constexpr CardInfo CARDS[] = {
    {CardId::NowReading, "now_reading", &renderNowReadingCard},
    {CardId::Day, "day", &renderDayCard},
    {CardId::Calendar, "calendar", &renderCalendarCard},
    {CardId::Quote, "quote", &renderQuoteCard},
    {CardId::Owner, "owner", &renderOwnerCard},
    {CardId::Sky, "sky", &renderSkyCard},
    {CardId::Pictures, "pictures", nullptr},
    {CardId::Shuffle, "shuffle", nullptr},
};

}  // namespace

const CardInfo* cardInfo(const CardId id) {
  for (const auto& info : CARDS) {
    if (info.id == id) return &info;
  }
  return nullptr;
}

CardId cardForSleepMode(const uint8_t sleepScreenMode) {
  switch (sleepScreenMode) {
    case SLEEP_MODE_NOW_READING:
      return CardId::NowReading;
    case SLEEP_MODE_DAY:
      return CardId::Day;
    case SLEEP_MODE_CALENDAR:
      return CardId::Calendar;
    case SLEEP_MODE_QUOTE:
      return CardId::Quote;
    case SLEEP_MODE_OWNER:
      return CardId::Owner;
    case SLEEP_MODE_SKY:
      return CardId::Sky;
    case SLEEP_MODE_SHUFFLE:
      return CardId::Shuffle;
    default:
      return CardId::None;
  }
}

CardId cardByName(const char* name) {
  if (name == nullptr) return CardId::None;
  for (const auto& info : CARDS) {
    if (strcmp(info.name, name) == 0) return info.id;
  }
  return CardId::None;
}

const char* cardName(const CardId id) {
  const CardInfo* info = cardInfo(id);
  return info ? info->name : "none";
}

bool renderCard(const CardId id, const CardContext& ctx, GfxRenderer& renderer) {
  const CardInfo* info = cardInfo(id);
  if (info == nullptr || info->render == nullptr || ctx.io == nullptr) return false;
  draw::clearKeptTones();
  renderer.clearScreen();
  if (!info->render(ctx, renderer)) {
    renderer.clearScreen();
    return false;
  }
  draw::drawSleepFooter(ctx, renderer);
  if (ctx.dark) draw::invertKeepingTones(renderer);
  return true;
}

}  // namespace sleepcards
