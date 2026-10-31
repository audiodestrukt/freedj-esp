"""screen_mock.py: a 480x320 picture of the ESP Deck play screen, for the render only.

Laid out from the constants in main/ui.cpp (name, time, overview y=94, phase meter
y=120, colour waveform y=156, key row y=272). The waveform is synthetic, not a track.
python3 screen_mock.py -> build/screen.png
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFont

W, H, M = 480, 320, 10
BG, PANEL, TEXT, DIM = "#0B0D10", "#1A1E24", "#E8EAED", "#8A9099"
GREEN, ORANGE, BLUE, DIMBLUE = "#2FBF71", "#F29D38", "#3D7EFF", "#243A5C"
KEYFACE, KEYDIM, KEYFAINT = "#3A4658", "#2A3340", "#4A525C"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
f = lambda s: ImageFont.truetype(FONT, s)

def envelope(t):
    # a fake house track: kick every beat, bass, breakdown in the middle
    beat = (t * 2.1) % 1.0
    e = 0.45 + 0.4 * math.exp(-beat * 9) + 0.12 * math.sin(t * 0.7)
    if 0.42 < t / 200 < 0.55: e *= 0.45
    return max(0.05, min(1.0, e + random.uniform(-0.06, 0.06)))

def main():
    random.seed(7)
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    d.text((M, 8), "Night Bus (Extended)", font=f(18), fill=TEXT)
    d.rounded_rectangle((W - M - 62, 8, W - M, 28), 4, fill=GREEN); d.text((W - M - 56, 10), "PLAY", font=f(14), fill=BG)
    for x0, t, c in ((W - M - 112, "SYNC", BLUE), (W - M - 156, "KEY", DIMBLUE), (W - M - 186, "Q", ORANGE)):
        d.rounded_rectangle((x0, 8, x0 + len(t) * 10 + 8, 28), 4, outline=c, width=2); d.text((x0 + 5, 10), t, font=f(13), fill=c)
    d.text((M, 38), "01:42.6", font=f(38), fill=TEXT)
    d.text((182, 54), "-2:31", font=f(22), fill=DIM)
    d.text((W - M - 92, 42), "+1.25%", font=f(22), fill=TEXT)
    d.text((W - M - 160, 50), "±10", font=f(14), fill=DIM)
    d.text((W - M - 118, 74), "126.0 BPM", font=f(14), fill=DIM)
    # overview
    ox, oy, ow, oh = M, 94, W - 2 * M, 22
    d.rectangle((ox, oy, ox + ow, oy + oh), fill=PANEL); play = int(ow * 0.4)
    for x in range(ow):
        a = envelope(x / ow * 200) * oh / 2
        c = "#6F8FCF" if x < play else "#9FC3FF"
        d.line((ox + x, oy + oh / 2 - a, ox + x, oy + oh / 2 + a), fill=c)
    d.line((ox + play, oy, ox + play, oy + oh), fill="#FFFFFF", width=2)
    d.line((ox + 30, oy, ox + 30, oy + oh), fill=ORANGE)
    # phase meter: 2x4 boxes + tick lane
    my = 120
    for i in range(8):
        bx, by = M + (i % 4) * 19, my + (0 if i < 4 else 16)
        lit = (i == 1) or (i == 4 + 2)
        d.rectangle((bx, by, bx + 16, by + 13), fill=(ORANGE if i >= 4 and lit else BLUE) if lit else PANEL)
    tx = 92; d.rectangle((tx, my, W - 1 - 150, my + 31), fill=PANEL)
    for row, col in ((my + 9, ORANGE), (my + 25, BLUE)):
        for k in range(9):
            x = tx + 10 + k * 30; d.rectangle((x, row - 4, x + 2, row + 4), fill=col)
    d.line((tx + 119, my, tx + 119, my + 31), fill="#FFFFFF")
    d.text((W - 140, my + 2), "MEM  4.2", font=f(12), fill=DIM); d.text((W - 140, my + 17), "HOT  8.1", font=f(12), fill=GREEN)
    # colour waveform, centred playhead
    wy, wh = 156, 80; mid = wy + wh / 2
    for x in range(W):
        t = 90 + (x - W / 2) / 85.0
        a = envelope(t * 4.3) * wh / 2 * 0.95
        low = 0.5 + 0.5 * math.exp(-((t * 2.1) % 1) * 7)
        r, g, b = int(40 + 200 * low), int(60 + 120 * (1 - low)), int(220 - 120 * low)
        if x < W / 2: r, g, b = r * 6 // 10, g * 6 // 10, b * 6 // 10
        d.line((x, mid - a, x, mid + a), fill=(r, g, b))
        d.line((x, mid - a * 0.45, x, mid + a * 0.45), fill=(min(255, r + 60), min(255, g + 60), min(255, b + 60)))
    for k in range(-6, 7):
        x = W / 2 + k * 85 / 2.1 - 20
        d.line((x, wy, x, wy + (12 if k % 4 else 22)), fill="#E0453A" if k % 4 == 0 else "#C8CCD2")
    d.line((W / 2, wy, W / 2, wy + wh), fill="#FFFFFF", width=2)
    d.text((M, 244), "Pro DJ Link: 2 players", font=f(12), fill=DIM)
    # key row
    n, gap = 5, 2; kw = (W - 2 * M - (n - 1) * gap) // n
    for i, (t, cap, on) in enumerate((("BROWSE", "SEARCH", 1), ("TAG LIST", "", 0), ("INFO", "LINK INFO", 0), ("MENU", "UTILITY", 0), ("PERFORM", "", 1))):
        x = M + i * (kw + gap); d.rounded_rectangle((x, 272, x + kw, 318), 5, fill=KEYFACE if on else KEYDIM)
        tw = d.textlength(t, font=f(13)); d.text((x + (kw - tw) / 2, 280 if cap else 287), t, font=f(13), fill=TEXT if on else KEYFAINT)
        if cap:
            cw = d.textlength(cap, font=f(10)); d.text((x + (kw - cw) / 2, 299), cap, font=f(10), fill=DIM if on else KEYFAINT)
    os.makedirs("build", exist_ok=True); im.save("build/screen.png"); print("wrote build/screen.png")

if __name__ == "__main__":
    main()
