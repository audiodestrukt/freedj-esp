"""machine_params.py: every shared freedj-esp enclosure dimension (mm), plus place().

Frame: x across the deck (0 = left edge), y front-to-back (0 = the edge nearest you),
z up (0 = underside of the shell). Sized around the 4" MSP4021 (480x320 ST7796S,
capacitive touch) the deck runs on today.

Two configurations, picked by FREEDJ_CONFIG before this module is imported:
  angled (default)  the CDJ look: screen bezel tilted 12 degrees
  flat              the whole top flat, so the shell prints top-down with no supports
"""
import math, os
from build123d import Location, Pos, Rot

CONFIG = os.environ.get("FREEDJ_CONFIG", "angled")
assert CONFIG in ("angled", "flat"), CONFIG
NAME = "freedj_esp" if CONFIG == "angled" else "freedj_esp_flat"   # output file stem
SUFFIX = "" if CONFIG == "angled" else "_flat"

# Body
W = 160.0                    # width; the depth D is derived below from the jog + screen layout
# Height of the top (jog area). Flat is 4 mm taller: the dev board's USB-A stacks sit under
# the screen, and without the angled bezel's rise they'd hit the screen's board.
H = 34.0 if CONFIG == "angled" else 38.0
CORNER_R = 10.0              # plan-view corner radius
WALL = 2.5                   # side walls
TOP_T = 4.0                  # top plate (stiff: the jog boss hangs off it)
EDGE_R = 2.0                 # top-edge round

# 4" LCD: the capacitive-touch version of the LCDwiki 4" SPI module (FT6336U, I2C), whose
# drawing is MSP4031 (lcdwiki.com/res/MSP4030_MSP4031/4.0inch_SPI_Module_MSP4031_Size.pdf).
# The resistive MSP4021 differs slightly (108.04 x 61.74 board, 102.04 x 54.90 holes).
LCD_ACTIVE = (83.52, 55.68)
LCD_AA_DX = 2.88             # active area sits off-centre, away from the 14-pin header end
GLASS = (94.57, 60.88)       # touch lens / TFT outline, centred on the board
CTP_T, CTP_TAPE, TFT_T, TAPE_T = 1.35, 0.5, 2.5, 0.5   # glass top -> board: 4.85 mm
STACK = CTP_T + CTP_TAPE + TFT_T + TAPE_T
LCD_PCB = (108.0, 60.88, 1.6)
LCD_HOLES = (102.0, 54.88)   # 4 x 3.2 mm, 3.00 mm in from the board edges
LCD_DEPTH = 14.8             # whole module incl. pin header, from the glass top
WINDOW_X = W / 2             # the visible window is centred on the deck
SCREEN_X = WINDOW_X - 2.88   # so the module sits 2.88 mm left: its active area is off-centre on the glass
SCREEN_BOSS_D = 6.8          # bosses on the 4 holes, down to the board; must clear the glass edge (3.7 mm away)
SCREEN_PILOT = (2.5, 6.0)    # M3 thread-forming screws for plastic: pilot diameter, depth from the board face
GASKET = 0.5                 # foam between the glass and the top's underside
WINDOW_MARGIN = 0.5          # window = active area + this all round
WINDOW_LIP = 1.0             # straight part of the window wall; above it a 45 degree chamfer for fingers

# Jog
JOG_C = (80.0, 60.0)
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
BOSS_BOTTOM = 10.0           # z of the boss underside = the lower bearing's bottom face
BASE_T = 3.0

# Small M3 heat-set inserts (M3 x 3, ~4.2 OD), used in the base plate
M3_INSERT_BORE = 4.0

# Shaft: printed in one piece with the platter, a snug fit through the 608s' 8 mm bores.
# Tune SHAFT_D with fit_gauge.stl (jog.py) on your printer before printing the platter.
SHAFT_D = 7.95
ECLIP = (6.0, 0.75, 0.7, 11.3)   # DIN 6799 size 6 (shafts 7-9 mm): groove dia, groove width, clip t, clip OD
MAGNET = (6.0, 1.6)          # diametric; thickness ~ the encoder's 1.6 mm FR4 (not measured yet)
MAGNET_CAP = 0.4             # solid plastic between the magnet pocket and the e-clip groove

