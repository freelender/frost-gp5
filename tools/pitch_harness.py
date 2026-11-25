"""Offline OCTAVE / DROP / SHIMMER comparison harness (host only, never flashes).

Builds tools/pitch_harness.c against two source trees -- the working tree
("work") and a Git revision exported to build/ ("base", default HEAD) -- so the
real firmware kernels of both algorithms run on identical, deterministic test
signals. For each case it reports, per tree:

  lsd      log-spectral distance to the ideal pitch-shifted reference (dB, lower is better)
  harm     energy on the expected partials vs everything else (dB, higher is better)
  warble   envelope modulation relative to the ideal (dB std, lower is better)
  lag      steady time offset to the ideal (ms; algorithmic latency)
  onset    onset delay of a note from silence (ms)
  cents    pitch error of single notes (cents)

WAVs (input, ideal, base, work) and a JSON report go to --out, which must be
outside the repository. These are host renders: listening and the slot-load
meter on the pedal decide; ARM instruction counts come from
tools/pitch_arm_bench.py.

  python tools/pitch_harness.py --out ../../_frost_archive/pitch-YYYY-MM-DD [--base HEAD] [--input di.wav ...]
"""
import argparse,ctypes as ct,json,math,os,shutil,struct,subprocess,sys,tarfile,io,wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
FS=44100
DROP,OCTAVE,SHIMMER,SHIFTER=0,1,2,3

def export_tree(rev,dest):
    """Export the paths the kernels need at a Git revision into dest."""
    if dest.exists():shutil.rmtree(dest)
    dest.mkdir(parents=True)
    data=subprocess.run(['git','-C',str(ROOT),'archive','--format=tar',rev,'effects','engine','firmware'],check=True,capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(data)) as t:t.extractall(dest,**({'filter':'data'} if hasattr(tarfile,'data_filter') else {}))
    # The harness itself always comes from the working tree (same metrics for both).
    (dest/'tools').mkdir(exist_ok=True);shutil.copy(ROOT/'tools/pitch_harness.c',dest/'tools/pitch_harness.c')
    return dest

class Lib:
    def __init__(s,root,name):
        ext='.dll' if os.name=='nt' else '.so'
        flags=os.environ.get('PITCH_FLAGS','').split() if name!='base' else []
        s.c=ct.CDLL(str(build(ROOT/('build/pitch/'+name+ext),Path(root)/'tools/pitch_harness.c',includes=(Path(root),),flags=flags)))
        for f in ('ph_lsd','ph_partial_ratio','ph_envelope_mod','ph_pitch_cents','ph_peak','ph_partial_am'):getattr(s.c,f).restype=ct.c_double
        s.c.ph_add_note.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_double,ct.c_double,ct.c_double,ct.c_double,ct.c_uint,ct.c_double]
        s.c.ph_set_vibrato.argtypes=[ct.c_double,ct.c_double]
        s.c.ph_add_pluck.argtypes=[ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_double,ct.c_double,ct.c_uint]
        s.c.ph_lsd.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_uint]
        s.c.ph_partial_ratio.argtypes=[ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_void_p,ct.c_uint,ct.c_double]
        s.c.ph_envelope_mod.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_uint]
        s.c.ph_envelope_lag.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_uint,ct.c_uint]
        s.c.ph_onset.argtypes=[ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_double]
        s.c.ph_pitch_cents.argtypes=[ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_double]
        s.c.ph_peak.argtypes=[ct.c_void_p,ct.c_uint]
        s.c.ph_partial_am.argtypes=[ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_double]
    def process(s,kernel,x,p):
        n=len(x);y=(ct.c_float*n)();pp=(ct.c_float*4)(*(list(p)+[0.]*4)[:4])
        if not s.c.ph_process(kernel,x,y,n,pp):raise ValueError('kernel %d missing'%kernel)
        return y

def buf(n):return (ct.c_float*n)()
def inharm(f0,ratio):
    """Partial frequencies of ph_add_note (same inharmonicity), shifted by ratio."""
    H=min(24,max(1,int(7000./f0)));return [f0*h*math.sqrt(1+.00012*h*h)*ratio for h in range(1,H+1) if f0*h*math.sqrt(1+.00012*h*h)*ratio<9000]

