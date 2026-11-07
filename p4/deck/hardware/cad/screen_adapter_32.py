# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""screen_adapter_32.py: holds a 3.2" screen in the 4" shell, to try screen sizes.

The 3.2" is the LCDwiki MSP3223 (sold as Hosyond 3.2" IPS capacitive): ILI9341V 240x320,
FT6336U touch, the same chips as the 2.8" MSP2834, so the 2.8" firmware build drives it.
lcdwiki.com/res/MSP3222_MSP3223/3.2inch_SPI_Module_MSP3223_Size.pdf. Along the 90.50 side,
from the end away from the header: holes 3.00 and 87.50, glass 6.29-84.19, active area
9.89-74.69, header row 88.50. Across: board 55.04, glass 55.24, holes 49.04, active area
48.60 (centred). Glass top -> board: 1.35 + 0.5 + 2.40 + 0.5 = 4.75.

The adapter takes the 4" module's place: a plate on the shell's four screen bosses
(M3 x 8 from below, into the bosses' pilots, as the 4" board was), and a block that fills
where the 4" glass sat, up to just under the top. Through the shell's 4" window you see the
block's face, with a window cut in it for the 3.2" active area. The 3.2" glass sits in a
pocket up from below, 1.2 mm under the face; its board is held by four M2.5 x 6
thread-forming screws up into the block. The header stays at the same end as the 4"'s.

The 3.2" active area sits 2.96 mm off the board centre, away from the header; the 4"'s sits
2.88 off its glass, the same way. So with both boards centred the active areas line up
within 0.1 mm, and the 3.2" is simply centred on the adapter.

Print face down (the face on the bed): every pocket opens upward, no supports.
Frame: x along the long side, header end at -x, z = 0 at the 4" board face (the bosses'
ends), +z up toward the top plate.
python3 screen_adapter_32.py -> build/screen_adapter_32.stl
"""
import os
from build123d import *
import machine_params as M

assert M.SCREEN == "4", "the adapter fits the 4\" shell (FREEDJ_SCREEN=4)"

C = (Align.CENTER, Align.CENTER, Align.MIN)
FIT = 0.2                         # clearance all round on pockets

# 3.2" module (MSP3223), centred on its board
PCB = (90.50, 55.04, 1.60)
GLASS = (77.90, 55.24)
ACTIVE = (64.80, 48.60)
AA_DX = 45.25 - (9.89 + 64.80 / 2)    # +2.96: active area toward +x (away from the header)
HOLES = (84.50, 49.04)                # 4 x 3.2
STACK = 1.35 + 0.5 + 2.40 + 0.5       # glass top -> board face
HEADER_X = -(PCB[0] / 2 - 2.00)       # pin row, 2.00 in from the header end
HEADER_SPAN = 13 * 2.54               # 14 pins, centred across

# Adapter
EAR_T = 2.5                       # plate under the bosses: M3 x 8 leaves 5.5 mm in the 6 mm pilots
TOP_GAP = 0.3                     # face below the top's underside, so the screws pull on the bosses
FACE_Z = M.STACK + M.GASKET - TOP_GAP # block face above the boss ends
SKIN = 1.2                        # face over the 3.2" glass
WINDOW_MARGIN = M.WINDOW_MARGIN
PILOT = (2.2, 4.5)                # M2.5 thread-forming: diameter, depth up from the 3.2" board face
M3_CLEAR = 3.4

GLASS_TOP = FACE_Z - SKIN
PCB_FACE = GLASS_TOP - STACK      # where the 3.2" board's top face presses


def adapter():
    # plate: the 4" board's outline, under the bosses
    plate = Pos(0, 0, -EAR_T) * Box(M.LCD_PCB[0], M.LCD_PCB[1], EAR_T, align=C)
    # block: the 4" glass's footprint, between the bosses, up to the face
    bw = M.GLASS[0] - 0.6             # 0.3 off the bosses' glass-side clearance
    block = Box(bw, M.LCD_PCB[1], FACE_Z, align=C)
    s = plate + block

    cuts = []
    # 3.2" board pocket, up from below to the board face
    cuts.append(Pos(0, 0, -EAR_T - 1) * Box(PCB[0] + 2 * FIT, PCB[1] + 2 * FIT, PCB_FACE + EAR_T + 1, align=C))
    # glass pocket, up to the skin
    gw = max(GLASS[1], PCB[1]) + 2 * FIT
    cuts.append(Pos(0, 0, -EAR_T - 1) * Box(GLASS[0] + 2 * FIT, gw, GLASS_TOP + EAR_T + 1, align=C))
    # window through the skin: the 3.2" active area + margin, off-centre like the active area
    ww, wh = (a + 2 * WINDOW_MARGIN for a in ACTIVE)
    cuts.append(Pos(AA_DX, 0, GLASS_TOP - 1) * Box(ww, wh, SKIN + 2, align=C))
    # header pins' solder joints on the board's top face
    cuts.append(Pos(HEADER_X, 0, PCB_FACE - 1) * Box(4.5, HEADER_SPAN + 4, 1 + 1.5, align=C))
    # M2.5 pilots for the 3.2" board
    for sx in (-1, 1):
        for sy in (-1, 1):
            cuts.append(Pos(sx * HOLES[0] / 2, sy * HOLES[1] / 2, PCB_FACE - 0.1) * Cylinder(PILOT[0] / 2, PILOT[1] + 0.1, align=C))
    # M3 clearance through the plate on the 4" hole pattern
    hx, hy = M.LCD_HOLES
    for sx in (-1, 1):
        for sy in (-1, 1):
            cuts.append(Pos(sx * hx / 2, sy * hy / 2, -EAR_T - 1) * Cylinder(M3_CLEAR / 2, EAR_T + 2, align=C))
    for c in cuts:
        s = s - c
    return s


def check():
    hx, hy = M.LCD_HOLES
    # the pilots must sit in solid between the glass pocket and the board pocket's edge
    wall_in = HOLES[0] / 2 - PILOT[0] / 2 - (GLASS[0] / 2 + FIT)
    wall_out = (PCB[0] / 2 + FIT) - (HOLES[0] / 2 + PILOT[0] / 2)
    assert wall_in >= 1.0, ("pilot breaks into the glass pocket", wall_in)
    # (outward the pilot is under the board pocket's ceiling, so wall_out is ceiling, not wall)
    assert PCB[0] / 2 + FIT < hx / 2 - M3_CLEAR / 2 - 0.5, "3.2\" board pocket reaches the M3 holes"
    assert PCB_FACE > -EAR_T + 0.5, ("3.2\" board face below the plate", PCB_FACE)
    assert abs(AA_DX - M.LCD_AA_DX) < 0.2, "active areas don't line up"
    print(f"face {FACE_Z:.2f} above the boss ends; 3.2\" glass top {GLASS_TOP:.2f}, board face {PCB_FACE:.2f}")
    print(f"3.2\" active area {AA_DX - M.LCD_AA_DX:+.2f} mm from the 4\" window centre")
    print(f"window {ACTIVE[0] + 2 * WINDOW_MARGIN:.1f} x {ACTIVE[1] + 2 * WINDOW_MARGIN:.1f} in the 4\"'s "
          f"{M.LCD_ACTIVE[0] + 2 * M.WINDOW_MARGIN:.1f} x {M.LCD_ACTIVE[1] + 2 * M.WINDOW_MARGIN:.1f}")
    print(f"3.2\" sits {STACK + SKIN + TOP_GAP - (M.STACK + M.GASKET):+.2f} mm deeper than the 4\" glass did; "
          f"header pins {PCB_FACE:+.2f} vs the 4\"'s 0")
    print(f"pilot walls: {wall_in:.2f} to the glass pocket")


if __name__ == "__main__":
    check()
    os.makedirs("build", exist_ok=True)
    s = adapter()
    assert s.is_valid, "invalid solid"
    print(f"volume {s.volume / 1000:.1f} cm3, {len(s.solids())} solid(s)")
    # face down for printing
    export_stl(Rot(180, 0, 0) * s, "build/screen_adapter_32.stl", tolerance=0.02, angular_tolerance=0.1)
    print("wrote build/screen_adapter_32.stl")
