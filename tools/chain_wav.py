"""Offline mono PCM16/44.1 kHz chain runner. Never accesses the pedal."""
import argparse
import math
from pathlib import Path
import struct
import wave
from chain_host import Chain

EFFECT_TYPES={'gain':0,'gate':1,'eq':2,'ir':3,'overdrive':4,'delay':5,'chorus':6,'flanger':7,'reverb':8}
# Appended pedal-inspired models (stable IDs, never reassigned); see docs/User%20Guide.md.
LIBRARY_TYPES={'ts':9,'bd2':10,'klon':11,'bluesbreaker':12,'ocd':13,'ds1':14,'rat':15,'distplus':16,
    'dd3':17,'dm2':18,'rv6':19,'spring':20,'phase90':21,'ce2':22,'mistress':23,'m117':24,'univibe':25,
    'vb2':26,'tr2':27,'pog':28,'dynacomp':29,'compplus':30,'ge7':31,'ns2':32,
    'amp':33,  # 33: EXPERIMENTAL AMP (Frost Capture Model, converted NAM approximation)
    'drop':34,'ns1x':35,'multiband':36,'highpass':37,'lowpass':38,'autowah':39,'treble_boost':40,'foxx':41,'tape':42,'vintage_fuzz':43,'acoustic':44,'deluxe':45,'twin':46,'ac30':47,'jc120':48,'jcm800':49,'rectifier':50,'5150':51}  # 34: stage 5 drop tune
ALL_TYPES={**EFFECT_TYPES,**LIBRARY_TYPES}

def run(source,destination,gain=1.,threshold=.001,eq=(1.,1.,1.),ir=None,effects=None,bypass_policy='keep',overdrive=(4.,.5,.5),delay=(250.,.35,.3),tail_seconds=0.,chorus=(.8,.5,.3),flanger=(.3,.5,.5),reverb=(.75,.5,.25)):
    if Path(source).resolve()==Path(destination).resolve():raise ValueError('input and output must differ')
    names=list(effects) if effects is not None else ['gain','gate','eq','ir']
    if len(names)>6 or any(name not in ALL_TYPES for name in names):raise ValueError('requires at most six registered effects')
    if ir is not None and 'ir' not in names:raise ValueError('IR file requires an IR slot')
    if bypass_policy not in ('keep','cut'):raise ValueError('unknown bypass policy')
    if not math.isfinite(tail_seconds) or not 0.<=tail_seconds<=30.:raise ValueError('tail seconds must be finite in [0,30]')
    with wave.open(str(source),'rb') as wav:
        if (wav.getnchannels(),wav.getsampwidth(),wav.getframerate(),wav.getcomptype())!=(1,2,44100,'NONE'):
            raise ValueError('requires mono uncompressed 16-bit PCM at 44100 Hz')
        c=Chain()
        c.configure([ALL_TYPES[name] for name in names])
        tail=round(tail_seconds*44100)
        values=None
        if ir is not None:
            from convert_ir import coefficients
            values=coefficients(ir)
        for slot,name in enumerate(names):
            c.bypass_policy(slot,int(bypass_policy=='cut'))
            parameters={'gain':(gain,),'gate':(threshold,),'eq':eq,'ir':(1.,),'overdrive':overdrive,'delay':delay,'chorus':chorus,'flanger':flanger,'reverb':reverb}.get(name,())
            for p,v in enumerate(parameters):
                if not c.lib.chain_set(slot,p,v):raise ValueError('parameter out of range')
            if name=='ir' and values is not None:
                c.load_ir(values,slot);tail+=len(values)-1
            c.lib.chain_enable(slot,int(name!='ir' or values is not None))
        with wave.open(str(destination),'wb') as out:
            out.setparams((1,2,44100,0,'NONE','not compressed'))
            while data:=wav.readframes(64):
                samples=struct.unpack('<'+'h'*(len(data)//2),data)
                result=c.process([x/32768. for x in samples])
                # Saturation belongs to PCM serialization, not the float engine.
                out.writeframesraw(struct.pack('<'+'h'*len(result),*[max(-32768,min(32767,round(y*32768))) for y in result]))
            while tail:
                n=min(64,tail);result=c.process([0.]*n);tail-=n
                out.writeframesraw(struct.pack('<'+'h'*n,*[max(-32768,min(32767,round(y*32768))) for y in result]))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input');p.add_argument('output')
    p.add_argument('--gain',type=float,default=1.);p.add_argument('--threshold',type=float,default=.001)
    p.add_argument('--eq',type=float,nargs=3,default=(1.,1.,1.));p.add_argument('--ir')
    p.add_argument('--overdrive',type=float,nargs=3,default=(4.,.5,.5),metavar=('DRIVE','TONE','LEVEL'))
    p.add_argument('--delay',type=float,nargs=3,default=(250.,.35,.3),metavar=('TIME_MS','FEEDBACK','MIX'))
    p.add_argument('--chorus',type=float,nargs=3,default=(.8,.5,.3),metavar=('RATE_HZ','DEPTH','MIX'))
    p.add_argument('--flanger',type=float,nargs=3,default=(.3,.5,.5),metavar=('RATE_HZ','DEPTH','FEEDBACK'))
    p.add_argument('--reverb',type=float,nargs=3,default=(.75,.5,.25),metavar=('DECAY','DAMP','MIX'))
    p.add_argument('--tail-seconds',type=float,default=0.,help='append 0..30 seconds of silence to render recursive tails')
    p.add_argument('--effects',nargs='*',choices=ALL_TYPES,help='ordered cold route, max six; repeated effects have separate state')
    p.add_argument('--bypass-policy',choices=('keep','cut'),default='keep',help='keep histories (default) or fade/cut/suspend')
    a=p.parse_args()
    run(a.input,a.output,a.gain,a.threshold,a.eq,a.ir,a.effects,a.bypass_policy,a.overdrive,a.delay,a.tail_seconds,a.chorus,a.flanger,a.reverb)