E2,A2,B2,D3,E3,G2,G3,B3,D4,E4,C4,G4,GS3=82.41,110.,123.47,146.83,164.81,98.,196.,246.94,293.66,329.63,261.63,392.,207.65
DROP_CASES=[  # name, notes (f0, start s), decay s, seconds
    ('note_E2',[(E2,.05)],1.6,2.0),('note_G3',[(G3,.05)],1.0,1.5),('note_E4',[(E4,.05)],.8,1.2),
    ('power_E2',[(E2,.05),(B2,.05),(E3,.05)],1.6,2.0),
    ('open_G',[(G2,.05),(B2,.058),(D3,.066),(G3,.074),(B3,.082),(392.,.09)],1.4,2.0),
    ('triad_C4',[(C4,.05),(E4,.05),(G4,.05)],1.0,1.5),
    ('steady_E',[(E2,.05),(B2,.05),(E3,.05),(GS3,.05)],0.,1.5),
    # real strings are not exactly equal-tempered: no common period exists
    ('detuned_G',[(G2*2**(4/1200),.05),(B2*2**(-7/1200),.058),(D3*2**(3/1200),.066),(G3*2**(-5/1200),.074),(B3*2**(6/1200),.082)],1.4,2.0),
    ('vibrato_A3',[(220.,.05)],1.2,1.5,'vib'),('vibrato_E2',[(E2,.05)],1.6,2.0,'vib')]
VIB=(25.,5.5)   # cents, Hz: a wide finger vibrato

def synth(lib,notes,decay,seconds,ratios,amp=.25,click=.15,vib=False):
    n=int(seconds*FS);x=buf(n);refs={r:buf(n) for r in ratios}
    lib.c.ph_set_vibrato(*(VIB if vib else (0.,5.5)))
    for k,(f,t) in enumerate(notes):
        for j,r in enumerate(ratios):
            lib.c.ph_add_note(x if j==0 else None,refs[r],n,int(t*FS),f,amp/len(notes)**.5,decay,r,k+1,click if j==0 else 0.)
    lib.c.ph_set_vibrato(0.,5.5)
    return x,refs

def metrics(lib,y,ref,n,notes,ratio,from_s=.25,vib=False):
    c=lib.c;fr=int(from_s*FS)
    lag=c.ph_envelope_lag(y,ref,n,4000)
    freqs=sorted({round(v,3) for f,_ in notes for v in inharm(f,ratio)});fa=(ct.c_double*len(freqs))(*freqs)
    out=dict(lsd=round(c.ph_lsd(y,ref,n,lag,fr,n),2),harm=round(c.ph_partial_ratio(y,n,fr+lag,n,fa,len(freqs),25.),2),
             warble=round(c.ph_envelope_mod(y,ref,n,lag,fr,n),3),lag_ms=round(lag/FS*1000,2),peak=round(c.ph_peak(y,n),3))
    # per-note AM in excess of the ideal's (beating with other partials cancels out)
    to=n if from_s<.3 else n
    ams=[c.ph_partial_am(y,n,fr+lag,n,f*ratio)-c.ph_partial_am(ref,n,fr,n-lag,f*ratio) for f,_ in notes if f*ratio>40]
    out['am']=round(sum(ams)/len(ams),3) if ams else None
    st=int(notes[0][1]*FS);on_r=c.ph_onset(ref,n,st,8000,.5);on_y=c.ph_onset(y,n,st,8000,.5)
    out['onset_ms']=round((on_y-on_r)/FS*1000,2)
    if len(notes)==1 and not vib:
        to=min(n,fr+lag+int(.75*FS));out['cents']=round(c.ph_pitch_cents(y,fr+lag,to,notes[0][0]*ratio*math.sqrt(1.00012)),2)
    return out

def write_wav(path,y,scale=1.):
    path.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(path),'wb') as w:
        w.setparams((1,2,FS,0,'NONE','not compressed'))
        w.writeframes(struct.pack('<%dh'%len(y),*[max(-32768,min(32767,round(v*scale*32767))) for v in y]))
