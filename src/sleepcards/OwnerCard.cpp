#include "OwnerCard.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CardDraw.h"
#include "fontIds.h"
#include "images/X4ProLogo.h"

namespace sleepcards {
namespace owner {

void tidyLine(const char* in, const size_t inCap, char* out, const size_t outCap) {
  if (out == nullptr || outCap == 0) return;
  out[0] = '\0';
  if (in == nullptr) return;
  size_t n = 0;
  bool pendingSpace = false;
  for (size_t i = 0; i < inCap && in[i] != '\0'; i++) {
    const auto c = static_cast<unsigned char>(in[i]);
    if (c <= ' ' || c == 0x7F) {
      pendingSpace = n > 0;  // collapse runs; never a leading space
      continue;
    }
    if (pendingSpace) {
      if (n + 1 >= outCap) break;
      out[n++] = ' ';
      pendingSpace = false;
    }
    if (n + 1 >= outCap) break;
    out[n++] = static_cast<char>(c);
  }
  // Never end inside a UTF-8 sequence: drop a trailing lead byte and its partial continuation.
  size_t cut = n;
  while (cut > 0 && (static_cast<unsigned char>(out[cut - 1]) & 0xC0) == 0x80) cut--;
  if (cut > 0 && (static_cast<unsigned char>(out[cut - 1]) & 0x80) != 0) {
    const auto lead = static_cast<unsigned char>(out[cut - 1]);
    const size_t need = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
    if (n - (cut - 1) < need) n = cut - 1;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
}

bool collectOwnerLines(const SleepCardSettings& settings, OwnerLines& out) {
  out = OwnerLines{};
  char contact[MAX_CONTACTS][OWNER_LINE_CAP];
  tidyLine(settings.ownerName, sizeof(settings.ownerName), out.headline, sizeof(out.headline));
  tidyLine(settings.ownerContact1, sizeof(settings.ownerContact1), contact[0], sizeof(contact[0]));
  tidyLine(settings.ownerContact2, sizeof(settings.ownerContact2), contact[1], sizeof(contact[1]));
  for (const auto& line : contact) {
    if (line[0] == '\0') continue;
    if (out.headline[0] == '\0') {
      std::memcpy(out.headline, line, sizeof(out.headline));
    } else {
      std::memcpy(out.contacts[out.contactCount++], line, sizeof(out.contacts[0]));
    }
  }
  return out.headline[0] != '\0';
}

void hyphenBreaks(const char* in, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return;
  size_t n = 0;
  for (size_t i = 0; in != nullptr && in[i] != '\0' && n + 1 < cap; i++) {
    out[n++] = in[i];
    const bool between = in[i] == '-' && i > 0 && in[i - 1] != ' ' && in[i + 1] != '\0' && in[i + 1] != ' ';
    if (between && n + 1 < cap) out[n++] = ' ';
  }
  out[n] = '\0';
}

void undoHyphenBreaks(std::string& line) {
  size_t at = 0;
  while ((at = line.find("- ", at)) != std::string::npos) {
    if (at > 0 && line[at - 1] != ' ' && at + 2 < line.size()) {
      line.erase(at + 1, 1);
    } else {
      at += 2;
    }
  }
}

float fitScale(const int width1x, const int available, const float minScale, const float maxScale) {
  if (available <= 0) return 0.0f;
  if (width1x <= 0) return maxScale;
  const float exact = static_cast<float>(available) / static_cast<float>(width1x);
  const float stepped = std::floor(std::min(exact, maxScale) * 4.0f) / 4.0f;
  return stepped + 1e-6f >= minScale ? stepped : 0.0f;
}

}  // namespace owner

namespace {

// Layout (portrait 480x800; the shared footer owns the bottom FOOTER_HEIGHT px).
constexpr int FRAME_OUTER = 16;                   // outer rule inset
constexpr int FRAME_GAP = 7;                      // between the outer and inner rules
constexpr int FRAME_BOTTOM_GAP = 16;              // frame bottom above the footer rule (well clear: no double rule)
constexpr int TEXT_INSET = 52;                    // text keeps this far from the screen edge
constexpr int HEADER_INSET = 38;                  // the one-line header may run a little wider
constexpr int MARK_SIZE = X4PRO_LOGO_SMALL_SIZE;  // the X4 Pro mark drawn natively at 80 px

constexpr int HEADER_FONT = NOTOSERIF_16_FONT_ID;
constexpr int NAME_FONT = NOTOSERIF_18_FONT_ID;
constexpr int CONTACT_FONT = NOTOSANS_16_FONT_ID;
constexpr int CONTACT_FONT_SMALL = NOTOSANS_14_FONT_ID;  // a contact too long for one line at 16
constexpr int THANKS_FONT = NOTOSERIF_14_FONT_ID;

constexpr float NAME_MAX_SCALE = 2.0f;
constexpr float NAME_ONE_LINE_MIN = 1.25f;
constexpr float NAME_TWO_LINE_MAX = 1.5f;

// A filled diamond of half-diagonal `half`.
void fillDiamond(GfxRenderer& r, const int cx, const int cy, const int half) {
  for (int dy = -half; dy <= half; dy++) {
    const int span = half - std::abs(dy);
    r.fillRect(cx - span, cy + dy, 2 * span + 1, 1, true);
  }
}

// ———◆——— : two rules either side of a small diamond.
void drawOrnament(GfxRenderer& r, const int cx, const int cy, const int halfWidth) {
  constexpr int diamond = 5;
  constexpr int gap = 8;
  const int ruleLen = halfWidth - diamond - gap;
  if (ruleLen > 0) {
    r.fillRect(cx - halfWidth, cy, ruleLen, 1, true);
    r.fillRect(cx + diamond + gap, cy, ruleLen, 1, true);
  }
  fillDiamond(r, cx, cy, diamond);
}

// The name: one line as large as fits (2x down to 1.25x); else two lines at up to 1.5x; else up
// to three at 1x-1.25x. A double-barrelled name may break after its hyphen.
struct NameLayout {
  std::vector<std::string> lines;  // empty = the one line is OwnerLines::headline itself
  float scale = 1.0f;
  int lineHeight = 0;
  int count() const { return lines.empty() ? 1 : static_cast<int>(lines.size()); }
  int height() const { return count() * lineHeight; }
};

// Names rarely use their descender room: pitch the lines a little tighter than the font's line
// box, so a two-line name reads as one block.
int nameLineHeight(const GfxRenderer& r, const float scale) {
  return static_cast<int>(std::lround(r.getLineHeight(NAME_FONT) * scale * 0.92f));
}

NameLayout layoutName(const GfxRenderer& r, const char* name, const int available, const int maxHeight) {
  NameLayout out;
  const int width1x = draw::textWidthScaled(r, NAME_FONT, name, 1.0f, EpdFontFamily::BOLD);
  const float one = owner::fitScale(width1x, available, NAME_ONE_LINE_MIN, NAME_MAX_SCALE);
  float maxScale = NAME_TWO_LINE_MAX;
  if (one > 0.0f) {
    out.scale = one;
  } else {
    // wrappedText allocates one short string per line (at most three, of a < 2 * OWNER_LINE_CAP
    // byte name). Narrow enough for 1.25x first; past two lines there, the full width at 1x.
    char breakable[2 * OWNER_LINE_CAP];
    owner::hyphenBreaks(name, breakable, sizeof(breakable));
    const auto wrap = [&](const int width) {
      std::vector<std::string> lines = r.wrappedText(NAME_FONT, breakable, width, 3, EpdFontFamily::BOLD);
      for (auto& line : lines) owner::undoHyphenBreaks(line);
      return lines;
    };
    out.lines = wrap(static_cast<int>(available / NAME_ONE_LINE_MIN));
    if (out.lines.size() > 2) {
      out.lines = wrap(available);
      maxScale = NAME_ONE_LINE_MIN;
    }
    if (out.lines.empty()) out.lines.emplace_back(name);
    int widest = 0;
    for (const auto& line : out.lines) {
      widest = std::max(widest, draw::textWidthScaled(r, NAME_FONT, line.c_str(), 1.0f, EpdFontFamily::BOLD));
    }
    const float fit = owner::fitScale(widest, available, 1.0f, maxScale);
    out.scale = fit > 0.0f ? fit : 1.0f;
  }
  out.lineHeight = nameLineHeight(r, out.scale);
  // Long contact lines leave less room: step the name down rather than overrun the frame.
  while (out.height() > maxHeight && out.scale > 1.0f) {
    out.scale -= 0.25f;
    out.lineHeight = nameLineHeight(r, out.scale);
  }
  return out;
}

}  // namespace

bool renderOwnerCard(const CardContext& ctx, GfxRenderer& r) {
  owner::OwnerLines who;
  if (!owner::collectOwnerLines(ctx.settings, who)) return false;

  const int w = r.getScreenWidth();
  const int h = r.getScreenHeight();
  const int cx = w / 2;
  const int textWidth = w - 2 * TEXT_INSET;

  // Frame: a heavy rounded rule with a hairline inside it.
  const int frameBottom = h - FOOTER_HEIGHT - FRAME_BOTTOM_GAP;
  const int outerH = frameBottom - FRAME_OUTER;
  r.drawRoundedRect(FRAME_OUTER, FRAME_OUTER, w - 2 * FRAME_OUTER, outerH, 3, 18, true);
  const int inner = FRAME_OUTER + FRAME_GAP;
  r.drawRoundedRect(inner, inner, w - 2 * inner, outerH - 2 * FRAME_GAP, 1, 12, true);

  // Header.
  int y = 74;
  y += draw::drawWrapped(r, HEADER_FONT, HEADER_INSET, y, w - 2 * HEADER_INSET, tr(STR_OWNER_IF_FOUND), 2,
                         draw::Align::Center, EpdFontFamily::ITALIC);
  const int headerOrnamentY = y + 20;
  drawOrnament(r, cx, headerOrnamentY, 70);

  // The mark at the bottom, with an ornament above it mirroring the header's.
  const int markTop = frameBottom - FRAME_GAP - 30 - MARK_SIZE;
  const int footOrnamentY = markTop - 34;
  drawOrnament(r, cx, footOrnamentY, 70);
  r.drawIcon(X4ProLogoSmall, cx - MARK_SIZE / 2, markTop, MARK_SIZE);  // pixel-exact, unlike the byte-aligned drawImage

  // The block between the two ornaments: name, a short rule, the contacts, thanks.
  constexpr int contactGap = 10;
  std::vector<std::string> contactLines[owner::MAX_CONTACTS];
  int contactFont[owner::MAX_CONTACTS] = {CONTACT_FONT, CONTACT_FONT};
  int contactsH = 0;
  for (int i = 0; i < who.contactCount; i++) {
    if (r.getTextWidth(CONTACT_FONT, who.contacts[i]) > textWidth) contactFont[i] = CONTACT_FONT_SMALL;
    contactLines[i] = r.wrappedText(contactFont[i], who.contacts[i], textWidth, 2);
    contactsH += static_cast<int>(contactLines[i].size()) * r.getLineHeight(contactFont[i]) + (i > 0 ? contactGap : 0);
  }
  constexpr int ruleGap = 26;  // name -> rule -> contacts
  const int thanksLh = r.getLineHeight(THANKS_FONT);
  constexpr int thanksGap = 30;
  const int top = headerOrnamentY + 24;
  const int bottom = footOrnamentY - 24;
  const int contactsBlockH = contactsH > 0 ? 2 * ruleGap + 1 + contactsH : 0;
  const NameLayout name = layoutName(r, who.headline, textWidth, bottom - top - contactsBlockH);
  const int nameH = name.height();
  int blockH = nameH + contactsBlockH;
  // The thanks line is the first thing to go when a long name and long contacts fill the frame.
  const bool thanks = blockH + thanksGap + thanksLh <= bottom - top;
  if (thanks) blockH += thanksGap + thanksLh;
  y = top + std::max(0, (bottom - top - blockH) / 2);

  if (name.lines.empty()) {
    draw::drawCenteredTextScaled(r, NAME_FONT, cx, y, who.headline, name.scale, true, EpdFontFamily::BOLD);
  } else {
    for (size_t i = 0; i < name.lines.size(); i++) {
      draw::drawCenteredTextScaled(r, NAME_FONT, cx, y + static_cast<int>(i) * name.lineHeight, name.lines[i].c_str(),
                                   name.scale, true, EpdFontFamily::BOLD);
    }
  }
  y += nameH;

  if (contactsH > 0) {
    y += ruleGap;
    r.fillRect(cx - 40, y, 80, 1, true);
    y += 1 + ruleGap;
    for (int i = 0; i < who.contactCount; i++) {
      if (i > 0) y += contactGap;
      for (const auto& line : contactLines[i]) {
        draw::drawTextCenteredAt(r, contactFont[i], cx, y, line.c_str());
        y += r.getLineHeight(contactFont[i]);
      }
    }
  }

  if (thanks) {
    draw::drawTextCenteredAt(r, THANKS_FONT, cx, y + thanksGap, tr(STR_OWNER_THANK_YOU), true, EpdFontFamily::ITALIC);
  }
  return true;
}

}  // namespace sleepcards
