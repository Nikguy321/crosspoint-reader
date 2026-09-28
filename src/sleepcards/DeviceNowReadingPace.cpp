#include <HalStorage.h>
#include <Logging.h>

#include "NowReadingPace.h"

// The reader side of the Now Reading pace: the page hook and the state file on the SD card.
namespace sleepcards::pace {
namespace {

bool referenceSeeded = false;

bool readStored(PaceState& out) {
  HalFile file;
  if (!Storage.exists(STATE_PATH) || !Storage.openFileForRead("PACE", STATE_PATH, file)) return false;
  char text[STATE_TEXT_CAP];
  const int got = file.read(text, sizeof(text) - 1);
  return got > 0 && parseState(text, static_cast<size_t>(got), out);
}

void persist(PaceSession& s) {
  PaceState stored;
  readStored(stored);  // none yet: start from empty
  const PaceState next = merged(stored, s);
  char text[STATE_TEXT_CAP];
  const size_t len = formatState(next, text, sizeof(text));
  if (len == 0) return;
  Storage.ensureDirectoryExists(STATE_DIR);
  HalFile file;
  if (!Storage.openFileForWrite("PACE", STATE_PATH, file) || file.write(text, len) != len) {
    LOG_ERR("PACE", "Could not save the reading pace");
    return;
  }
  clearSamples(s);
}

}  // namespace

void noteReaderPage(const uint32_t nowMs, const int spineIndex, const int page, const int pageCount,
                    const float chapterShare) {
  PaceSession& s = session();
  if (!referenceSeeded) {
    referenceSeeded = true;
    PaceState stored;
    if (readStored(stored)) seedReference(s, stored);
  }
  if (observePage(s, nowMs, spineIndex, page, pageCount, chapterShare) && s.samples >= FLUSH_EVERY) persist(s);
}

void flushReaderPace() {
  PaceSession& s = session();
  if (s.samples > 0) persist(s);
}

}  // namespace sleepcards::pace
