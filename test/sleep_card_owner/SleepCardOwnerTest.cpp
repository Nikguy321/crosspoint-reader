// Owner card: the tidy-up of the typed fields, when the card declines, the name's fit, the
// small X4 Pro mark, and the rendered card (build/cards/owner*.png). The owner fields here
// are fictional (555-01xx is the reserved fictional range, example.com is reserved).
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "CardPreview.h"
#include "src/images/X4ProLogo.h"
#include "src/sleepcards/OwnerCard.h"

using namespace sleepcards;
using namespace sleepcards::owner;

namespace {

constexpr double BUDGET_MS = preview::HOST_RUNAWAY_MS;

std::string tidy(const char* in, const size_t outCap = OWNER_LINE_CAP) {
  char out[OWNER_LINE_CAP + 8];
  tidyLine(in, std::strlen(in) + 1, out, outCap);
  return out;
}

void setField(char (&field)[OWNER_LINE_CAP], const char* text) {
  std::strncpy(field, text, sizeof(field) - 1);
  field[sizeof(field) - 1] = '\0';
}

CardContext ownerContext(const char* name, const char* c1, const char* c2) {
  CardContext ctx = preview::sampleContext();
  setField(ctx.settings.ownerName, name);
  setField(ctx.settings.ownerContact1, c1);
  setField(ctx.settings.ownerContact2, c2);
  return ctx;
}

// Logical (portrait) pixel of the framebuffer, as writeFramePng reads it.
bool inkAt(const int x, const int y) {
  const uint8_t* fb = display.getFrameBuffer();
  const int px = y;
  const int py = HalDisplay::DISPLAY_HEIGHT - 1 - x;
  return ((fb[py * HalDisplay::DISPLAY_WIDTH_BYTES + px / 8] >> (7 - (px & 7))) & 1) == 0;
}

// No ink in the side and top margins outside the frame, or between the frame and the footer:
// nothing the owner typed may spill out of the card.
void expectInsideFrame(const std::string& what) {
  GfxRenderer& r = preview::renderer();
  const int w = r.getScreenWidth();
  const int frameBottom = r.getScreenHeight() - FOOTER_HEIGHT - 8;
  for (int y = 0; y < frameBottom; y++) {
    for (int x = 0; x < 16; x++) {
      ASSERT_FALSE(inkAt(x, y)) << what << " left margin at " << x << "," << y;
      ASSERT_FALSE(inkAt(w - 1 - x, y)) << what << " right margin at " << w - 1 - x << "," << y;
    }
  }
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < 16; y++) ASSERT_FALSE(inkAt(x, y)) << what << " top margin at " << x << "," << y;
    for (int y = frameBottom + 1; y < frameBottom + 7; y++) {
      ASSERT_FALSE(inkAt(x, y)) << what << " below the frame at " << x << "," << y;
    }
  }
}

}  // namespace

TEST(SleepCardOwner, TidyLine) {
  EXPECT_EQ(tidy(""), "");
  EXPECT_EQ(tidy("   \t "), "");
  EXPECT_EQ(tidy("  Sam   Example  "), "Sam Example");
  EXPECT_EQ(tidy("a\tb\nc\x7F"
                 "d"),
            "a b c d");
  EXPECT_EQ(tidy("Zoë Ångström"), "Zoë Ångström");
  // Cut to the output buffer, never inside a UTF-8 sequence, never with a trailing space.
  EXPECT_EQ(tidy("abcdë", 6), "abcd");  // "abcd" + 2-byte ë needs 7 bytes with the NUL
  EXPECT_EQ(tidy("abc  def", 5), "abc");
  EXPECT_EQ(tidy("€€€", 7), "€€");  // 3-byte characters: 6 bytes fit, the 3rd would be cut
}

TEST(SleepCardOwner, TidyLineReadsAtMostInCap) {
  char unterminated[OWNER_LINE_CAP];
  std::memset(unterminated, 'x', sizeof(unterminated));
  char out[OWNER_LINE_CAP];
  tidyLine(unterminated, sizeof(unterminated), out, sizeof(out));
  EXPECT_EQ(std::strlen(out), sizeof(out) - 1);
  tidyLine(nullptr, 4, out, sizeof(out));
  EXPECT_STREQ(out, "");
}

