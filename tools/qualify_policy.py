"""Offline live-policy audition using the real DSP handler. Never accesses a pedal.

Six existing FX (drive/chorus/flanger/delay/reverb/512-tap synthetic IR), no NAM.
Full float output must match across callback partitions before writing a WAV.
"""
import argparse
import ctypes as ct
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import wave
from host import ROOT,build
from preset_bank import Capture,recall_packets
from preset_wire import bits

class Qualification:
    def __init__(self):
        self.lib=ct.CDLL(str(build(ROOT/('build/policy_qualification.dll' if os.name=='nt' else 'build/policy_qualification.so'),
            ROOT/'tests/policy_host.c',includes=[ROOT/'tests/stubs'])))
        c=self.lib;c.protocol_send.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
        c.protocol_audio.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        self.types=[4,6,7,5,8,3]
    def send(self,p):
        a=(ct.c_uint8*len(p))(*p);c=self.lib
        if c.protocol_send(int.from_bytes(p[:2],'little'),a,len(p))!=1:raise RuntimeError('handler rejected command')
        status=c.protocol_reply(12)
        if status not in (0,15):raise RuntimeError('control status '+str(status))
    def audio(self,values):
        a=(ct.c_float*len(values))(*values);b=(ct.c_float*len(values))();self.lib.protocol_audio(a,b,len(a));return bytes(b)
    def body(self):
        params=[4.,.5,.25,.8,.5,.3,.3,.5,.5,80.,.35,.3,.75,.5,.25,1.,0,0]
        ir=[.7]+[(-1 if i&1 else 1)*.0001 for i in range(1,512)]
        return [1,44100,3]+self.types+list(range(6))+[1]*6+[0]*6+[bits(v) for v in params]+[512,0]+[bits(v) for v in ir]+[0]*512
    def render(self,chunk):
        c=self.lib;c.protocol_init();self.token=2000
        for p in recall_packets(Capture('Six existing FX live policy',self.body()),1,1,1000):self.send(p)
        self.audio([0.]*256);output=bytearray()
        events=[(0,22050,None),(22050,33075,('enable',0)),(33075,44100,('policy',1)),
            (44100,66150,('enable',1)),(66150,176400,('policy',0))]
        for start,end,action in events:
            if action:
                self.token+=1;revision=c.protocol_revision();value=action[1]
                if action[0]=='enable':p=struct.pack('<HH8I',0x7f40,36,1,self.token,2,revision,3,5,0xffffffff,value)
                else:p=struct.pack('<HH7I',0x7f45,32,1,self.token,1,revision,3,5,value)
                self.send(p)
            for begin in range(start,end,chunk):
                values=[]
                for i in range(begin,min(begin+chunk,end)):
                    if i>=88200:values.append(0.);continue
                    t=i/44100;env=min(1.,(i%11025)/440.)*math.exp(-3*(t%.25))
                    values.append(.12*env*(math.sin(2*math.pi*110*t)+.3*math.sin(2*math.pi*330*t)))
                output.extend(self.audio(values))
        if c.policy_faults():raise RuntimeError('DSP numeric containment fault')
        if c.policy_actual(3)!=0 or c.protocol_revision()!=6:raise RuntimeError('unexpected final control state')
        return bytes(output)

def run(folder):
    folder=Path(folder)
    if folder.exists():raise ValueError('choose a fresh output directory to preserve artifacts')
    q=Qualification();a=q.render(64);b=q.render(37)
    if a!=b:raise RuntimeError('callback partition changed float output')
    samples=struct.unpack('<'+'f'*(len(a)//4),a)
    pcm=[max(-32768,min(32767,round(v*32768))) for v in samples]
    report=dict(kind=('Windows' if os.name=='nt' else 'Native')+' host DSP audition; not physical timing/audio qualification',
        frames=len(samples),sample_rate=44100,partitions=[64,37],float_bit_identical=True,float_sha256=hashlib.sha256(a).hexdigest(),
        peak=max(abs(v) for v in samples),unity_exceeding_frames=sum(abs(v)>1 for v in samples),
        pcm_rail_samples=sum(v in (-32768,32767) for v in pcm),containment_faults=0,
        effects=['overdrive','chorus','flanger','delay','reverb','synthetic IR512'],amp_backend=False,
        policy_changes_reset_all_histories=True,flashed=False,physical_measurement=False)
    folder.mkdir(parents=True)
    path=folder/'live-policy.wav'
    with wave.open(str(path),'wb') as w:w.setparams((1,2,44100,0,'NONE',''));w.writeframes(struct.pack('<'+'h'*len(pcm),*pcm))
    report['wav_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    (folder/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    print(json.dumps(run(p.parse_args().output),indent=2))
