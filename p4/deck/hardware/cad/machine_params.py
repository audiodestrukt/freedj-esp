# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
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
SHAFT_LEAD = 0.5             # 45 degree lead-in on the tip, for pushing it through the bearings
MAGNET = (6.0, 1.6)          # diametric; thickness ~ the encoder's 1.6 mm FR4 (not measured yet)
# Nothing holds the platter down yet (it sits on the upper bearing); retention comes later.

# Encoder: AS5600 breakout (WWZMDiB), 15/16" square (ruler), 4 corner holes 5/8" apart, chip
# in the middle, chip side up under the shaft, on four bosses on the base plate with small
# M3 inserts. Two pin columns either side of the chip (jog.HEADERS).
ENC_PCB = (23.8, 23.8, 1.6)
ENC_CHIP_H = 1.5             # AS5600 in SOIC-8
_h = 15.875 / 2
ENC_HOLES = [(-_h, -_h), (_h, -_h), (-_h, _h), (_h, _h)]   # from the board centre (= chip = jog axis)
AIR_GAP = 1.1                # magnet face to chip top; a magnet +-0.4 mm off 1.6 gives 0.7-1.5

# The stack, top down from the lower bearing (the shell is printed; BOSS_BOTTOM is fixed):
# the shaft ends just under the lower bearing, the magnet is glued flush in a pocket in its tip
SHAFT_BOTTOM = BOSS_BOTTOM - SHAFT_LEAD
ENC_Z = SHAFT_BOTTOM - AIR_GAP - ENC_CHIP_H - ENC_PCB[2]   # PCB underside
ENC_STANDOFF = ENC_Z - BASE_T                               # boss height above the plate
assert ENC_STANDOFF >= 2.0, ENC_STANDOFF
assert 0.5 <= AIR_GAP <= 3.0, AIR_GAP     # AS5600 working range

# CUE / PLAY, bottom-left as on a CDJ. Each button is a cap and a light ring, put in from
# below and held by the control board (no glue):
#   ring  translucent tube in the top's hole (OD RING_D, ID BTN_D + 0.5), 0.6 proud, with a
#         skirt (SKIRT_D, 1 mm wall) from the top's underside down to the board. The step
#         between tube and skirt bears on the underside; the board clamps the skirt. The
#         skirt carries the LEDs' light up to the visible ring (notches over the LEDs).
#   cap   BTN_D, CAP_RISE proud, with a flange under the ring tube (CAP_FLANGE) that stops it
#         coming out the top, and a nub under that onto the tact switch's actuator.
BTN_D = 19.0                 # cap
RING_D = 22.0                # light ring tube (the top's hole is RING_D + 0.4)
SKIRT_D = 23.6               # ring skirt OD: PLAY and CUE are 24 apart, so 0.4 between skirts
SKIRT_WALL = 1.0
CAP_FLANGE = (21.0, 1.2)     # dia, thickness (45 degree chamfer on top, so it prints upside down)
CAP_GAP = 0.2                # flange below the ring tube at rest (the cap sits on the switch)
NUB_D = 4.0
# Plain buttons (no light ring, LEDs optional): the cap fills the top's hole itself, and
# its flange catches the top's underside. Same shell, board and switch as the lit ones.
PLAIN_D = RING_D             # in the RING_D + 0.4 hole
PLAIN_FLANGE = (SKIRT_D, 1.2)   # as wide as fits: the buttons are 24 apart
PLATTER = os.environ.get("FREEDJ_PLATTER", "knurled")  # assembly: "knurled" (jog.py) or "pioneer" (pioneer_platter.py)
assert PLATTER in ("knurled", "pioneer"), PLATTER
BUTTONS = os.environ.get("FREEDJ_BUTTONS", "lit")   # assembly/clash check: "lit" or "plain"
assert BUTTONS in ("lit", "plain"), BUTTONS
PLAY_C = (16.0, 16.0)
CUE_C = (16.0, 40.0)         # ~4 mm clear of the jog well

