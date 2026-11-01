# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""jog.py: the jog module. Printed platter with its shaft printed in one piece, through two
608 bearings (pressed into the shell's boss); nothing holds it down yet, it sits on the
upper bearing. Steel inertia ring in the underside, aluminium touch plate on top, diametric
magnet glued into the shaft end over an AS5600 breakout on the base plate.

Print the platter upside down (shaft up): the shaft comes out round and needs no support;
only the 1.2 mm touch-plate recess needs support (build plate only).

Each part's local frame: axis = z, z = 0 at its own underside, so "seat" = Pos(0,0,0).
The assembly places everything off the shell's "jog" mate (the well floor at the axis).
python3 jog.py -> build/platter.stl, build/fit_gauge.stl + the stack numbers
(FREEDJ_CONFIG=flat -> build/platter_flat.stl: the shaft is 4 mm longer)
"""
import math, os
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)
KNURL_N, KNURL_D = 72, 2.4
TOUCH_T, TOUCH_RECESS = 1.5, 1.2
CAP_H = 2.5
B_ID, B_OD, B_W = M.B608
FLOOR = M.H - M.WELL_DEPTH

def platter():
    p = Cylinder(M.JOG_D / 2, M.PLATTER_H, align=C)
    try:
        p = fillet(p.edges().filter_by(lambda e: e.center().Z > M.PLATTER_H - 0.1), 1.2)
    except Exception as e:
        print("  (platter round skipped:", e, ")")
    cuts = [Pos(M.JOG_D / 2 * math.cos(a), M.JOG_D / 2 * math.sin(a), 1.5) *
            Cylinder(KNURL_D / 2, M.PLATTER_H, align=C)
            for a in (2 * math.pi * i / KNURL_N for i in range(KNURL_N))]
    cuts.append(Pos(0, 0, M.PLATTER_H - TOUCH_RECESS) * Cylinder(M.TOUCH_D / 2 + 0.2, 5, align=C))
    ri, ro, rh = M.INERTIA
    cuts.append(Pos(0, 0, -1) * (Cylinder(ro + 0.2, rh + 1, align=C) - Cylinder(ri - 0.2, rh + 1, align=C)))
    # inner-race spacer: only the hub touches the bearing, never the outer race
    hub = Pos(0, 0, -M.PLATTER_GAP) * Cylinder(6.0, M.PLATTER_GAP + 1, align=C)
    return p.fuse(hub, shaft()).cut(*cuts)

SHAFT_L = FLOOR + M.PLATTER_GAP - M.SHAFT_BOTTOM   # below the platter's underside
LEAD = M.SHAFT_LEAD

def shaft():
    """In the platter's frame (z = 0 at its underside): down through both bearings,
    ending just under the lower one, with a pocket in the tip for the magnet."""
    r = M.SHAFT_D / 2
    z0 = -SHAFT_L
    s = Pos(0, 0, z0 + LEAD) * Cylinder(r, SHAFT_L - LEAD + 1, align=C)
    s = s.fuse(Pos(0, 0, z0) * Cone(r - LEAD, r, LEAD, align=C))
    pocket = Pos(0, 0, z0 - 0.01) * Cylinder(M.MAGNET[0] / 2 + 0.08, M.MAGNET[1] + 0.01, align=C)
    return s.cut(pocket)

def fit_gauge():
    """Four 12 mm pins at SHAFT_D -0.1 .. +0.05, printed upright like the shaft. Push a 608
    onto each: the shaft wants the one that slides on by hand without wobble."""
    steps = (-0.10, -0.05, 0.0, 0.05)
    plate = Pos(0, 0, 0) * Box(4 * 13, 16, 2, align=(Align.MIN, Align.CENTER, Align.MIN))
    parts = []
    for i, d in enumerate(steps):
        r = (M.SHAFT_D + d) / 2
        parts.append(Pos(6.5 + 13 * i, 2, 2) * (Cylinder(r, 11.5, align=C).fuse(
            Pos(0, 0, 11.5) * Cone(r, r - LEAD, LEAD, align=C))))
    labels = [Pos(6.5 + 13 * i, -5.5, 1.4) * extrude(Text(f"{M.SHAFT_D + d:.2f}", 3.0), 1) for i, d in enumerate(steps)]
    return plate.fuse(*parts).cut(*labels)

def touch_plate():   # aluminium disc, wired to a capacitive input through a slip-free air gap
    return Cylinder(M.TOUCH_D / 2, TOUCH_T, align=C) - Pos(0, 0, -1) * Cylinder(M.CAP_D / 2 + 0.3, 5, align=C)

def center_cap():
    return Cylinder(M.CAP_D / 2, CAP_H, align=C)

def cap_marker():    # orange position marker, rotates with the platter
    return Pos(0, M.CAP_D / 2 - 4.5, 0) * Box(2.2, 6, 0.4, align=C)

def inertia_ring():
    ri, ro, rh = M.INERTIA
    return Cylinder(ro, rh, align=C) - Pos(0, 0, -1) * Cylinder(ri, rh + 2, align=C)

def bearing():
    return Cylinder(B_OD / 2, B_W, align=C) - Pos(0, 0, -1) * Cylinder(B_ID / 2, B_W + 2, align=C)

def magnet():
    return Cylinder(M.MAGNET[0] / 2, M.MAGNET[1], align=C)

ENC_HOLE_D = 3.5            # big corner holes (from the listing photo)
ENC_CORNER_R = 2.0
HDR_PITCH, HDR_TAIL = 2.54, 3.0
# Two pin columns either side of the chip (from the listing photo, +-0.5 mm), each with a
# right-angle header pointing outwards: (x of the column, y of its middle, pins, direction)
HEADERS = [(-6.8, 0.0, 3, -1),     # VCC OUT GND, plug to the left
           (6.6, 0.6, 4, +1)]      # DIR SCL SDA GPO, plug to the right

def encoder_pcb():   # local z = 0 on the underside, board centred on the chip
    w, d, t = M.ENC_PCB
    return extrude(RectangleRounded(w, d, ENC_CORNER_R), t) - [
        Pos(x, y, -1) * Cylinder(ENC_HOLE_D / 2, t + 2, align=C) for x, y in M.ENC_HOLES]

def encoder_chip():  # AS5600, SOIC-8; local z = 0 on the board's top face
    return Box(4.9, 3.9, M.ENC_CHIP_H, align=C)

def encoder_header():
    """Both right-angle headers on the chip side, pointing out to the sides, each with a
    Dupont plug, and their solder tails under the board. Local z = 0 on the underside."""
    t = M.ENC_PCB[2]
    out = []
    for x, y, n, s in HEADERS:
        L = n * HDR_PITCH
        out.append(Pos(x, y, t) * Box(2.5, L, 2.5, align=C))
        out.append(Pos(x + s * (1.25 + 3.0), y, t + 1.25) * Box(6.0, L - 1, 0.64))
        out.append(Pos(x + s * (1.25 + 1.0 + 7.0), y, t) * Box(14.0, L, 2.5, align=C))
        out.append(Pos(x, y, -HDR_TAIL) * Box(0.64, (n - 1) * HDR_PITCH + 0.64, HDR_TAIL, align=C))
    return out[0].fuse(*out[1:])

def tail_slots():
    """Relief in the base plate under each column's solder tails: (x, y, length) from the axis."""
    return [(x, y, (n - 1) * HDR_PITCH + 2.0) for x, y, n, _ in HEADERS]

# Heights above the well floor (the shell's "jog" mate), so the stack follows the params
STACK = {
    "platter":      FLOOR + M.PLATTER_GAP,
    "touch_plate":  FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS,
    "center_cap":   FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS,
    "cap_marker":   FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS + CAP_H,
    "inertia_ring": FLOOR + M.PLATTER_GAP,
    "bearing_top":  FLOOR - B_W,
    "bearing_bot":  M.BOSS_BOTTOM,
    "magnet":       M.SHAFT_BOTTOM,
    "encoder_pcb":  M.ENC_Z,
    "encoder_chip": M.ENC_Z + M.ENC_PCB[2],
    "encoder_header": M.ENC_Z,
}
MATES = {"seat": Pos(0, 0, 0)}   # every jog part: its underside on the axis

if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    print(f"config {M.CONFIG}: shaft {M.SHAFT_D} mm x {SHAFT_L:.2f} mm below the platter, "
          f"tip at z {M.SHAFT_BOTTOM:.2f}, board on {M.ENC_STANDOFF:.2f} mm bosses, "
          f"magnet {M.MAGNET[1]} mm, air gap {M.AIR_GAP:.2f} mm")
    import trimesh
    for name, solid in ((f"platter{M.SUFFIX}", platter()), ("fit_gauge", fit_gauge())):
        f = f"build/{name}.stl"
        export_stl(solid, f, tolerance=0.02, angular_tolerance=0.1)
        m = trimesh.load(f)
        print(f"{name}:", (m.bounds[1] - m.bounds[0]).round(2),
              "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
