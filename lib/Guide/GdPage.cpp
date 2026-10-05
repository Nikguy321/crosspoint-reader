#include "GdPage.h"

#include <cstring>

#include "GdText.h"

namespace gd {

namespace {

bool startsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

// "N. text": the number (1..999) and where the text starts; false when the line is not a step.
bool stepPrefix(const char* line, uint16_t& number, const char*& rest) {
  int n = 0, digits = 0;
  while (line[digits] >= '0' && line[digits] <= '9' && digits < 4) {
    n = n * 10 + (line[digits] - '0');
    digits++;
  }
  if (digits == 0 || digits > 3 || line[digits] != '.' || line[digits + 1] != ' ') return false;
  number = static_cast<uint16_t>(n);
  rest = line + digits + 2;
  return true;
}

}  // namespace

GpError TopicText::fail(const GpError e, const int line) {
  pages = 0;
  blocks = 0;
  badLine = line;
  return e;
}

int TopicText::clampPage(const int index) const {
  if (pages <= 0 || index < 0) return 0;
  return index >= pages ? pages - 1 : index;
}

GpError TopicText::parse(char* text, const size_t len) {
  pages = 0;
  blocks = 0;
  badLine = 0;
  if (!text) return fail(GpError::Empty, 0);
  if (len > MAX_GP_BYTES) return fail(GpError::TooBig, 0);
  LineSplitter ls(text, len);
  char* line;
  size_t lineLen;
  bool inPage = false;      // a "= " line opened the current page
  bool afterTitle = false;  // nothing but the title on this page yet
  while (ls.next(line, lineLen)) {
    const int ln = ls.lineNumber();
    if (lineLen == 0) continue;
    if (std::strcmp(line, "---") == 0) {
      if (!inPage) return fail(GpError::NoTitle, ln);
      inPage = false;
      continue;
    }
    if (startsWith(line, "= ")) {
      // A title without a "---" before it still starts a new page (the break is implied).
      if (pages >= MAX_PAGES) return fail(GpError::TooBig, ln);
      const char* title = line + 2;
      while (*title == ' ') title++;
      if (!*title) return fail(GpError::NoTitle, ln);
      PageText& p = pageList[pages++];
      p.title = title;
      p.figure = nullptr;
      p.caption = nullptr;
      p.firstBlock = static_cast<uint16_t>(blocks);
      p.blockCount = 0;
      inPage = true;
      afterTitle = true;
      continue;
    }
    if (!inPage) return fail(GpError::NoTitle, ln);
    PageText& page = pageList[pages - 1];
    if (startsWith(line, "@fig ") || std::strcmp(line, "@fig") == 0) {
      if (!afterTitle) return fail(GpError::BadFigure, ln);
      char* name = line + 4;
      while (*name == ' ') name++;
      char* sp = std::strchr(name, ' ');
      char* caption = sp ? sp + 1 : name + std::strlen(name);
      if (sp) *sp = '\0';
      if (!validId(name, MAX_ID)) return fail(GpError::BadFigure, ln);
      page.figure = name;
      page.caption = caption;
      afterTitle = false;
      continue;
    }
    afterTitle = false;
    Block b;
    uint16_t number = 0;
    const char* rest = nullptr;
    if (startsWith(line, "- ")) {
      b.kind = BlockKind::Bullet;
      b.text = line + 2;
    } else if (startsWith(line, "! ")) {
      b.kind = BlockKind::Warning;
      b.text = line + 2;
    } else if (startsWith(line, "* ")) {
      b.kind = BlockKind::Note;
      b.text = line + 2;
    } else if (stepPrefix(line, number, rest)) {
      if (number == 0) return fail(GpError::BadLine, ln);
      b.kind = BlockKind::Step;
      b.number = number;
      b.marker = line;
      b.markerLen = static_cast<uint8_t>(rest - line - 1);  // the digits and the '.'
      b.text = rest;
    } else if (line[0] == '\\') {
      b.kind = BlockKind::Para;
      b.text = line + 1;
    } else {
      b.kind = BlockKind::Para;
      b.text = line;
    }
    while (*b.text == ' ') b.text++;
    if (!*b.text) return fail(GpError::BadLine, ln);
    if (blocks >= MAX_BLOCKS) return fail(GpError::TooBig, ln);
    blockList[blocks++] = b;
    page.blockCount++;
  }
  if (pages == 0) return fail(GpError::Empty, 0);
  return GpError::None;
}

}  // namespace gd
