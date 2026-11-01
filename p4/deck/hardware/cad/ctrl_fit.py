# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""ctrl_fit.py: the populated control board (../ctrl-pcb) in the deck, for a test fit.

Source: tscircuit's GLB export of the board, which carries every part's real 3D body
(EasyEDA/JLCPCB models). Its STEP export can't merge those models, so GLB it is.
Registration: the board's own origin, not a bounding box. The design's centre is deck
CTRL_XY's centre by construction (gen_geom.py), and its top face sits at CTRL_Z.
The GLB frame is Y-up: board (x, y, z) = (-X, Z, Y - 0.7) (0.7 = half its 1.4 mm plate).

One model is wrong: the switch (ZX-QC66-7.5TP, C7470157) is drawn 5.0 mm tall, but the part
is 7.5 mm (LCSC). Its body is replaced with a 6 x 6 x 3.5 body + Ø3.5 actuator to 7.5 mm.

    FREEDJ_CONFIG=flat python3 ctrl_fit.py   [--buttons lit|plain]
writes build/ctrl_board_pop{SUFFIX}.stl   board in the deck frame (drops onto the shell STLs)
       build/ctrl_board_pop_local.stl     board alone: centre (0,0), top face z = 0
       build/ctrl_board_print.stl         printable stand-in: one solid, no connectors, flat on the bed
       build/ctrl_fit{SUFFIX}.glb         deck + populated board, real part colours
       build/ctrl_fit{SUFFIX}_cut.glb     section through the button centres (x = 16)