def read_wav(path):
    with wave.open(str(path),'rb') as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,FS):raise ValueError('needs mono 16-bit 44.1 kHz: '+str(path))
        d=w.readframes(w.getnframes())
    v=struct.unpack('<%dh'%(len(d)//2),d);x=buf(len(v))
    for i,s in enumerate(v):x[i]=s/32768.
    return x

def phrase(lib,seconds=9.):
    """Listening phrase: plucked single notes, a riff and strummed chords."""
    n=int(seconds*FS);x=buf(n);t=.1;seed=1
    for f in (E2,G2,A2,B2,D3,E3):lib.c.ph_add_pluck(x,n,int(t*FS),f,.35,seed);t+=.28;seed+=1
    for chord in ((E2,B2,E3,GS3,B3,E4),(A2,E3,A2*2,C4*1.0595,E4),(G2,B2,D3,G3,B3,392.)):
        for k,f in enumerate(chord):lib.c.ph_add_pluck(x,n,int((t+.012*k)*FS),f,.2,seed);seed+=1
        t+=1.4
    for f in (E4,D4,B3,G3,E3):lib.c.ph_add_pluck(x,n,int(t*FS),f,.3,seed);t+=.18;seed+=1
    return x

def run_drop(libs,out,wavs):
    rows=[]
    for k,semis in ((0,1),(1,2),(3,4),(5,6)):
        r=2**(-semis/12)
        for name,notes,decay,sec,*opt in DROP_CASES:
            x,refs=synth(libs['work'],notes,decay,sec,[r],vib=bool(opt));n=len(x);row=dict(effect='DROP',setting='-%d'%semis,case=name)
            for tag,lib in libs.items():
                y=lib.process(DROP,x,(k,100.));row[tag]=metrics(lib,y,refs[r],n,notes,r,vib=bool(opt))
                if wavs and semis in (2,6):write_wav(out/'drop'/('%s_-%d_%s.wav'%(name,semis,tag)),y)
            if wavs and semis in (2,6):write_wav(out/'drop'/('%s_input.wav'%name),x);write_wav(out/'drop'/('%s_-%d_ideal.wav'%(name,semis)),refs[r])
            rows.append(row)
    return rows

OCT_CASES=DROP_CASES
def run_octave(libs,out,wavs):
    rows=[]
    for setting,p,ratio in (('sub',(0.,100.,0.),.5),('up',(0.,0.,100.),2.)):
        for name,notes,decay,sec,*opt in OCT_CASES:
            x,refs=synth(libs['work'],notes,decay,sec,[ratio],vib=bool(opt));n=len(x);row=dict(effect='OCTAVE',setting=setting,case=name)
            for tag,lib in libs.items():
                y=lib.process(OCTAVE,x,p);row[tag]=metrics(lib,y,refs[ratio],n,notes,ratio,vib=bool(opt))
                if wavs:write_wav(out/'octave'/('%s_%s_%s.wav'%(name,setting,tag)),y)
            if wavs:write_wav(out/'octave'/('%s_input.wav'%name),x);write_wav(out/'octave'/('%s_%s_ideal.wav'%(name,setting)),refs[ratio])
            rows.append(row)
    return rows

def run_shifter(libs,out,wavs):
    """SHIMMER's octave shifter alone (open loop), scored like OCTAVE up."""
    rows=[]
    for name,notes,decay,sec,*opt in DROP_CASES:
        x,refs=synth(libs['work'],notes,decay,sec,[2.],vib=bool(opt));n=len(x);row=dict(effect='SHIFTER',setting='x2',case=name)
        for tag,lib in libs.items():
            y=lib.process(SHIFTER,x,());row[tag]=metrics(lib,y,refs[2.],n,notes,2.,vib=bool(opt))
            if wavs:write_wav(out/'shimmer'/('shifter_%s_%s.wav'%(name,tag)),y)
        rows.append(row)
    return rows

def run_shimmer(libs,out,wavs):
    """Chord for 1.5 s, then the tail. harm: tail energy on the chord's partials
    and their octaves vs between them; oct: share of tail energy on the octave-up
    partials (the shimmer amount; keep it similar)."""
    rows=[]
    for name,notes in (('power_E2',[(E2,.05),(B2,.05),(E3,.05)]),('open_G',[(G2,.05),(B2,.058),(D3,.066),(G3,.074),(B3,.082),(392.,.09)]),('note_G3',[(G3,.05)])):
        sec=7.;n=int(sec*FS);x=buf(n)
        for k,(f,t) in enumerate(notes):libs['work'].c.ph_add_note(x,None,int(1.5*FS),int(t*FS),f,.25/len(notes)**.5,1.2,1.,k+1,.1)
        allp=sorted({round(v*m,3) for f,_ in notes for v in inharm(f,1.) for m in (1,2,4) if v*m<9000})
        octp=sorted({round(v*m,3) for f,_ in notes for v in inharm(f,1.) for m in (2,4) if v*m<9000}-{round(v,3) for f,_ in notes for v in inharm(f,1.)})
        row=dict(effect='SHIMMER',setting='TIME30',case=name)
        for tag,lib in libs.items():
            for time in (30.,80.):
                y=lib.process(SHIMMER,x,(100.,50.,time,4.));c=lib.c
                a=(ct.c_double*len(allp))(*allp);o=(ct.c_double*len(octp))(*octp)
                fr,to=int(2.*FS),int(6.5*FS)
                m=dict(harm=round(c.ph_partial_ratio(y,n,fr,to,a,len(allp),20.),2),oct=round(c.ph_partial_ratio(y,n,fr,to,o,len(octp),20.),2),peak=round(c.ph_peak(y,n),3))
                # grain roughness: AM of the octave-up fundamentals in the tail
                ams=[c.ph_partial_am(y,n,fr,to,2*f) for f,_ in notes];m['am']=round(sum(ams)/len(ams),3)
                row.setdefault(tag,{})['T%d'%time]=m
                if wavs:write_wav(out/'shimmer'/('%s_T%d_%s.wav'%(name,time,tag)),y)
        if wavs:write_wav(out/'shimmer'/('%s_input.wav'%name),x)
        rows.append(row)
    return rows

def listening(libs,out,inputs):
    srcs=[('phrase',phrase(libs['work']))]+[(Path(p).stem,read_wav(p)) for p in inputs]
    for name,x in srcs:
        write_wav(out/'listen'/(name+'_input.wav'),x)
        for tag,lib in libs.items():
            for label,k,p in (('drop-2',DROP,(1,100.)),('drop-5',DROP,(4,100.)),('oct_default',OCTAVE,(70.,40.,40.)),('oct_sub',OCTAVE,(0.,100.,0.)),('oct_up',OCTAVE,(0.,0.,100.)),('shimmer',SHIMMER,(60.,50.,60.,4.))):
                write_wav(out/'listen'/('%s_%s_%s.wav'%(name,label,tag)),lib.process(k,x,p))

def summarize(rows):
    keys=('lsd','harm','am','lag_ms','onset_ms','cents')
    lines=[]
    for r in rows:
        if r['effect']=='SHIMMER':
            for t in ('T30','T80'):
                lines.append('%-8s %-6s %-9s %s'%(r['effect'],t,r['case'],'  '.join('%s harm %6.2f oct %6.2f am %5.2f'%(tag,r[tag][t]['harm'],r[tag][t]['oct'],r[tag][t]['am']) for tag in ('base','work') if tag in r)))
            continue
        cells=[]
        for k in keys:
            b,w=r.get('base',{}).get(k),r.get('work',{}).get(k)
            if b is None and w is None:continue
            cells.append('%s %s->%s'%(k,b,w))
        lines.append('%-7s %-4s %-9s %s'%(r['effect'],r['setting'],r['case'],'  '.join(cells)))
    groups={}
    for r in rows:
        if r['effect']=='SHIMMER':continue
        for tag in ('base','work'):
            for k in ('lsd','harm','am','onset_ms'):
                v=r.get(tag,{}).get(k)
                if v is not None:groups.setdefault((r['effect'],r['setting'],tag,k),[]).append(v)
    mean=lambda v:sum(v)/len(v)
    for eff,st in sorted({(e,s) for e,s,_,_ in groups}):
        lines.append('MEAN %-7s %-4s %s'%(eff,st,'  '.join('%s %.2f->%.2f'%(k,mean(groups[(eff,st,'base',k)]),mean(groups[(eff,st,'work',k)]))
                                                     for k in ('lsd','harm','am','onset_ms') if (eff,st,'base',k) in groups)))
    return '\n'.join(lines)

def main():
    a=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('--out',required=True,help='output folder outside the repository')
    a.add_argument('--base',default='HEAD',help='Git revision of the previous algorithms')
    a.add_argument('--input',nargs='*',default=[],help='extra mono 16-bit 44.1 kHz DI WAVs for listening renders')
    a.add_argument('--only',choices=('drop','octave','shimmer'),nargs='*')
    a.add_argument('--no-wav',action='store_true')
    o=a.parse_args()
    out=Path(o.out).resolve()
    if ROOT in out.parents or out==ROOT:raise SystemExit('--out must be outside the repository')
    base=export_tree(o.base,ROOT/'build/pitch/base-src')
    libs={'base':Lib(base,'base'),'work':Lib(ROOT,'work')}
    for k,name in enumerate(('DROP','OCTAVE','SHIMMER')):
        print('%-8s state %s -> %s bytes, workspace %s -> %s bytes'%(name,libs['base'].c.ph_state_bytes(k),libs['work'].c.ph_state_bytes(k),libs['base'].c.ph_workspace_bytes(k),libs['work'].c.ph_workspace_bytes(k)))
    rows=[];only=o.only or ('drop','octave','shimmer')
    if 'drop' in only:rows+=run_drop(libs,out,not o.no_wav)
    if 'octave' in only:rows+=run_octave(libs,out,not o.no_wav)
    if 'shimmer' in only:rows+=run_shifter(libs,out,not o.no_wav)+run_shimmer(libs,out,not o.no_wav)
    if not o.no_wav:listening(libs,out,o.input)
    out.mkdir(parents=True,exist_ok=True)
    (out/'pitch_report.json').write_text(json.dumps(dict(base=o.base,rows=rows),indent=1))
    text=summarize(rows);(out/'pitch_report.txt').write_text(text+'\n');print(text)

if __name__=='__main__':main()
