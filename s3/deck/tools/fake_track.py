#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
"""A made-up 126 BPM track's waveform for the UI previews, so they need no real music.

Synthesises kick, hats, bass and a pad in an intro / build / breakdown / drop / outro shape,
then writes what the deck's analysis would: per 128 samples a peak (0..255) and an RGB565
colour from the low / mid / high energy (low blue, mid amber, high white).

    python3 tools/fake_track.py host/fake_track.bin
"""
import struct, sys
import numpy as np

SR, BPM, BARS = 44100, 126.0, 84
spb = 60 / BPM
n = int(BARS * 4 * spb * SR)
t = np.arange(n) / SR
rng = np.random.default_rng(1)
beat = t / spb
bar = (beat // 4).astype(int)
ph = beat % 1

def env(x, k): return np.exp(-x * k)

kick = np.sin(2 * np.pi * (45 + 120 * env(ph * spb, 30)) * ph * spb) * env(ph * spb, 9)
hat_ph = (beat * 2) % 1
hats = rng.standard_normal(n) * env(hat_ph * spb / 2, 60) * ((beat * 2).astype(int) % 2 == 1)
bass = np.sign(np.sin(2 * np.pi * 55 * t)) * 0.5 * env(((beat + 0.5) % 1) * spb, 6)
pad = (np.sin(2 * np.pi * 220 * t) + 0.6 * np.sin(2 * np.pi * 277.2 * t) + 0.5 * np.sin(2 * np.pi * 329.6 * t)) * 0.25

def on(a, b): return ((bar >= a) & (bar < b)).astype(float)
mix = (kick * (on(0, 32) + on(40, 84))
       + hats * 0.07 * (on(8, 32) + on(40, 76))
       + bass * 0.45 * (on(16, 32) + on(44, 76))
       + pad * (on(28, 44) * np.clip((bar - 28 + ph / 4 * 0) / 4, 0, 1) + on(56, 72) * 0.5))
mix *= 0.9 / np.max(np.abs(mix))

B = 128
nb = n // B
blk = mix[:nb * B].reshape(nb, B)
peak = np.sqrt(np.mean(blk ** 2, axis=1))     # RMS: what reads as loudness on a waveform
win = np.hanning(2 * B)
pad2 = np.concatenate([mix, np.zeros(2 * B)])
lo = np.zeros(nb); mi = np.zeros(nb); hi = np.zeros(nb)
f = np.fft.rfftfreq(2 * B, 1 / SR)
for i in range(nb):
    s = np.abs(np.fft.rfft(pad2[i * B:i * B + 2 * B] * win)) ** 2
    lo[i] = s[f < 200].sum(); mi[i] = s[(f >= 200) & (f < 2500)].sum(); hi[i] = s[f >= 2500].sum()
# colour: each band's share of the energy, the highs boosted (they carry little energy but
# matter to the eye), mixed from blue (low), amber (mid) and white (high)
e = np.stack([lo, mi * 3, hi * 25], 1) ** 0.5
e /= e.sum(1, keepdims=True) + 1e-12
base = np.array([[30, 90, 255], [255, 160, 30], [255, 255, 255]], float)
c = np.clip(e @ base, 0, 255).astype(int)
col = ((c[:, 0] & 0xF8) << 8) | ((c[:, 1] & 0xFC) << 3) | (c[:, 2] >> 3)
pk = np.clip((peak / peak.max()) ** 0.8 * 255, 0, 255).astype(np.uint8)

with open(sys.argv[1], "wb") as fo:
    fo.write(b"RWAV" + struct.pack("<If", nb, SR / B))
    fo.write(pk.tobytes())
    fo.write(col.astype("<u2").tobytes())
print(f"{nb} bins, {n / SR:.1f} s, {BPM} BPM")
