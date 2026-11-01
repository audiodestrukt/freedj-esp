"""shell.py: freedj-esp top shell. One print, upside down on the bed (fits a 256 mm bed).

Carries the jog bearing boss, CUE/PLAY holes, the control board's bosses, the pitch slot
with the pot's pads and countersunk M2 holes, the tilted LCD bezel with the
USB-A stick slot beside it, and the rear-panel cutouts. Open underneath; base.py closes it.
python3 shell.py -> build/shell.stl   (FREEDJ_CONFIG=flat -> build/shell_flat.stl)
"""
import math, os
from build123d import *
import machine_params as M
from machine_params import face_loc

# Base fixing: a short boss in each corner, fused into the two walls, for an M3 heat-set
# insert. 10 mm tall with a 45 degree cone on top, so it prints without supports upside
# down and stays below the buttons, the USB slot, the screen and the dev board's ports.
CORNER_POST_D, CORNER_POST_H = 9.0, 10.0
INSERT_BORE, INSERT_DEPTH = 4.6, 6.0       # M3 x 5.7 heat-set insert
POST_INSET = 7.5                            # keeps the base plate's countersinks inside its rounded corners
WELL_GAP = (M.WELL_D - M.JOG_D) / 2

def posts():
    """The four base-plate screw positions, one per corner."""
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
    r = CORNER_POST_D / 2
    for x, y in posts():
        adds.append(Pos(x, y, M.BASE_T) * Cylinder(r, CORNER_POST_H, align=(Align.CENTER, Align.CENTER, Align.MIN)))
        adds.append(Pos(x, y, M.BASE_T + CORNER_POST_H - 0.01) * Cone(r, 0.3, r - 0.3, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    # light pipes / switch guides under CUE and PLAY
    for c in (M.PLAY_C, M.CUE_C):
        adds.append(Pos(*c, M.H - 12) * Cylinder(M.RING_D / 2 + 2, 11, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    # control board bosses: down from the top's underside to the board's top face
    for x, y in M.CTRL_HOLES:
        adds.append(Pos(x, y, M.CTRL_Z) * Cylinder(M.CTRL_BOSS_D / 2, M.H - M.TOP_T - M.CTRL_Z + 0.5,
                                                   align=(Align.CENTER, Align.CENTER, Align.MIN)))
    # pitch pot pads: the frame's top face sits on these, POT_PAD under the top
    for s_ in (-1, 1):
        adds.append(Pos(M.SLIDER_X, M.SLIDER_C + s_ * M.POT_HOLES / 2, M.H - M.TOP_T - M.POT_PAD)
                    * Box(M.POT_FRAME[0] - 1, 6.0, M.POT_PAD + 0.5, align=(Align.CENTER, Align.CENTER, Align.MIN)))
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
    for s_ in (-1, 1):   # pot screws: M2 clearance, 90 degree countersink for a flush head
        y = M.SLIDER_C + s_ * M.POT_HOLES / 2
        cuts.append(Pos(M.SLIDER_X, y, M.H - M.TOP_T - M.POT_PAD - 1) * Cylinder(1.2, M.TOP_T + M.POT_PAD + 2, align=(Align.CENTER, Align.CENTER, Align.MIN)))
        cuts.append(Pos(M.SLIDER_X, y, M.H - 1.0) * Cone(1.0, 2.0, 1.01, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    for x, y in M.CTRL_HOLES:   # control board: pilots up from the board face
        pd, pdepth = M.CTRL_PILOT
        cuts.append(Pos(x, y, M.CTRL_Z - 1) * Cylinder(pd / 2, pdepth + 1, align=(Align.CENTER, Align.CENTER, Align.MIN)))
    for k in range(-4, 5):   # tempo scale ticks over the lever's travel, engraved 0.6 deep
        y = M.SLIDER_C + k * M.POT_TRAVEL / 8
        ln = 6.0 if k in (-4, 0, 4) else 3.5
        cuts.append(Pos(M.SLIDER_X + 9.5 + ln / 2, y, M.H - 0.6) * Box(ln, 0.8, 2))
    cuts.append(Pos(M.SLIDER_X + 12.5, M.SLIDER_C, M.H - 0.6) * Cylinder(1.0, 2))   # tempo-zero LED
    for x, y in posts():
        cuts.append(Pos(x, y, -1) * Cylinder(INSERT_BORE / 2, M.BASE_T + INSERT_DEPTH + 1, align=(Align.CENTER, Align.CENTER, Align.MIN)))
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
        cuts.append(Pos(M.W, y, top + B.USBC_SIZE[2] / 2) * Box(30, 13.0, 7.5))   # deep: the plug reaches in past the wall
    # brand, engraved on the bezel left of the screen, below the USB-A slot
    try:
        win_l = M.WINDOW_X - M.LCD_ACTIVE[0] / 2 - M.WINDOW_MARGIN - (M.TOP_T_PERP - M.WINDOW_LIP)
        bx = win_l / 2                                     # centred in the strip left of the window
        txt = F * Pos(bx - M.W / 2, M.BRAND_V, -0.6) * extrude(Text("FREEDJ", 3.5, font_style=FontStyle.BOLD,
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
    "pot":    Pos(M.SLIDER_X, M.SLIDER_C, M.H),   # slot centre on the top surface
    "slider_led": Pos(M.SLIDER_X + 12.5, M.SLIDER_C, M.H),
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