and prints every part's clearance clash (convex hulls, so it errs toward reporting one).
"""
import os, sys, subprocess, argparse
import numpy as np
import trimesh
import machine_params as M

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, "..", "ctrl-pcb")
GLB = os.path.join(PCB, "build", "pop.glb")
x0, y0, x1, y1 = M.CTRL_XY
CX, CY = (x0 + x1) / 2, (y0 + y1) / 2
HALF_T = 0.7                     # the GLB's board plate is 1.4 thick, centred on Y = 0


def export_glb():
    env = {**os.environ, "PATH": f"{os.environ['HOME']}/.bun/bin:{os.environ.get('PATH', '')}"}
    r = subprocess.run([os.path.join(PCB, "node_modules", ".bin", "tsci"), "export", "index.circuit.tsx",
                        "-f", "glb", "-o", "build/pop.glb"], cwd=PCB, env=env, capture_output=True, text=True, timeout=300)
    if not os.path.exists(GLB):
        sys.exit("tsci glb export failed:\n" + r.stdout[-2000:] + r.stderr[-2000:])


def board_meshes(local=False):
    """[(name, trimesh)] in the deck frame (or board-local with local=True), switch fixed."""
    sc = trimesh.load(GLB)
    # GLB (X, Y, Z) -> board (-X, Z, Y - 0.7): a proper rotation, plus the plate offset
    R = np.array([[-1, 0, 0, 0], [0, 0, 1, 0], [0, 1, 0, -HALF_T], [0, 0, 0, 1]], float)
    to_deck = np.eye(4) if local else trimesh.transformations.translation_matrix((CX, CY, M.CTRL_Z))
    out = []
    for node in sc.graph.nodes_geometry:
        T, g = sc.graph[node]
        name = node.rsplit("_", 1)[0]
        if name.startswith("SW_"):
            continue                                   # replaced below
        m = sc.geometry[g].copy()
        m.apply_transform(to_deck @ R @ T)
        out.append((name, m))
    bx, by, bh, top = M.TACT
    for key, c in (("SW_PLAY", M.PLAY_C), ("SW_CUE", M.CUE_C)):
        px, py = (c[0] - CX, c[1] - CY) if local else c
        z = 0.0 if local else M.CTRL_Z
        body = trimesh.creation.box((bx, by, bh)); body.apply_translation((px, py, z + bh / 2))
        act = trimesh.creation.cylinder(1.75, top - bh, sections=32); act.apply_translation((px, py, z + bh + (top - bh) / 2))
        for m, tint in ((body, (25, 25, 28, 255)), (act, (60, 60, 64, 255))):
            m.visual.face_colors = tint
        out += [(key, body), (key + "_act", act)]
    return out


def print_board(path="build/ctrl_board_print.stl"):
    """A printable stand-in: the real 1.6 mm plate with its screw holes, every top-side part
    as its convex hull (switches at their true 7.5 mm), no connectors. One watertight body,
    bottom face on z = 0, so it prints flat with no supports."""
    from build123d import export_stl, Pos
    import ctrlboard
    plate = Pos(-CX, -CY, -M.CTRL_Z + M.CTRL_T) * ctrlboard.board()      # top face at z = CTRL_T
    export_stl(plate, "build/_plate.stl", tolerance=0.02)
    body = trimesh.load("build/_plate.stl")
    tops = []
    for name, m in board_meshes(local=True):
        if name.startswith("J_") or m.bounds[1][2] <= 0.05:
            continue                                   # connectors; pads and silk (flat, at z 0)
        h = m.convex_hull
        if not h.is_volume:
            continue
        h.apply_translation((0, 0, M.CTRL_T))          # board-local top face (z 0) -> plate top
        tops.append(h)
    out = trimesh.boolean.union([body] + tops, engine="manifold")
    out.export(path)
    os.remove("build/_plate.stl")
    print(f"printable board: {np.round(out.extents, 1)} mm, {len(tops)} part bodies, "
          f"watertight {out.is_watertight}, bodies {len(out.split(only_watertight=False))} -> {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--buttons", default=os.environ.get("FREEDJ_BUTTONS", "lit"))
    ap.add_argument("--no-export", action="store_true", help="reuse ../ctrl-pcb/build/pop.glb")
    a = ap.parse_args()
    os.environ["FREEDJ_BUTTONS"] = a.buttons
    if not a.no_export or not os.path.exists(GLB):
        export_glb()
    os.makedirs("build", exist_ok=True)

    parts = board_meshes()
    trimesh.util.concatenate([m for _, m in parts]).export(f"build/ctrl_board_pop{M.SUFFIX}.stl")
    trimesh.util.concatenate([m for _, m in board_meshes(local=True)]).export("build/ctrl_board_pop_local.stl")
    print_board()
    allb = trimesh.util.concatenate([m for _, m in parts])
    print(f"populated board: {np.round(allb.extents, 1)} mm, z {allb.bounds[0][2]:.1f}..{allb.bounds[1][2]:.1f}"
          f" -> build/ctrl_board_pop{M.SUFFIX}.stl, build/ctrl_board_pop_local.stl")

    # clash: every board part (convex hull) against the enclosure parts near it
    import machine
    from build123d import export_stl
    stand_ins = ("ctrl_board", "play_switch", "cue_switch", "play_led", "cue_led", "j_", "u1")
    scene = [(n, s, lk) for n, s, lk in machine.scene_parts() if not n.startswith(stand_ins)]
    region = (x0 - 3, y0 - 3, x1 + 3, y1 + 3)
    near = []
    for n, s, lk in scene:
        bb = s.bounding_box()
        if bb.max.X < region[0] or bb.min.X > region[2] or bb.max.Y < region[1] or bb.min.Y > region[3]:
            continue
        f = f"build/viz_{n}.stl"; export_stl(s, f, tolerance=0.02, angular_tolerance=0.1)
        near.append((n, trimesh.load(f)))
    hits = 0
    for pn, pm in parts:
        hull = pm.convex_hull
        if not hull.is_volume:
            continue                                   # flat decals (silk, pads), not bodies
        for en, em in near:
            if not (hull.bounds[0] < em.bounds[1]).all() or not (em.bounds[0] < hull.bounds[1]).all():
                continue
            v = hull.intersection(em, engine="manifold").volume if em.is_watertight else 0
            if v > 0.05:
                print(f"  CLASH {pn} x {en}: {v:.2f} mm3"); hits += 1
    print(f"{M.CONFIG}, {a.buttons} buttons: {hits} clash(es) between the populated board and the enclosure")

    # render scene: the deck with the populated board in real colours, plus a section at x = 16
    machine.export(scene, f"build/ctrl_fit{M.SUFFIX}.glb")
    sec = []
    from build123d import Box, Pos, Align
    keep = Pos(-5, -5, -5) * Box(M.PLAY_C[0] + 5, 200, 120, align=(Align.MIN, Align.MIN, Align.MIN))
    for n, s, lk in scene:
        bb = s.bounding_box()
        if bb.min.X > M.PLAY_C[0] or bb.min.Y > y1 + 10:
            continue
        try:
            c = s & keep
            if c.volume > 0.01: sec.append((n, c, lk))
        except ValueError:
            pass
    machine.export(sec, f"build/ctrl_fit{M.SUFFIX}_cut.glb", with_screen=False, look=machine.CUT_LOOK)
    for path, clip in ((f"build/ctrl_fit{M.SUFFIX}.glb", None), (f"build/ctrl_fit{M.SUFFIX}_cut.glb", M.PLAY_C[0])):
        sc = trimesh.load(path)
        to_y_up = trimesh.transformations.rotation_matrix(-np.pi / 2, [1, 0, 0]) @ trimesh.transformations.scale_matrix(0.001)
        for pn, pm in parts:
            m = pm.copy()
            if clip is not None:
                if m.bounds[0][0] > clip:
                    continue
                if m.bounds[1][0] > clip:
                    m = m.slice_plane((clip, 0, 0), (-1, 0, 0), cap=m.is_watertight)
                    if m is None or len(m.faces) == 0:
                        continue
            m.apply_transform(to_y_up)
            sc.add_geometry(m, geom_name=f"pcb_{pn}")
        sc.export(path)
    print(f"wrote build/ctrl_fit{M.SUFFIX}.glb, build/ctrl_fit{M.SUFFIX}_cut.glb")


if __name__ == "__main__":
    main()
