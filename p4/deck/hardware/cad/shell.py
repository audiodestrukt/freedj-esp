"""shell.py: ESP Deck top shell. One print, upside down on the bed (fits a 256 mm bed).

Carries the jog bearing boss, CUE/PLAY holes, pitch slot, the tilted LCD bezel with the
USB-A stick slot beside it, and the rear-panel cutouts. Open underneath; base.py closes it.
python3 shell.py -> build/shell.stl
"""
import math, os
from build123d import *
import machine_params as M
from machine_params import face_loc

CORNER_POST_D, INSERT_BORE = 8.0, 4.6       # M3 heat-set inserts, through from below
POST_INSET = 7.0
WELL_GAP = (M.WELL_D - M.JOG_D) / 2

def posts():
    i = POST_INSET
    return [(i, i), (M.W - i, i), (i, M.D - i), (M.W - i, M.D - i)]

def _body(inset=0.0, drop=0.0):
    plan = Pos(M.W / 2, M.D / 2, -1) * extrude(
        RectangleRounded(M.W - 2 * inset, M.D - 2 * inset, M.CORNER_R - inset), 80)
    prof = Polygon((-1, -1 - drop), (M.D + 1, -1 - drop), (M.D + 1, M.H_BACK - drop),
                   (M.BEZEL_Y, M.H - drop), (-1, M.H - drop), align=None)
    side = Pos(-1, 0, 0) * extrude(Plane.YZ * prof, M.W + 2)   # profile in YZ, swept along +x
    return plan & side

