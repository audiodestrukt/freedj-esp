"""jog.py: the jog module. Printed platter on an 8 mm shaft through two 608 bearings
(pressed into the shell's boss), steel inertia ring in the underside, aluminium touch
plate on top, diametric magnet in the shaft end over an MT6701 encoder board.

Each part's local frame: axis = z, z = 0 at its own underside, so "seat" = Pos(0,0,0).
The assembly places everything off the shell's "jog" mate (the well floor at the axis).
python3 jog.py -> build/platter.stl (the printed part) + sanity line
"""
import math, os
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)
KNURL_N, KNURL_D = 72, 2.4
TOUCH_T, TOUCH_RECESS = 1.5, 1.2
CAP_H = 2.5
SHAFT_IN_PLATTER = 7.0
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
    cuts.append(Pos(0, 0, -1) * Cylinder(M.SHAFT_D / 2, SHAFT_IN_PLATTER + 1, align=C))
    # inner-race spacer: only the hub touches the bearing, never the outer race
    hub = Pos(0, 0, -M.PLATTER_GAP) * (Cylinder(6.0, M.PLATTER_GAP + 1, align=C) - Pos(0, 0, -1) * Cylinder(M.SHAFT_D / 2, 5, align=C))
    return p.fuse(hub).cut(*cuts)

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

def shaft():
    top = FLOOR + M.PLATTER_GAP + SHAFT_IN_PLATTER
    s = Cylinder(M.SHAFT_D / 2, top - M.SHAFT_BOTTOM, align=C)
    return s - Pos(0, 0, -0.01) * Cylinder(M.MAGNET[0] / 2 + 0.05, M.MAGNET[1], align=C)

def magnet():
    return Cylinder(M.MAGNET[0] / 2, M.MAGNET[1], align=C)

def eclip():
    return Cylinder(7.0, 0.8, align=C) - Pos(0, 0, -1) * Cylinder(M.SHAFT_D / 2 - 0.3, 3, align=C)

def encoder_pcb():
    w, d, t = M.ENC_PCB
    return Box(w, d, t, align=C)

def encoder_chip():  # MT6701, SOP-8
    return Box(4.9, 3.9, 1.5, align=C)

# Heights above the well floor (the shell's "jog" mate), so the stack follows the params
STACK = {
    "platter":      FLOOR + M.PLATTER_GAP,
    "touch_plate":  FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS,
    "center_cap":   FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS,
    "cap_marker":   FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS + CAP_H,
    "inertia_ring": FLOOR + M.PLATTER_GAP,
    "bearing_top":  FLOOR - B_W,
    "bearing_bot":  M.BOSS_BOTTOM,
    "eclip":        M.BOSS_BOTTOM - 0.9,
    "shaft":        M.SHAFT_BOTTOM,
    "magnet":       M.SHAFT_BOTTOM,
    "encoder_pcb":  M.ENC_Z,
    "encoder_chip": M.ENC_Z + M.ENC_PCB[2],
}
MATES = {"seat": Pos(0, 0, 0)}   # every jog part: its underside on the axis

if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    gap = STACK["magnet"] - (STACK["encoder_chip"] + 1.5)
    print(f"magnet-to-chip air gap {gap:.1f} mm")
    export_stl(platter(), "build/platter.stl", tolerance=0.05, angular_tolerance=0.1)
    import trimesh; m = trimesh.load("build/platter.stl")
    print("platter:", (m.bounds[1] - m.bounds[0]).round(1),
          "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