# Pitch slider: Bourns PTA6043-2015DPB103 (60 mm travel, no centre detent, 15 mm metal
# DP lever, 10k linear). Datasheet: 75 x 9 mm frame, two M2 tapped holes 71 apart in the
# frame top, 3 PC pins down (62.8 apart end to end). It hangs under the top on two pads,
# held by two M2 x 8 countersunk screws from above (flush: a pan head would catch the knob
# at the travel ends). Wired to the control board; its PC pins aren't used as board pins.
SLIDER_X = 146.0
SLIDER_C = JOG_C[1]          # slot centred on the jog
POT_TRAVEL = 60.0
POT_FRAME = (9.0, 75.0, 6.5) # x (across), y (along the travel), z (frame top -> body bottom)
POT_HOLES = 71.0             # M2 tapped, on the centre line
POT_PINS = 62.8              # pin 1/2 end to pin 3 end
LEVER = (1.0, 4.0, 15.0)     # DP lever: across, along the travel, above the frame top
POT_PAD = 3.0                # pads under the top: frame top at H - TOP_T - POT_PAD
SLOT_W = 2.6
SLOT_L = POT_TRAVEL + LEVER[1] + 0.6                    # 64.6
SLIDER_Y = (SLIDER_C - SLOT_L / 2, SLIDER_C + SLOT_L / 2)
KNOB = (14.0, 11.0, 10.0)    # base x (across the slot), base y (along it), height
KNOB_TOP = (12.0, 5.5)       # flat top: x, y. Tapers from the base, CDJ tempo-knob style
KNOB_SCOOP = (14.0, 1.3)     # finger scoops in the front and back faces: radius, depth
KNOB_LIFT = 1.0              # knob underside above the top
KNOB_SOCKET = (1.4, 4.3, 7.2)   # lever socket: across, along, depth (the lever goes in 7.0)
KNOB_Y = SLIDER_C - 4.0      # rendered at +1.25 % (pitch range is firmware's business)
TICKS_X = 6.0                # tick scale starts this far right of the slot; long 3.5, short 2
TEMPO_LED_X = 11.0           # tempo-zero LED, right of the slot: clear of the 2 mm edge round
assert SLIDER_C - POT_HOLES / 2 + 1.9 < SLIDER_Y[0] - 1.0, "M2 countersink too close to the slot end"

# Control board: one PCB under CUE / PLAY with the two tact switches, the light-ring LEDs
# and the I2C chip (CH32V003); the pot and the AS5600 wire to it, one cable to the dev board.
# 6 x 6 SMD tact switches 7.5 mm tall (ZX-QC66-7.5TP, LCSC C7470157), right under the caps.
TACT = (6.0, 6.0, 3.5, 7.5)     # body x, y, body height, actuator top above the board
CAP_BOTTOM = 6.0                # the cap's nub reaches this far below the top surface
CTRL_T = 1.6
CTRL_Z = H - CAP_BOTTOM - TACT[3]                       # board top face
CTRL_XY = (4.0, 4.0, 46.0, 62.0)                       # x0, y0, x1, y1 (the skirts sit fully on it)
CTRL_CORNER_R = 6.0
CTRL_HOLES = [(41.5, 10.0), (41.5, 57.5), (10.0, 57.5)]
CTRL_BOSS_D = 6.0
CTRL_PILOT = (2.2, 5.0)      # M2.5 thread-forming screws up from below: pilot dia, depth

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
BRAND_V = 9.0                # "FREEDJ", left of the screen near the bend

def face_loc():
    """Location of the tilted bezel face: origin on the bend line at x = W/2,
    local +y up the slope, local +z out of the face."""
    return Pos(W / 2, BEZEL_Y, H) * Rot(TILT, 0, 0)

def place(solid, frm: Location, onto: Location):
    """Snap a part so its mate `frm` (local frame) coincides with world `onto`."""
    return (onto * frm.inverse()) * solid
