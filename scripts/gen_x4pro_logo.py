#!/usr/bin/env python3
"""Draw the X4 Pro mark and write it as a 1-bit drawImage header.

The mark is original geometry (no font, no third-party artwork): a black
rounded badge whose top-right corner is folded over like a dog-eared page,
a heavy geometric "X4" knocked out in white, and "PRO" in small stroked
capitals underneath. Everything is drawn at 8x and thresholded once, so the
1-bit result is crisp on the e-ink panel.

    python3 scripts/gen_x4pro_logo.py
    ./bin/clang-format-fix -g      # the header is checked in clang-formatted

Outputs (both checked in):
  src/images/X4ProLogo.png   the mark as seen on screen (160x160, portrait)
  src/images/X4ProLogo.h     the mark for GfxRenderer::drawImage at 160 px and,
                             drawn from the same geometry rather than shrunk
                             from the 160 px pixels, at 80 px (the Owner card).
                             Packed like Logo120.h: 1 bit per pixel, MSB first,
                             1 = white, 0 = ink, stored rotated 90 degrees
                             counter-clockwise (the panel's native landscape
                             orientation, as scripts/convert_icon.py does)
"""
import os

from PIL import Image, ImageDraw

SIZE = 160      # logical pixels; square, so drawImage's width/height cannot be swapped by mistake
SMALL_SIZE = 80  # the Owner card's copy
SCALE = 8       # supersampling factor
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNG_PATH = os.path.join(ROOT, "src", "images", "X4ProLogo.png")
HEADER_PATH = os.path.join(ROOT, "src", "images", "X4ProLogo.h")

INK = 0
PAPER = 255


def s(v):
    """Logical pixels -> supersampled pixels."""
    return int(round(v * SCALE))


def poly(draw, pts, fill):
    draw.polygon([(s(x), s(y)) for x, y in pts], fill=fill)


def stroke(draw, x0, y0, x1, y1, w, fill):
    """A straight stroke of width w with square ends, as a polygon."""
    dx, dy = x1 - x0, y1 - y0
    length = (dx * dx + dy * dy) ** 0.5
    nx, ny = -dy / length * w / 2, dx / length * w / 2
    poly(draw, [(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)], fill)


