"""stock.py: bought parts as simple stand-ins (LCD, cover glass, connectors).
Local frame: z = 0 on the mounting face, +z out of the panel; the body goes to -z.
"""
from build123d import *
import machine_params as M

C = (Align.CENTER, Align.CENTER, Align.MIN)
CM = (Align.CENTER, Align.CENTER, Align.MAX)

def touch_glass():  # local: z = 0 at the glass top (flush with the deck face)
    return Box(*M.GLASS, M.CTP_T, align=CM)

def tft():
    return Pos(0, 0, -M.CTP_T - M.CTP_TAPE) * Box(*M.GLASS, M.TFT_T, align=CM)

def lcd_pcb():
    w, h, t = M.LCD_PCB
    pcb = Pos(0, 0, -M.STACK) * Box(w, h, t, align=CM)
    hx, hy = M.LCD_HOLES
    return pcb.cut(*[Pos(sx * hx / 2, sy * hy / 2, -M.STACK - 5) * Cylinder(1.6, 10)
                     for sx in (-1, 1) for sy in (-1, 1)])

def lcd_header():   # 14-pin header along the board's left end, down to LCD_DEPTH
    top = M.STACK + M.LCD_PCB[2]
    return Pos(-M.LCD_PCB[0] / 2 + 3.3, 0, -top) * Box(2.5, 35.6, M.LCD_DEPTH - top, align=CM)

def screw_m3():     # pan head M3 x 5 thread-former (1.6 board + 3.4 into a 3.8 pilot); z = 0 under the head
    head = Pos(0, 0, -2.0) * Cylinder(2.8, 2.0, align=C)
    return head.fuse(Cylinder(1.5, 5.0, align=C))

def usb_a():        # receptacle, mouth 1 mm below the bezel face
    shell = Pos(0, 0, -1) * Box(13.1, 5.7, 14, align=CM)
    return shell - Pos(0, 0, -1.5) * Box(12.1, 4.7, 13, align=CM)

def usb_a_tongue():
    return Pos(0, -0.9, -2.5) * Box(11.0, 1.6, 9, align=CM)

def rj45():
    body = Pos(0, 0, 0.3) * Box(16.0, 13.6, 21, align=CM)
    return body - Pos(0, -1, 0.31) * Box(11.8, 8.0, 15, align=CM)

def rca():
    sleeve = Cylinder(4.1, 8.5, align=C) - Pos(0, 0, 1) * Cylinder(3.3, 10, align=C)
    pin = Cylinder(1.0, 7, align=C)
    nut = Pos(0, 0, 0) * Cylinder(5.2, 1.4, align=C)
    return sleeve.fuse(pin, nut)

def rca_ring():
    return Pos(0, 0, 1.4) * (Cylinder(4.6, 1.0, align=C) - Pos(0, 0, -1) * Cylinder(4.15, 3, align=C))

def usb_c():
    o = Pos(0, 0, 0.2) * Box(8.9, 3.2, 7.5, align=CM)
    return o - Pos(0, 0, 0.21) * Box(8.1, 2.4, 6.5, align=CM)

MATES = {"seat": Pos(0, 0, 0)}
