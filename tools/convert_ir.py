"""Convert a mono PCM16/44.1 kHz cabinet IR WAV to exact float32 C constants.

No automatic resampling, truncation, normalization or invented cabinet capture.
--diagnostic emits a labeled synthetic FIR solely for timing/reference tests.
"""
import argparse
import math
from pathlib import Path
import struct
import wave

def coefficients(path):
    with wave.open(path if hasattr(path,'read') else str(path),'rb') as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getcomptype())!=(1,2,44100,'NONE'):
            raise ValueError('IR requires mono PCM16 at 44100 Hz')
        n=w.getnframes()
        if not 1<=n<=512:raise ValueError('IR must have 1..512 samples; no implicit truncation')
        raw=w.readframes(n)
        if len(raw)!=2*n:raise ValueError('truncated IR WAV')
        values=[v/32768.0 for v in struct.unpack('<'+'h'*n,raw)]
        if sum(abs(v) for v in values)>8:raise ValueError('IR absolute coefficient sum exceeds 8')
        return values

def emit(values,path,diagnostic=False):
    floats=[struct.unpack('<f',struct.pack('<f',v))[0] for v in values]
    if not 1<=len(floats)<=512 or any(not math.isfinite(v) for v in floats) or sum(abs(v) for v in floats)>8:
        raise ValueError('invalid FIR coefficients')
    name='frost_test_ir' if diagnostic else 'frost_cabinet_ir'
    label='SYNTHETIC DIAGNOSTIC FIR; NOT A CABINET CAPTURE' if diagnostic else 'User-provided cabinet IR; not normalized or resampled'
    Path(path).write_text('/* '+label+' */\nstatic const float '+name+'['+str(len(floats))+']={\n'+
        ',\n'.join(v.hex()+'f' for v in floats)+'\n};\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output');p.add_argument('--wav');p.add_argument('--diagnostic',action='store_true')
    a=p.parse_args()
    if bool(a.wav)==a.diagnostic:p.error('choose exactly one of --wav and --diagnostic')
    emit([.03*.97**i for i in range(512)] if a.diagnostic else coefficients(a.wav),a.output,a.diagnostic)
