#!/usr/bin/env python3
"""Write cover_thumb_300.bmp: a stand-in book cover as the reader stores its home-screen
thumbnails (1-bit BMP, error-diffusion dithered, 0.6 x 300 px), so the host preview draws a
real dithered thumbnail through the card's own cover path. Original artwork: a grey sea
gradient, a moon and the sample book's title in the repository's Noto Sans.

    python3 test/sleep_card_preview/fixtures/make_cover_thumb.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
FONT = os.path.join(ROOT, "lib", "EpdFont", "builtinFonts", "source", "NotoSans", "NotoSans-Bold.ttf")
W, H = 180, 300

img = Image.new("L", (W, H), 255)
d = ImageDraw.Draw(img)
for y in range(H):  # sky to sea: light at the top, dark at the bottom
    d.line([(0, y), (W, y)], fill=int(235 - 170 * y / H))
d.ellipse([112, 34, 150, 72], fill=250)                       # the moon
d.polygon([(0, 210), (60, 196), (120, 214), (180, 200), (180, 300), (0, 300)], fill=40)  # swell
d.rectangle([10, 118, W - 10, 176], fill=255)
title = ImageFont.truetype(FONT, 24)
author = ImageFont.truetype(FONT, 13)
d.text((W / 2, 136), "MOBY-DICK", font=title, fill=0, anchor="mm")
d.text((W / 2, 162), "HERMAN MELVILLE", font=author, fill=40, anchor="mm")
img.convert("1").save(os.path.join(HERE, "cover_thumb_300.bmp"))
print("wrote cover_thumb_300.bmp")
