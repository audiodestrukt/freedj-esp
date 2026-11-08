# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""build_all.py: build the printable parts of every deck variant, one folder each.

    python3 build_all.py                    # every variant + build/common/
    python3 build_all.py a1s-flat p4-4in-flat
    python3 build_all.py --panel --glb      # also the panel-button shells and the assemblies
    python3 build_all.py --list             # variant names

Writes build/<variant>/{shell,base,platter,jog_test_*}.stl and a PRINT.md listing exactly
which files to print for that variant (its own plus the ones from build/common/).
"""
import argparse, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def run(script, variant=None, **env):
    e = dict(os.environ, **env)
    for k in ("FREEDJ_VARIANT", "FREEDJ_BOARD", "FREEDJ_SCREEN", "FREEDJ_CONFIG"):
        e.pop(k, None)
    if variant:
        e["FREEDJ_VARIANT"] = variant
    e.update(env)
    r = subprocess.run([sys.executable, *script.split()], cwd=HERE, env=e, capture_output=True, text=True)
    tag = f"[{variant or 'common'}{' ' + ' '.join(f'{k}={v}' for k, v in env.items()) if env else ''}] {script}"
    if r.returncode:
        print(f"FAIL {tag}\n{r.stdout}{r.stderr}", flush=True)
        return False
    lines = [l for l in r.stdout.splitlines() if "watertight" in l or l.startswith("wrote")]
    print(f"ok   {tag}" + "".join(f"\n       {l}" for l in lines), flush=True)
    return True


def manifest():
    """Runs inside a variant (FREEDJ_VARIANT set): writes build/<variant>/PRINT.md."""
    import machine_params as M
    v = M.VARIANT
    common = os.path.join("..", "common")
    rows = [("shell.stl", "1", "top shell; upside down" + ("" if M.CONFIG == "flat" else ", supports under the bezel")),
            ("base.stl", "1", "base plate"),
            ("platter.stl", "1", f"{M.JOG_D:.0f} mm jog, shaft printed on; upside down, shaft up"),
            (f"{common}/play_cap.stl, {common}/cue_cap.stl", "1 each", "lit buttons"),
            (f"{common}/light_ring.stl", "2", "translucent (amber, green)"),
            (f"{common}/slider_knob_{M.POT}.stl", "1", f"for the {M.POT.upper()} pitch pot"),
            ("jog_test_*.stl", "optional", "magnet gap test discs (1.1 / 0.8 / 0.5 mm)"),
            (f"{common}/fit_gauge.stl, {common}/magnet_gauge.stl", "optional", "tune the shaft and magnet fits first")]
    alts = [("shell_panel.stl", "16 mm panel-mount buttons instead of the lit caps (`FREEDJ_BUTTONS=panel`)"),
            (f"{common}/play_cap_plain.stl, {common}/cue_cap_plain.stl", "plain caps, no light rings"),
            ("platter_pioneer.stl", "Pioneer-style platter (`python3 pioneer_platter.py`)")]
    if M.SCREEN == "4":
        alts.append((f"{common}/screen_adapter_32.stl", "holds a 3.2\" MSP3223 in the 4\" window"))
    with open(M.out("PRINT.md"), "w") as f:
        f.write(f"# {v}\n\n{M.BOARD.upper()} board, {M.SCREEN}\" screen, {M.CONFIG} top. "
                f"{M.W:.0f} x {M.D:.1f} x {M.H:.0f} mm.\n"
                f"Rebuild: `FREEDJ_VARIANT={v} python3 shell.py` (and base.py, jog.py), "
                f"or `python3 build_all.py {v}`.\n\n"
                "## Print\n\n| File | Qty | Notes |\n|---|---|---|\n")
        f.writelines(f"| `{a}` | {b} | {c} |\n" for a, b, c in rows)
        f.write("\n## Alternatives\n\n")
        f.writelines(f"- `{a}`: {b}\n" for a, b in alts)
        f.write("\nAssembly views (not printed): `deck.glb`, `deck_cut.glb` from "
                f"`FREEDJ_VARIANT={v} python3 machine.py`.\n")
    print("wrote", M.out("PRINT.md"))


def main():
    import machine_params as M
    ap = argparse.ArgumentParser()
    ap.add_argument("variants", nargs="*", help="default: all")
    ap.add_argument("--panel", action="store_true", help="also build shell_panel.stl per variant")
    ap.add_argument("--glb", action="store_true", help="also build the assembly GLBs (slow)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("-j", type=int, default=2, help="parallel jobs (each takes ~1 GB)")
    ap.add_argument("--manifest", action="store_true", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.manifest:
        return manifest()
    if a.list:
        print("\n".join(M.VARIANTS)); return
    vs = a.variants or list(M.VARIANTS)
    for v in vs:
        assert v in M.VARIANTS, f"{v}: one of {', '.join(M.VARIANTS)}"

    jobs = [("controls.py", None, {"FREEDJ_POT": "pta6043"}), ("controls.py --knob", None, {"FREEDJ_POT": "sc6080"}),
            ("ctrlboard.py", None, {}), ("screen_adapter_32.py", "p4-4in-angled", {})]
    for v in vs:
        jobs += [("shell.py", v, {}), ("base.py", v, {}), ("jog.py", v, {}), ("build_all.py --manifest", v, {})]
        if a.panel:
            jobs.append(("shell.py", v, {"FREEDJ_BUTTONS": "panel"}))
    with ThreadPoolExecutor(a.j) as ex:
        ok = all(ex.map(lambda j: run(j[0], j[1], **j[2]), jobs))
    if a.glb:   # the assemblies reuse build/common/screen.png; run them one at a time
        ok = run("screen_mock.py") and ok
        for v in vs:
            ok = run("machine.py", v) and ok
    print("all ok" if ok else "SOME FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
