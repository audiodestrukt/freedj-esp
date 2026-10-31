"""machine_flat.py: the flat-top ESP Deck. Same parts and params as machine.py, with
the screen lying flat in the top instead of on a 12 degree bezel, so the shell prints
top-down on the bed with no supports.
python3 machine_flat.py -> build/esp_deck_flat.glb, build/esp_deck_flat_cut.glb
"""
import os
os.environ["ESP_DECK_CONFIG"] = "flat"   # must be set before the params are imported
import machine

if __name__ == "__main__":
    machine.build()
