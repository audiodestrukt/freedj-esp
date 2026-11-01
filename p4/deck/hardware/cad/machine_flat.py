# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""machine_flat.py: the flat-top freedj-esp. Same parts and params as machine.py, with
the screen lying flat in the top instead of on a 12 degree bezel, so the shell prints
top-down on the bed with no supports.
python3 machine_flat.py -> build/freedj_esp_flat.glb, build/freedj_esp_flat_cut.glb
"""
import os
os.environ["FREEDJ_CONFIG"] = "flat"   # must be set before the params are imported
import machine

if __name__ == "__main__":
    machine.build()
