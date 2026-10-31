"""machine_params.py: every shared ESP Deck enclosure dimension (mm), plus place().

Frame: x across the deck (0 = left edge), y front-to-back (0 = the edge nearest you),
z up (0 = underside of the shell). Sized around the 4" MSP4021 (480x320 ST7796S,
capacitive touch) the deck runs on today.

Two configurations, picked by ESP_DECK_CONFIG before this module is imported:
  angled (default)  the CDJ look: screen bezel tilted 12 degrees
  flat              the whole top flat, so the shell prints top-down with no supports
"""
import math, os
from build123d import Location, Pos, Rot

# Body
W, D = 160.0, 205.0          # footprint (195 for a 3.5" panel; the 4" glass is 61 mm deep)
H = 34.0                     # height of the flat top (jog area)
CORNER_R = 10.0              # plan-view corner radius
WALL = 2.5                   # side walls
TOP_T = 4.0                  # top plate (stiff: the jog boss hangs off it)
EDGE_R = 2.0                 # top-edge round

CONFIG = os.environ.get("ESP_DECK_CONFIG", "angled")
assert CONFIG in ("angled", "flat"), CONFIG
NAME = "esp_deck" if CONFIG == "angled" else "esp_deck_flat"   # output file stem
SUFFIX = "" if CONFIG == "angled" else "_flat"

# Screen bezel: the top tilts up from BEZEL_Y to the back edge (flat: it doesn't)
BEZEL_Y = 130.0
TILT = 12.0 if CONFIG == "angled" else 0.0   # degrees
SLOPE_L = (D - BEZEL_Y) / math.cos(math.radians(TILT))   # length along the slope
H_BACK = H + (D - BEZEL_Y) * math.tan(math.radians(TILT))

# 4" LCD: LCDwiki MSP4021 (ST7796S, FT6336U touch). From the vendor size data
# (lcdwiki / Elecrow 4.0" SPI module); check against the real board before printing.
LCD_ACTIVE = (83.52, 55.68)
GLASS = (94.57, 60.88)       # TFT + touch glass outline (bonded stack)
TFT_T, CTP_T = 2.5, 1.35     # the stack sits flush in an opening through the top
TAPE_T = 0.35                # stack-to-board tape; the board's ends then rest on the top's underside
LCD_PCB = (108.04, 61.74, 1.6)
LCD_DEPTH = 14.8             # whole module incl. pin header, from the glass top
SCREEN_X = 84.0              # screen centre x (off-centre to leave room for the USB-A slot)
SCREEN_V = SLOPE_L / 2       # screen centre, measured up the slope
CLAMP_DX = LCD_PCB[0] / 2 + 4.5   # clamp screws, just past the board's ends (boss r 4 clears it)
USBA_V = SCREEN_V + 18.0     # USB-A slot sits up the slope, clear of the left clamp boss

# Jog
JOG_C = (80.0, 65.0)
JOG_D = 100.0                # platter
WELL_D = 104.0               # recess around it
WELL_DEPTH = 2.0
PLATTER_H = 10.0
PLATTER_GAP = 0.5            # platter underside above the well floor
TOUCH_D = 80.0               # aluminium top plate = capacitive touch electrode
CAP_D = 24.0                 # centre cap
INERTIA = (38.0, 46.0, 5.0)  # steel ring in the platter underside: r_in, r_out, h

# Two 608 skateboard bearings, 8 x 22 x 7
B608 = (8.0, 22.0, 7.0)
BOSS_OD = 32.0
BOSS_BOTTOM = 10.0           # z of the boss underside
SHAFT_D = 8.0
SHAFT_BOTTOM = 8.0           # magnet is recessed in this end
MAGNET = (6.0, 2.5)          # diametric, diameter x height
BASE_T = 3.0
ENC_PCB = (22.0, 22.0, 1.6)
ENC_Z = BASE_T + 1.0         # PCB underside (on 1 mm standoffs)

# CUE / PLAY, bottom-left as on a CDJ
BTN_D = 19.0                 # cap
RING_D = 22.0                # light ring (translucent)
PLAY_C = (18.0, 18.0)
CUE_C = (18.0, 42.0)

# Pitch slider (45 mm travel pot, Bourns PTA4543 class)
SLIDER_X = 146.0
SLIDER_Y = (28.0, 102.0)     # slot ends
SLOT_W = 2.6
KNOB = (14.0, 10.0, 9.0)     # x, y, height above the top
KNOB_Y = 61.0                # rendered at +1.25 %

# Rear panel (y = D)
RJ45 = (40.0, 13.0, 16.2, 13.8)   # x, z centre, w, h
RCA_X, RCA_Z, RCA_HOLE = (78.0, 94.0), 15.0, 8.6
USBC = (124.0, 10.0, 9.2, 3.6)
USBA_FACE = (17.0, 13.6, 6.2)     # on the bezel: x, opening w, h (stick goes in from the top)

def face_loc():
    """Location of the tilted bezel face: origin on the bend line at x = W/2,
    local +y up the slope, local +z out of the face."""
    return Pos(W / 2, BEZEL_Y, H) * Rot(TILT, 0, 0)

def place(solid, frm: Location, onto: Location):
    """Snap a part so its mate `frm` (local frame) coincides with world `onto`."""
    return (onto * frm.inverse()) * solid
