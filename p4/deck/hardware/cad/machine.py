"""machine.py: the whole ESP Deck, every part placed by its mates, exported as a GLB
for rendering (metres, Y-up) plus a cutaway through the jog axis.

python3 machine.py -> build/esp_deck.glb, build/esp_deck_cut.glb
"""
import os, sys
import numpy as np
import trimesh
from trimesh.visual.material import PBRMaterial
from PIL import Image
from build123d import *
import machine_params as M
from machine_params import place
import shell, base, jog, controls, stock

# name: (rgba, metallic, roughness, emissive rgb or None)
LOOK = {
    "shell":    ((0.075, 0.08, 0.09, 1), 0, 0.55, None),
    "base":     ((0.05, 0.05, 0.055, 1), 0, 0.6, None),
    "feet":     ((0.02, 0.02, 0.02, 1), 0, 0.9, None),
    "platter":  ((0.035, 0.035, 0.04, 1), 0, 0.42, None),
    "touch":    ((0.78, 0.79, 0.81, 1), 1, 0.32, None),
    "cap":      ((0.10, 0.10, 0.11, 1), 0, 0.35, None),
    "marker":   ((1.0, 0.55, 0.12, 1), 0, 0.4, (1.0, 0.45, 0.08)),
    "steel":    ((0.62, 0.63, 0.65, 1), 1, 0.3, None),
    "bearing":  ((0.62, 0.63, 0.65, 1), 1, 0.3, None),
    "magnet":   ((0.35, 0.35, 0.37, 1), 1, 0.4, None),
    "pcb":      ((0.05, 0.22, 0.12, 1), 0, 0.5, None),
    "chip":     ((0.02, 0.02, 0.02, 1), 0, 0.4, None),
    "btn":      ((0.06, 0.06, 0.065, 1), 0, 0.75, None),
    "ring_cue": ((1.0, 0.55, 0.1, 1), 0, 0.3, (1.0, 0.42, 0.05)),
    "ring_play": ((0.2, 0.95, 0.4, 1), 0, 0.3, (0.1, 0.9, 0.3)),
    "icon_cue": ((1.0, 0.6, 0.2, 1), 0, 0.5, None),
    "icon_play": ((0.85, 0.87, 0.88, 1), 0, 0.5, None),
    "knob":     ((0.03, 0.03, 0.035, 1), 0, 0.45, None),
    "white":    ((0.9, 0.9, 0.9, 1), 0, 0.5, None),
    "led_green": ((0.2, 1.0, 0.4, 1), 0, 0.3, (0.1, 1.0, 0.3)),
    "led_blue": ((0.3, 0.6, 1.0, 1), 0, 0.3, (0.2, 0.5, 1.0)),
    "glass":    ((0.02, 0.02, 0.025, 0.35), 0, 0.04, None),
    "lcd":      ((0.02, 0.02, 0.02, 1), 0, 0.5, None),
    "usb_blue": ((0.1, 0.25, 0.8, 1), 0, 0.5, None),
    "rca_red":  ((0.75, 0.08, 0.06, 1), 0, 0.5, None),
    "rca_white": ((0.85, 0.85, 0.85, 1), 0, 0.5, None),
    "gold":     ((0.95, 0.75, 0.35, 1), 1, 0.3, None),
}

def scene_parts():
    S = shell.MATES
    out = []
    add = lambda name, solid, look: out.append((name, solid, look))
    add("shell", shell.part(), "shell")
    add("base", place(base.part(), base.MATES["to_shell"], S["base"]), "base")
    for i, f in enumerate(base.feet()):
        add(f"foot{i}", f, "feet")
    # jog stack: each part seated on the axis at its STACK height
    axis = Pos(*M.JOG_C, 0)
    seat = lambda key: axis * Pos(0, 0, jog.STACK[key])
    for key, fn, look in (("platter", jog.platter, "platter"), ("touch_plate", jog.touch_plate, "touch"),
                          ("center_cap", jog.center_cap, "cap"), ("cap_marker", jog.cap_marker, "marker"),
                          ("inertia_ring", jog.inertia_ring, "steel"), ("bearing_top", jog.bearing, "bearing"),
                          ("bearing_bot", jog.bearing, "bearing"), ("eclip", jog.eclip, "steel"),
                          ("shaft", jog.shaft, "steel"), ("magnet", jog.magnet, "magnet"),
                          ("encoder_pcb", jog.encoder_pcb, "pcb"), ("encoder_chip", jog.encoder_chip, "chip")):
        add(key, place(fn(), jog.MATES["seat"], seat(key)), look)
    # buttons
    for which in ("play", "cue"):
        at = S[which]; P = lambda s: place(s, controls.MATES["seat"], at)
        add(f"{which}_ring", P(controls.light_ring()), f"ring_{which}")
        add(f"{which}_cap", P(controls.button_cap(which)), "btn")
        fill = controls.icon_solid(which) & Pos(0, 0, controls.CAP_RISE - 0.5) * Box(20, 20, 0.45, align=(Align.CENTER, Align.CENTER, Align.MIN))
        add(f"{which}_icon", P(fill), f"icon_{which}")
        add(f"{which}_switch", P(controls.switch()), "chip")
    at = S["slider"]; P = lambda s: place(s, controls.MATES["seat"], at)
    add("slider_knob", P(controls.slider_knob()), "knob")
    add("slider_line", P(controls.knob_line()), "white")
    add("slider_pot", place(controls.pot(), controls.MATES["seat"], Pos(M.SLIDER_X, sum(M.SLIDER_Y) / 2, M.H)), "chip")
    add("tempo_led", place(controls.led(), controls.MATES["seat"], S["slider_led"]), "led_green")
    add("usb_led", place(controls.led(), controls.MATES["seat"], S["usb_led"]), "led_blue")
    # screen + connectors
    seat_on = lambda s, m: place(s, stock.MATES["seat"], S[m])
    add("glass", seat_on(stock.glass(), "glass"), "glass")
    add("lcd_module", seat_on(stock.lcd_module(), "lcd"), "lcd")
    add("usb_a", seat_on(stock.usb_a(), "usb_a"), "steel")
    add("usb_a_tongue", seat_on(stock.usb_a_tongue(), "usb_a"), "usb_blue")
    add("rj45", seat_on(stock.rj45(), "rj45"), "steel")
    add("usb_c", seat_on(stock.usb_c(), "usb_c"), "steel")
    for side, look in (("rca_l", "rca_white"), ("rca_r", "rca_red")):
        add(side, seat_on(stock.rca(), side), "gold")
        add(side + "_ring", seat_on(stock.rca_ring(), side), look)
    return out