def part():
    outer = _body() & Pos(M.W / 2, M.D / 2, 0) * Box(M.W + 4, M.D + 4, 80, align=(Align.CENTER, Align.CENTER, Align.MIN))
    try:
        top_edges = outer.edges().filter_by(lambda e: e.center().Z > M.H - 0.5)
        outer = fillet(top_edges, M.EDGE_R)
    except Exception as e:
        print("  (top-edge round skipped:", e, ")")
    inner = _body(M.WALL, M.TOP_T)
    jx, jy = M.JOG_C
    adds = [Pos(jx, jy, M.BOSS_BOTTOM) * Cylinder(M.BOSS_OD / 2, M.H - M.BOSS_BOTTOM - 0.5,
                                                  align=(Align.CENTER, Align.CENTER, Align.MIN))]
    for x, y in posts():
        adds.append(Pos(x, y, M.BASE_T) * Cylinder(CORNER_POST_D / 2, M.H_BACK,
                                                     align=(Align.CENTER, Align.CENTER, Align.MIN)))
    # light pipes / switch guides under CUE and PLAY
    for c in (M.PLAY_C, M.CUE_C):
        adds.append(Pos(*c, M.H - 12) * Cylinder(M.RING_D / 2 + 2, 11, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    hollow = outer - inner
    # posts and bosses must stay inside the outer skin
    inside = Pos(M.W / 2, M.D / 2, 0) * Box(M.W, M.D, 80, align=(Align.CENTER, Align.CENTER, Align.MIN)) & _body(0.2, 0.0)
    shell = hollow.fuse(*[a & inside for a in adds])

    cuts = []
    floor = M.H - M.WELL_DEPTH
    cuts.append(Pos(jx, jy, floor) * Cylinder(M.WELL_D / 2, 20, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    b_id, b_od, b_w = M.B608
    cuts.append(Pos(jx, jy, floor - b_w) * Cylinder(b_od / 2 + 0.05, b_w + 1, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    cuts.append(Pos(jx, jy, M.BOSS_BOTTOM - 1) * Cylinder(b_od / 2 + 0.05, b_w + 1, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    cuts.append(Pos(jx, jy, 0) * Cylinder(6.0, 40, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    for c in (M.PLAY_C, M.CUE_C):
        cuts.append(Pos(*c, M.H - 15) * Cylinder(M.RING_D / 2 + 0.2, 20, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    y0, y1 = M.SLIDER_Y
    cuts.append(Pos(M.SLIDER_X, (y0 + y1) / 2, M.H - 10) * Box(M.SLOT_W, y1 - y0, 20, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    for k in range(-4, 5):   # tempo scale ticks, engraved 0.6 deep
        y = (y0 + y1) / 2 + k * (y1 - y0 - 12) / 8
        ln = 6.0 if k in (-4, 0, 4) else 3.5
        cuts.append(Pos(M.SLIDER_X + 9.5 + ln / 2, y, M.H - 0.6) * Box(ln, 0.8, 2))
    for x, y in posts():
        # blind from below: stops 2 mm under the top skin so nothing shows on the deck
        cuts.append(Pos(x, y, -1) * Cylinder(INSERT_BORE / 2, M.H - M.TOP_T - 2 + 1, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    # bezel: LCD window through, glass pocket, USB-A slot
    F = face_loc()
    aw, ah = M.LCD_ACTIVE
    cuts.append(F * Pos(0, M.SCREEN_V, -10) * Box(aw + 2, ah + 2, 20))
    gw, gh, gt = M.GLASS
    cuts.append(F * Pos(0, M.SCREEN_V, 0) * Box(gw + 0.4, gh + 0.4, 2 * gt))
    ux, uw, uh = M.USBA_FACE
    cuts.append(F * Pos(ux - M.W / 2, M.SCREEN_V, 0) * Box(uw, uh, 20))
    cuts.append(F * Pos(ux - M.W / 2, M.SCREEN_V - 9, -0.6) * Cylinder(1.0, 2))   # stick activity LED
    # rear panel
    x, z, w, h = M.RJ45
    cuts.append(Pos(x, M.D, z) * Box(w, 10, h))
    for x in M.RCA_X:
        cuts.append(Pos(x, M.D, M.RCA_Z) * Rot(90, 0, 0) * Cylinder(M.RCA_HOLE / 2, 10))
    x, z, w, h = M.USBC
    cuts.append(Pos(x, M.D, z) * Box(w, 10, h))
    # brand, engraved on the flat strip under the bezel
    try:
        txt = Pos(14, 121, M.H - 0.6) * extrude(Text("ESP DECK", 5.0, font_style=FontStyle.BOLD,
                                                   align=(Align.MIN, Align.CENTER)), 2)
        cuts.append(txt)
    except Exception as e:
        print("  (brand text skipped:", e, ")")
    return shell.cut(*cuts)

F = face_loc()
MATES = {   # world frame (the shell is the datum); +z points out of the contact face
    "jog":    Pos(*M.JOG_C, M.H - M.WELL_DEPTH),
    "play":   Pos(*M.PLAY_C, M.H),
    "cue":    Pos(*M.CUE_C, M.H),
    "slider": Pos(M.SLIDER_X, M.KNOB_Y, M.H),
    "slider_led": Pos(M.SLIDER_X + 12.5, sum(M.SLIDER_Y) / 2, M.H),
    "glass":  F * Pos(0, M.SCREEN_V, -M.GLASS[2]),
    "lcd":    F * Pos(0, M.SCREEN_V, -M.TOP_T * math.cos(math.radians(M.TILT))),
    "usb_a":  F * Pos(M.USBA_FACE[0] - M.W / 2, M.SCREEN_V, 0),
    "usb_led": F * Pos(M.USBA_FACE[0] - M.W / 2, M.SCREEN_V - 9, 0),
    "rj45":   Pos(M.RJ45[0], M.D, M.RJ45[1]) * Rot(-90, 0, 0),
    "rca_l":  Pos(M.RCA_X[0], M.D, M.RCA_Z) * Rot(-90, 0, 0),
    "rca_r":  Pos(M.RCA_X[1], M.D, M.RCA_Z) * Rot(-90, 0, 0),
    "usb_c":  Pos(M.USBC[0], M.D, M.USBC[1]) * Rot(-90, 0, 0),
    "base":   Pos(0, 0, 0),
}

if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    p = part()
    export_stl(p, "build/shell.stl", tolerance=0.05, angular_tolerance=0.1)
    import trimesh; m = trimesh.load("build/shell.stl")
    print("shell:", (m.bounds[1] - m.bounds[0]).round(1),
          "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
