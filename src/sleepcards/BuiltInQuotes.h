#pragma once

#include <cstddef>

// The Quote card's built-in set: short public-domain passages (works published before 1930) about
// the outdoors, reading, travel and night, compiled into the firmware so the card always has
// something to show. Every entry was checked word for word against the Project Gutenberg edition
// named beside it in BuiltInQuotes.cpp. A '\n' in the text is a line break of verse; the
// attribution is "Name, Work" (QuoteCard's splitAttribution).
namespace sleepcards::quote {

struct BuiltInQuote {
  const char* text;
  const char* attribution;
};

size_t builtInCount();
// nullptr past the end.
const BuiltInQuote* builtInQuote(size_t index);

}  // namespace sleepcards::quote
