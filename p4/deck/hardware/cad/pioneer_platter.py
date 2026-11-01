# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""pioneer_platter.py: optional Pioneer-style platter for the jog, a drop-in swap for the
knurled one in jog.py. It's modelled on Dan's reference mesh (meshy_1788016598940.glb):
- a short vertical base band with one fine line;
- a sloped shoulder carrying a ring of oval finger dimples;
- a flat top (the reference's dome flattened) whose centre is the touch plate's seat.
The hollow skirt and hub come from freedj-3000's jog_wheel.py.

Fitted to this deck's jog stack:
- 100 mm across, PLATTER_H tall, so its top sits where the knurled platter's does.
- The flat centre is the touch plate's seat (TOUCH_D + 0.4, TOUCH_RECESS deep), so the
  aluminium plate, centre cap and marker stack exactly as before.
- The skirt's inner wall locates the steel inertia ring (0.2 mm radial clearance); the
  ring sits flush with the skirt's foot, against the top plate (RING_Z = 0).
- The hub carries jog.py's printed shaft and the inner-race spacer, so it uses the same
  608 bearings, magnet and AS5600.
Print it upside down (top face on the bed, shaft up), like the knurled one. The shoulder
is 45 degrees then rounds into the band, so it needs no support; only the centre recess
does (build plate only).

    FREEDJ_PLATTER=pioneer python3 machine.py   assembly + render with this platter
    python3 pioneer_platter.py -> build/platter_pioneer.stl   (FREEDJ_CONFIG=flat -> _flat)
"""
import math, os
from build123d import *
import machine_params as M
import jog

C = (Align.CENTER, Align.CENTER, Align.MIN)

R          = M.JOG_D / 2            # 50
H          = M.PLATTER_H            # 10
BASE_H     = 2.5                    # vertical band (the reference's is ~3.3 at this scale)
BASE_LINE  = (BASE_H / 2, 0.35)     # decorative line round the band: z, groove radius
FOOT_R     = 6.0                    # band -> shoulder round; the shoulder then runs at 45 degrees
TOP_R      = R - FOOT_R * (1 - math.cos(math.radians(45))) - (H - BASE_H - FOOT_R * math.sin(math.radians(45)))   # 45.0
REC_R      = (M.TOUCH_D + 0.4) / 2  # touch plate seat
REC_DEPTH  = jog.TOUCH_RECESS
RING_CLEAR = 0.2
WALL       = R - (M.INERTIA[1] + RING_CLEAR)   # 3.8 at the foot: the skirt locates the ring
RING_Z     = 0.0                    # inertia ring's underside, in the platter frame
Z_UNDER    = RING_Z + M.INERTIA[2]  # hollow's ceiling = ring top
HUB_OD     = 24.0
SPACER_R   = 6.0                    # inner-race spacer, as on the knurled platter
DIMPLES    = 24                     # the reference has about 24
DIMPLE     = (6.0, 4.0, 1.1)        # ellipsoid semi-axes: along the rim, across it; depth

assert TOP_R > REC_R + 3, "flat land between the touch plate and the shoulder"
assert Z_UNDER < H - REC_DEPTH - 2, "top plate too thin under the touch plate"


def _shoulder():
    """(r, z) points of the outer profile: band top, the round's midpoint and end, top edge."""
    a = math.radians(45)
    c = (R - FOOT_R, BASE_H)                                   # round's centre
    mid = (c[0] + FOOT_R * math.cos(a / 2), c[1] + FOOT_R * math.sin(a / 2))
    end = (c[0] + FOOT_R * math.cos(a), c[1] + FOOT_R * math.sin(a))
    return (R, BASE_H), mid, end, (TOP_R, H)


def platter():
    s0, s_mid, s1, s2 = _shoulder()
    z_floor = H - REC_DEPTH
    with BuildPart() as bp:
        with BuildSketch(Plane.XZ):
            with BuildLine():
                Polyline((0, z_floor), (0, Z_UNDER), (R - WALL, Z_UNDER), (R - WALL, 0), (R, 0), s0)
                ThreePointArc(s0, s_mid, s1)
                Polyline(s1, s2, (REC_R, H), (REC_R, z_floor), (0, z_floor))
            make_face()
        revolve(axis=Axis.Z)
    p = bp.part
    hub = Cylinder(HUB_OD / 2, Z_UNDER + 0.5, align=C)
    spacer = Pos(0, 0, -M.PLATTER_GAP) * Cylinder(SPACER_R, M.PLATTER_GAP + 1, align=C)
    p = p.fuse(hub, spacer, jog.shaft())

    cuts = [Pos(0, 0, BASE_LINE[0]) * Torus(R, BASE_LINE[1])]
    # dimples: ellipsoids centred on the 45 degree shoulder's midpoint, sunk DIMPLE[2] deep
    at, ac, depth = DIMPLE
    sr, sz = (s1[0] + s2[0]) / 2, (s1[1] + s2[1]) / 2
    n = (math.cos(math.radians(45)), math.sin(math.radians(45)))   # outward normal in (r, z)
    cr, cz = sr + n[0] * (ac - depth), sz + n[1] * (ac - depth)
    blob = scale(Sphere(1), by=(ac, at, ac))                        # long axis along y = tangential at +x
    for i in range(DIMPLES):
        t = 360 * i / DIMPLES
        cuts.append(Rot(0, 0, t) * Pos(cr, 0, cz) * blob)
    return p.cut(*cuts)


if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    f = f"build/platter_pioneer{M.SUFFIX}.stl"
    export_stl(platter(), f, tolerance=0.02, angular_tolerance=0.1)
    import trimesh
    m = trimesh.load(f)
    print(f"platter_pioneer{M.SUFFIX}:", (m.bounds[1] - m.bounds[0]).round(2), "bodies:",
          len(m.split(only_watertight=False)), "watertight:", m.is_watertight,
          f"top edge r {TOP_R:.1f}, wall {WALL:.2f}, {m.volume / 1000:.1f} cm3")
