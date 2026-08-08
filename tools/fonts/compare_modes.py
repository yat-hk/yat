#!/usr/bin/env python3
"""Throwaway visual comparison of the three 1-bit rasterization modes we could
ship, so the choice in gen_fonts.py is made by looking rather than guessing.

  mono      FT_LOAD_TARGET_MONO  (full hinting, B/W rasterizer)
  monolight FT_LOAD_TARGET_MONO + FT_LOAD_FORCE_AUTOHINT
  thresh    8-bit AA render, then >= 128 -> ink

Writes /tmp/fontcmp.png, 4x nearest-neighbour so pixels are inspectable.
"""
import freetype
from PIL import Image

TC = "src/NotoSansTC[wght].ttf"
LAT = "src/NotoSans-Regular.ttf"

SAMPLES = [
    ("觀塘 天氣 廿三度 微雨", TC),
    ("東鐵綫 紅磡 落馬洲 上水", TC),
    ("Kwun Tong 23° light rain", LAT),
    ("Observatory / upd 14:32", LAT),
]
SIZES = [16, 24, 32]
ZOOM = 4


def face(path, px):
    f = freetype.Face(path)
    if "wght" in path:
        f.set_var_design_coords([400])
    f.set_pixel_sizes(0, px)
    return f


def render(f, text, mode):
    flags = freetype.FT_LOAD_RENDER
    if mode == "mono":
        flags |= freetype.FT_LOAD_TARGET_MONO
    elif mode == "monolight":
        flags |= freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_FORCE_AUTOHINT
    cells = []
    for ch in text:
        f.load_char(ch, flags)
        g = f.glyph
        b = g.bitmap
        px = []
        for r in range(b.rows):
            row = []
            for c in range(b.width):
                if b.pixel_mode == 1:  # MONO
                    byte = b.buffer[r * b.pitch + (c >> 3)]
                    row.append(1 if byte & (0x80 >> (c & 7)) else 0)
                else:
                    row.append(1 if b.buffer[r * b.pitch + c] >= 128 else 0)
            px.append(row)
        cells.append((px, b.width, b.rows, g.bitmap_left, g.bitmap_top, g.advance.x >> 6))
    return cells


rows = []
for px in SIZES:
    for mode in ("mono", "monolight", "thresh"):
        for text, path in SAMPLES:
            f = face(path, px)
            cells = render(f, text, mode)
            asc = f.size.ascender >> 6
            h = (f.size.height >> 6) + 4
            w = sum(c[5] for c in cells) + 8
            img = Image.new("L", (w, h), 255)
            pen = 4
            for bm, bw, bh, bl, bt, adv in cells:
                for r in range(bh):
                    for c in range(bw):
                        if bm[r][c]:
                            x, y = pen + bl + c, asc - bt + r + 2
                            if 0 <= x < w and 0 <= y < h:
                                img.putpixel((x, y), 0)
                pen += adv
            rows.append((f"{px}px {mode}", img))

label_w = 120
tot_h = sum(r[1].height * ZOOM + 6 for r in rows) + 8
tot_w = label_w + max(r[1].width for r in rows) * ZOOM + 8
sheet = Image.new("L", (tot_w, tot_h), 245)
from PIL import ImageDraw
d = ImageDraw.Draw(sheet)
y = 4
for label, img in rows:
    big = img.resize((img.width * ZOOM, img.height * ZOOM), Image.NEAREST)
    sheet.paste(big, (label_w, y))
    d.text((4, y + 4), label, fill=0)
    y += big.height + 6
sheet.save("/tmp/fontcmp.png")
print("wrote /tmp/fontcmp.png", sheet.size)
