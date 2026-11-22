"""Frost Capture Model (FCM) bank builder: packs converted amp models into the
FCB1 bank read by the DSP AMP block (effects/fcm/fcm_effect.h).
Layout (little endian): header {magic 'FCB1', version 1, count, record_bytes}
then `count` records {name[16], FcmiModel (380 B), crc32 over name+model}.
A models-section image pads the bank to the stock section length with 0xFF.
Converted models are APPROXIMATIONS of their source NAM files.
Usage: python fcm_bank.py --out bank.bin NAME=model.npz [NAME=model.npz ...] [--section]"""
import argparse,struct,zlib
from pathlib import Path
MAGIC=0x31424346;VERSION=1;MAX=64;NAME=16;SOS=4;AA=20
MODEL_BYTES=3*4+3*SOS*5*4+9*4+3*4+AA*4      # 380
RECORD_BYTES=NAME+MODEL_BYTES+4              # 400
SECTION_LEN=0x42FE4                          # stock models section (0x66) length
def pack_model(m):
    """m: dict with pre/mid/post lists of [b0,b1,b2,a1,a2], gain, Ap/An/Kp/Kn [2], env_a, env_k[2], aa[20]."""
    out=struct.pack('<3I',len(m['pre']),len(m['mid']),len(m['post']))
    for key in ('pre','mid','post'):
        secs=list(m[key]);assert len(secs)<=SOS,(key,len(secs))
        for s in secs+[[0.0]*5]*(SOS-len(secs)):out+=struct.pack('<5f',*s)
    out+=struct.pack('<9f',m['gain'],m['Ap'][0],m['Ap'][1],m['An'][0],m['An'][1],m['Kp'][0],m['Kp'][1],m['Kn'][0],m['Kn'][1])
    out+=struct.pack('<3f',m['env_a'],m['env_k'][0],m['env_k'][1])
    aa=list(m['aa']);assert len(aa)==AA;out+=struct.pack('<%df'%AA,*aa)
    assert len(out)==MODEL_BYTES;return out
def pack_record(name,m):
    n=name.encode('ascii','replace')[:NAME-1];body=n+b'\0'*(NAME-len(n))+pack_model(m)
    return body+struct.pack('<I',zlib.crc32(body)&0xffffffff)
def build_bank(records):
    assert len(records)<=MAX
    return struct.pack('<4I',MAGIC,VERSION,len(records),RECORD_BYTES)+b''.join(pack_record(n,m) for n,m in records)
def section_image(bank):
    assert len(bank)<=SECTION_LEN;return bank+b'\xff'*(SECTION_LEN-len(bank))
def model_from_npz(path):
    """From build/nam-distill iirfit output (<capture>/<TAG>-sos.npz)."""
    import numpy as np
    z=np.load(path);sec=lambda s:[[float(r[0]),float(r[1]),float(r[2]),float(r[4]),float(r[5])] for r in s]
    for r in list(z['pre'])+list(z['mid'])+list(z['post']):assert abs(float(r[3])-1.0)<1e-12
    A=z['A'];K=z['K']
    return dict(pre=sec(z['pre']),mid=sec(z['mid']),post=sec(z['post']),gain=float(z['g']),
                Ap=[float(A[0][0]),float(A[1][0])],An=[float(A[0][1]),float(A[1][1])],Kp=[float(K[0][0]),float(K[1][0])],Kn=[float(K[0][1]),float(K[1][1])],
                env_a=float(z['ea']),env_k=[float(z['ek'][0]),float(z['ek'][1])],aa=[float(v) for v in z['AA']])
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',required=True);ap.add_argument('--section',action='store_true');ap.add_argument('models',nargs='+')
    a=ap.parse_args();recs=[]
    for spec in a.models:
        name,_,path=spec.partition('=');recs.append((name,model_from_npz(path)))
    bank=build_bank(recs);data=section_image(bank) if a.section else bank
    Path(a.out).write_bytes(data);print('wrote',a.out,len(data),'bytes,',len(recs),'models')
