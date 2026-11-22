"""Version1 offline preset files for existing blocks. Not pedal persistence.

All JSON/asset parsing happens before audio processing. No executable content.
The renderer has no amp; pre_count is preserved for later on-pedal support.
"""
import ctypes as ct
from dataclasses import dataclass
import hashlib
import io
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
import wave
from chain_host import Chain
from headroom import Headroom,HeadroomError
from chain_wav import EFFECT_TYPES,ALL_TYPES
from convert_ir import coefficients

TYPES={**ALL_TYPES,'empty':255}
LEGACY_TYPES=set(EFFECT_TYPES)|{'empty'}
WORKSPACE_CAPACITY_GROUP={1:0,2:1,3:2,4:3} # native workspace class -> capacity group

def workspace_class(type_id):
    return 0 if type_id==255 else Chain.library().chain_type_workspace(type_id)

def check_capacity(type_ids):
    lib=Chain.library();counts={}
    for t in type_ids:
        w=workspace_class(t)
        if w:counts[w]=counts.get(w,0)+1
    if any(n>lib.chain_workspace_capacity(WORKSPACE_CAPACITY_GROUP[w]) for w,n in counts.items()):
        raise ValueError('preset exceeds static workspace capacity')

def catalog():
    c=Chain.library();c.chain_parameter_name.restype=ct.c_char_p;c.chain_parameter_bound.restype=ct.c_float
    if c.chain_effect_count()!=len(ALL_TYPES) or sorted(ALL_TYPES.values())!=list(range(c.chain_effect_count())):raise ValueError('host type map differs from native registry')
    return {name:{c.chain_parameter_name(type,p).decode():tuple(c.chain_parameter_bound(type,p,f) for f in range(3))
        for p in range(c.chain_parameter_count(type))} for name,type in TYPES.items()}

def keys(value,allowed,required=()):
    if type(value) is not dict or set(value)-set(allowed) or set(required)-set(value):raise ValueError('unknown or missing preset fields')

def number(value,lo,hi):
    if type(value) not in (int,float):raise ValueError('preset parameter must be numeric')
    try:v=float(value)
    except OverflowError:raise ValueError('preset number is too large') from None
    if not math.isfinite(v):raise ValueError('preset number must be finite')
    v=ct.c_float(v).value
    if not math.isfinite(v) or not lo<=v<=hi:raise ValueError('preset parameter is out of range')
    return v

@dataclass(frozen=True)
class Preset:
    data:dict
    assets:dict

