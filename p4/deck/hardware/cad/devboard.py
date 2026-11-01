# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""devboard.py: the Waveshare ESP32-P4-Module-DEV-KIT as a stand-in, plus where it sits.

Raspberry Pi B+ footprint, from Waveshare's outline drawing: 85 x 56 mm, four M2.5 holes
58 x 49 mm apart, 3.5 mm in from the edges. Ethernet and two stacked pairs of USB-A on
the right short edge; USB-C ("USB to UART" and "USB") on the bottom long edge; 40-pin
header along the top. Component heights are typical part heights, not measured.

Board frame (as the drawing is printed): u along the long edge (0 = left, 85 = port end),
y = -w (w measured down from the header edge), z = 0 on the board's underside.
In the deck it sits back-right, rotated so the port end faces the rear wall and the
USB-C edge faces the right-hand wall.
"""
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)
L_, W_, T_ = 85.0, 56.0, 1.6
HOLES = [(3.5, 3.5), (61.5, 3.5), (3.5, 52.5), (61.5, 52.5)]   # (u, w)
HOLE_D = 2.75
USBA = [(8.95, 13.1, 15.6), (26.95, 13.1, 15.6)]   # w centre, width, height: dual-stack
USBA_DEPTH, PORT_OVERHANG = 17.0, 2.5
RJ45 = (45.82, 16.0, 13.5, 21.0)                   # w centre, width, height, depth
USBC = [11.1, 26.5]                                # u centres (USB to UART, USB)
USBC_SIZE = (8.94, 7.3, 3.2)                       # along u, depth, height
USBC_OVERHANG = 1.0

def _at(u, w, z=T_):
    return Pos(u, -w, z)

def board():
    return _at(L_ / 2, W_ / 2, 0) * Box(L_, W_, T_, align=C) - [
        _at(u, w, -1) * Cylinder(HOLE_D / 2, T_ + 2, align=C) for u, w in HOLES]

def ports():
    """The parts that reach a wall, as one list of (name, solid)."""
    out = []
    for i, (w, wd, h) in enumerate(USBA):
        out.append((f"usb_a{i}", _at(L_ + PORT_OVERHANG - USBA_DEPTH / 2, w) * Box(USBA_DEPTH, wd, h, align=C)))
    w, wd, h, dp = RJ45
    out.append(("rj45", _at(L_ + PORT_OVERHANG - dp / 2, w) * Box(dp, wd, h, align=C)))
    for i, u in enumerate(USBC):
        lu, dp, h = USBC_SIZE
        out.append((f"usb_c{i}", _at(u, W_ + USBC_OVERHANG - dp / 2) * Box(lu, dp, h, align=C)))
    return out

def parts():
    """Everything else on top worth modelling for clearance."""
    return [("header40", _at(32.5, 3.5) * Box(51.0, 5.0, 8.5, align=C)),
            ("module", _at(28.5, 24.7) * Box(26.0, 26.6, 3.3, align=C))]

# Placement in the deck (world), derived from the walls
STANDOFF = 3.0                                      # printed on the base plate
X0 = M.W - 7.5 - 4.5 - 0.5 - W_                    # deck x of the header edge (w = 0): clear of the
                                                    # back-right base boss (inset 7.5, r 4.5)
YB = M.D - M.WALL - 1.2 - L_                        # deck y of the left end (u = 0); ports end ~1 mm inside the back face
Z0 = M.BASE_T + STANDOFF                            # board underside
LOC = Pos(X0, YB, Z0) * Rot(0, 0, 90)               # board frame -> deck: (u, -w) -> (X0 + w, YB + u)

def deck_xy(u, w):
    return (X0 + w, YB + u)

MATES = {"seat": Pos(0, 0, 0)}
