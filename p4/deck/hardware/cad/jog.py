"""jog.py: the jog module. Printed platter with its shaft printed in one piece, through two
608 bearings (pressed into the shell's boss), held down by an e-clip under the lower
bearing. Steel inertia ring in the underside, aluminium touch plate on top, diametric
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
LEAD = 0.5                                         # 45 degree lead-in on the tip, for the bearings

def shaft():
    """In the platter's frame (z = 0 at its underside): down through both bearings,
    an e-clip groove under the lower one, and a pocket in the tip for the magnet."""
    r = M.SHAFT_D / 2
    z0 = -SHAFT_L
    s = Pos(0, 0, z0 + LEAD) * Cylinder(r, SHAFT_L - LEAD + 1, align=C)
    s = s.fuse(Pos(0, 0, z0) * Cone(r - LEAD, r, LEAD, align=C))
    to_local = lambda z: z - (FLOOR + M.PLATTER_GAP)
    g_d, g_w = M.ECLIP[0], M.ECLIP[1]
    groove = Pos(0, 0, to_local(M.GROOVE_BOT)) * (Cylinder(r + 1, g_w, align=C) - Pos(0, 0, -1) * Cylinder(g_d / 2, g_w + 2, align=C))
    pocket = Pos(0, 0, z0 - 0.01) * Cylinder(M.MAGNET[0] / 2 + 0.08, M.MAGNET[1] + 0.01, align=C)
    return s.cut(groove, pocket)

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

def eclip():         # DIN 6799 size 6, drawn as a ring
    d, _, t, od = M.ECLIP
    return Cylinder(od / 2, t, align=C) - Pos(0, 0, -1) * Cylinder(d / 2, t + 2, align=C)

ENC_HOLE_D = 3.2
HDR_N, HDR_PITCH = 7, 2.54                # VCC OUT GND DIR SCL SDA GPO
HDR_Y = M.ENC_PCB[1] / 2 - 1.5            # pin row, in from the back edge
HDR_TAIL = 3.0                            # solder tails under the board

def encoder_pcb():   # local z = 0 on the underside, board centred on the chip
    w, d, t = M.ENC_PCB
    return Box(w, d, t, align=C) - [Pos(x, y, -1) * Cylinder(ENC_HOLE_D / 2, t + 2, align=C) for x, y in M.ENC_HOLES]

def encoder_chip():  # AS5600, SOIC-8; local z = 0 on the board's top face
    return Box(4.9, 3.9, M.ENC_CHIP_H, align=C)

def encoder_header():
    """Right-angle header on the chip side, pointing back, with a Dupont plug on it,
    and its solder tails under the board. Local z = 0 on the board's underside."""
    t = M.ENC_PCB[2]
    L = HDR_N * HDR_PITCH
    body = Pos(0, HDR_Y, t) * Box(L, 2.5, 2.5, align=C)
    pins = Pos(0, HDR_Y + 1.25 + 3.0, t + 1.25) * Box(L - 1, 6.0, 0.64)
    plug = Pos(0, HDR_Y + 1.25 + 1.0 + 7.0, t) * Box(L, 14.0, 2.5, align=C)
    tails = Pos(0, HDR_Y, -HDR_TAIL) * Box(L - 1, 0.64, HDR_TAIL, align=C)
    return body.fuse(pins, plug, tails)

# Heights above the well floor (the shell's "jog" mate), so the stack follows the params
STACK = {
    "platter":      FLOOR + M.PLATTER_GAP,
    "touch_plate":  FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS,
    "center_cap":   FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS,
    "cap_marker":   FLOOR + M.PLATTER_GAP + M.PLATTER_H - TOUCH_RECESS + CAP_H,
    "inertia_ring": FLOOR + M.PLATTER_GAP,
    "bearing_top":  FLOOR - B_W,
    "bearing_bot":  M.BOSS_BOTTOM,
    "eclip":        M.GROOVE_BOT + (M.ECLIP[1] - M.ECLIP[2]) / 2,
    "magnet":       M.SHAFT_BOTTOM,
    "encoder_pcb":  M.ENC_Z,
    "encoder_chip": M.ENC_Z + M.ENC_PCB[2],
    "encoder_header": M.ENC_Z,
}
MATES = {"seat": Pos(0, 0, 0)}   # every jog part: its underside on the axis

if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    print(f"config {M.CONFIG}: shaft {M.SHAFT_D} mm x {SHAFT_L:.2f} mm below the platter, "
          f"e-clip groove {M.GROOVE_BOT:.2f}-{M.GROOVE_TOP:.2f}, tip at z {M.SHAFT_BOTTOM:.2f}, "
          f"magnet {M.MAGNET[1]} mm, air gap {M.AIR_GAP:.2f} mm")
    import trimesh
    for name, solid in ((f"platter{M.SUFFIX}", platter()), ("fit_gauge", fit_gauge())):
        f = f"build/{name}.stl"
        export_stl(solid, f, tolerance=0.02, angular_tolerance=0.1)
        m = trimesh.load(f)
        print(f"{name}:", (m.bounds[1] - m.bounds[0]).round(2),
              "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
