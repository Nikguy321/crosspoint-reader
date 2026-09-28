#pragma once

#include "NowReadingPace.h"
#include "SleepCard.h"

// NOW READING: the cover, title, author, a progress bar with %, chapter + page, and the estimated
// reading time left in the chapter and the book from the owner's own measured page speed
// (NowReadingPace.h). Entry point: sleepcards::renderNowReadingCard() (declared in SleepCard.h).
// Declines (logo screen) when no book is open or it cannot be read.
namespace sleepcards::nowreading {

// The pace to show: the stored state (text of STATE_PATH, may be empty) with this boot's session
// applied. Pure.
pace::PaceState currentPace(const char* storedText, size_t storedLen, const pace::PaceSession& session);

// The book is read to the end.
bool bookFinished(const CardBook& book);

}  // namespace sleepcards::nowreading
