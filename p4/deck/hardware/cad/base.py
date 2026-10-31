"""base.py: freedj-esp base plate. Drops inside the shell walls and screws into the four
corner posts (M3 countersunk into heat-set inserts). Vent slots under the electronics.
Small M3 heat-set inserts (M3 x 3) hold the dev board (on standoffs) and the AS5600
jog encoder (on four short bosses).
python3 base.py -> build/base.stl   (FREEDJ_CONFIG=flat -> build/base_flat.stl)
"""
import os
from build123d import *
import machine_params as M
from shell import posts

C = (Align.CENTER, Align.CENTER, Align.MIN)
CLEAR = 0.3
STANDOFF_D, INSERT_DEPTH = 8.0, 4.5   # dev board: M3 x 3 (or x 4) insert, from the top
ENC_BOSS_D = 7.0                       # encoder board bosses, on its 4 corner holes
FOOT_D, FOOT_H, FOOT_INSET = 12.0, 2.5, 16.0

def part():
    w, d = M.W - 2 * (M.WALL + CLEAR), M.D - 2 * (M.WALL + CLEAR)
    p = Pos(M.W / 2, M.D / 2, 0) * extrude(RectangleRounded(w, d, M.CORNER_R - M.WALL - CLEAR), M.BASE_T)
    cuts = []
    for x, y in posts():
        cuts.append(Pos(x, y, -1) * Cylinder(1.7, M.BASE_T + 2, align=C))
        cuts.append(Pos(x, y, -0.01) * Cone(3.2, 1.6, 1.6, align=C))
    for i in range(7):   # vents, left of the dev board's standoffs
        cuts.append(Pos(55 + (i - 3) * 9, 150, -1) * Box(3, 30, M.BASE_T + 2, align=C))
    # dev board standoffs (Pi B+ pattern), M2.5 heat-set inserts from the top
    import devboard as B
    standoffs = []
    for u, w in B.HOLES:
        x, y = B.deck_xy(u, w)
        standoffs.append(Pos(x, y, M.BASE_T - 1) * Cylinder(STANDOFF_D / 2, B.STANDOFF + 1, align=C))
        cuts.append(Pos(x, y, M.BASE_T + B.STANDOFF - INSERT_DEPTH) * Cylinder(M.M3_INSERT_BORE / 2, INSERT_DEPTH + 1, align=C))
    # jog encoder: four bosses up to the board, small M3 inserts from the top (blind, 0.6 mm
    # floor); a shallow pocket under each header column takes the solder tails
    import jog
    jx, jy = M.JOG_C
    for x, y in M.ENC_HOLES:
        standoffs.append(Pos(jx + x, jy + y, M.BASE_T - 1) * Cylinder(ENC_BOSS_D / 2, M.ENC_STANDOFF + 1, align=C))
        cuts.append(Pos(jx + x, jy + y, 0.6) * Cylinder(M.M3_INSERT_BORE / 2, M.ENC_Z, align=C))
    for x, y, ln in jog.tail_slots():
        # 1.5 deep into the plate, and up through any boss it meets (notches the top-right one)
        cuts.append(Pos(jx + x, jy + y, M.BASE_T - 1.5) * Box(2.0, ln, 1.5 + M.ENC_STANDOFF + 0.5, align=C))
    return p.fuse(*standoffs).cut(*cuts)

def feet():
    i = FOOT_INSET
    return [Pos(x, y, -FOOT_H) * Cylinder(FOOT_D / 2, FOOT_H, align=C)
            for x, y in ((i, i), (M.W - i, i), (i, M.D - i), (M.W - i, M.D - i))]

MATES = {"to_shell": Pos(0, 0, 0)}

if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    export_stl(part(), f"build/base{M.SUFFIX}.stl", tolerance=0.05, angular_tolerance=0.1)
    import trimesh; m = trimesh.load(f"build/base{M.SUFFIX}.stl")
    print("base:", (m.bounds[1] - m.bounds[0]).round(1),
          "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
