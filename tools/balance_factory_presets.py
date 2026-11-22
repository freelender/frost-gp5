"""Offline static factory LEVEL calibration against Deluxe Clean.

Uses the production C chain with compact reverb and the chosen 512-tap q15
cabinet, with VOL80 authored in factory.json. Measures the same synthetic
plucked/chord phrase at three input amplitudes, matching 80 Hz..8 kHz energy
at the 0.20 input peak and checking headroom at 0.08/0.40 input peaks.
This is a repeatable guitar-band RMS proxy, not LUFS,
audition, live normalization, device timing or a guarantee for every guitar.
Private input coefficients/renders stay in the caller's ignored output folder.
"""
import argparse
import ctypes as ct
import json
import math
import os
from pathlib import Path
import struct
import wave
from cab_ir import convert,q15
from factory_presets import build_bank
from host import build,ROOT
from library_render import phrase

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cab',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--apply',action='store_true',help='update factory.json LEVELs and regenerate factory header')
    args=p.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
    source=json.loads((ROOT/'presets/factory.json').read_text(encoding='utf-8'))
    manifest=build_bank(out/'before')
    records=[(out/'before'/('%02d.fpst'%i)).read_bytes() for i in range(1,21)]
    h,info=convert(args.cab.read_bytes(),args.cab.name,512);q,scale=q15(h)
    ir=(ct.c_float*512)(*[v*scale for v in q])
    dll=build(out/('render.dll' if os.name=='nt' else 'render.so'),ROOT/'tests/factory_level_host.c')
    c=ct.CDLL(str(dll.resolve()));ptr=ct.POINTER(ct.c_float)
    c.factory_setup.argtypes=[ct.POINTER(ct.c_uint8),ptr,ct.c_uint]
    c.factory_render.argtypes=[ptr,ptr,ct.c_uint]
    c.factory_energy.argtypes=[ptr,ct.c_uint];c.factory_energy.restype=ct.c_double
    x=phrase();peak=max(map(abs,x));active=len(x)
    signals=[(level,(ct.c_float*(active+44100))(*[v*level/peak for v in x])) for level in (.08,.20,.40)]
    n=len(signals[0][1]);dst=(ct.c_float*n)()
    def measure(record,save=None):
        energies=[];peaks=[]
        for level,signal in signals:
            data=(ct.c_uint8*len(record))(*record)
            if not c.factory_setup(data,ir,512):raise ValueError('invalid render configuration')
            if not c.factory_render(signal,dst,n):raise ValueError('chain fault during render')
            energies.append(c.factory_energy(dst,active));peaks.append(max(map(abs,dst)))
            if save and level==.20:
                with wave.open(str(out/(save+'.wav')),'wb') as w:
                    w.setparams((1,2,44100,0,'NONE','not compressed'))
                    w.writeframes(struct.pack('<'+'h'*n,*[max(-32768,min(32767,round(v*32767))) for v in dst]))
        return dict(energy=energies[1],energies=energies,peaks=peaks)
    before=[measure(record) for record in records]
    target=before[1]['energy'];rows=[]
    # Match only output LEVEL: amp level, or acoustic simulator level. MASTER,
    # GAIN, compression, effect mix and all cab VOL80 settings stay authored.
    for i,(row,record,metrics) in enumerate(zip(source['presets'],records,before)):
        slot=next((j for j,b in enumerate(row['chain']) if 45<=b['type']<=51),None)
        if slot is None:slot=next(j for j,b in enumerate(row['chain']) if b['type']==44)
        block=row['chain'][slot];meta=manifest[i]['chain'][slot]['params'];old=meta['LEVEL']
        # AMP/ACOUST LEVEL uses frost_level: amplitude=(knob/50)^2,
        # so measured energy varies with the fourth power of the knob.
        required=old*(target/metrics['energy'])**.25
        rows.append(dict(number=i+1,name=row['name'],slot=slot,old_level=old,required_level=required,before=metrics))
    # The common target must be reachable with every existing LEVEL control.
    limiting=min(1.,min(100/r['required_level'] for r in rows))
    target*=limiting**4
    for row,result,record in zip(source['presets'],rows,records):
        level=max(1,min(100,round(result['required_level']*limiting)))
        row['chain'][result['slot']]['params']['LEVEL']=level
        data=bytearray(record);slot=result['slot'];offset=32+36*slot
        param=4 if data[offset]==44 else 6
        struct.pack_into('<f',data,offset+4+4*param,float(level))
        measured=measure(data,save='%02d-%s'%(result['number'],row['name'].replace(' ','-')))
        result.update(level=level,after=measured,error_db=10*math.log10(measured['energy']/target))
        print('%02d %-16s LEVEL %2d -> %3d  %+5.2f dB  peak %.3f'%(result['number'],row['name'],result['old_level'],level,result['error_db'],max(measured['peaks'])),flush=True)
    if max(max(r['after']['peaks']) for r in rows)>=.95:raise ValueError('calibrated bank exceeds peak headroom budget')
    if max(abs(r['error_db']) for r in rows)>.6:raise ValueError('calibration error exceeds 0.6 dB')
    report=dict(scope=__doc__,cab=info,input_peaks=[s[0] for s in signals],target_energy=target,target_adjustment_db=20*math.log10(limiting),presets=rows)
    (out/'levels.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if args.apply:
        (ROOT/'presets/factory.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
        build_bank(out/'bank',ROOT/'firmware/controller/factory_bank.h')
    print('Offline only; no pedal access. Peak/headroom and energy are host measurements, not listening qualification.')

if __name__=='__main__':main()
