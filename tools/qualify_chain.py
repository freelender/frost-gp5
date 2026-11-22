"""Host-only existing-block integration check and synthetic WAV audition set.

Never accesses the pedal. Host elapsed time includes capture/statistics and
must not be converted to a GP-5 CPU/deadline result. No NAM or pitch prototype.
"""
import argparse
import ctypes as ct
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import struct
import time
import wave
from host import ROOT,build
from chain_wav import EFFECT_TYPES,run as render_wav

NON_IR=('gain','gate','eq','overdrive','delay','chorus','flanger','reverb')

class Qualification:
    def __init__(self):
        self.lib=ct.CDLL(str(build(ROOT/('build/qualification.dll' if os.name=='nt' else 'build/qualification.so'),ROOT/'tests/qualification_host.c')))
        c=self.lib;c.qa_setup.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint,ct.c_uint]
        c.qa_run.argtypes=[ct.c_uint]*4+[ct.POINTER(ct.c_float)];c.qa_peak.restype=ct.c_float
        c.qa_value.restype=c.fx_faults.restype=ct.c_uint
        c.fx_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        c.fx_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.fx_amp_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint,ct.c_uint]
        c.fx_request.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint]
    def setup(self,c,names,policy=0):
        types=[EFFECT_TYPES[name] for name in names];a=(ct.c_uint*len(types))(*types)
        if not self.lib.qa_setup(c,a,len(types),policy):raise ValueError('invalid qualification route')
    def run(self,c,frames,chunk,controls=0):
        output=(ct.c_float*frames)();start=time.perf_counter()
        if not self.lib.qa_run(c,frames,chunk,controls,output):raise ValueError('invalid chunk size')
        elapsed=time.perf_counter()-start;data=bytes(output)
        return dict(frames=self.lib.qa_value(c,0),invalid_outputs=self.lib.qa_value(c,1),
            unity_exceedance_frames=self.lib.qa_value(c,2),faults=self.lib.qa_value(c,4),peak=self.lib.qa_peak(c),
            audio_sha256=hashlib.sha256(data).hexdigest(),host_elapsed_ms=elapsed*1000,_audio=data)
    def matrix(self,frames=32768):
        if frames<32768 or frames>441000:raise ValueError('matrix frames must be in [32768,441000]')
        results=[]
        for chosen in itertools.combinations(NON_IR,4):
            route=[*chosen,'ir']
            for label,names,policy,controls,second_chunk in (
                ('normal_keep',route,0,0,37),('reverse_control_cut',route[::-1],1,1,257)):
                self.setup(0,names,policy);self.setup(1,names,policy)
                a=self.run(0,frames,64,controls);b=self.run(1,frames,second_chunk,controls)
                equal=a.pop('_audio')==b.pop('_audio')
                if not equal or a['invalid_outputs'] or b['invalid_outputs']:raise AssertionError('integration failure: '+str(names))
                if label=='normal_keep' and (a['faults'] or b['faults']):raise AssertionError('unexpected normal-route fault: '+str(names))
                if label=='normal_keep' and (a['unity_exceedance_frames'] or b['unity_exceedance_frames']):raise AssertionError('normal route exceeds PCM unity: '+str(names))
                results.append(dict(profile=label,effects=names,ir_taps=512,policy='cut' if policy else 'keep',
                    controls=bool(controls),bit_identical=True,chunks=[64,second_chunk],runs=[a,b]))
        return dict(kind='Windows/Linux native host integration; not physical GP-5 timing',
            distinct_five_effect_combinations=70,cases=len(results),streams=len(results)*2,frames_per_stream=frames,
            total_frames=frames*len(results)*2,all_partition_checks_bit_identical=True,
            normal_peak=max(v['runs'][0]['peak'] for v in results if v['profile']=='normal_keep'),
            control_peak=max(v['runs'][0]['peak'] for v in results if v['profile']=='reverse_control_cut'),
            normal_unity_exceedance_frames=sum(v['runs'][0]['unity_exceedance_frames'] for v in results if v['profile']=='normal_keep'),
            control_unity_exceedance_frames=sum(v['runs'][0]['unity_exceedance_frames'] for v in results if v['profile']=='reverse_control_cut'),
            all_orders_tested=False,all_repeated_type_layouts_tested=False,
            ir='dense deterministic synthetic 512-tap test FIR, not a cabinet capture',
            timing_scope='host wall time includes capture/statistics; not a GP-5 CPU estimate',results=results,
            nam_integrated=False,physical_cycles_measured=False,hardware_ready=False)

def write_wav(path,samples):
    with wave.open(str(path),'wb') as w:
        w.setparams((1,2,44100,0,'NONE','not compressed'))
        w.writeframes(struct.pack('<'+'h'*len(samples),*[max(-32768,min(32767,round(v*32768))) for v in samples]))