def validate(data,folder):
    keys(data,('format','version','name','sample_rate','slots','order','pre_count','tail_seconds'),
        ('format','version','name','sample_rate','slots','order','pre_count'))
    if data['format']!='frost-preset' or type(data['version']) is not int or data['version'] not in (1,2):raise ValueError('unsupported preset format/version')
    if type(data['sample_rate']) is not int or data['sample_rate']!=44100:raise ValueError('preset requires44100 Hz')
    name=data['name']
    if type(name) is not str or not name or len(name.encode('utf-8'))>64 or not name.isprintable():raise ValueError('invalid preset name')
    order=data['order'];pre=data['pre_count']
    if type(order) is not list or len(order)!=6 or any(type(v) is not int for v in order) or sorted(order)!=list(range(6)):raise ValueError('invalid preset order')
    if type(pre) is not int or not 0<=pre<=6:raise ValueError('invalid pre/post boundary')
    if type(data['slots']) is not list or len(data['slots'])!=6:raise ValueError('preset needs six stable slots')
    meta=catalog();slots=[None]*6;assets={};counts={}
    for raw in data['slots']:
        keys(raw,('id','type','enabled','parameters','bypass','asset','asset_sha256'),('id','type','enabled'))
        slot=raw['id'];type_name=raw['type'];enabled=raw['enabled'];policy=raw.get('bypass','keep')
        if type(slot) is not int or not 0<=slot<6 or slots[slot] is not None:raise ValueError('invalid/duplicate slot ID')
        if type(type_name) is not str or type_name not in TYPES or type(enabled) is not bool or policy not in ('keep','cut'):raise ValueError('invalid slot type/enable/bypass')
        if data['version']==1 and type_name not in LEGACY_TYPES:raise ValueError('pedal-inspired models require preset version 2')
        if type_name=='empty' and enabled:raise ValueError('empty slot cannot be enabled')
        params=raw.get('parameters',{});keys(params,meta[type_name])
        values={p:number(params.get(p,bounds[2]),bounds[0],bounds[1]) for p,bounds in meta[type_name].items()}
        # Omitted v1 JSON knobs retain their historical unity IR default.
        # Version2/new product blocks use the current catalog (VOL80).
        if data['version']==1 and type_name=='ir' and 'LEVEL' not in params:values['LEVEL']=1.0
        if type_name!='empty':
            for index,(p,v) in enumerate(values.items()):
                if Chain.library().chain_parameter_flags(TYPES[type_name],index)&1 and v!=int(v):raise ValueError('mode parameter must be an integer')
        entry=dict(id=slot,type=type_name,enabled=enabled,parameters=values,bypass=policy)
        asset=raw.get('asset');digest=raw.get('asset_sha256')
        if type_name!='ir' and ('asset' in raw or 'asset_sha256' in raw):raise ValueError('asset belongs only to IR')
        if type_name=='ir':
            if asset is None:
                if enabled or digest is not None:raise ValueError('enabled IR requires an explicit asset')
            else:
                if type(asset) is not str or not asset or '\\' in asset or ':' in asset or Path(asset).is_absolute() or '..' in Path(asset).parts:raise ValueError('IR asset must use a portable relative path within preset folder')
                root=Path(folder).resolve();path=(root/asset).resolve()
                if not path.is_relative_to(root):raise ValueError('IR asset escapes preset folder')
                if not path.is_file() or path.stat().st_size>1048576:raise ValueError('IR asset missing/too large')
                with path.open('rb') as file:payload=file.read(1048577)
                if len(payload)>1048576:raise ValueError('IR asset too large')
                actual=hashlib.sha256(payload).hexdigest()
                if digest is not None and (type(digest) is not str or digest!=actual):raise ValueError('IR asset hash mismatch')
                try:assets[slot]=tuple(coefficients(io.BytesIO(payload)))
                except (wave.Error,EOFError,struct.error) as e:raise ValueError('invalid IR asset') from e
                entry.update(asset=asset,asset_sha256=actual)
        slots[slot]=entry;counts[type_name]=counts.get(type_name,0)+1
    check_capacity([TYPES[s['type']] for s in slots])
    tail=number(data.get('tail_seconds',0.),0.,30.)
    return Preset(dict(format='frost-preset',version=data['version'],name=name,sample_rate=44100,slots=slots,order=order[:],pre_count=pre,tail_seconds=tail),assets)

def unique_pairs(pairs):
    value={}
    for key,item in pairs:
        if key in value:raise ValueError('duplicate preset field')
        value[key]=item
    return value

def load(path):
    path=Path(path)
    with path.open('rb') as file:payload=file.read(32769)
    if len(payload)>32768:raise ValueError('preset file too large')
    try:data=json.loads(payload.decode('utf-8'),object_pairs_hook=unique_pairs)
    except (json.JSONDecodeError,UnicodeError) as e:raise ValueError('invalid preset JSON') from e
    return validate(data,path.parent)

def save(path,preset):
    path=Path(path)
    if path.suffix.lower()!='.json':raise ValueError('preset destination requires .json')
    # Revalidate public data and destination-relative assets before replacing.
    checked=validate(preset.data,path.parent);temporary=None
    if any(path.resolve()==(path.parent/s['asset']).resolve() for s in checked.data['slots'] if 'asset' in s):raise ValueError('preset must not overwrite IR asset')
    try:
        with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=path.parent,delete=False) as f:
            temporary=Path(f.name);json.dump(checked.data,f,indent=2,allow_nan=False);f.write('\n');f.flush();os.fsync(f.fileno())
        os.replace(temporary,path)
    finally:
        if temporary is not None and temporary.exists():temporary.unlink()

