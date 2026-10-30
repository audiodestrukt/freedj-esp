#!/usr/bin/env python3
"""Hold the P4 deck's serial port open, log everything, and type keys at it.

    tools/p4_serial.py deck.log &          # log (doesn't reset the board)
    echo -n 3lp >> deck.log.cmd            # select track 3, load, play
    grep -a "audio:\\|cpu:\\|top:" deck.log  # health report every 2 s

Opening the port with DTR/RTS low avoids resetting the board, and keeping
one process attached means flashing is the only thing that reboots it.
Anything appended to <log>.cmd is sent to the board and the file emptied.
Stop it before flashing (esptool needs the port).
"""
import os
import sys

import serial

log = sys.argv[1] if len(sys.argv) > 1 else "deck.log"
port = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"
cmd = log + ".cmd"
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.1
s.dtr = s.rts = False
s.open()
with open(log, "ab", buffering=0) as f:
    while True:
        f.write(s.read(4096))
        if os.path.exists(cmd) and os.path.getsize(cmd):
            with open(cmd, "r+b") as c:
                data = c.read()
                c.seek(0)
                c.truncate()
            s.write(data)
