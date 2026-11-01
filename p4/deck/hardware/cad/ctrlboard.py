# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""ctrlboard.py: the control board under CUE / PLAY, as an outline for the PCB layout and
a stand-in for clearance checks.

Top side (up, toward the top plate): two 12 x 12 tact switches right under the caps, and
four small LEDs under each light ring. Bottom side (down, toward the base): the I2C chip
and three JST-XH connectors, top entry, so the plugs go in from below: the pitch pot
(3 pins), the AS5600 jog encoder (4) and the dev board (5: 3V3, GND, SDA, SCL, INT).
Three M2.5 thread-forming screws from below into bosses that come down from the top.

World frame (deck): the board's top face is at z = CTRL_Z.
python3 ctrlboard.py -> build/ctrl_board_outline.dxf, prints the hole and part positions
"""
import math, os
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)
HOLE_D = 2.7                       # M2.5 clearance
LED = (2.0, 1.25, 0.8)             # 0805, on the ring's centre circle
LED_R = M.SKIRT_D / 2 - M.SKIRT_WALL / 2           # under the ring's skirt wall
LED_ANGLES = (45, 135, 225, 315)
XH_PITCH, XH_W, XH_H = 2.5, 5.75, 7.0
# bottom side, along y, near the right edge: (name, pins, centre x, centre y)
JST = [("j_pot", 3, 38.0, 20.0), ("j_enc", 4, 38.0, 32.0), ("j_host", 5, 38.0, 47.0)]
IC = ("u1", 26.0, 28.0, (5.0, 5.0, 1.0))   # placeholder: the I2C chip, bottom side

def outline():
    x0, y0, x1, y1 = M.CTRL_XY
    return Pos((x0 + x1) / 2, (y0 + y1) / 2) * RectangleRounded(x1 - x0, y1 - y0, M.CTRL_CORNER_R)

def board():
    b = Pos(0, 0, M.CTRL_Z - M.CTRL_T) * extrude(outline(), M.CTRL_T)
    return b - [Pos(x, y, M.CTRL_Z - M.CTRL_T - 1) * Cylinder(HOLE_D / 2, M.CTRL_T + 2, align=C) for x, y in M.CTRL_HOLES]

def leds(c):
    return [Pos(*c, M.CTRL_Z) * Rot(0, 0, a) * Pos(LED_R, 0, 0) * Rot(0, 0, 90) * Box(*LED, align=C)
            for a in LED_ANGLES]

def tact(c):
    bx, by, bh, top = M.TACT
    body = Pos(*c, M.CTRL_Z) * Box(bx, by, bh, align=C)
    act = Pos(*c, M.CTRL_Z + bh) * Cylinder(1.75, top - bh, align=C)
    return body.fuse(act)

def parts():
    """(name, solid, look) for the render and clearance checks."""
    out = []
    for n, c in (("play", M.PLAY_C), ("cue", M.CUE_C)):
        out.append((f"{n}_switch", tact(c), "chip"))
        for i, l in enumerate(leds(c)):
            out.append((f"{n}_led{i}", l, "white"))
    zb = M.CTRL_Z - M.CTRL_T
    for n, pins, x, y in JST:
        out.append((n, Pos(x, y, zb - XH_H) * Box(XH_W, pins * XH_PITCH + 2.4, XH_H, align=C), "white"))
    n, x, y, (a, b, h) = IC
    out.append((n, Pos(x, y, zb - h) * Box(a, b, h, align=C), "chip"))
    return out

if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    x0, y0, x1, y1 = M.CTRL_XY
    print(f"config {M.CONFIG}: board {x1 - x0:.1f} x {y1 - y0:.1f} mm, corner r {M.CTRL_CORNER_R}, "
          f"top face at z {M.CTRL_Z:.1f}, {M.CTRL_T} thick")
    print("  coordinates below are relative to the board's lower-left corner (deck x0, y0 =", x0, y0, ")")
    for x, y in M.CTRL_HOLES:
        print(f"  hole M2.5 ({HOLE_D} mm) at {x - x0:.2f}, {y - y0:.2f}")
    for n, c in (("PLAY", M.PLAY_C), ("CUE", M.CUE_C)):
        print(f"  {n} switch centre {c[0] - x0:.2f}, {c[1] - y0:.2f}; 4 LEDs (tangential) on r {LED_R:.2f} at 45/135/225/315 deg;"
              f" ring skirt sits on r {M.SKIRT_D / 2 - M.SKIRT_WALL:.1f}-{M.SKIRT_D / 2:.1f}: keep it clear")
    for n, pins, x, y in JST:
        print(f"  {n}: JST-XH {pins}-pin (bottom side), centre {x - x0:.2f}, {y - y0:.2f}, along y")
    try:
        exporter = ExportDXF(); exporter.add_shape(Pos(-x0, -y0) * outline())
        for x, y in M.CTRL_HOLES:
            exporter.add_shape(Pos(x - x0, y - y0) * Circle(HOLE_D / 2))
        exporter.write("build/ctrl_board_outline.dxf"); print("wrote build/ctrl_board_outline.dxf")
    except Exception as e:
        print("  (DXF skipped:", e, ")")