def render(preset_path,source,destination,ceiling_dbfs=None):
    levels=Headroom(ceiling_dbfs)
    preset_path=Path(preset_path);source=Path(source);destination=Path(destination);p=load(preset_path)
    protected={preset_path.resolve(),source.resolve()}
    protected.update((preset_path.parent/s['asset']).resolve() for s in p.data['slots'] if 'asset' in s)
    if destination.resolve() in protected:raise ValueError('render must not overwrite input/preset/assets')
    temporary=None;frames=clips=0;peak=0.;source_frames=0
    with wave.open(str(source),'rb') as wav:
        if (wav.getnchannels(),wav.getsampwidth(),wav.getframerate(),wav.getcomptype())!=(1,2,44100,'NONE'):raise ValueError('input requires mono PCM16/44100 Hz')
        c=Chain();ordered=[p.data['slots'][i] for i in p.data['order']]
        # No amp backend: flatten stable slot order for this cold offline route.
        # Preserve pre_count in the file; no device/live/persistence claim.
        c.configure([TYPES[s['type']] for s in ordered]);tail=round(p.data['tail_seconds']*44100)
        for position,s in enumerate(ordered):
            c.bypass_policy(position,int(s['bypass']=='cut'))
            if s['type']=='empty':continue
            for parameter,value in enumerate(s['parameters'].values()):
                if not c.lib.chain_set(position,parameter,value):raise ValueError('DSP rejected preset parameter')
            if s['id'] in p.assets:
                c.load_ir(p.assets[s['id']],position)
                if s['enabled']:tail+=len(p.assets[s['id']])-1
            if not c.lib.chain_enable(position,int(s['enabled'])):raise ValueError('DSP rejected preset enable')
        try:
            with tempfile.NamedTemporaryFile(dir=destination.parent,delete=False) as f:temporary=Path(f.name)
            with wave.open(str(temporary),'wb') as out:
                out.setparams((1,2,44100,0,'NONE','not compressed'))
                def emit(x):
                    nonlocal frames,clips,peak
                    result=c.process(x)
                    if c.lib.chain_faults():raise ValueError('render encountered DSP containment fault; output not committed')
                    levels.observe(result)
                    frames+=len(result);clips+=sum(abs(v)>=1. for v in result);peak=max(peak,max(map(abs,result),default=0.))
                    out.writeframesraw(struct.pack('<'+'h'*len(result),*[max(-32768,min(32767,round(v*32768))) for v in result]))
                while data:=wav.readframes(64):
                    if len(data)%2:raise ValueError('truncated PCM16 sample; output not committed')
                    source_frames+=len(data)//2
                    emit([v/32768 for v in struct.unpack('<'+'h'*(len(data)//2),data)])
                if source_frames!=wav.getnframes():raise ValueError('truncated WAV input; output not committed')
                while tail:
                    n=min(64,tail);emit([0.]*n);tail-=n
            levels.finish()
            os.replace(temporary,destination)
        finally:
            if temporary is not None and temporary.exists():temporary.unlink()
    return dict(frames=frames,float_peak=peak,unity_exceedance_frames=clips,containment_faults=0,headroom=levels.report(),
        sha256=hashlib.sha256(destination.read_bytes()).hexdigest())

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('preset',type=Path);parser.add_argument('input',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--ceiling-dbfs',type=float,help='reject final float peaks above -60..0 dBFS or PCM16 clipping; no automatic attenuation')
    args=parser.parse_args()
    try:print(json.dumps(render(args.preset,args.input,args.output,args.ceiling_dbfs),indent=2))
    except HeadroomError as error:
        print(json.dumps(error.report,indent=2),file=sys.stderr);parser.exit(2,str(error)+'\n')
    except (ValueError,OSError) as error:parser.exit(2,str(error)+'\n')