TEST(SleepCardOwner, CollectOwnerLines) {
  SleepCardSettings s;
  OwnerLines lines;
  EXPECT_FALSE(collectOwnerLines(s, lines));  // the defaults: all blank

  setField(s.ownerName, "   ");
  setField(s.ownerContact1, "\t");
  EXPECT_FALSE(collectOwnerLines(s, lines));

  setField(s.ownerName, "Sam Example");
  setField(s.ownerContact1, "");
  ASSERT_TRUE(collectOwnerLines(s, lines));
  EXPECT_STREQ(lines.headline, "Sam Example");
  EXPECT_EQ(lines.contactCount, 0);

  setField(s.ownerContact1, " 555-0100 ");
  setField(s.ownerContact2, "sam@example.com");
  ASSERT_TRUE(collectOwnerLines(s, lines));
  EXPECT_STREQ(lines.headline, "Sam Example");
  ASSERT_EQ(lines.contactCount, 2);
  EXPECT_STREQ(lines.contacts[0], "555-0100");
  EXPECT_STREQ(lines.contacts[1], "sam@example.com");

  // No name: the first contact takes its place.
  setField(s.ownerName, "");
  ASSERT_TRUE(collectOwnerLines(s, lines));
  EXPECT_STREQ(lines.headline, "555-0100");
  ASSERT_EQ(lines.contactCount, 1);
  EXPECT_STREQ(lines.contacts[0], "sam@example.com");

  // Only the second contact line.
  setField(s.ownerContact1, "");
  ASSERT_TRUE(collectOwnerLines(s, lines));
  EXPECT_STREQ(lines.headline, "sam@example.com");
  EXPECT_EQ(lines.contactCount, 0);

  // A blank first contact does not leave a gap.
  setField(s.ownerName, "Sam");
  ASSERT_TRUE(collectOwnerLines(s, lines));
  ASSERT_EQ(lines.contactCount, 1);
  EXPECT_STREQ(lines.contacts[0], "sam@example.com");
}

TEST(SleepCardOwner, HyphenBreaks) {
  char out[2 * OWNER_LINE_CAP];
  hyphenBreaks("Ann-Marie Smith-Jones", out, sizeof(out));
  EXPECT_STREQ(out, "Ann- Marie Smith- Jones");
  hyphenBreaks("Ann - Lee -x y-", out, sizeof(out));  // only hyphens joining two words
  EXPECT_STREQ(out, "Ann - Lee -x y-");
  hyphenBreaks("a-b", out, 4);  // cut to the buffer, still terminated
  EXPECT_STREQ(out, "a- ");
  hyphenBreaks(nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "");

  std::string line = "Ann- Marie Smith-";
  undoHyphenBreaks(line);
  EXPECT_EQ(line, "Ann-Marie Smith-");
  line = "Ann - Lee";
  undoHyphenBreaks(line);
  EXPECT_EQ(line, "Ann - Lee");
  line = "- x";
  undoHyphenBreaks(line);
  EXPECT_EQ(line, "- x");
}

TEST(SleepCardOwner, FitScale) {
  EXPECT_FLOAT_EQ(fitScale(100, 376, 1.25f, 2.0f), 2.0f);   // capped
  EXPECT_FLOAT_EQ(fitScale(200, 376, 1.25f, 2.0f), 1.75f);  // 1.88 -> 1.75
  EXPECT_FLOAT_EQ(fitScale(300, 376, 1.25f, 2.0f), 1.25f);  // exactly 1.253
  EXPECT_FLOAT_EQ(fitScale(301, 376, 1.25f, 2.0f), 0.0f);   // 1.249.. rounds down below min
  EXPECT_FLOAT_EQ(fitScale(310, 376, 1.25f, 2.0f), 0.0f);   // does not fit at 1.25
  EXPECT_FLOAT_EQ(fitScale(376, 376, 1.0f, 2.0f), 1.0f);
  EXPECT_FLOAT_EQ(fitScale(377, 376, 1.0f, 2.0f), 0.0f);
  EXPECT_FLOAT_EQ(fitScale(0, 376, 1.0f, 1.5f), 1.5f);
  EXPECT_FLOAT_EQ(fitScale(10, 0, 1.0f, 1.5f), 0.0f);
  // Whatever it returns fits.
  for (int width = 1; width < 800; width += 7) {
    const float s = fitScale(width, 376, 1.0f, 2.0f);
    if (s > 0) EXPECT_LE(width * s, 376.0f) << width;
  }
}

