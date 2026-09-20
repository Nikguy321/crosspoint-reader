#include "OpdsPublicationDoc.h"

#include <StreamingJsonParser.h>

#include <cstring>

#include "OpdsEntry.h"

namespace {

// Scans a standalone OPDS Publication document's top-level `links` array for
// the best acquisition link. Link objects live at depth 3 (root > "links" >
// object); a nested "properties"/"indirectAcquisition" object is skipped by
// only committing fields seen at the link-object depth.
struct PubDocCtx {
  std::string* outHref = nullptr;
  bool* outIsEpub = nullptr;

  uint8_t depth = 0;
  bool inLinksArray = false;
  uint8_t linkObjectDepth = 0;
  char pendingKey[16] = {0};

  struct {
    std::string href;
    int rank = -1;
    bool isEpub = false;
  } link;

  int bestRank = -1;
  bool bestIsEpub = false;
};

void onKey(void* ud, const char* key, size_t len) {
  auto& ctx = *static_cast<PubDocCtx*>(ud);
  const size_t max = sizeof(ctx.pendingKey) - 1;
  if (len > max) len = max;
  memcpy(ctx.pendingKey, key, len);
  ctx.pendingKey[len] = '\0';
}

void applyRel(PubDocCtx& ctx, const char* rel) {
  const int rank = opdsAcquisitionRank(rel);
  if (rank > ctx.link.rank) ctx.link.rank = rank;
}

void onString(void* ud, const char* value, size_t len) {
  auto& ctx = *static_cast<PubDocCtx*>(ud);
  if (ctx.linkObjectDepth == 0) return;
  if (ctx.depth == ctx.linkObjectDepth) {
    if (strcmp(ctx.pendingKey, "href") == 0) {
      ctx.link.href.assign(value, len < OpdsLimits::MAX_HREF_CHARS ? len : OpdsLimits::MAX_HREF_CHARS);
    } else if (strcmp(ctx.pendingKey, "rel") == 0) {
      applyRel(ctx, value);
    } else if (strcmp(ctx.pendingKey, "type") == 0) {
      if (strcmp(value, "application/epub+zip") == 0) ctx.link.isEpub = true;
    }
  } else if (ctx.depth == ctx.linkObjectDepth + 1 && strcmp(ctx.pendingKey, "rel") == 0) {
    applyRel(ctx, value);  // rel expressed as an array
  }
}

void onObjectStart(void* ud) {
  auto& ctx = *static_cast<PubDocCtx*>(ud);
  ++ctx.depth;
  // root object (1) > "links" array (2) > link object (3).
  if (ctx.inLinksArray && ctx.linkObjectDepth == 0 && ctx.depth == 3) {
    ctx.linkObjectDepth = ctx.depth;
    ctx.link = {};
  }
  ctx.pendingKey[0] = '\0';
}

void onObjectEnd(void* ud) {
  auto& ctx = *static_cast<PubDocCtx*>(ud);
  if (ctx.linkObjectDepth != 0 && ctx.depth == ctx.linkObjectDepth) {
    // Prefer a real EPUB, then higher acquisition rank; commit the winner.
    if (!ctx.link.href.empty() && ctx.link.rank >= 0) {
      const bool better = ctx.outHref->empty() || (ctx.link.isEpub && !ctx.bestIsEpub) ||
                          (ctx.link.isEpub == ctx.bestIsEpub && ctx.link.rank > ctx.bestRank);
      if (better) {
        *ctx.outHref = ctx.link.href;
        ctx.bestRank = ctx.link.rank;
        ctx.bestIsEpub = ctx.link.isEpub;
      }
    }
    ctx.linkObjectDepth = 0;
  }
  if (ctx.depth > 0) --ctx.depth;
  ctx.pendingKey[0] = '\0';
}

void onArrayStart(void* ud) {
  auto& ctx = *static_cast<PubDocCtx*>(ud);
  ++ctx.depth;
  if (ctx.depth == 2 && strcmp(ctx.pendingKey, "links") == 0) ctx.inLinksArray = true;
  // Keep pendingKey: a "rel" array's element strings need to see their key.
}

void onArrayEnd(void* ud) {
  auto& ctx = *static_cast<PubDocCtx*>(ud);
  if (ctx.inLinksArray && ctx.depth == 2) ctx.inLinksArray = false;
  if (ctx.depth > 0) --ctx.depth;
  ctx.pendingKey[0] = '\0';
}

}  // namespace

bool resolveOpdsIndirectAcquisition(const char* json, const size_t len, std::string& outHref, bool& outIsEpub) {
  outHref.clear();
  outIsEpub = false;
  PubDocCtx ctx;
  ctx.outHref = &outHref;
  ctx.outIsEpub = &outIsEpub;
  JsonCallbacks callbacks{&ctx,    &onKey,         &onString,    nullptr,       nullptr,
                          nullptr, &onObjectStart, &onObjectEnd, &onArrayStart, &onArrayEnd};
  StreamingJsonParser parser(callbacks);
  parser.feed(json, len);
  if (parser.hasError()) return false;
  outIsEpub = ctx.bestIsEpub;
  return !outHref.empty();
}
