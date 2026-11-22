"""Stage 2 storage on the pedal (docs/Build%20Firmware.md), Python twin of web/store.js.

One-sector GROUP files in controller flash area A, rewritten copy-on-write
like the firmware (firmware/controller/preset_store.h): new copy into a free
sector, data, read back, header last, then EVERY older copy erased.
  AMP models: firmware FFCM, amp-0..amp-6, 10 x 400-byte records (MODEL 0..63)
  presets:    firmware FPST, pst-0..pst-3, 15 x 264-byte records (1..60)
Legacy single amp-NN files (Frost 31s/31t3) are migrated into the groups.
Every write is a device write: run it only with the user's approval.
"""
import base64,datetime,json,struct,zlib
import fcm_files as F
GROUPS={'amp':dict(firm=0x4d434646,prefix='amp-',files=7,slots=10,size=400),
        'pst':dict(firm=0x54535046,prefix='pst-',files=4,slots=15,size=264)}
PRESETS=60
def _ok(b,size):return len(b)==size and zlib.crc32(b[:size-4])&0xffffffff==struct.unpack_from('<I',b,size-4)[0]
def is_preset(r):return r[:3]==b'FP\x01' and _ok(r,264)
def preset_name(r):return r[4:20].split(b'\0')[0].decode('ascii','replace')
def rename_preset(r,name):
    if not is_preset(r):raise ValueError('not a Frost preset (v1)')
    n=name.encode('ascii','replace')[:16];r=bytearray(r);r[4:20]=n+b'\0'*(16-len(n))
    struct.pack_into('<I',r,260,zlib.crc32(bytes(r[:260]))&0xffffffff);return bytes(r)
class Store:
    def __init__(self,pedal):self.p=pedal          # an fcm_files.Pedal
    def newest(self,files,firm,name):
        best=None
        for h in files:
            if h['firm']==firm and h['name']==name and (best is None or h['seq']>best['seq']):best=h
        return best
    def read_group(self,kind,g,files=None):
        G=GROUPS[kind];h=self.newest(files if files is not None else self.p.files(),G['firm'],G['prefix']+str(g));n=G['slots']*G['size']
        if not h or h['length']!=n:return None
        d=self.p.read(F.BASE+h['sector']*F.SECTOR+F.HEADER,n)
        return d if zlib.crc32(d)&0xffffffff==h['crc'] else None
    def write_group(self,kind,g,data,say=lambda *_:None):
        G=GROUPS[kind];name=G['prefix']+str(g);files=self.p.files();others=[h for h in files if h['firm']==G['firm'] and h['name']==name]
        if data and any(data):
            covered={s for h in files for s in range(h['sector'],h['sector']+h['sectors'])}
            free=next((s for s in range(F.SECTORS) if s not in covered),None)
            if free is None:raise RuntimeError('storage full: all 33 places are used')
            seq=max([h['seq'] for h in files]+[0])+1;addr=F.BASE+free*F.SECTOR;say('writing '+name)
            self.p.erase(addr);self.p.program(addr+F.HEADER,bytes(data))
            if self.p.read(addr+F.HEADER,len(data))!=bytes(data):raise RuntimeError('read-back failed; the old copy is untouched')
            self.p.program(addr,F.header(seq,G['firm'],name,bytes(data))[:60])
            if F.parse_header(self.p.read(addr,64),free) is None:raise RuntimeError('header read-back failed')
        for h in others:self.p.erase(F.BASE+h['sector']*F.SECTOR)
        self.p.changed()
    def slot(self,kind,i,files=None):
        G=GROUPS[kind];d=self.read_group(kind,i//G['slots'],files)
        if d is None:return None
        r=d[(i%G['slots'])*G['size']:(i%G['slots']+1)*G['size']];return r if _ok(r,G['size']) else None
    def put_slot(self,kind,i,rec,say=lambda *_:None):
        G=GROUPS[kind];g=i//G['slots'];o=(i%G['slots'])*G['size']
        d=bytearray(self.read_group(kind,g) or bytes(G['slots']*G['size']))
        d[o:o+G['size']]=rec if rec else bytes(G['size']);self.write_group(kind,g,bytes(d),say)
    def migrate(self,say=lambda *_:None):
        files=self.p.files();legacy=[h for h in files if h['firm']==F.FIRM and len(h['name'])==6 and h['name'][:4]=='amp-' and h['name'][4:].isdigit()]
        if not legacy:return 0
        best={}
        for h in legacy:
            if h['name'] not in best or h['seq']>best[h['name']]['seq']:best[h['name']]=h
        groups={}
        for h in best.values():
            i=int(h['name'][4:]);g=i//10;rec=self.p.read(F.BASE+h['sector']*F.SECTOR+F.HEADER,400)
            if h['length']!=400 or not _ok(rec,400):continue
            if g not in groups:groups[g]=bytearray(self.read_group('amp',g,files) or bytes(4000))
            if not _ok(bytes(groups[g][(i%10)*400:(i%10+1)*400]),400):groups[g][(i%10)*400:(i%10+1)*400]=rec
        for g,d in groups.items():say('moving AMP models into amp-%d'%g);self.write_group('amp',g,bytes(d))
        for h in legacy:self.p.erase(F.BASE+h['sector']*F.SECTOR)
        self.p.changed();return len(best)
    def amps(self):
        files=self.p.files();out={}
        for g in range(GROUPS['amp']['files']):
            d=self.read_group('amp',g,files)
            if d is None:continue
            for s in range(10):
                r=d[s*400:s*400+400]
                if _ok(r,400):out[g*10+s]=F.record_label(r)
        return out
    def amp_put(self,i,rec,say=lambda *_:None):
        F.model_name(i)
        if len(rec)!=400 or not _ok(rec,400):raise ValueError('not an AMP model record')
        self.migrate(say);self.put_slot('amp',i,rec,say)
    def amp_remove(self,i):self.migrate();self.put_slot('amp',i,None)
    def presets(self):
        files=self.p.files();out={}
        for g in range(GROUPS['pst']['files']):
            d=self.read_group('pst',g,files)
            if d is None:continue
            for s in range(15):
                r=d[s*264:s*264+264]
                if is_preset(r):out[g*15+s]=preset_name(r)
        return out
    def preset_rename(self,i,name):
        r=self.slot('pst',i)
        if r is None:raise ValueError('empty preset')
        self.put_slot('pst',i,rename_preset(r,name))
    def preset_delete(self,i):self.put_slot('pst',i,None)
    def backup(self):
        self.migrate();files=self.p.files();out=[]
        for kind,G in GROUPS.items():
            for g in range(G['files']):
                d=self.read_group(kind,g,files)
                if d is not None:out.append(dict(kind=kind,g=g,data=base64.b64encode(d).decode()))
        return json.dumps(dict(format='frost-backup',version=1,created=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=out),indent=1)
    def restore(self,text,say=lambda *_:None):
        j=json.loads(text)
        if j.get('format')!='frost-backup' or j.get('version')!=1:raise ValueError('not a Frost backup file')
        want={}
        for f in j['files']:
            G=GROUPS[f['kind']];d=base64.b64decode(f['data'])
            if not 0<=f['g']<G['files'] or len(d)!=G['slots']*G['size']:raise ValueError('bad backup entry')
            want[(f['kind'],f['g'])]=d
        self.migrate()
        for kind,G in GROUPS.items():
            for g in range(G['files']):say('restoring %s%d'%(G['prefix'],g));self.write_group(kind,g,want.get((kind,g)))