namespace {
// Is the mark in `bits` (size x size, stored like the icons: row = size - 1 - x, column = y,
// 0 = ink) inked at (x, y) as seen?
bool markInk(const uint8_t* bits, const int size, const int x, const int y) {
  const int rowBytes = (size + 7) / 8;
  return ((bits[(size - 1 - x) * rowBytes + (y >> 3)] >> (7 - (y & 7))) & 1) == 0;
}
double inkRatio(const uint8_t* bits, const int size) {
  int ink = 0;
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) ink += markInk(bits, size, x, y) ? 1 : 0;
  }
  return ink / static_cast<double>(size * size);
}
}  // namespace

TEST(SleepCardOwner, SmallMarkIsTheSameMark) {
  // The 80 px copy is drawn from the same geometry: the same share of ink, the folded corner
  // paper, the badge's left middle ink, and drawIcon (the card's) plots it where asked and nowhere else.
  EXPECT_NEAR(inkRatio(X4ProLogoSmall, X4PRO_LOGO_SMALL_SIZE), inkRatio(X4ProLogo, X4PRO_LOGO_SIZE), 0.03);
  EXPECT_FALSE(markInk(X4ProLogo, X4PRO_LOGO_SIZE, X4PRO_LOGO_SIZE - 2, 1));
  EXPECT_FALSE(markInk(X4ProLogoSmall, X4PRO_LOGO_SMALL_SIZE, X4PRO_LOGO_SMALL_SIZE - 2, 1));
  EXPECT_TRUE(markInk(X4ProLogo, X4PRO_LOGO_SIZE, 6, X4PRO_LOGO_SIZE / 2));
  EXPECT_TRUE(markInk(X4ProLogoSmall, X4PRO_LOGO_SMALL_SIZE, 3, X4PRO_LOGO_SMALL_SIZE / 2));

  GfxRenderer& r = preview::renderer();
  constexpr int x = 40, y = 40, size = X4PRO_LOGO_SMALL_SIZE;
  r.clearScreen();
  r.drawIcon(X4ProLogoSmall, x, y, size);
  int mismatch = 0;
  for (int dy = 0; dy < size; dy++) {
    for (int dx = 0; dx < size; dx++) mismatch += inkAt(x + dx, y + dy) != markInk(X4ProLogoSmall, size, dx, dy);
  }
  EXPECT_EQ(mismatch, 0);
  for (int dx = -2; dx < size + 2; dx++) {
    EXPECT_FALSE(inkAt(x + dx, y - 1));
    EXPECT_FALSE(inkAt(x + dx, y + size));
  }
}

TEST(SleepCardOwner, DeclinesWhenBlank) {
  bool declined = false;
  preview::renderCardPng(CardId::Owner, preview::sampleContext(), "owner_blank", &declined);
  EXPECT_TRUE(declined);
  preview::renderCardPng(CardId::Owner, ownerContext("  ", "\t", " "), "owner_blank", &declined);
  EXPECT_TRUE(declined);
}

// The card needs no clock and no location.
TEST(SleepCardOwner, NeedsNoTimeOrLocation) {
  CardContext ctx = ownerContext("Sam Example", "555-0100", "");
  ctx.timeValid = false;
  ctx.location.valid = false;
  bool declined = true;
  preview::renderCardPng(CardId::Owner, ctx, "owner_notime", &declined);
  EXPECT_FALSE(declined);
}

struct Variant {
  const char* file;
  const char* name;
  const char* c1;
  const char* c2;
};

TEST(SleepCardOwner, RendersWithinBudgetAndInsideTheFrame) {
  const Variant variants[] = {
      {"owner", "Sam Example", "555-0100", "sam@example.com"},
      {"owner_name_only", "Sam Example", "", ""},
      {"owner_contact_only", "", "+1 555-0100", ""},
      {"owner_hyphen", "Ann-Marie Wolkenstein-Fairweather", "555-0100", ""},
      {"owner_long", "Maximiliana Wolkenstein-Fairweather", "maximiliana.wolkenstein@example.com",
       "c/o Example Library, 1234 Long Street, Anytown"},
      {"owner_no_spaces", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
       "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM"},
  };
  for (const auto& v : variants) {
    bool declined = true;
    const double ms = preview::renderCardPng(CardId::Owner, ownerContext(v.name, v.c1, v.c2), v.file, &declined);
    std::printf("  %-20s drawn in %.2f ms -> build/cards/%s.png\n", v.file, ms, v.file);
    EXPECT_FALSE(declined) << v.file;
    EXPECT_LT(ms, BUDGET_MS) << v.file;
    expectInsideFrame(v.file);
  }
}
