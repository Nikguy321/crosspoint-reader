#include "Opds2Parser.h"

#include <Logging.h>

#include <cstring>

using namespace OpdsLimits;

Opds2Parser::Opds2Parser()
    : parser(JsonCallbacks{this, &sOnKey, &sOnString, nullptr, &sOnBool, nullptr, &sOnObjectStart, &sOnObjectEnd,
                           &sOnArrayStart, &sOnArrayEnd}) {
  entries.reserve(ENTRY_STORAGE_CAPACITY);
}

size_t Opds2Parser::write(const uint8_t c) { return write(&c, 1); }

size_t Opds2Parser::write(const uint8_t* data, const size_t length) {
  if (errorOccured) return length;
  parser.feed(reinterpret_cast<const char*>(data), length);
  if (parser.hasError()) {
    errorOccured = true;
    LOG_DBG("OPDS2", "JSON parse error");
  }
  return length;
}

bool Opds2Parser::error() const { return errorOccured || parser.hasError(); }

void Opds2Parser::assignBounded(std::string& target, const char* value, const size_t len, const size_t maxLen) {
  target.assign(value, len < maxLen ? len : maxLen);
}

// Acquisition relations per OPDS 2.0 §5.3: the OPDS 1.x URI aliases plus the
// simplified values usable for a direct download.
bool Opds2Parser::relIsAcquisition(const char* rel) {
  if (strstr(rel, "opds-spec.org/acquisition") != nullptr) return true;
  return strcmp(rel, "acquisition") == 0 || strcmp(rel, "download") == 0 || strcmp(rel, "open-access") == 0;
}

void Opds2Parser::resetLink() {
  link.href.clear();
  link.title.clear();
  link.relSearch = link.relNext = link.relPrev = link.relAcquisition = false;
  link.typeEpub = false;
  link.templated = false;
}

void Opds2Parser::applyRel(const char* rel) {
  if (strcmp(rel, "search") == 0) link.relSearch = true;
  if (strcmp(rel, "next") == 0) link.relNext = true;
  if (strcmp(rel, "previous") == 0 || strcmp(rel, "prev") == 0) link.relPrev = true;
  if (relIsAcquisition(rel)) link.relAcquisition = true;
}

// ---- scope machine ----

Opds2Parser::Scope Opds2Parser::scopeForChild(const Scope parent, const bool isObject) const {
  switch (parent) {
    case Scope::FEED:
    case Scope::GROUP:
      if (!isObject) {
        if (strcmp(pendingKey, "links") == 0) return Scope::FEED_LINKS;
        if (strcmp(pendingKey, "navigation") == 0) return Scope::NAV;
        if (strcmp(pendingKey, "publications") == 0) return Scope::PUBS;
        if (parent == Scope::FEED && strcmp(pendingKey, "groups") == 0) return Scope::GROUPS;
      }
      return Scope::SKIP;
    case Scope::FEED_LINKS:
      return isObject ? Scope::FEED_LINK : Scope::SKIP;
    case Scope::FEED_LINK:
    case Scope::PUB_LINK:
      if (!isObject && strcmp(pendingKey, "rel") == 0) return Scope::LINK_REL;
      return Scope::SKIP;  // properties, alternate, children
    case Scope::NAV:
      return isObject ? Scope::NAV_LINK : Scope::SKIP;
    case Scope::PUBS:
      return isObject ? Scope::PUB : Scope::SKIP;
    case Scope::PUB:
      if (isObject && strcmp(pendingKey, "metadata") == 0) return Scope::PUB_META;
      if (!isObject && strcmp(pendingKey, "links") == 0) return Scope::PUB_LINKS;
      return Scope::SKIP;  // images, reading order, resources
    case Scope::PUB_META:
      if (strcmp(pendingKey, "author") == 0) return isObject ? Scope::AUTHOR : Scope::AUTHOR_ARR;
      if (isObject && strcmp(pendingKey, "title") == 0) return Scope::PUB_TITLE;
      return Scope::SKIP;  // belongsTo, subject, other contributors
    case Scope::AUTHOR_ARR:
      return isObject ? Scope::AUTHOR : Scope::SKIP;
    case Scope::PUB_LINKS:
      return isObject ? Scope::PUB_LINK : Scope::SKIP;
    case Scope::GROUPS:
      return isObject ? Scope::GROUP : Scope::SKIP;
    default:
      return Scope::SKIP;
  }
}

void Opds2Parser::onContainerStart(const bool isObject) {
  if (depth >= MAX_DEPTH) {
    errorOccured = true;
    return;
  }
  Scope next;
  if (!sawRoot) {
    sawRoot = true;
    next = isObject ? Scope::FEED : Scope::SKIP;
  } else {
    next = scopeForChild(current(), isObject);
  }
  if (next == Scope::FEED_LINK || next == Scope::NAV_LINK || next == Scope::PUB_LINK) resetLink();
  if (next == Scope::PUB) {
    currentEntry = OpdsEntry{};
    pubHasPlainEpub = false;
  }
  stack[depth++] = next;
  pendingKey[0] = '\0';
}

void Opds2Parser::onContainerEnd() {
  if (depth == 0) return;
  const Scope closed = stack[--depth];
  pendingKey[0] = '\0';
  switch (closed) {
    case Scope::FEED_LINK:
      commitFeedLink();
      break;
    case Scope::NAV_LINK:
      commitNavLink();
      break;
    case Scope::PUB_LINK:
      commitPubLink();
      break;
    case Scope::PUB:
      commitPublication();
      break;
    default:
      break;
  }
}

