# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""a1s_board.py: the AI-Thinker ESP32-A1S Audio Kit as a stand-in, plus where it sits
(FREEDJ_BOARD=a1s). Same interface as devboard.py: board(), ports(), parts(), shell_cuts(),
HOLES, STANDOFF, Z0, LOC, deck_xy(), VENTS.

!! PLACEHOLDER GEOMETRY. Ai-Thinker publishes no dimensioned drawing of the Audio Kit, so
!! every number in the table below is a guess until measured with calipers. Measure the
!! board, fix the table, rebuild, and run this file: it prints the clashes.

Board frame: lying flat, components up, axes along the deck's: u along x (0 = the board's
left edge), v along y (0 = its front edge), z = 0 on the underside. The jack edge faces
the rear wall, the micro-USB edge the right wall.
"""
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)

# ---- measure these ----------------------------------------------------------------------
L_, W_, T_ = 90.0, 60.0, 1.6                     # outline u x v, thickness
HOLE_D = 3.2
HOLES = [(3.5, 3.5), (L_ - 3.5, 3.5), (3.5, W_ - 3.5), (L_ - 3.5, W_ - 3.5)]   # (u, v)
# Ports that reach a wall: (name, edge, position along that edge from its u/v = 0 end,
#   body (along the edge, depth into the board, height), overhang past the edge,
#   opening: ("round", dia) or ("rect", w, h) around the port's centre, through the wall)
PORTS = [
    ("headphone", "rear", 62.0, (6.0, 12.0, 5.0), 1.0, ("round", 7.0)),
    ("line_in",   "rear", 76.0, (6.0, 12.0, 5.0), 1.0, ("round", 7.0)),
    ("usb_uart",  "right", 16.0, (7.5, 5.5, 2.6), 0.8, ("rect", 12.0, 8.0)),   # micro-USB, plug overmould
    ("usb_power", "right", 44.0, (7.5, 5.5, 2.6), 0.8, ("rect", 12.0, 8.0)),
]
# Everything else on top worth checking for clearance: (name, u, v, size u x v x h)
TALL = [
    ("module", 30.0, 32.0, (32.0, 18.0, 3.2)),   # ESP32-A1S module (shield can)
    ("caps",   58.0, 20.0, (16.0, 8.0, 8.0)),    # electrolytics / speaker JSTs, a guessed envelope
    ("keys",   45.0, 6.0, (70.0, 6.0, 5.0)),     # KEY1-6, RST, BOOT along the front edge
]
# -----------------------------------------------------------------------------------------

def _top(u, v, z=T_):
    return Pos(u, v, z)

def board():
    return Pos(L_ / 2, W_ / 2, 0) * Box(L_, W_, T_, align=C) - [
        Pos(u, v, -1) * Cylinder(HOLE_D / 2, T_ + 2, align=C) for u, v in HOLES]

def _port_centre(edge, along, depth, over):
    """Body centre (u, v) of a port on an edge."""
    if edge == "rear":
        return along, W_ + over - depth / 2
    if edge == "right":
        return L_ + over - depth / 2, along
    if edge == "left":
        return -over + depth / 2, along
    raise ValueError(edge)

def ports():
    out = []
    for name, edge, along, (a, d, h), over, _ in PORTS:
        u, v = _port_centre(edge, along, d, over)
        sx, sy = (a, d) if edge == "rear" else (d, a)
        out.append((name, _top(u, v) * Box(sx, sy, h, align=C)))
    return out

def parts():
    return [(n, _top(u, v) * Box(*s, align=C)) for n, u, v, s in TALL]

# Placement in the deck (world). Right edge clear of the back-right base boss (inset 7.5,
# r 4.5), rear edge so the jack faces sit 0.3 mm inside the rear wall.
STANDOFF = 3.0                                      # printed on the base plate
_REAR_OVER = max([p[4] for p in PORTS if p[1] == "rear"], default=0.0)
X0 = M.W - 7.5 - 4.5 - 0.5 - L_                    # deck x of u = 0
Y0 = M.D - M.WALL - 0.3 - _REAR_OVER - W_          # deck y of v = 0
Z0 = M.BASE_T + STANDOFF                            # board underside
LOC = Pos(X0, Y0, Z0)

def deck_xy(u, v):
    return (X0 + u, Y0 + v)

def shell_cuts():
    """Openings in the shell's walls for the ports (world frame)."""
    cuts = []
    for name, edge, along, (a, d, h), over, op in PORTS:
        u, v = _port_centre(edge, along, d, over)
        x, y = deck_xy(u, v)
        z = Z0 + T_ + h / 2
        if edge == "rear":
            at, rot = Pos(x, M.D, z), Rot(90, 0, 0)
        elif edge == "right":
            at, rot = Pos(M.W, y, z), Rot(0, 90, 0)
        else:
            at, rot = Pos(0, y, z), Rot(0, 90, 0)
        deep = 30.0          # past the wall to the port face: the plug reaches in
        if op[0] == "round":
            cuts.append(at * rot * Cylinder(op[1] / 2, deep))
        else:
            w, hh = op[1], op[2]
            box = Box(w, 10, hh) if edge == "rear" else Box(deep, w, hh)
            cuts.append(at * box)
    return cuts

# Base plate vents under the board, between the standoffs: (x, y, length along y)
VENTS = [(X0 + 20 + i * 10, Y0 + W_ / 2, W_ - 20) for i in range(6)]

MATES = {"seat": Pos(0, 0, 0)}

def clashes():
    """Board solids against the shell and everything hanging inside it."""
    import shell, stock, controls, ctrlboard
    S = shell.MATES
    seat_on = lambda s, m: M.place(s, stock.MATES["seat"], S[m])
    others = [("shell", shell.part()),
              ("lcd_pcb", seat_on(stock.lcd_pcb(), "screen")),
              ("lcd_header", seat_on(stock.lcd_header(), "screen")),
              ("pot", M.place(controls.pot(M.KNOB_Y - M.SLIDER_C), controls.MATES["seat"], S["pot"])),
              ] + ([(f"{w}_panel", M.place(controls.panel_button(), controls.MATES["seat"], S[w])) for w in ("play", "cue")]
                   if M.PANEL else [("ctrl_board", ctrlboard.board())])
    others += [(f"screen_screw{i}", seat_on(stock.screw_m3(), f"screen_screw{i}")) for i in range(4)]
    mine = [("board", LOC * board())] + [(n, LOC * s) for n, s in ports() + parts()]
    hits = []
    for a, sa in mine:
        for b, sb in others:
            v = (sa & sb).volume
            if v > 0.01:
                hits.append((a, b, round(v, 2)))
    # vertical room under the screen module, over the board's top parts
    top = max(s.bounding_box().max.Z for _, s in mine)
    low = min(seat_on(f(), "screen").bounding_box().min.Z for f in (stock.lcd_pcb, stock.lcd_header))
    return hits, top, low

if __name__ == "__main__":
    print(f"A1S at x {X0:.1f}-{X0 + L_:.1f}, y {Y0:.1f}-{Y0 + W_:.1f}, underside z {Z0:.1f} "
          f"(deck {M.W:.0f} x {M.D:.1f} x {M.H:.0f}, {M.CONFIG})")
    hits, top, low = clashes()
    print(f"tallest board part z {top:.1f}; screen module's lowest point z {low:.1f}")
    for h in hits:
        print("  clash:", *h)
    print(f"{len(hits)} clash(es)")
