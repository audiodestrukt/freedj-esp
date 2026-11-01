# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""pioneer_platter.py: optional Pioneer-style platter for the jog, a drop-in swap for the
knurled one in jog.py. The profile is freedj-3000's jog_wheel.py (hardware/cad): a top
plate with a dished centre, a hollow grip skirt with horizontal grooves, and a central
hub. It's resized here to the deck's jog well and fitted to its jog stack:
- OUTER_D = JOG_D (100 in a 104 well) and H = PLATTER_H, so its top sits where the
  knurled platter's does.
- The dished centre is the touch plate's seat (TOUCH_D + 0.4, TOUCH_RECESS deep), so the
  aluminium plate, centre cap and marker stack exactly as before.
- The skirt's inner wall locates the steel inertia ring (0.2 mm radial clearance); the
  ring sits up against the top plate's underside, inside the hollow (RING_Z).
- The hub carries jog.py's printed shaft and the inner-race spacer, so it uses the same
  608 bearings, magnet and AS5600.
Print it upside down (top face on the bed, shaft up), like the knurled one: only the
centre recess needs support (build plate only).

    FREEDJ_PLATTER=pioneer python3 machine.py   assembly + render with this platter
    python3 pioneer_platter.py -> build/platter_pioneer.stl   (FREEDJ_CONFIG=flat -> _flat)
"""
import os
from build123d import *
import machine_params as M
import jog

C = (Align.CENTER, Align.CENTER, Align.MIN)

# ---- freedj-3000's parameters, set for this deck ------------------------------------
OUTER_D   = M.JOG_D                 # 206 on freedj-3000
H         = M.PLATTER_H             # 18
TOP_T     = 4.0                     # same
RING_CLEAR = 0.2
WALL      = M.JOG_D / 2 - (M.INERTIA[1] + RING_CLEAR)   # 3.8 (3.5): the skirt locates the ring
REC_D     = M.TOUCH_D + 0.4         # 92: here the touch plate's seat
REC_DEPTH = jog.TOUCH_RECESS        # 1.5
HUB_OD    = 24.0                    # same; reaches the skirt's foot
SPACER_R  = 6.0                     # inner-race spacer, as on the knurled platter
GROOVES   = 3                       # 4 on freedj-3000; fewer on a 10 mm skirt
GROOVE_R  = 0.8                     # 1.2
GROOVE_Z  = (1.5, H - 2.0)          # first and last groove centre heights
RIM_ROUND = 0.8

RING_Z = H - TOP_T - M.INERTIA[2]   # inertia ring's underside, in the platter frame
assert TOP_T > REC_DEPTH, "recess must be shallower than the top plate"
assert RING_Z >= 0, "inertia ring must fit inside the skirt"
assert REC_D / 2 < OUTER_D / 2 - WALL, "recess must fit inside the skirt"


def platter():
    R = OUTER_D / 2
    z_under, z_floor = H - TOP_T, H - REC_DEPTH
    pts = [(0, z_under), (R - WALL, z_under), (R - WALL, 0), (R, 0), (R, H),
           (REC_D / 2, H), (REC_D / 2, z_floor), (0, z_floor)]
    with BuildPart() as bp:
        with BuildSketch(Plane.XZ):
            with BuildLine():
                Polyline(pts, close=True)
            make_face()
        revolve(axis=Axis.Z)
    p = bp.part
    try:
        p = fillet(p.edges().filter_by(lambda e: abs(e.center().Z - H) < 0.01 and e.radius > R - 0.1), RIM_ROUND)
    except Exception as e:
        print("  (rim round skipped:", e, ")")
    hub = Cylinder(HUB_OD / 2, z_under + 0.5, align=C)
    spacer = Pos(0, 0, -M.PLATTER_GAP) * Cylinder(SPACER_R, M.PLATTER_GAP + 1, align=C)
    p = p.fuse(hub, spacer, jog.shaft())
    z0, z1 = GROOVE_Z
    for i in range(GROOVES):
        z = z0 + (z1 - z0) * i / max(GROOVES - 1, 1)
        p -= Pos(0, 0, z) * Torus(R, GROOVE_R)
    return p


if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    f = f"build/platter_pioneer{M.SUFFIX}.stl"
    export_stl(platter(), f, tolerance=0.02, angular_tolerance=0.1)
    import trimesh
    m = trimesh.load(f)
    print(f"platter_pioneer{M.SUFFIX}:", (m.bounds[1] - m.bounds[0]).round(2), "bodies:",
          len(m.split(only_watertight=False)), "watertight:", m.is_watertight,
          f"wall {WALL:.2f}, ring at z {RING_Z:.1f}, {m.volume / 1000:.1f} cm3")
