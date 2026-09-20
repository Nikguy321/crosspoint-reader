#pragma once
#include <cstddef>
#include <string>

/**
 * Resolve an indirect OPDS acquisition. Given a standalone OPDS Publication
 * document (application/opds-publication+json, fetched from an indirect
 * acquisition link), find the best direct download link in its top-level
 * `links`: a real EPUB acquisition preferred, otherwise any acquisition link
 * (which may still be DRM-wrapped — the caller verifies the downloaded bytes).
 *
 * Returns true and sets outHref when an acquisition link is found. outIsEpub
 * reports whether the chosen link is typed application/epub+zip.
 */
bool resolveOpdsIndirectAcquisition(const char* json, size_t len, std::string& outHref, bool& outIsEpub);
