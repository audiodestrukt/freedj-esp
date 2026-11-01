# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""make_bom.py: JLCPCB BOM + CPL for the control board, from the LCSC numbers in the design.

The skill's gen_bom.py maps passives by value against its own table and missed most of
ours, so this reads the supplier part numbers straight from tscircuit's circuit.json:
    every part with a JLCPCB number is assembled, except the through-hole JST connectors
    (hand-soldered on the bottom) and the test pads (bare copper).
Needs: dist/index/circuit.json (tsci build) and the routed index.circuit.kicad_pcb.
    python3 make_bom.py   -> fab/freedj-ctrl-bom.csv, fab/freedj-ctrl-cpl.csv, fab/freedj-ctrl-handsolder.csv
"""
import collections, csv, json, os, subprocess, tempfile

NAME = "freedj-ctrl"
HAND = ("J_",)                      # through-hole, bottom side: solder by hand
PACKAGE = {"C5187096": "TSSOP-20", "C7470157": "SMD 6x6 tact", "C15850": "0805", "C2297": "0805", "C2296": "0805"}   # the rest are 0603
d = json.load(open("dist/index/circuit.json"))
src = [c for c in d if c["type"] == "source_component"]

def value(c):
    for k in ("resistance", "capacitance", "color"):
        if c.get(k) is not None:
            v = c[k]
            if k == "resistance": return f"{v:g}".replace("e+03", "k") if v < 1000 else f"{v / 1000:g}k"
            if k == "capacitance": return f"{v * 1e9:g}nF" if v < 1e-6 else f"{v * 1e6:g}uF"
            return f"LED {v}"
    return c.get("manufacturer_part_number") or c["name"]

groups, hand = collections.OrderedDict(), []
for c in src:
    lcsc = (c.get("supplier_part_numbers") or {}).get("jlcpcb", [None])[0]
    if c["name"].startswith("TP_") or not lcsc: continue
    if c["name"].startswith(HAND):
        hand.append((c["name"], value(c), lcsc)); continue
    groups.setdefault((value(c), lcsc), []).append(c["name"])

os.makedirs("fab", exist_ok=True)
with open(f"fab/{NAME}-bom.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Comment", "Designator", "Footprint", "JLCPCB Part #"])
    for (v, lcsc), refs in groups.items(): w.writerow([v, ",".join(refs), PACKAGE.get(lcsc, "0603"), lcsc])
with open(f"fab/{NAME}-handsolder.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Designator", "Part", "LCSC", "Note"])
    for n, v, l in hand: w.writerow([n, v, l, "JST-XH, solder the housing on the BOTTOM side (outline on the bottom silkscreen)"])

placed = {r for refs in groups.values() for r in refs}
raw = tempfile.mktemp(suffix=".csv")
subprocess.run(["kicad-cli", "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
                "-o", raw, "index.circuit.kicad_pcb"], check=True, capture_output=True)
with open(raw) as fi, open(f"fab/{NAME}-cpl.csv", "w", newline="") as fo:
    w = csv.writer(fo); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"]); n = 0
    for r in csv.DictReader(fi):
        if r["Ref"] in placed:
            w.writerow([r["Ref"], r["PosX"], r["PosY"], "Top" if r["Side"] == "top" else "Bottom", r["Rot"]]); n += 1
os.remove(raw)
print(f"BOM: {sum(map(len, groups.values()))} parts in {len(groups)} lines; CPL: {n} placed; hand-solder: {len(hand)}")
