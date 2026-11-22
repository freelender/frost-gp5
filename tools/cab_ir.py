"""Bake a user cabinet IR WAV into a firmware header (explicit, documented processing).

Accepts RIFF WAV: PCM 16/24/32-bit or IEEE float 32/64, any channel count
(averaged to mono), any rate (band-limited windowed-sinc resample to 44.1 kHz).
Then, explicitly: trim leading silence (keep 2 samples before the first sample
>= 10% of peak), keep the first TAPS samples, apply a half-cosine fade over the
last FADE taps, and calibrate the frequency-response peak near unity, with
sum(|h|) <= 7.999. Output gain/headroom still depend on the signal and chain.

128 taps (~2.9 ms) is the direct-form limit for the line-test route; 31a's
FFT path admits 256 taps (~5.8 ms), or the opt-in 512-tap profile (~11.6 ms).
The 512-tap profile requires its own timing/listening qualification. The
truncated response keeps the cabinet's main colouration but loses its longest
low-frequency tail. Standard library only; no IO beyond the given paths.
"""
import argparse
import hashlib
import math
from pathlib import Path
import struct
from ir_normalize import response_gain

TAPS=128
FADE=24
ARRAY=512
MAX_SECONDS=10

def read_wav(data):
    """Return (rate, mono float list) from RIFF WAV bytes."""
    if len(data)<12 or data[:4]!=b'RIFF' or data[8:12]!=b'WAVE':raise ValueError('not a RIFF/WAVE file')
    pos=12;fmt=None;samples=None
    while pos+8<=len(data):
        cid=data[pos:pos+4];size=struct.unpack_from('<I',data,pos+4)[0];body=data[pos+8:pos+8+size]
        if len(body)!=size:raise ValueError('truncated WAV chunk')
        if cid==b'fmt ':
            if size<16:raise ValueError('short fmt chunk')
            tag,channels,rate,_,align,bits=struct.unpack_from('<HHIIHH',body)
            if tag==0xFFFE:
                if size<40:raise ValueError('short extensible fmt chunk')
                tag=struct.unpack_from('<H',body,24)[0]
            fmt=(tag,channels,rate,align,bits)
        elif cid==b'data':samples=body
        pos+=8+size+(size&1)
    if fmt is None or samples is None:raise ValueError('WAV needs fmt and data chunks')
    tag,channels,rate,align,bits=fmt
    if not 1<=channels<=8 or not 8000<=rate<=192000:raise ValueError('unsupported channels or rate')
    width=bits//8
    if align!=channels*width:raise ValueError('inconsistent WAV block alignment')
    if tag==1 and bits in (16,24,32):
        def sample(b,o):
            if bits==24:
                v=b[o]|(b[o+1]<<8)|(b[o+2]<<16);v-=(v&0x800000)<<1;return v/8388608.0
            return struct.unpack_from('<h' if bits==16 else '<i',b,o)[0]/(32768.0 if bits==16 else 2147483648.0)
    elif tag==3 and bits in (32,64):
        def sample(b,o):return struct.unpack_from('<f' if bits==32 else '<d',b,o)[0]
    else:raise ValueError('unsupported WAV encoding (need PCM 16/24/32 or float 32/64)')
    frames=len(samples)//align
    if not 1<=frames<=rate*MAX_SECONDS:raise ValueError('IR must be 1 sample .. %d seconds'%MAX_SECONDS)
    mono=[sum(sample(samples,f*align+c*width) for c in range(channels))/channels for f in range(frames)]
    if any(not math.isfinite(v) for v in mono):raise ValueError('non-finite IR samples')
    return rate,mono

def resample(x,rate,out=44100,zeros=32):
    """Windowed-sinc (Blackman) band-limited resampling; identity at equal rates."""
    if rate==out:return list(x)
    ratio=out/rate;cut=min(1.0,ratio)*0.95;n=int(math.ceil(len(x)*ratio));y=[]
    half=zeros/cut
    for i in range(n):
        t=i/ratio;lo=max(0,int(math.floor(t-half)));hi=min(len(x)-1,int(math.ceil(t+half)));acc=0.0
        for k in range(lo,hi+1):
            d=t-k;u=d/half
            if abs(u)>=1:continue
            w=0.42+0.5*math.cos(math.pi*u)+0.08*math.cos(2*math.pi*u)
            acc+=x[k]*cut*(1.0 if d==0 else math.sin(math.pi*cut*d)/(math.pi*cut*d))*w
        y.append(acc)
    return y

def shape(x,taps=TAPS,fade=FADE):
    peak=max((abs(v) for v in x),default=0.0)
    if not peak>1e-6:raise ValueError('IR is silent')
    start=next(i for i,v in enumerate(x) if abs(v)>=0.1*peak);start=max(0,start-2)
    h=list(x[start:start+taps]);h+=[0.0]*(taps-len(h))
    for i in range(fade):h[taps-fade+i]*=0.5*(1+math.cos(math.pi*(i+1)/fade))
    l1=sum(abs(v) for v in h)
    if not l1>1e-9:raise ValueError('IR has no energy in the kept window')
    gain=response_gain(h)
    h=[struct.unpack('<f',struct.pack('<f',v*gain))[0] for v in h]
    return h,dict(trimmed_leading_samples=start,source_peak=peak,kept_taps=taps,fade_taps=fade)

def convert(data,name='cab.wav',taps=TAPS):
    rate,mono=read_wav(data)
    h,info=shape(resample(mono,rate),taps)
    info.update(source=name,source_sha256=hashlib.sha256(data).hexdigest(),source_rate=rate,source_frames=len(mono),l1=sum(abs(v) for v in h))
    return h,info

def q15(h):
    """31a: 16-bit table plus one float32 scale (peak -> 32767), half the flash
    of float32. Coefficient error <= scale/2 (a 16-bit IR WAV's precision)."""
    peak=max(abs(v) for v in h)
    scale=struct.unpack('<f',struct.pack('<f',peak/32767.0))[0]
    q=[max(-32768,min(32767,int(round(v/scale)))) for v in h]
    return q,scale

def header(h,info,fmt='float'):
    # 28a: store only the kept taps; the loader zero-pads longer TAPS settings
    # exactly as the former 512-entry zero-padded table did (saves DSP flash).
    body=list(h)
    label='USER CABINET IR %s sha256 %s: mono, %d Hz -> 44100, lead trim %d, first %d taps, %d-tap fade, response-normalized'%(
        info['source'].replace('*',''),info['source_sha256'],info['source_rate'],info['trimmed_leading_samples'],info['kept_taps'],info['fade_taps'])
    if fmt=='q15':
        q,scale=q15(body)
        return ('/* '+label+', 16-bit q15 x scale */\n#include <stdint.h>\n#define FROST_TEST_IR_Q15 1\n'
                'static const float frost_test_ir_q15_scale='+float(scale).hex()+'f;\n'
                'static const int16_t frost_test_ir_q15['+str(len(q))+']={\n'+',\n'.join(str(v) for v in q)+'\n};\n')
    if fmt!='float':raise ValueError('cab IR header format: float or q15')
    return ('/* '+label+' */\nstatic const float frost_test_ir['+str(len(body))+']={\n'+
            ',\n'.join(float(v).hex()+'f' for v in body)+'\n};\n')

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('wav',type=Path);p.add_argument('header',type=Path)
    a=p.parse_args(argv);data=a.wav.read_bytes();h,info=convert(data,a.wav.name)
    a.header.write_text(header(h,info));print(info)

if __name__=='__main__':main()
