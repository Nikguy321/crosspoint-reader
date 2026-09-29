#pragma once

#include <EpdFontFamily.h>

#include <cstdint>

#include "SleepCard.h"

class GfxRenderer;
struct CardDigitFont;  // src/images/CardDigits.h

// Drawing helpers the cards share, so no card invents its own circle. All
// coordinates are logical (portrait 480x800); black = true draws ink. Every
// helper clips to the screen through GfxRenderer::drawPixel/fillRect.
namespace sleepcards::draw {

// ---- shapes -------------------------------------------------------------------------------------
void fillCircle(GfxRenderer& r, int cx, int cy, int radius, bool black = true);
// A ring `thickness` px wide whose outer edge is `radius`.
void drawCircle(GfxRenderer& r, int cx, int cy, int radius, int thickness = 1, bool black = true);
void fillEllipse(GfxRenderer& r, int cx, int cy, int rx, int ry, bool black = true);
void drawEllipse(GfxRenderer& r, int cx, int cy, int rx, int ry, int thickness = 1, bool black = true);
// Dashed horizontal / vertical lines (dash and gap in px), e.g. a horizon or a grid.
void drawDashedHLine(GfxRenderer& r, int x0, int x1, int y, int dash = 4, int gap = 4, bool black = true);
void drawDashedVLine(GfxRenderer& r, int x, int y0, int y1, int dash = 4, int gap = 4, bool black = true);

// ---- grey on a 1-bit panel: ordered dither ----------------------------------------------------
// level 0 = white .. 16 = black, a 4x4 Bayer pattern anchored to the screen (adjacent fills tile).
constexpr uint8_t DITHER_LEVELS = 16;
bool ditherInk(int x, int y, uint8_t level);
void fillRectDithered(GfxRenderer& r, int x, int y, int w, int h, uint8_t level);
void fillCircleDithered(GfxRenderer& r, int cx, int cy, int radius, uint8_t level);

// ---- the moon ---------------------------------------------------------------------------------
// The lit part of a moon disc of `radius` on row dy (-radius..radius) as x offsets from the
// centre, inclusive: [litLeft, litRight]; litLeft > litRight = none lit on that row. illum is the
// illuminated fraction 0..1; waxing = lit on the right as seen from the northern hemisphere
// (southern = true mirrors it). Pure; the terminator is the ellipse x = (1 - 2 illum) * half-width.
void moonLitSpan(double illum, bool waxing, bool southern, int radius, int dy, int& litLeft, int& litRight);
// A moon as seen: lit part white, dark part dithered at darkLevel, outline ring. Its disc inside
// the ring keeps its tones on a dark card (keepTonesDisc).
void drawMoon(GfxRenderer& r, int cx, int cy, int radius, double illum, bool waxing, bool southern = false,
              uint8_t darkLevel = 11);

// ---- dark cards ---------------------------------------------------------------------------------
// A dark card (CardContext::dark) is drawn as usual and then inverted whole: white on black. A
// picture whose tones mean something - the moon (its lit part is the bright part; a new moon must
// not read as full) and a book cover (no negatives) - registers its area while it draws and is
// inverted back, so it shows as it is. renderCard() clears the list before each card.
void clearKeptTones();
void keepTonesRect(int x, int y, int w, int h);
// The pixels of fillCircle(cx, cy, radius).
void keepTonesDisc(int cx, int cy, int radius);
// Invert the frame, then invert every kept area back.
void invertKeepingTones(GfxRenderer& r);

// ---- bars ---------------------------------------------------------------------------------------
// Outline box with the first `fraction` (0..1, clamped) filled.
void drawProgressBar(GfxRenderer& r, int x, int y, int w, int h, float fraction);

// ---- text ---------------------------------------------------------------------------------------
// Text magnified from a built-in font's glyphs (anti-aliased glyphs are resampled bilinearly and
// thresholded, so edges stay smooth at 2-4x). y is the top of the line box, like drawText. The
// widest built-in is NOTOSANS_18_FONT_ID (ascender 41 px): scale 3 gives ~120 px digits.
void drawTextScaled(GfxRenderer& r, int fontId, int x, int y, const char* text, float scale, bool black = true,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR);
int textWidthScaled(const GfxRenderer& r, int fontId, const char* text, float scale,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR);
void drawCenteredTextScaled(GfxRenderer& r, int fontId, int cx, int y, const char* text, float scale, bool black = true,
                            EpdFontFamily::Style style = EpdFontFamily::REGULAR);

// Digits drawn from a pre-drawn set (src/images/CardDigits.h), for numerals larger than any
// built-in font; characters other than 0-9 are skipped. top = the digits' top. The width is
// the ink's, first digit's left edge to the last one's right edge.
int digitsWidth(const CardDigitFont& font, const char* digits);
void drawDigits(GfxRenderer& r, const CardDigitFont& font, int x, int top, const char* digits, bool black = true);
void drawCenteredDigits(GfxRenderer& r, const CardDigitFont& font, int cx, int top, const char* digits,
                        bool black = true);

// drawText centred on cx (not on the screen) / right-aligned to `right`.
void drawTextCenteredAt(GfxRenderer& r, int fontId, int cx, int y, const char* text, bool black = true,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR);
void drawTextRight(GfxRenderer& r, int fontId, int right, int y, const char* text, bool black = true,
                   EpdFontFamily::Style style = EpdFontFamily::REGULAR);

enum class Align : uint8_t { Left, Center, Right };
// Word-wrapped paragraph in [x, x + width), at most maxLines lines (the last one ends in an
// ellipsis when the text is longer). lineHeight 0 = the font's own. Returns the height used.
int drawWrapped(GfxRenderer& r, int fontId, int x, int y, int width, const char* text, int maxLines,
                Align align = Align::Left, EpdFontFamily::Style style = EpdFontFamily::REGULAR, int lineHeight = 0);

// ---- the shared frame ---------------------------------------------------------------------------
// A battery outline w x h px (a nub on the right) filled to percent (0..100), top-left at (x, y).
void drawBatteryIcon(GfxRenderer& r, int x, int y, int w, int h, int percent);

// The footer every card gets (renderCard() draws it): "Asleep since 21:04 . [battery] 88%"
// centred in the bottom FOOTER_HEIGHT px over a hairline; parts that are unknown are left out.
void drawSleepFooter(const CardContext& ctx, GfxRenderer& r);

}  // namespace sleepcards::draw
