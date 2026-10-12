#pragma once

// Card paths read the way the card's library (SdFat) reads them, for the guards that keep a name
// away from the web File Transfer server and WebDAV (network/WebPathGuard) and the AI app's key
// folder rules (activities/apps/AiText). Pure: no Arduino, no card; host-tested in test/card_path.

#include <cstddef>

namespace cardpath {

// The next name of the path at p, as the card reads it: SdFat skips a name's leading spaces and
// drops its trailing dots and spaces, so " API Keys", "API Keys." and "API Keys " all open the folder
// "API Keys", and " .crosspoint" opens ".crosspoint". A name with nothing left ("", ".", "..", " ")
// is skipped - SdFat skips the spaces between names and cannot open one of only dots - so a guard
// reads past it too. [name, name + len) is the name; false at the end of the path.
bool nextName(const char*& p, const char*& name, size_t& len);

// A hidden name: one starting with '.' - the reader's own /.crosspoint (its settings, saved Wi-Fi
// networks and the KOSync login) and every other hidden file or folder. `name` as nextName gives it.
bool isHiddenName(const char* name, size_t len);

// True when any name of the path, each read as the card reads it, is hidden.
bool pathHasHiddenName(const char* path);

}  // namespace cardpath
