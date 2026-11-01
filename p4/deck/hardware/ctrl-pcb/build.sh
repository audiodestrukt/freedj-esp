#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
# build.sh: control board, end to end. CAD anchors -> tscircuit placement -> route -> finish ->
# gates -> fab bundle. Needs the pcb-layout skill's scripts as ./scripts (a symlink, not committed):
#   ln -s ~/.claude/skills/pcb-layout/scripts scripts
set -eu
cd "$(dirname "$0")"
export PATH="$HOME/.bun/bin:$PATH"
KPY=/usr/bin/python3                      # KiCad's pcbnew lives in the system python

python3 gen_geom.py                       # anchors from ../cad
./node_modules/.bin/tsci build index.circuit.tsx > build/build.log 2>&1
# route: Freerouting + the capacity router, best applied (bottom GND pour)
POUR="GND:B.Cu" MAXT=${MAXT:-120} bash scripts/route2.sh 2>&1 | grep -E "^\| [12] |SUMMARY|RESULT" || true

# drop dead copper islands, finish the open tail with TraceMaker, drop islands again
islands() { "$KPY" - "$1" <<'PY'
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
for z in b.Zones(): z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
print("pour pieces:", [z.GetFilledPolysList(z.GetLayer()).OutlineCount() for z in b.Zones()])
b.Save(sys.argv[1])
PY
}
islands index.circuit.kicad_pcb
cp index.circuit.kicad_pro build/finished.kicad_pro
python3 scripts/finish_legal.py index.circuit.kicad_pcb -o build/finished.kicad_pcb --time 30 2>&1 | tail -1
islands build/finished.kicad_pcb
cp build/finished.kicad_pcb index.circuit.kicad_pcb

# gates
python3 scripts/drc_check.py index.circuit.kicad_pcb | tail -1
python3 scripts/dfm_check.py index.circuit.kicad_pcb 2>&1 | grep -E "✓|✗"
"$KPY" scripts/check_floating.py index.circuit.kicad_pcb 2>&1 | tail -1

# fab: Gerbers + drill (skill), BOM + CPL from the design's LCSC numbers (ours)
bash scripts/make_fab.sh index.circuit.kicad_pcb freedj-ctrl jlcpcb > build/fab.log 2>&1
rm -f fab/*.gbr fab/*.g?? fab/*.gm1 fab/*.drl fab/*.gbrjob fab/freedj-ctrl-bom-pcbway.csv   # loose copies; the zip has them
python3 make_bom.py
