#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline jog filter tuning: replays raw AS5600 captures (serial R, one value per line)
through the as5600.cpp filter and prints stray ticks at rest and tracking on a spin.
    python3 tools/jog_filter_sim.py rest.txt [more_rest.txt ...] spin.txt
(the last file is the spin; the others are at rest)"""
import sys
import numpy as np, math
def alpha(fc, dt=0.002): tau=1/(2*math.pi*fc); return dt/(dt+tau)
def run(v, minc, beta, dead, stop=6, fs=5.0, win=50):
    d=np.diff(v); d=d-4096*(d>2048)+4096*(d<-2048); P=np.concatenate([[0],np.cumsum(d)])*180/4096
    m0=P[:100].mean(); smooth=slow=m0; hist=[m0]*win; emitted=round(m0); outs=0; dirn=0; lag=[]
    for i,pos in enumerate(P):
        slow+=alpha(fs)*(pos-slow); hist.append(slow); old=hist.pop(0)
        speed=(slow-old)/(win*0.002)
        smooth+=alpha(minc+beta*abs(speed))*(pos-smooth)
        e0=emitted
        if smooth>=emitted+1 and (dirn==1 or smooth>=emitted+dead):
            while smooth>=emitted+1: emitted+=1
            dirn=1
        elif smooth<=emitted-1 and (dirn==-1 or smooth<=emitted-dead):
            while smooth<=emitted-1: emitted-=1
            dirn=-1
        if abs(speed)<stop: dirn=0
        outs+=abs(emitted-e0); lag.append(pos-emitted)
    return outs, P[-1]-m0, emitted-round(m0), np.abs(lag[100:]).max()
for args in [(1,0.15,3),(1,0.15,2),(1.5,0.15,2)]:
    r=[run(np.loadtxt(f).astype(int),*args) for f in sys.argv[1:]]
    print(args,' rest stray:',[x[0] for x in r[:-1]],' spin %d of %.1f ticks, worst lag %.1f'%(r[-1][2],r[-1][1],r[-1][3]))
