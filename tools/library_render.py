"""Render host WAVs of every pedal-inspired model at default controls.

Input: a mono PCM16/44.1 kHz WAV (e.g. a DI guitar take) or, by default, a
synthetic plucked phrase. Output goes to build/library/renders/ (ignored).
These are HOST renders of the C kernels for listening/inspection only; they
are not GP-5 measurements or audio qualification.
"""
import argparse
import ctypes as ct
import math
import os
from pathlib import Path
import struct
import sys
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from chain_wav import LIBRARY_TYPES

def phrase(seconds=6.):
    notes=[110.,146.83,196.,246.94,329.63,196.,(110.,164.81,220.)];x=[0.]*int(44100*seconds);step=int(44100*.75)
    for k,n in enumerate(notes):
        for f in (n if isinstance(n,tuple) else (n,)):
            for i in range(step*2):
                j=k*step+i
                if j<len(x):x[j]+=.12*math.exp(-3.*i/44100)*sum(math.sin(2*math.pi*f*h*i/44100)/h for h in range(1,7))
    return x

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path);p.add_argument('--tail',type=float,default=2.)
    a=p.parse_args()
    if a.input:
        with wave.open(str(a.input),'rb') as w:
            if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,44100):raise SystemExit('input must be mono PCM16 44.1 kHz')
            data=w.readframes(w.getnframes());x=[v/32768 for v in struct.unpack('<'+'h'*(len(data)//2),data)]
    else:x=phrase()
    x+= [0.]*int(44100*a.tail)
    c=ct.CDLL(str(build(ROOT/('build/library_render'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/library_host.c')))
    out=ROOT/'build/library/renders';out.mkdir(parents=True,exist_ok=True)
    src=(ct.c_float*len(x))(*x);dst=(ct.c_float*len(x))()
    for name,t in LIBRARY_TYPES.items():
        c.lib_init(0);types=(ct.c_uint*1)(t);assert c.lib_configure(0,types,1);c.lib_enable(0,0,1)
        c.lib_process(0,src,dst,len(x),64);peak=max(abs(v) for v in dst) or 1.
        norm=.9/peak if peak>.9 else 1. # listening convenience; the printed peak is the raw model output
        with wave.open(str(out/(name+'.wav')),'wb') as w:
            w.setparams((1,2,44100,0,'NONE','not compressed'))
            w.writeframes(struct.pack('<'+'h'*len(x),*[max(-32768,min(32767,round(v*norm*32767))) for v in dst]))
        print(f'{name:14s} raw peak {peak:.3f} (input peak {max(map(abs,x)):.3f}){"  normalized" if norm<1 else ""}')

if __name__=='__main__':main()
