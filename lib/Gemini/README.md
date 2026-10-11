# Gemini and WikiLookup: the AI app's rules

The pure half of **Apps > AI** (X4 Pro): the key file `gemini.txt`, the request body Google is sent, the parsing of
its answers, the screen's plain ASCII, the model ladder (retries, the fallback, the free tier's daily limits and
"Retry if it times out"), the topic, and the Wikipedia lookup before each question (`lib/WikiLookup`: the keywords,
the one URL, the response, the relevance rule, the background and the "Wikipedia: ..." line). No Arduino or ESP-IDF
header and no allocation: the device half is `src/network/AiRun` (the key on the card, the topic in PSRAM, the
ladder loop) and `src/network/AiHttps` (the wire, with the pinned roots in `src/network/AiRoots.h`); the screens are
`src/activities/apps/Ai*`.

## Where it comes from

`gemini.h`, `gemini.cpp`, `json_read.h`, `json_read.cpp` and `lib/WikiLookup/wiki_lookup.{h,cpp}` are copied from the
[WiPhone firmware repository](https://github.com/Nikguy321/wiphone-meshtastic) (`WiPhone/`), as its working tree
stood on 2026-10-11 (round 3: contractions, follow-ups that walk back, relevance; then the rules the three devices -
WiPhone, COVEY, X4 - share exactly: THE SHARED FOLD, gemini.cpp's table read one character at a time by
`geminiFoldNext`, before the keywords and the relevance match, every other character a separator, never deleted;
the contraction off before the 40-letter limit; and every 5xx from Gemini "busy", the 503 ladder). The WiPhone's own
comments are kept (they speak of "the phone"); each file starts with a note saying it is shared. The X4 copies differ
only in:

- `GEM_SYSTEM_INSTRUCTION`'s device sentence (an e-ink e-reader with an on-screen keyboard, not a 240x320 keypad
  phone; the plain-ASCII rules after it are the WiPhone's word for word);
- `geminiFailText`'s `GEM_NET_NOMEM` sentence ("The reader is low on memory ...");
- `WIKI_USER_AGENT`'s device word (`X4`).

(Until the shared fold the X4 had its own `geminiAsciiFold` for the keywords; the WiPhone's `geminiFoldNext` replaced
it, so an accented question - the X4's keyboard types one - now gives the same keywords on all three devices.)

`json_read` is unchanged below its note. Keep it that way: to bring a WiPhone change over, diff the WiPhone's files
against these, apply the change, and keep the three differences above.

The host suites are the WiPhone's own, on gtest: `test/gemini` (its `tests/test_gemini.cpp`, less the checks
against the phone's fonts) and `test/wiki_lookup` (its `tests/test_wiki.cpp`, with the shared vectors: the fold's
questions, COVEY's round-5 table and the relevance cases), plus the X4's checks at the end of the latter: more keyword
vectors the three devices share (produced by the WiPhone's code and COVEY's `gemini.py`, which agreed with this copy
on every one), and the fold of what the X4's keyboard types. `scripts/check_ai_roots.py` checks the pinned roots.

These files are left out of `bin/clang-format-fix` and of cppcheck (like `lib/TzGrid`), so they stay diffable
against their source.

## Licence

The code was written for this fork's owner and is shared with his WiPhone firmware, where the repository as a whole
is GPL v3. The same author offers it here under this repository's MIT licence.
