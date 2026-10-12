#pragma once

#include <WString.h>

// What the web File Transfer server (CrossPointWebServer) and WebDAV (WebDAVHandler) never touch:
// - hidden names, any name starting with '.': above all the reader's own /.crosspoint, which holds
//   its settings, saved Wi-Fi networks and KOSync login;
// - Apps > AI's key folder, "API Keys" in any case: "/API Keys/gemini.txt" holds the owner's Gemini
//   key, and a key goes onto the card with a computer (see the README's AI section).
// Neither server lists one, serves, moves, renames or deletes what is in one, or writes into one.
//
// True for a path with such a name ANYWHERE in it, each name read as the card reads it
// (util/CardPath: "/ .crosspoint/x" and "/API Keys./gemini.txt" open those folders too), and for a
// path the card resolves to one by another spelling: FAT keeps an 8.3 alias for a long name
// (".crosspoint" is "CROSSP~1"; "API Keys" is "API_KE~1" made on the reader, "APIKEY~1" on a
// computer) and SdFat opens a file or folder by its alias as well - and then gives back the alias
// as its name. So each leading part of the path that exists is looked up and the long name the
// card stored for it compared (HalStorage::storedName). A part that does not exist yet (an upload,
// a new folder) is judged as typed. `path` is a card path as the servers normalise it.
bool isProtectedWebPath(const String& path);
