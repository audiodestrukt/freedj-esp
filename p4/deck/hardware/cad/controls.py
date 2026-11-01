# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""controls.py: CUE/PLAY buttons with light rings, the pitch-slider knob and pot,
indicator LEDs. Local frame: z = 0 on the deck's top surface, +z up; MATES["seat"]
sits on the shell's "play" / "cue" / "slider" mates.
python3 controls.py -> build/play_cap.stl, cue_cap.stl, light_ring.stl (print 2, translucent), slider_knob.stl
  and play_cap_plain.stl, cue_cap_plain.stl: the same buttons without light rings
"""
import os
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)
CAP_RISE, RING_RISE = 2.8, 0.6

def light_ring():
    """Translucent tube + skirt (see machine_params). Print it upside down, ring face on the
    bed. Notches in the skirt's foot sit over the board's LEDs."""
    zb = M.CTRL_Z - M.H                              # board top face, top-surface frame
    tube = Pos(0, 0, -M.TOP_T - 0.01) * Cylinder(M.RING_D / 2, M.TOP_T + RING_RISE + 0.01, align=C)
    skirt = Pos(0, 0, zb) * Cylinder(M.SKIRT_D / 2, -M.TOP_T - zb, align=C)
    r = tube.fuse(skirt)
    r -= Pos(0, 0, -M.TOP_T - 1) * Cylinder(M.BTN_D / 2 + 0.25, M.TOP_T + RING_RISE + 2, align=C)
    r -= Pos(0, 0, zb - 1) * Cylinder(M.SKIRT_D / 2 - M.SKIRT_WALL, -M.TOP_T - zb + 1, align=C)
    import ctrlboard
    for a in ctrlboard.LED_ANGLES:
        r -= Pos(0, 0, zb - 0.01) * Rot(0, 0, a) * Pos(ctrlboard.LED_R, 0, 0) * Box(2.0, 3.6, 1.0, align=C)   # clears 0805 pads
    return r

def button_cap(icon, plain=False):
    """Cap, flange (chamfered 45 degrees on top) and the nub onto the switch. Print it upside
    down, top face on the bed. Lit: a BTN_D cap inside the light ring, its flange under the
    ring. Plain: no ring; a PLAIN_D cap in the top's hole, its flange under the top."""
    top_of_flange = -M.TOP_T - M.CAP_GAP
    bd = M.PLAIN_D if plain else M.BTN_D
    fd, ft = M.PLAIN_FLANGE if plain else M.CAP_FLANGE
    body = Pos(0, 0, top_of_flange) * Cylinder(bd / 2, CAP_RISE - top_of_flange, align=C)
    try:
        body = fillet(body.edges().filter_by(lambda e: e.center().Z > CAP_RISE - 0.1), 1.5)
    except Exception as e:
        print("  (cap round skipped:", e, ")")
    ch = (fd - bd) / 2
    flange = (Pos(0, 0, top_of_flange - ft) * Cylinder(fd / 2, ft - ch, align=C)).fuse(
        Pos(0, 0, top_of_flange - ch) * Cone(fd / 2, bd / 2, ch, align=C))
    nub_top = top_of_flange - ft
    nub = Pos(0, 0, -M.CAP_BOTTOM) * Cylinder(M.NUB_D / 2, nub_top + M.CAP_BOTTOM + 0.01, align=C)
    return body.fuse(flange, nub).cut(icon_solid(icon))

def icon_solid(icon):
    """The icon, as a 0.5 mm deep engraving (filled with paint in the render)."""
    z = CAP_RISE - 0.5
    if icon == "play":   # play/pause: triangle + two bars
        tri = Pos(-2.0, 0, z) * extrude(Polygon((-3, -3.5), (2.5, 0), (-3, 3.5)), 2)
        bars = [Pos(x, 0, z) * Box(1.2, 7, 2, align=C) for x in (3.2, 5.4)]
        return tri.fuse(*bars)
    return Pos(0, 0, z) * extrude(Text("CUE", 4.2, font_style=FontStyle.BOLD), 2)

def slider_knob():
    """CDJ-style tempo knob, a push fit on the pot's metal lever, KNOB_LIFT above the top.
    Tapered body, wider across the slot than along it; concave finger scoops front and
    back; flat top with an index line across it (knob_line). Prints upright, socket down."""
    kx, ky, kh = M.KNOB
    tx, ty = M.KNOB_TOP
    foot = 1.2                                       # straight skirt at the base
    base = Pos(0, 0, 0) * extrude(RectangleRounded(kx, ky, 1.6), foot)
    body = loft([Plane.XY.offset(foot) * RectangleRounded(kx, ky, 1.6),
                 Plane.XY.offset(kh) * RectangleRounded(tx, ty, 1.0)])
    k = base.fuse(body)
    R, depth = M.KNOB_SCOOP
    zc = (foot + kh) / 2 + 1.0                       # scoop centre, a little above mid-height
    yface = lambda z: ky / 2 - (ky - ty) / 2 * (z - foot) / (kh - foot)
    for s in (-1, 1):
        k -= Pos(0, s * (yface(zc) - depth + R), zc) * Rot(0, 90, 0) * Cylinder(R, kx + 2)
    try:
        k = fillet(k.edges().filter_by(lambda e: e.center().Z > kh - 0.05), 0.6)
    except Exception as e:
        print("  (knob top round skipped:", e, ")")
    sa, sl, sd = M.KNOB_SOCKET
    k = k.cut(Pos(0, 0, -1) * Box(sa, sl, sd + 1, align=C))
    return Pos(0, 0, M.KNOB_LIFT) * k

def knob_line():
    """White index line across the flat top (an inlay in the render; paint it on the print)."""
    return Pos(0, 0, M.KNOB_LIFT + M.KNOB[2] - 0.4) * Box(M.KNOB_TOP[0] - 2.5, 0.9, 0.6, align=C)

def pot(lever_y=0.0):
    """PTA6043 stand-in on the "pot" mate (slot centre, top surface): frame, lever at
    lever_y along the travel, three PC pins."""
    fx, fy, fh = M.POT_FRAME
    top = -M.TOP_T - M.POT_PAD
    frame = Pos(0, 0, top - fh) * Box(fx, fy, fh, align=C)
    frame -= [Pos(0, s * M.POT_HOLES / 2, top - 1.5) * Cylinder(1.0, 2, align=C) for s in (-1, 1)]
    la, ll, lh = M.LEVER
    lever = Pos(0, lever_y, top) * Box(la, ll, lh, align=C)
    c = M.POT_PINS / 2
    pins = [Pos(px, py, top - fh - 3.5) * Box(0.5, 0.8, 3.5, align=C)
            for px, py in ((-1.75, -c), (1.75, -c), (0, c))]
    return frame.fuse(lever, *pins)

def led():          # 3 mm indicator, flush
    return Pos(0, 0, -0.6) * Cylinder(1.0, 0.7, align=C)

MATES = {"seat": Pos(0, 0, 0)}

if __name__ == "__main__":
    import trimesh
    os.makedirs("build", exist_ok=True)
    for name, s in (("play_cap", button_cap("play")), ("cue_cap", button_cap("cue")),
                    ("light_ring", light_ring()),
                    ("play_cap_plain", button_cap("play", plain=True)), ("cue_cap_plain", button_cap("cue", plain=True)),
                    ("slider_knob", slider_knob())):
        export_stl(s, f"build/{name}.stl", tolerance=0.02, angular_tolerance=0.1)
        m = trimesh.load(f"build/{name}.stl")
        print(name, (m.bounds[1] - m.bounds[0]).round(1),
              "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
