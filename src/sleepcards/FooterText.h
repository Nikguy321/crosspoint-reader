#pragma once

#include <cstddef>
#include <cstdint>

#include "CardTime.h"

// The footer's "when" (CardDraw's drawSleepFooter): the moment the screen was drawn, with its date,
// since the e-ink keeps a card for hours or days and its battery figure is only as old as that.
// The strings are tr() keys with named placeholders a translation may reorder: {time} and {date}
// in the sentence, {weekday} {month} {day} in the date.
namespace sleepcards::footer {

// Longest first; the footer takes the first that fits beside the battery.
enum class Form : uint8_t {
  Full,       // "Screen updated 2:17 PM, Wed Oct 1"
  NoWeekday,  // "Screen updated 2:17 PM, Oct 1"
  Short,      // "Updated 2:17 PM, Oct 1"
  TimeOnly,   // "Updated 2:17 PM"
  Count,
};

struct Field {
  const char* name;  // without the braces
  const char* value;
};

// tmpl with every "{name}" of fields replaced by its value; anything else, an unknown "{...}"
// included, is copied as written. Always terminated (when cap > 0); returns the length written.
size_t expand(const char* tmpl, const Field* fields, size_t count, char* out, size_t cap);

// One form of the line for local wall time `when` (12- or 24-hour clock). Returns its length.
size_t formatUpdated(const LocalDate& when, bool clock12h, Form form, char* out, size_t cap);

// Width in px of a line of text.
using WidthFn = int (*)(void* user, const char* text);

// The longest form no wider than maxWidth into out (TimeOnly when none fits). Returns that form.
Form fitUpdated(const LocalDate& when, bool clock12h, int maxWidth, WidthFn width, void* user, char* out, size_t cap);

}  // namespace sleepcards::footer
