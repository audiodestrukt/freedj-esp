"""controls.py: CUE/PLAY buttons with light rings, the pitch-slider knob and pot,
indicator LEDs. Local frame: z = 0 on the deck's top surface, +z up; MATES["seat"]
sits on the shell's "play" / "cue" / "slider" mates.
python3 controls.py -> build/play_cap.stl, cue_cap.stl, light_ring.stl (print 2, translucent), slider_knob.stl
"""
import os
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)
CAP_RISE, RING_RISE = 2.8, 0.6

def light_ring():   # translucent PETG/resin, lit by an LED underneath
    h = RING_RISE + 8
    return Pos(0, 0, RING_RISE - h) * (Cylinder(M.RING_D / 2, h, align=C)
                                       - Pos(0, 0, -1) * Cylinder(M.BTN_D / 2 + 0.25, h + 2, align=C))

def button_cap(icon):
    h = CAP_RISE + M.CAP_BOTTOM      # underside rests on the tact switch's actuator
    cap = Pos(0, 0, CAP_RISE - h) * Cylinder(M.BTN_D / 2, h, align=C)
    try:
        cap = fillet(cap.edges().filter_by(lambda e: e.center().Z > CAP_RISE - 0.1), 1.5)
    except Exception as e:
        print("  (cap round skipped:", e, ")")
    return cap.cut(icon_solid(icon))

def icon_solid(icon):
    """The icon, as a 0.5 mm deep engraving (filled with paint in the render)."""
    z = CAP_RISE - 0.5
    if icon == "play":   # play/pause: triangle + two bars
        tri = Pos(-2.0, 0, z) * extrude(Polygon((-3, -3.5), (2.5, 0), (-3, 3.5)), 2)
        bars = [Pos(x, 0, z) * Box(1.2, 7, 2, align=C) for x in (3.2, 5.4)]
        return tri.fuse(*bars)
    return Pos(0, 0, z) * extrude(Text("CUE", 4.2, font_style=FontStyle.BOLD), 2)

def slider_knob():
    """Sits KNOB_LIFT above the top on the pot's metal lever (a push fit in the socket)."""
    kx, ky, kh = M.KNOB
    k = Box(kx, ky, kh, align=C)
    try:
        k = fillet(k.edges().filter_by(Axis.Z), 2.0)
    except Exception as e:
        print("  (knob round skipped:", e, ")")
    k = k.cut(Pos(0, ky / 2 - 1, kh + 12 - 1.6) * Rot(0, 90, 0) * Cylinder(12, kx + 2))   # finger dish, back edge
    sa, sl, sd = M.KNOB_SOCKET
    k = k.cut(Pos(0, 0, -1) * Box(sa, sl, sd + 1, align=C))
    return Pos(0, 0, M.KNOB_LIFT) * k

def knob_line():
    return Pos(0, -2.5, M.KNOB_LIFT + M.KNOB[2] - 0.4) * Box(M.KNOB[0] - 1.5, 0.9, 0.6, align=C)   # index line across the top

def pot(lever_y=0.0):
    """PTA4543 stand-in on the "pot" mate (slot centre, top surface): frame, lever at
    lever_y along the travel, three PC pins."""
    fx, fy, fh = M.POT_FRAME
    top = -M.TOP_T - M.POT_PAD
    frame = Pos(0, 0, top - fh) * Box(fx, fy, fh, align=C)
    frame -= [Pos(0, s * M.POT_HOLES / 2, top - 1.5) * Cylinder(1.0, 2, align=C) for s in (-1, 1)]
    la, ll, lh = M.LEVER
    lever = Pos(0, lever_y, top) * Box(la, ll, lh, align=C)
    pins = [Pos(px, py, top - fh - 3.5) * Box(0.5, 0.8, 3.5, align=C)
            for px, py in ((-1.75, -23.9), (1.75, -23.9), (0, 23.9))]
    return frame.fuse(lever, *pins)

def led():          # 3 mm indicator, flush
    return Pos(0, 0, -0.6) * Cylinder(1.0, 0.7, align=C)

MATES = {"seat": Pos(0, 0, 0)}

if __name__ == "__main__":
    import trimesh
    os.makedirs("build", exist_ok=True)
    for name, s in (("play_cap", button_cap("play")), ("cue_cap", button_cap("cue")),
                    ("light_ring", light_ring()), ("slider_knob", slider_knob())):
        export_stl(s, f"build/{name}.stl", tolerance=0.02, angular_tolerance=0.1)
        m = trimesh.load(f"build/{name}.stl")
        print(name, (m.bounds[1] - m.bounds[0]).round(1),
              "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
