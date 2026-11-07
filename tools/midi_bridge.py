#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
"""Forward a MIDI controller plugged into this workstation to the deck over its
serial console, as if it were on the deck's own USB host. For boards without USB
host (the ESP32-A1S build), or to test mappings without replugging.

Each MIDI message goes out as 0x01 + the 4-byte USB-MIDI packet the deck's USB
host would deliver; the deck (serialKeysTask in p4/deck/main/main.cpp) hands it
to onMidiPacket(), so the DJ2Go decoder sees exactly what it sees over USB.

Also a monitor: prints the deck's log, and single keypresses go to the deck as
console keys (p play, c cue, l load, ...). Ctrl-C quits.

  python3 tools/midi_bridge.py                      # DJ2Go -> /dev/ttyUSB0
  python3 tools/midi_bridge.py --midi DJ2Go --port /dev/ttyACM0 -v
  python3 tools/midi_bridge.py --reset              # reset the board on connect

Reads the raw ALSA MIDI device (/dev/snd/midiC<card>D0); no MIDI library needed.
Close any other serial monitor first (the port can only be open once).
"""
import argparse
import os
import re
import sys
import termios
import threading
import time
import tty

import serial

# USB-MIDI code index numbers for channel messages, by status high nibble.
DATA_LEN = {0x8: 2, 0x9: 2, 0xA: 2, 0xB: 2, 0xC: 1, 0xD: 1, 0xE: 2}


def find_midi(name):
    """ALSA rawmidi device of the first sound card whose name contains `name`."""
    with open("/proc/asound/cards") as f:
        for line in f:
            m = re.match(r"\s*(\d+)\s+\[(.*?)\s*\]:\s*(.*)", line)
            if m and (name.lower() in m.group(2).lower() or name.lower() in m.group(3).lower()):
                dev = f"/dev/snd/midiC{m.group(1)}D0"
                if os.path.exists(dev):
                    return dev, m.group(3).strip()
    return None, None


def packets(stream):
    """Parse a raw MIDI byte stream into USB-MIDI packets (cable 0).
    Handles running status; skips SysEx (the deck's decoders don't use it)."""
    status = 0
    data = []
    in_sysex = False
    for b in stream:
        if b >= 0xF8:                       # realtime: single byte, anywhere
            yield bytes([0x0F, b, 0, 0])
            continue
        if b == 0xF0:
            in_sysex = True
            continue
        if b == 0xF7:
            in_sysex = False
            continue
        if in_sysex:
            continue
        if b & 0x80:
            status = b if b < 0xF0 else 0   # system common: dropped
            data = []
            continue
        if not status:
            continue
        data.append(b)
        need = DATA_LEN[status >> 4]
        if len(data) == need:
            d = data + [0] * (2 - need)
            yield bytes([status >> 4, status, d[0], d[1]])
            data = []                       # running status: keep `status`


def midi_bytes(dev):
    fd = os.open(dev, os.O_RDONLY)
    try:
        while True:
            chunk = os.read(fd, 64)
            if not chunk:
                return
            yield from chunk
    finally:
        os.close(fd)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--midi", default="DJ2Go", help="sound card name to look for (default DJ2Go)")
    ap.add_argument("--port", default=None, help="deck serial port (default /dev/ttyUSB0, else /dev/ttyACM0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--reset", action="store_true", help="reset the board when connecting")
    ap.add_argument("-v", "--verbose", action="store_true", help="print each forwarded packet")
    a = ap.parse_args()

    dev, card = find_midi(a.midi)
    if not dev:
        sys.exit(f"no MIDI device matching '{a.midi}' (see /proc/asound/cards, `amidi -l`)")
    port = a.port or next((p for p in ("/dev/ttyUSB0", "/dev/ttyACM0") if os.path.exists(p)), None)
    if not port:
        sys.exit("no serial port found; pass --port")

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, a.baud, 0.1
    ser.dtr = ser.rts = False                # don't reset the board by opening the port
    ser.open()
    if a.reset:
        ser.rts = True; time.sleep(0.1); ser.rts = False
    lock = threading.Lock()
    print(f"bridge: {card} ({dev}) -> {port} @ {a.baud}. Keys go to the deck; Ctrl-C quits.\r")

    def forward():
        n = 0
        for pkt in packets(midi_bytes(dev)):
            with lock:
                ser.write(b"\x01" + pkt)
            n += 1
            if a.verbose:
                sys.stdout.write(f"[midi {n}] {pkt.hex(' ')}\r\n")
                sys.stdout.flush()
        sys.stdout.write("bridge: MIDI device closed\r\n")

    def monitor():
        while True:
            data = ser.read(4096)
            if data:
                sys.stdout.write(data.decode(errors="replace").replace("\n", "\r\n"))
                sys.stdout.flush()

    threading.Thread(target=forward, daemon=True).start()
    threading.Thread(target=monitor, daemon=True).start()

    old = None
    if sys.stdin.isatty():
        old = termios.tcgetattr(sys.stdin)
        tty.setcbreak(sys.stdin.fileno())    # single keypresses, Ctrl-C still quits
    try:
        while True:
            ch = sys.stdin.read(1) if old else (time.sleep(1) or "")
            if ch and ch != "\x01":          # 0x01 is the packet marker
                with lock:
                    ser.write(ch.encode())
    except KeyboardInterrupt:
        pass
    finally:
        if old:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
        ser.close()


if __name__ == "__main__":
    main()