def screen_mesh():
    """The LCD image as a textured quad just under the cover glass."""
    aw, ah = M.LCD_ACTIVE
    loc = shell.MATES["glass"] * Pos(0, 0, -0.05)
    pts = []
    for x, y in ((-aw / 2, -ah / 2), (aw / 2, -ah / 2), (aw / 2, ah / 2), (-aw / 2, ah / 2)):
        p = (loc * Pos(x, y, 0)).position; pts.append((p.X, p.Y, p.Z))
    img = Image.open("build/screen.png").convert("RGB")
    mat = PBRMaterial(name="screen", baseColorTexture=img, emissiveTexture=img,
                      emissiveFactor=[1.0, 1.0, 1.0], metallicFactor=0, roughnessFactor=0.2)
    uv = np.array([(0, 0), (1, 0), (1, 1), (0, 1)])
    return trimesh.Trimesh(vertices=np.array(pts), faces=[[0, 1, 2], [0, 2, 3]],
                           visual=trimesh.visual.TextureVisuals(uv=uv, material=mat), process=False)

CUT_LOOK = {   # section view: lighter plastics so the cut faces read
    "shell": ((0.42, 0.44, 0.47, 1), 0, 0.6, None), "base": ((0.30, 0.31, 0.33, 1), 0, 0.6, None),
    "platter": ((0.16, 0.16, 0.18, 1), 0, 0.5, None), "steel": ((0.80, 0.81, 0.83, 1), 0.4, 0.4, None), "bearing": ((0.55, 0.65, 0.85, 1), 0.4, 0.4, None),
    "magnet": ((0.75, 0.2, 0.2, 1), 0, 0.4, None), "pcb": ((0.1, 0.5, 0.25, 1), 0, 0.5, None),
}

def export(parts, path, with_screen=True, look=None):
    look = {**LOOK, **(look or {})}
    scene = trimesh.Scene()
    for name, solid, lk in parts:
        f = f"build/viz_{name}.stl"
        export_stl(solid, f, tolerance=0.02, angular_tolerance=0.08)
        m = trimesh.load(f)
        rgba, metal, rough, emis = look[lk]
        kw = dict(name=lk, baseColorFactor=rgba, metallicFactor=metal, roughnessFactor=rough,
                  alphaMode="BLEND" if rgba[3] < 1 else "OPAQUE", doubleSided=True)
        if emis: kw["emissiveFactor"] = list(emis)
        m.visual = trimesh.visual.TextureVisuals(material=PBRMaterial(**kw))
        scene.add_geometry(m, geom_name=name)
    if with_screen:
        scene.add_geometry(screen_mesh(), geom_name="screen")
    scene.apply_transform(trimesh.transformations.scale_matrix(0.001))                  # mm -> m
    scene.apply_transform(trimesh.transformations.rotation_matrix(-np.pi / 2, [1, 0, 0]))  # Z-up -> Y-up
    scene.export(path)

def manifest(parts):
    lo = np.array([1e9] * 3); hi = -lo
    for name, solid, _ in parts:
        bb = solid.bounding_box(); lo = np.minimum(lo, bb.min.to_tuple()); hi = np.maximum(hi, bb.max.to_tuple())
    print(f"{len(parts)} parts, assembly bbox {np.round(hi - lo, 1)} mm (min {np.round(lo, 1)})")

if __name__ == "__main__":
    os.makedirs("build", exist_ok=True)
    parts = scene_parts()
    manifest(parts)
    export(parts, "build/esp_deck.glb")
    # cutaway: keep x <= jog axis, i.e. a section through the jog module
    keep = Pos(M.JOG_C[0], M.D / 2, 0) * Box(400, 400, 200, align=(Align.MAX, Align.CENTER, Align.CENTER))
    cut = []
    for name, solid, look in parts:
        bb = solid.bounding_box()
        if bb.min.X > M.JOG_C[0]: continue
        if bb.max.X <= M.JOG_C[0]: cut.append((name, solid, look)); continue
        s = solid & keep
        if s.volume > 0.01: cut.append((name, s, look))
    export(cut, "build/esp_deck_cut.glb", with_screen=False, look=CUT_LOOK)
    print("wrote build/esp_deck.glb, build/esp_deck_cut.glb")
