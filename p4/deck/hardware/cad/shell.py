"""shell.py: freedj-esp top shell. One print, upside down on the bed (fits a 256 mm bed).

Carries the jog bearing boss, CUE/PLAY holes, pitch slot, the tilted LCD bezel with the
USB-A stick slot beside it, and the rear-panel cutouts. Open underneath; base.py closes it.
python3 shell.py -> build/shell.stl   (FREEDJ_CONFIG=flat -> build/shell_flat.stl)
"""
import math, os
from build123d import *
import machine_params as M
from machine_params import face_loc

CORNER_POST_D, INSERT_BORE = 8.0, 4.6       # M3 heat-set inserts, through from below
POST_INSET = 7.0
WELL_GAP = (M.WELL_D - M.JOG_D) / 2

def posts():
    """Base-plate screw posts. The back-right one moves forward, in front of the dev board,
    whose port corner fills that corner of the deck."""
    import devboard as B
    i = POST_INSET
    return [(i, i, CORNER_POST_D), (M.W - i, i, CORNER_POST_D), (i, M.D - i, CORNER_POST_D),
            (M.W - 5.9, B.YB - 5.0, 7.0)]   # slimmer: squeezed between the pitch pot and the board

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
    for x, y, d in posts():
        adds.append(Pos(x, y, M.BASE_T) * Cylinder(d / 2, M.H_BACK,
                                                     align=(Align.CENTER, Align.CENTER, Align.MIN)))
    # light pipes / switch guides under CUE and PLAY
    for c in (M.PLAY_C, M.CUE_C):
        adds.append(Pos(*c, M.H - 12) * Cylinder(M.RING_D / 2 + 2, 11, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    # screen bosses on the module's 4 mounting holes: they come down from the top past the
    # glass and gasket and stop on the board, which then holds the glass up against the gasket
    F = face_loc()
    for L in screen_holes():
        adds.append(L * Cylinder(M.SCREEN_BOSS_D / 2, M.STACK + M.GASKET + 1.5, align=(Align.CENTER, Align.CENTER, Align.MIN)))
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
    for x, y, _ in posts():
        # blind from below: stops 2 mm under the top skin so nothing shows on the deck
        cuts.append(Pos(x, y, -1) * Cylinder(INSERT_BORE / 2, M.H - M.TOP_T - 2 + 1, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    # bezel: a window over the visible area only (off-centre, as the active area is),
    # straight for WINDOW_LIP then chamfered 45 degrees so a finger reaches the edges
    ww, wh = (a + 2 * M.WINDOW_MARGIN for a in M.LCD_ACTIVE)
    W0 = F * Pos(M.SCREEN_X - M.W / 2 + M.LCD_AA_DX, M.SCREEN_V, 0)
    cuts.append(W0 * Pos(0, 0, -M.TOP_T_PERP - 1) * Box(ww, wh, M.TOP_T_PERP + 2, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    ch = M.TOP_T_PERP - M.WINDOW_LIP
    cuts.append(W0 * Pos(0, 0, -ch) * extrude(Rectangle(ww, wh), amount=ch + 0.01, taper=-45))
    cuts.append(W0 * Pos(0, 0, 0) * Box(ww + 2 * ch, wh + 2 * ch, 2, align=(Align.CENTER, Align.CENTER, Align.MIN)))   # clean top edge
    pd, pdepth = M.SCREEN_PILOT
    for L in screen_holes():   # blind pilots up from the board face
        cuts.append(L * Pos(0, 0, -1) * Cylinder(pd / 2, pdepth + 1, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    ux, uw, uh = M.USBA_FACE
    cuts.append(F * Pos(ux - M.W / 2, M.USBA_V, 0) * Box(uw, uh, 20))
    cuts.append(F * Pos(ux - M.W / 2, M.USBA_V - 9, -0.6) * Cylinder(1.0, 2))   # stick activity LED
    # rear panel
    for x in M.RCA_X:
        cuts.append(Pos(x, M.D, M.RCA_Z) * Rot(90, 0, 0) * Cylinder(M.RCA_HOLE / 2, 10))
    # dev board ports: Ethernet + 2 x dual USB-A through the rear wall, 2 x USB-C through the
    # right wall (opening sized for the plug's overmould so it seats fully)
    import devboard as B
    top = B.Z0 + B.T_
    for w, wd, h in B.USBA:
        x, _ = B.deck_xy(0, w)
        cuts.append(Pos(x, M.D, top - 0.5) * Box(wd + 1.0, 10, h + 1.0, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    w, wd, h, _ = B.RJ45
    x, _ = B.deck_xy(0, w)
    cuts.append(Pos(x, M.D, top - 0.5) * Box(wd + 1.0, 10, h + 1.0, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    for u in B.USBC:
        _, y = B.deck_xy(u, 0)
        cuts.append(Pos(M.W, y, top + B.USBC_SIZE[2] / 2) * Box(10, 12.5, 7.0))
    # brand, engraved on the bezel left of the screen, below the USB-A slot
    try:
        bx = (M.SCREEN_X - M.GLASS[0] / 2) / 2           # centred in the strip left of the glass
        txt = F * Pos(bx - M.W / 2, M.BRAND_V, -0.6) * extrude(Text("FREEDJ-ESP", 4.0, font_style=FontStyle.BOLD,
                                                   align=(Align.CENTER, Align.CENTER)), 2)
        cuts.append(txt)
    except Exception as e:
        print("  (brand text skipped:", e, ")")
    return shell.cut(*cuts)

def screen_holes():
    """The LCD module's 4 mounting holes, as Locations on the board's top face (+z out of the deck)."""
    hx, hy = M.LCD_HOLES
    return [face_loc() * Pos(M.SCREEN_X - M.W / 2 + sx * hx / 2, M.SCREEN_V + sy * hy / 2, M.GLASS_Z - M.STACK)
            for sx in (-1, 1) for sy in (-1, 1)]

F = face_loc()
MATES = {   # world frame (the shell is the datum); +z points out of the contact face
    "jog":    Pos(*M.JOG_C, M.H - M.WELL_DEPTH),
    "play":   Pos(*M.PLAY_C, M.H),
    "cue":    Pos(*M.CUE_C, M.H),
    "slider": Pos(M.SLIDER_X, M.KNOB_Y, M.H),
    "slider_led": Pos(M.SLIDER_X + 12.5, sum(M.SLIDER_Y) / 2, M.H),
    "screen": F * Pos(M.SCREEN_X - M.W / 2, M.SCREEN_V, M.GLASS_Z),    # glass top, behind the top plate
    **{f"screen_screw{i}": L * Pos(0, 0, -M.LCD_PCB[2]) for i, L in enumerate(screen_holes())},   # under the board, +z up into the boss
    "usb_a":  F * Pos(M.USBA_FACE[0] - M.W / 2, M.USBA_V, 0),
    "usb_led": F * Pos(M.USBA_FACE[0] - M.W / 2, M.USBA_V - 9, 0),
    "rca_l":  Pos(M.RCA_X[0], M.D, M.RCA_Z) * Rot(-90, 0, 0),
    "rca_r":  Pos(M.RCA_X[1], M.D, M.RCA_Z) * Rot(-90, 0, 0),
    "base":   Pos(0, 0, 0),
}

if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    p = part()
    out = f"build/shell{M.SUFFIX}.stl"
    export_stl(p, out, tolerance=0.05, angular_tolerance=0.1)
    import trimesh; m = trimesh.load(out)
    print(out, (m.bounds[1] - m.bounds[0]).round(1),
          "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
