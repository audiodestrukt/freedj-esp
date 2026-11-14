#!/bin/sh
# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
# Pack audio files and write them to the `tracks` partition (0x310000, partitions.csv).
#   tools/flash_tracks.sh ~/Music/dj-library/a.mp3 ~/Music/dj-library/b.mp3
# 44.1 kHz mono (the board has one speaker), 40 s of each from 1:00. Override with RATE,
# START, LENGTH. Uses freedj-nano's packer (the same format the C6 deck plays).
set -e
here=$(dirname "$0")
nano=${NANO:-$here/../../../../freedj-nano}
out=${OUT:-$here/../tracks.bin}
python3 "$nano/tools/pack_tracks.py" --rate "${RATE:-44100}" --size 0xCF0000 \
    --start "${START:-60}" --length "${LENGTH:-40}" -o "$out" "$@"
esptool --chip esp32s3 -p "${PORT:-/dev/ttyACM0}" -b 921600 write-flash 0x310000 "$out"