void Opds2Parser::onStringValue(const char* value, const size_t len) {
  switch (current()) {
    case Scope::FEED_LINK:
    case Scope::NAV_LINK:
    case Scope::PUB_LINK:
      if (strcmp(pendingKey, "href") == 0) {
        assignBounded(link.href, value, len, MAX_HREF_CHARS);
      } else if (strcmp(pendingKey, "title") == 0) {
        assignBounded(link.title, value, len, MAX_TITLE_CHARS);
      } else if (strcmp(pendingKey, "rel") == 0) {
        applyRel(value);
      } else if (strcmp(pendingKey, "type") == 0) {
        if (strcmp(value, "application/epub+zip") == 0) link.typeEpub = true;
      }
      break;
    case Scope::LINK_REL:
      applyRel(value);
      break;
    case Scope::PUB_META:
      if (strcmp(pendingKey, "title") == 0) {
        assignBounded(currentEntry.title, value, len, MAX_TITLE_CHARS);
      } else if (strcmp(pendingKey, "author") == 0) {
        if (currentEntry.author.empty()) assignBounded(currentEntry.author, value, len, MAX_AUTHOR_CHARS);
      } else if (strcmp(pendingKey, "identifier") == 0) {
        assignBounded(currentEntry.id, value, len, MAX_ID_CHARS);
      }
      break;
    case Scope::PUB_TITLE:
      // Localized title object: take the first translation.
      if (currentEntry.title.empty()) assignBounded(currentEntry.title, value, len, MAX_TITLE_CHARS);
      break;
    case Scope::AUTHOR:
      if (strcmp(pendingKey, "name") == 0 && currentEntry.author.empty()) {
        assignBounded(currentEntry.author, value, len, MAX_AUTHOR_CHARS);
      }
      break;
    case Scope::AUTHOR_ARR:
      // Array of contributor name strings: take the first.
      if (currentEntry.author.empty()) assignBounded(currentEntry.author, value, len, MAX_AUTHOR_CHARS);
      break;
    default:
      break;
  }
}

// ---- commits ----

void Opds2Parser::commitFeedLink() {
  if (link.href.empty()) return;
  if (link.relSearch && (link.templated || link.href.find('{') != std::string::npos)) {
    if (searchTemplate.empty() && link.href.size() <= MAX_SEARCH_TEMPLATE_CHARS) searchTemplate = link.href;
  } else if (link.relNext && nextPageUrl.empty()) {
    nextPageUrl = link.href;
  } else if (link.relPrev && prevPageUrl.empty()) {
    prevPageUrl = link.href;
  }
}

void Opds2Parser::commitNavLink() {
  if (link.href.empty() || link.title.empty()) return;
  if (entries.size() >= MAX_ENTRIES) {
    feedTruncated = true;
    return;
  }
  OpdsEntry entry;
  entry.type = OpdsEntryType::NAVIGATION;
  entry.title = std::move(link.title);
  entry.href = std::move(link.href);
  entries.push_back(std::move(entry));
}

void Opds2Parser::commitPubLink() {
  if (link.href.empty() || !link.typeEpub || !link.relAcquisition) return;
  // Prefer plain EPUB links over derived formats when a publication carries
  // several acquisition links (same heuristic as the Atom parser).
  const bool isPlainEpub =
      link.href.find(".epub") != std::string::npos || link.href.find("/epub/") != std::string::npos;
  if (currentEntry.href.empty() || (isPlainEpub && !pubHasPlainEpub)) {
    currentEntry.href = std::move(link.href);
    pubHasPlainEpub = isPlainEpub;
  }
}

void Opds2Parser::commitPublication() {
  if (currentEntry.title.empty() || currentEntry.href.empty()) return;
  if (entries.size() >= MAX_ENTRIES) {
    feedTruncated = true;
    return;
  }
  currentEntry.type = OpdsEntryType::BOOK;
  entries.push_back(std::move(currentEntry));
  currentEntry = OpdsEntry{};
}

// ---- static callbacks ----

void Opds2Parser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<Opds2Parser*>(ctx);
  const size_t max = sizeof(self->pendingKey) - 1;
  if (len > max) len = max;
  memcpy(self->pendingKey, key, len);
  self->pendingKey[len] = '\0';
}

void Opds2Parser::sOnString(void* ctx, const char* value, const size_t len) {
  static_cast<Opds2Parser*>(ctx)->onStringValue(value, len);
}

void Opds2Parser::sOnBool(void* ctx, const bool value) {
  auto* self = static_cast<Opds2Parser*>(ctx);
  const Scope scope = self->current();
  if ((scope == Scope::FEED_LINK || scope == Scope::PUB_LINK) && strcmp(self->pendingKey, "templated") == 0) {
    self->link.templated = value;
  }
}

void Opds2Parser::sOnObjectStart(void* ctx) { static_cast<Opds2Parser*>(ctx)->onContainerStart(true); }
void Opds2Parser::sOnObjectEnd(void* ctx) { static_cast<Opds2Parser*>(ctx)->onContainerEnd(); }
void Opds2Parser::sOnArrayStart(void* ctx) { static_cast<Opds2Parser*>(ctx)->onContainerStart(false); }
void Opds2Parser::sOnArrayEnd(void* ctx) { static_cast<Opds2Parser*>(ctx)->onContainerEnd(); }
