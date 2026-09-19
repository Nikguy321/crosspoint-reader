#pragma once
#include <cstddef>
#include <string>

/**
 * Type of OPDS entry.
 */
enum class OpdsEntryType {
  NAVIGATION,  // Link to another catalog
  BOOK         // Downloadable book
};

/**
 * Represents an entry from an OPDS feed (either a navigation link or a book).
 * Shared between the OPDS 1.x (Atom) and OPDS 2.0 (JSON) parsers.
 */
struct OpdsEntry {
  OpdsEntryType type = OpdsEntryType::NAVIGATION;
  std::string title;
  std::string author;  // Only for books
  std::string href;    // Navigation URL or epub download URL
  std::string id;
};

// Shared memory bounds for both feed parsers.
namespace OpdsLimits {
constexpr size_t ENTRY_STORAGE_CAPACITY = 64;
constexpr size_t MAX_ENTRIES = ENTRY_STORAGE_CAPACITY - 2;
constexpr size_t MAX_TITLE_CHARS = 160;
constexpr size_t MAX_AUTHOR_CHARS = 120;
constexpr size_t MAX_ID_CHARS = 128;
constexpr size_t MAX_HREF_CHARS = 768;
constexpr size_t MAX_SEARCH_TEMPLATE_CHARS = 768;
constexpr size_t MAX_PAGE_URL_CHARS = 768;
}  // namespace OpdsLimits