def draw_mark():
    img = Image.new("L", (s(SIZE), s(SIZE)), PAPER)
    d = ImageDraw.Draw(img)

    # Badge: a rounded square, top-right corner folded like a dog-eared page.
    d.rounded_rectangle([0, 0, s(SIZE) - 1, s(SIZE) - 1], radius=s(26), fill=INK)
    fold = 42
    right = SIZE
    poly(d, [(right - fold, 0), (right + 1, 0), (right + 1, fold)], PAPER)            # the torn-away corner
    poly(d, [(right - fold, 0), (right - fold, fold), (right, fold)], PAPER)           # the flap folded over
    stroke(d, right - fold, 0, right, fold, 3.2, INK)                                  # the fold line
    # The flap's own outline so it reads as paper lying on the badge.
    stroke(d, right - fold + 1.5, 0, right - fold + 1.5, fold - 1.5, 3, INK)
    stroke(d, right - fold, fold - 1.5, right, fold - 1.5, 3, INK)

    # "X": two heavy strokes crossing, cut flat top and bottom by the text band. The group
    # (X4 over PRO) sits a little above the badge's middle so it does not look bottom-heavy.
    top, bottom = 42, 106
    xl, xr = 16, 80
    w = 17
    band = Image.new("L", img.size, 0)
    bd = ImageDraw.Draw(band)
    stroke(bd, xl + 4, top - 6, xr - 4, bottom + 6, w, 255)
    stroke(bd, xr - 4, top - 6, xl + 4, bottom + 6, w, 255)
    bd.rectangle([s(xl - 10), s(top), s(xr + 10), s(bottom)], outline=None)
    clip = Image.new("L", img.size, 0)
    ImageDraw.Draw(clip).rectangle([0, s(top), s(SIZE), s(bottom)], fill=255)
    x_mask = Image.composite(band, Image.new("L", img.size, 0), clip)
    img.paste(PAPER, mask=x_mask)

    # "4": a stem, a crossbar, and a diagonal from the stem's top to the bar's left end.
    stem_l, stem_r = 121, 138
    bar_t, bar_b = 80, 95
    left = 86
    poly(d, [(stem_l, top), (stem_r, top), (stem_r, bottom), (stem_l, bottom)], PAPER)
    poly(d, [(left, bar_t), (147, bar_t), (147, bar_b), (left, bar_b)], PAPER)
    # The diagonal: its outer edge runs from the bar's left end to the stem's top-left corner, its inner
    # edge parallel to it, 19 px further right, which leaves a triangular counter above the bar.
    poly(d, [(left, bar_t), (stem_l, top), (stem_l + 19, top), (left + 19, bar_t)], PAPER)

    # "PRO": small stroked capitals centred under the monogram.
    sw = 4.2          # stroke width
    h = 17            # cap height
    y0 = 118
    gap = 7
    lw = 15           # letter width
    x0 = (SIZE - (3 * lw + 2 * gap)) / 2

    def bowl(x, y, width, height):
        # The P/R bowl: a stroked half-round on the stem's right.
        r = height / 2
        box = [s(x + width - 2 * r), s(y), s(x + width), s(y + height)]
        d.arc(box, start=-90, end=90, fill=PAPER, width=s(sw))
        d.rectangle([s(x), s(y), s(x + width - r), s(y + sw)], fill=PAPER)
        d.rectangle([s(x), s(y + height - sw), s(x + width - r), s(y + height)], fill=PAPER)

    # P
    px = x0
    d.rectangle([s(px), s(y0), s(px + sw), s(y0 + h)], fill=PAPER)
    bowl(px, y0, lw, 10.5)
    # R
    rx = x0 + lw + gap
    d.rectangle([s(rx), s(y0), s(rx + sw), s(y0 + h)], fill=PAPER)
    bowl(rx, y0, lw, 10.5)
    stroke(d, rx + 6, y0 + 9, rx + lw, y0 + h, sw + 0.4, PAPER)
    # O
    ox = x0 + 2 * (lw + gap)
    d.ellipse([s(ox), s(y0), s(ox + lw + 1), s(y0 + h)], outline=PAPER, width=s(sw))

    return img


def to_one_bit(master, size):
    small = master.resize((size, size), Image.LANCZOS)
    return small.point(lambda v: 255 if v >= 128 else 0, mode="1")


def packed_rows(mark):
    # drawImage blits in panel (landscape) orientation; see scripts/convert_icon.py.
    rotated = mark.convert("L").rotate(90, expand=True)
    width, height = rotated.size
    px = rotated.load()
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for b in range(8):
                if x + b < width and px[x + b, y] >= 128:
                    byte |= 1 << (7 - b)
            packed.append(byte)
    return packed


def array_lines(name, packed):
    # inline: one copy in flash however many files include the header.
    lines = [f"inline constexpr uint8_t {name}[] = {{"]
    for i in range(0, len(packed), 16):
        lines.append("    " + ", ".join(f"0x{v:02X}" for v in packed[i:i + 16]) + ",")
    lines.append("};")
    return lines


def header_text(mark, small):
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        "// Generated by scripts/gen_x4pro_logo.py - do not edit; change the script and rerun it.",
        f"// Image dimensions: {SIZE}x{SIZE} and {SMALL_SIZE}x{SMALL_SIZE}",
        f"inline constexpr int X4PRO_LOGO_SIZE = {SIZE};",
        f"inline constexpr int X4PRO_LOGO_SMALL_SIZE = {SMALL_SIZE};",
        "",
    ]
    lines += array_lines("X4ProLogo", packed_rows(mark))
    lines.append("")
    lines += array_lines("X4ProLogoSmall", packed_rows(small))
    return "\n".join(lines) + "\n"


def main():
    master = draw_mark()
    mark = to_one_bit(master, SIZE)
    small = to_one_bit(master, SMALL_SIZE)
    mark.save(PNG_PATH, optimize=True)
    with open(HEADER_PATH, "w") as f:
        f.write(header_text(mark, small))
    print(f"wrote {PNG_PATH} and {HEADER_PATH}")


if __name__ == "__main__":
    main()
