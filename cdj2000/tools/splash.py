#!/usr/bin/env python3
"""The boot splash for the CDJ-2000's screen: audiodestrukt's transistor mark
(audiodestrukt.com's favicon, lime on near-black) over "audiodestrukt" and
"FREEDJ CDJ-2000", a lime rule and the build stamp, laid out like the P4
deck's splash.  480 x 255 like the display processor's frame; everything
inside the panel's 234 visible lines.

  python3 tools/splash.py <out.rgb555> [<out.png>]   (the raw frame: RGB555, little-endian, row after row)
"""
import datetime, os, struct, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

W, H, VISIBLE = 480, 255, 234
BG, LIME, TEXT, DIM = (0x0d, 0x0d, 0x0d), (0xc8, 0xff, 0x00), (0xf0, 0xf0, 0xf0), (0x8a, 0x8a, 0x8a)
FONTS = "/usr/share/fonts/truetype/dejavu/"


def mark(d, x0, y0, s):
    """The transistor mark from the favicon's 32-unit grid, at scale s."""
    def P(x, y): return (x0 + x * s, y0 + y * s)
    def line(a, b, w):
        d.line([P(*a), P(*b)], fill=LIME, width=max(1, round(w * s)))
        r = w * s / 2
        for x, y in (P(*a), P(*b)):
            d.ellipse([x - r, y - r, x + r, y + r], fill=LIME)       # round caps
    line((12, 7), (12, 25), 2.5)     # base bar
    line((2, 16), (12, 16), 2)       # base lead
    line((12, 10), (25, 4), 2)       # collector
    line((25, 2), (25, 6), 2)
    line((12, 22), (25, 28), 2)      # emitter
    line((25, 26), (25, 30), 2)
    d.polygon([P(22.5, 26.6), P(17.8, 26.8), P(19.2, 23.2)], fill=LIME)   # emitter arrow


def stamp():
    try:
        rev = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                             cwd=os.path.dirname(os.path.abspath(__file__))).stdout.strip()
    except OSError:
        rev = ""
    day = datetime.date.today().isoformat()
    return f"build {day}" + (f"  {rev}" if rev else "")


def render():
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    s = 2.0
    mark(d, (W - 32 * s) / 2, 22, s)
    def centred(text, font, y, fill):
        w = d.textlength(text, font=font)
        d.text(((W - w) / 2, y), text, font=font, fill=fill)
    centred("audiodestrukt", ImageFont.truetype(FONTS + "DejaVuSans.ttf", 18), 98, DIM)
    centred("FREEDJ CDJ-2000", ImageFont.truetype(FONTS + "DejaVuSans-Bold.ttf", 34), 124, TEXT)
    d.rectangle([(W - 160) // 2, 176, (W + 160) // 2 - 1, 178], fill=LIME)
    centred(stamp(), ImageFont.truetype(FONTS + "DejaVuSans.ttf", 11), 190, DIM)
    return img


def rgb555(img):
    out = bytearray()
    raw = img.convert("RGB").tobytes()
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        out += struct.pack("<H", (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3))
    return bytes(out)


if __name__ == "__main__":
    img = render()
    open(sys.argv[1], "wb").write(rgb555(img))
    if len(sys.argv) > 2:
        img.save(sys.argv[2])