# Encoder: AS5600 breakout (WWZMDiB, 23 x 23 mm, typical listing; not measured yet), chip
# side up under the shaft, right-angle header along the back edge. It lies flat on the base
# plate; the header's solder tails drop into a slot in the plate.
ENC_PCB = (23.0, 23.0, 1.6)
ENC_CHIP_H = 1.5             # AS5600 in SOIC-8
ENC_HOLES = [(-8.5, -8.5), (8.5, -8.5)]   # from the board centre (= chip = jog axis); front edge, away from the header
ENC_Z = BASE_T               # PCB underside

# The stack, top down from the lower bearing (the shell is printed; BOSS_BOTTOM is fixed)
GROOVE_TOP = BOSS_BOTTOM - 0.05             # e-clip pulls the shaft down onto the lower inner race
GROOVE_BOT = GROOVE_TOP - ECLIP[1]
SHAFT_BOTTOM = GROOVE_BOT - MAGNET_CAP - MAGNET[1]   # magnet glued flush in a pocket in the shaft end
AIR_GAP = SHAFT_BOTTOM - (ENC_Z + ENC_PCB[2] + ENC_CHIP_H)   # magnet face to chip top
assert 0.5 <= AIR_GAP <= 3.0, AIR_GAP     # AS5600 working range

# CUE / PLAY, bottom-left as on a CDJ
BTN_D = 19.0                 # cap
RING_D = 22.0                # light ring (translucent)
PLAY_C = (16.0, 16.0)
CUE_C = (16.0, 40.0)         # ~4 mm clear of the jog well

# Pitch slider (45 mm travel pot, Bourns PTA4543 class)
SLIDER_X = 146.0
SLIDER_Y = (23.0, 97.0)      # slot ends, centred on the jog
SLOT_W = 2.6
KNOB = (14.0, 10.0, 9.0)     # x, y, height above the top
KNOB_Y = 56.0                # rendered at +1.25 %

# Rear panel (y = D)
RCA_X, RCA_Z, RCA_HOLE = (64.0, 80.0), 15.0, 8.6   # left of the dev board's ports
USBA_FACE = (14.5, 13.6, 6.2)     # on the bezel: x, opening w, h (stick goes in from the top)

# Screen bezel. Depth follows from the parts: jog well, a gap, the glass with a
# margin either side. The top tilts up from BEZEL_Y to the back (flat: it doesn't).
JOG_SCREEN_GAP = 8.0         # jog well edge to glass edge, in plan
GLASS_MARGIN = (4.0, 5.0)    # glass edge to the bend line / to the back edge, along the top
TILT = 12.0 if CONFIG == "angled" else 0.0   # degrees
SLOPE_L = GLASS_MARGIN[0] + GLASS[1] + 0.4 + GLASS_MARGIN[1]   # bezel length along the slope
BEZEL_Y = JOG_C[1] + WELL_D / 2 + JOG_SCREEN_GAP - GLASS_MARGIN[0]
D = round(BEZEL_Y + SLOPE_L * math.cos(math.radians(TILT)), 1)
H_BACK = H + SLOPE_L * math.sin(math.radians(TILT))
SCREEN_V = GLASS_MARGIN[0] + (GLASS[1] + 0.4) / 2   # screen centre, measured up the slope
TOP_T_PERP = TOP_T * math.cos(math.radians(TILT))   # top thickness measured square to the bezel
GLASS_Z = -(TOP_T_PERP + GASKET)                    # glass top, face-local: behind the top plate
USBA_V = SCREEN_V + 18.0     # USB-A slot sits up the slope, clear of the screen bosses
BRAND_V = 9.0                # "FREEDJ-ESP", left of the screen near the bend

def face_loc():
    """Location of the tilted bezel face: origin on the bend line at x = W/2,
    local +y up the slope, local +z out of the face."""
    return Pos(W / 2, BEZEL_Y, H) * Rot(TILT, 0, 0)

def place(solid, frm: Location, onto: Location):
    """Snap a part so its mate `frm` (local frame) coincides with world `onto`."""
    return (onto * frm.inverse()) * solid
