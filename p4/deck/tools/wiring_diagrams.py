#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wiring diagrams for the build guide (release 1: everything on jumper wires).

python3 wiring_diagrams.py OUT_DIR
  -> header.svg            the 40-pin header, every pin we use, colour-coded
  -> harness-screen.svg    screen to header
  -> harness-jog.svg       AS5600 to header
  -> harness-panel.svg     CUE, PLAY, pitch pot to header

The pin facts are the ones in main/board.h and README.md ("Wiring the MSP4021").
Header numbering follows the Raspberry Pi convention; the GPIO numbers are
Waveshare's silkscreen. Pin 1 (3V3) is at the end near the C6_UART connector.
"""
import os, sys

# Waveshare ESP32-P4-Module-DEV-KIT 40-pin header, from the board's silkscreen.
HEADER = {
    1: "3V3", 2: "5V", 3: "SDA·7", 4: "5V", 5: "SCL·8", 6: "GND", 7: "GPIO23", 8: "TXD·37",
    9: "GND", 10: "RXD·38", 11: "GPIO21", 12: "GPIO22", 13: "GPIO20", 14: "GND", 15: "GPIO6",
    16: "GPIO5", 17: "3V3", 18: "GPIO4", 19: "GPIO3", 20: "GND", 21: "GPIO2", 22: "GPIO1",
    23: "GPIO0", 24: "GPIO36", 25: "GND", 26: "GPIO32", 27: "GPIO24", 28: "GPIO25", 29: "GPIO33",
    30: "GND", 31: "GPIO26", 32: "GPIO54", 33: "GPIO48", 34: "GND", 35: "GPIO53", 36: "GPIO46",
    37: "GPIO47", 38: "GPIO27", 39: "GND", 40: "GPIO45",
}

RED, BLACK = "#d22", "#222"
# Each wire: (component, component pin, header pin, colour, note)
SCREEN = [
    ("VCC", 17, RED, "never 5V"),
    ("GND", 20, BLACK, ""),
    ("CS", 15, "#e67e22", ""),
    ("RESET", 18, "#8e44ad", ""),
    ("DC / RS", 16, "#16a085", ""),
    ("SDI (MOSI)", 13, "#2471a3", ""),
    ("SCK", 11, "#d4ac0d", ""),
    ("LED", 19, "#7f8c8d", "backlight"),
    ("CTP_SCL (touch)", 5, "#d4ac0d", "shared with the sensor's SCL"),
    ("CTP_SDA (touch)", 3, "#2471a3", "shared with the sensor's SDA"),
    ("CTP_RST (touch)", 21, "#c0392b", ""),
]
SCREEN_UNUSED = ["SDO (MISO)", "CTP_INT", "SD_* pins"]
JOG = [
    ("VCC", 1, RED, ""),
    ("GND", 6, BLACK, ""),
    ("SDA", 3, "#2471a3", ""),
    ("SCL", 5, "#d4ac0d", ""),
    ("DIR", 9, BLACK, "sets the direction"),
]
JOG_UNUSED = ["OUT", "GPO"]
PANEL = [
    ("CUE: one leg", 27, "#e67e22", ""),
    ("CUE: diagonal leg", 25, BLACK, ""),
    ("PLAY: one leg", 28, "#27ae60", ""),
    ("PLAY: diagonal leg", 30, BLACK, ""),
    ("pitch: wiper", 12, "#8e44ad", "analog in"),
    ("pitch: screen end", 1, RED, "shared with the sensor's VCC"),
    ("pitch: your end", 14, BLACK, ""),
]

FONT = 'font-family="ui-monospace, Menlo, Consolas, monospace"'


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def header_svg():
    """The header as it sits on the board: a vertical strip on the left edge,
    pin 1 at the bottom, odd pins in the inner column, even pins on the edge."""
    used = {}
    for group, wires in (("screen", SCREEN), ("jog", JOG), ("panel", PANEL)):
        for name, pin, col, _ in wires:
            used.setdefault(pin, []).append((group, name, col))
    pitch, top = 26, 40
    W, H = 900, top + 20 * pitch + 70
    xe, xo = 440, 470          # even (edge) and odd (inner) column x
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" {FONT} font-size="13">',
           f'<rect width="{W}" height="{H}" fill="#fff"/>',
           f'<text x="20" y="24" font-size="15" font-weight="bold">ESP32-P4 dev kit, 40-pin header (front view, header on the left edge)</text>',
           f'<rect x="{xe-12}" y="{top-6}" width="{xo-xe+24}" height="{20*pitch}" rx="4" fill="#333"/>']
    for row in range(20):
        y = top + (19 - row) * pitch + 8     # pin 1 at the bottom
        for pin, x, side in ((2 * row + 2, xe, "L"), (2 * row + 1, xo, "R")):
            label = HEADER[pin]
            hit = used.get(pin)
            fill = hit[0][2] if hit else ("#d22" if label == "3V3" else "#555" if label == "GND" else "#bbb")
            out.append(f'<circle cx="{x}" cy="{y}" r="7" fill="{fill}" stroke="#fff" stroke-width="1.5"/>')
            pn = f'{pin}'
            if side == "L":
                out.append(f'<text x="{x-20}" y="{y+4}" text-anchor="end" fill="#999">{pn}</text>')
                out.append(f'<text x="{x-46}" y="{y+4}" text-anchor="end" fill="{"#000" if hit else "#888"}">{esc(label)}</text>')
                if hit:
                    out.append(f'<text x="{x-118}" y="{y+4}" text-anchor="end" font-weight="bold" fill="{hit[0][2]}">'
                               f'{esc(", ".join(f"{g} {n}" for g, n, _ in hit))}</text>')
            else:
                out.append(f'<text x="{x+20}" y="{y+4}" fill="#999">{pn}</text>')
                out.append(f'<text x="{x+46}" y="{y+4}" fill="{"#000" if hit else "#888"}">{esc(label)}</text>')
                if hit:
                    out.append(f'<text x="{x+118}" y="{y+4}" font-weight="bold" fill="{hit[0][2]}">'
                               f'{esc(", ".join(f"{g} {n}" for g, n, _ in hit))}</text>')
    yb = top + 20 * pitch + 14
    out.append(f'<text x="20" y="{yb}" fill="#555">pin 1 (3V3) is at the end near the C6_UART connector and the USB-C ports;</text>')
    out.append(f'<text x="20" y="{yb+18}" fill="#555">the two 5V pins are on the board edge. Grey pins are unused. Never connect anything to 5V.</text>')
    out.append(f'<text x="20" y="{yb+36}" fill="#555">GND is also on pins 34 and 39 if you run out.</text>')
    out.append('</svg>')
    return "\n".join(out)


def harness_svg(title, part, wires, unused=(), note=""):
    """Component pins on the left, header pins on the right, one line per wire."""
    rowh, top = 34, 70
    n = len(wires)
    W = 900
    H = top + n * rowh + 40 + (22 if unused else 0) + (22 if note else 0)
    xl, xr = 240, 470
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" {FONT} font-size="13">',
           f'<rect width="{W}" height="{H}" fill="#fff"/>',
           f'<text x="20" y="26" font-size="15" font-weight="bold">{esc(title)}</text>',
           f'<rect x="20" y="{top-24}" width="{xl-20}" height="{n*rowh+14}" rx="6" fill="#f3f3f3" stroke="#999"/>',
           f'<text x="30" y="{top-30}" fill="#555">{esc(part)}</text>',
           f'<rect x="{xr}" y="{top-24}" width="150" height="{n*rowh+14}" rx="6" fill="#333"/>',
           f'<text x="{xr}" y="{top-30}" fill="#555">P4 header</text>']
    for i, (name, pin, col, desc) in enumerate(wires):
        y = top + i * rowh
        out.append(f'<text x="30" y="{y+4}">{esc(name)}</text>')
        out.append(f'<circle cx="{xl}" cy="{y}" r="5" fill="{col}"/>')
        out.append(f'<path d="M{xl} {y} C {xl+90} {y}, {xr-90} {y}, {xr+14} {y}" fill="none" stroke="{col}" stroke-width="4" stroke-linecap="round"/>')
        out.append(f'<circle cx="{xr+14}" cy="{y}" r="6" fill="{col}" stroke="#ccc" stroke-width="2"/>')
        out.append(f'<text x="{xr+28}" y="{y+4}" fill="#fff">pin {pin}</text>')
        out.append(f'<text x="{xr+88}" y="{y+4}" fill="#ddd" font-size="12">{esc(HEADER[pin])}</text>')
        if desc:
            out.append(f'<text x="{xr+162}" y="{y+4}" fill="#666" font-size="12">{esc(desc)}</text>')
    y = top + n * rowh + 10
    if unused:
        out.append(f'<text x="20" y="{y+8}" fill="#888">not connected: {esc(", ".join(unused))}</text>')
        y += 22
    if note:
        out.append(f'<text x="20" y="{y+8}" fill="#555">{esc(note)}</text>')
    out.append('</svg>')
    return "\n".join(out)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(d, exist_ok=True)
    files = {
        "header.svg": header_svg(),
        "harness-screen.svg": harness_svg("Screen harness: 11 wires", "4\" SPI screen (ST7796S)", SCREEN, SCREEN_UNUSED,
                                          "Touch and the jog sensor share pins 3 and 5 (I2C): double up those wires."),
        "harness-jog.svg": harness_svg("Jog sensor harness: 5 wires", "AS5600 board", JOG, JOG_UNUSED,
                                       "DIR must go to GND. Left floating, the jog reads noisy and can run backwards."),
        "harness-panel.svg": harness_svg("Buttons and pitch fader: 7 wires", "switches and slide pot", PANEL, (),
                                         "Tact switches: use two diagonally opposite legs. Fader backwards? swap its two end wires."),
    }
    for name, svg in files.items():
        with open(os.path.join(d, name), "w") as f:
            f.write(svg + "\n")
        print("wrote", os.path.join(d, name))


if __name__ == "__main__":
    main()
