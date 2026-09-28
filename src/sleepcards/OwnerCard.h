#pragma once

#include <cstddef>
#include <string>

#include "SleepCard.h"

// OWNER: "If found, please return to" + the owner's name and contact lines from Settings
// (typed on the device; nothing of anyone's is in the repo), inside a double frame, with the
// X4 Pro mark small at the bottom. Declines - the logo screen is shown instead - when every
// owner field is blank. The owner fields are never logged.
// Entry point: sleepcards::renderOwnerCard() (declared in SleepCard.h).
namespace sleepcards::owner {

constexpr int MAX_CONTACTS = 2;

// The owner fields as the card shows them: tidied, blanks dropped. With no name, the first
// contact line takes the name's place (a phone number alone is still a way home).
struct OwnerLines {
  char headline[OWNER_LINE_CAP] = "";
  char contacts[MAX_CONTACTS][OWNER_LINE_CAP] = {"", ""};
  int contactCount = 0;
};

// One settings field made presentable: control characters become spaces, runs of spaces
// collapse to one, both ends are trimmed. Reads at most inCap bytes of `in` (it need not be
// terminated); `out` is always terminated and never ends inside a UTF-8 sequence.
void tidyLine(const char* in, size_t inCap, char* out, size_t outCap);

// False when the name and both contact lines are blank (the card then declines).
bool collectOwnerLines(const SleepCardSettings& settings, OwnerLines& out);

// Wrapping help for a double-barrelled name (the renderer wraps only at spaces): hyphenBreaks
// copies `in` with a space after every hyphen that joins two words ("Ann-Marie" -> "Ann- Marie");
// undoHyphenBreaks takes such spaces back out of one wrapped line. `out` holds up to 2x `in`.
void hyphenBreaks(const char* in, char* out, size_t cap);
void undoHyphenBreaks(std::string& line);

// The largest magnification in 0.25 steps, at most maxScale, at which text `width1x` px wide
// at 1x fits in `available` px; 0 when it does not fit even at minScale.
float fitScale(int width1x, int available, float minScale, float maxScale);

}  // namespace sleepcards::owner