def audition(folder):
    from preset_io import catalog,validate,save,render
    # Synthetic damped harmonic notes, explicitly not a real guitar recording.
    samples=[];notes=(82.4069,110.,146.8324,196.,246.9417)
    for i in range(88200):
        t=i/44100;note=min(int(t/.4),4);age=t-note*.4
        envelope=(1-math.exp(-age*400))*math.exp(-age*8)
        f=notes[note];samples.append(envelope*sum(math.sin(2*math.pi*f*k*age)/k**1.4 for k in range(1,9))*.16)
    source=folder/'00-synthetic-dry.wav';ir=folder/'synthetic-512-tap-filter.wav'
    write_wav(source,samples);write_wav(ir,[.7]+[.001 if i&1 else -.001 for i in range(1,512)])
    profiles=[('01-clean',['gain','gate','eq','ir'],dict(gain=.85,eq=(.9,1.,1.1))),
        ('02-drive-delay',['gate','overdrive','delay','ir'],dict(overdrive=(4.,.6,.45),delay=(220.,.3,.2))),
        ('03-chorus-reverb',['gate','chorus','reverb','ir'],dict(chorus=(.8,.5,.2),reverb=(.75,.6,.2))),
        ('04-flanger-delay-reverb',['flanger','delay','reverb','ir'],dict(flanger=(.3,.5,.3),delay=(180.,.25,.15),reverb=(.7,.5,.15))),
        ('05-five-blocks',['gate','overdrive','chorus','delay','ir'],dict(overdrive=(4.,.6,.45),chorus=(.8,.5,.2),delay=(220.,.3,.2)))]
    manifest=[]
    for name,effects,parameters in profiles:
        output=folder/(name+'.wav');render_wav(source,output,effects=effects,ir=ir,tail_seconds=2.,**parameters)
        with wave.open(str(output),'rb') as w:
            count=w.getnframes();data=w.readframes(count);values=struct.unpack('<'+'h'*count,data)
        meta=catalog();slots=[]
        for slot in range(6):
            type_name=effects[slot] if slot<len(effects) else 'empty'
            raw=dict(id=slot,type=type_name,enabled=type_name!='empty',parameters={})
            chosen=parameters.get(type_name)
            if chosen is not None:
                if isinstance(chosen,(int,float)):chosen=(chosen,)
                raw['parameters']=dict(zip(meta[type_name],chosen))
            if type_name=='ir':raw['asset']=ir.name
            slots.append(raw)
        preset=validate(dict(format='frost-preset',version=1,name=name,sample_rate=44100,
            slots=slots,order=list(range(6)),pre_count=len(effects),tail_seconds=2.),folder)
        preset_path=folder/(name+'.json');save(preset_path,preset)
        recalled=folder/(name+'-recalled.wav');recalled_metrics=render(preset_path,source,recalled)
        if output.read_bytes()!=recalled.read_bytes():raise AssertionError('preset recall differs from CLI profile: '+name)
        manifest.append(dict(file=output.name,effects=effects,parameters=parameters,frames=count,
            pcm_peak=max(abs(v) for v in values)/32768,pcm_rail_samples=sum(v in (-32768,32767) for v in values),
            sha256=hashlib.sha256(output.read_bytes()).hexdigest(),preset=preset_path.name,
            recalled_file=recalled.name,preset_recall_byte_identical=True,recall_metrics=recalled_metrics))
    (folder/'AUDITION.md').write_text('''# Synthetic block audition

These PCM16/44.1 kHz files use synthetic damped harmonic notes and a synthetic
512-tap test filter. They are not real guitar/cabinet captures or listening
qualification. Files01–05 include two seconds of explicit tail silence.
The dry stimulus lasts two seconds. Normal low-level audition should be used;
zero PCM rail samples is not proof of safe physical pedal routing/headroom.
No NAM, new effects, device access or firmware flashing is involved.
''',encoding='utf-8')
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/simple-blocks-check')
    p.add_argument('--frames',type=int,default=32768);p.add_argument('--skip-audition',action='store_true');a=p.parse_args()
    if a.output.exists():raise FileExistsError('Choose a fresh output folder to preserve earlier evidence')
    if not 32768<=a.frames<=441000:raise ValueError('matrix frames must be in [32768,441000]')
    a.output.mkdir(parents=True)
    report=Qualification().matrix(a.frames)
    if not a.skip_audition:report['audition']=audition(a.output)
    (a.output/'qualification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:report[k] for k in ('distinct_five_effect_combinations','cases','streams','total_frames','all_partition_checks_bit_identical')},indent=2))
    print('Host results only. Output:',a.output.resolve())

if __name__=='__main__':main()
