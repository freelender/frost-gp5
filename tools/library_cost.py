"""Host-relative per-model cost ESTIMATE (not a GP-5 measurement).

Times each model alone through the guarded stage-major path on this PC and
reports the ratio to the original generic overdrive/chorus/reverb kernels.
Use only to rank models and plan hardware slot-profiling (0x7f3e); host CPU
pipelines, caches and FPU differ from the GP-5's flash-resident Cortex-M4.
"""
import ctypes as ct
import json
import math
import os
from pathlib import Path
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

NAMES={4:'overdrive(generic)',5:'delay(generic)',6:'chorus(generic)',8:'reverb(generic)',9:'ts',10:'bd2',11:'klon',12:'bluesbreaker',
    13:'ocd',14:'ds1',15:'rat',16:'distplus',17:'dd3',18:'dm2',19:'rv6',20:'spring',21:'phase90',22:'ce2',23:'mistress',24:'m117',
    25:'univibe',26:'vb2',27:'tr2',28:'pog',29:'dynacomp',30:'compplus',31:'ge7',32:'ns2',33:'amp'}

def measure(seconds=4.,repeats=3):
    c=ct.CDLL(str(build(ROOT/('build/library_cost'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/library_host.c')))
    c.lib_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
    n=int(44100*seconds);x=(ct.c_float*n)(*[.3*math.sin(i*.05)*math.exp(-(i%22050)/9000.) for i in range(n)]);y=(ct.c_float*n)()
    def run(t,mode=None):
        best=1e9
        for _ in range(repeats):
            c.lib_init(0);a=(ct.c_uint*1)(t);assert c.lib_configure(0,a,1);c.lib_enable(0,0,1)
            if mode is not None:c.lib_set(0,0,3,float(mode))
            t0=time.perf_counter();c.lib_process(0,x,y,n,64);best=min(best,time.perf_counter()-t0)
        return best/n*1e9
    empty=run(255) if False else 0.
    out={}
    for t,name in NAMES.items():out[name]=run(t)
    for m,label in enumerate(('room','hall','plate','mod','shimmer')):out['rv6.'+label]=run(19,m)
    ref=out['overdrive(generic)']
    return {k:dict(ns_per_sample=round(v,2),relative_to_generic_overdrive=round(v/ref,2)) for k,v in out.items()}

if __name__=='__main__':
    print(json.dumps(measure(),indent=1))
