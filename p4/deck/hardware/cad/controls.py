"""controls.py: CUE/PLAY buttons with light rings, the pitch-slider knob and pot,
indicator LEDs. Local frame: z = 0 on the deck's top surface, +z up; MATES["seat"]
sits on the shell's "play" / "cue" / "slider" mates.
python3 controls.py -> build/button_cap.stl, build/slider_knob.stl
"""
import os
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)
CAP_RISE, RING_RISE = 2.8, 0.6
SWITCH = (12.0, 12.0, 7.0)

def light_ring():   # translucent PETG/resin, lit by an LED underneath
    h = RING_RISE + 8
    return Pos(0, 0, RING_RISE - h) * (Cylinder(M.RING_D / 2, h, align=C)
                                       - Pos(0, 0, -1) * Cylinder(M.BTN_D / 2 + 0.25, h + 2, align=C))

def button_cap(icon):
    h = CAP_RISE + 6
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

def switch():       # Cherry-MX-footprint key switch stand-in, below the top
    return Pos(0, 0, -M.TOP_T - 8 - SWITCH[2]) * Box(*SWITCH, align=C)

def slider_knob():
    kx, ky, kh = M.KNOB
    k = Box(kx, ky, kh, align=C)
    try:
        k = fillet(k.edges().filter_by(Axis.Z), 2.0)
    except Exception as e:
        print("  (knob round skipped:", e, ")")
    k = k.cut(Pos(0, ky / 2 - 1, kh + 12 - 1.6) * Rot(0, 90, 0) * Cylinder(12, kx + 2))   # finger dish, back edge
    stem = Pos(0, 0, -6) * Box(1.6, 3, 7, align=C)
    return k.fuse(stem)

def knob_line():
    return Pos(0, -2.5, M.KNOB[2] - 0.4) * Box(M.KNOB[0] - 1.5, 0.9, 0.6, align=C)   # index line across the top

def pot():          # 45 mm slide pot body under the top
    return Pos(0, 0, -M.TOP_T - 8) * Box(9, 60, 7, align=C)

def led():          # 3 mm indicator, flush
    return Pos(0, 0, -0.6) * Cylinder(1.0, 0.7, align=C)

MATES = {"seat": Pos(0, 0, 0)}

if __name__ == "__main__":
    import trimesh
    os.makedirs("build", exist_ok=True)
    for name, s in (("button_cap", button_cap("play")), ("cue_cap", button_cap("cue")), ("slider_knob", slider_knob())):
        export_stl(s, f"build/{name}.stl", tolerance=0.02, angular_tolerance=0.1)
        m = trimesh.load(f"build/{name}.stl")
        print(name, (m.bounds[1] - m.bounds[0]).round(1),
              "bodies:", len(m.split(only_watertight=False)), "watertight:", m.is_watertight)
