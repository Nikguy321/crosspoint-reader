#pragma once

#include <WString.h>

// Apps > AI's key folder over the network. "/API Keys/gemini.txt" holds the owner's Gemini key, and
// the web File Transfer server (CrossPointWebServer) and WebDAV (WebDAVHandler) never list that
// folder, never serve, move, rename or delete what is in it and never write into it (a key goes onto
// the card with a computer; see the README's AI section).
//
// True for a path with "API Keys" as ANY of its names, in any case, each read as the card reads it
// (aitext::pathHasKeyDir: "/API Keys./gemini.txt" opens the key too), and for a path whose FIRST
// name the card resolves to its "API Keys" folder by another spelling: FAT keeps an 8.3 alias for a
// long name ("APIKEY~1"), and SdFat opens a folder by its alias as well - so that first name is
// opened and the folder's own long name compared. `path` is a card path as the servers normalise it.
bool isKeyFolderWebPath(const String& path);
