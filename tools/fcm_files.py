"""AMP (FCM) model files on the pedal's controller flash, EXPERIMENTAL.

Models are OpenGP5 0.5 files (GP5F header, docs/Build%20Firmware.md) of firmware id
'FFCM', named 'amp-NN' (NN = the AMP block's MODEL value 00..63), each holding
one 400-byte FCM record (tools/fcm_bank.py pack_record). Frost limits files to
run A (0x13F000..0x15FFFF, 33 sectors); one model uses one sector.

Converted models are APPROXIMATIONS of their source NAM files.

USB commands (Frost generic controller, same wire as OpenGP5 0.5):
7E 05 read <=64 B, 7E 07 erase a sector, 7E 08 program <=56 B, 7E 0D who,
7E 0E files changed. Writing never touches the app, the DSP or Valeton data.

  python fcm_files.py list
  python fcm_files.py add 3 build/nam-distill/<capture>/IIR313-sos.npz --name "MY CRUNCH"
  python fcm_files.py add 3 model.fcm          (a raw 400-byte record)
  python fcm_files.py remove 3
  python fcm_files.py presets                  (storage 2: list presets)
  python fcm_files.py rename 5 "Clean Lead"    (preset number 1..60)
  python fcm_files.py backup all.frostbackup / restore all.frostbackup

Writing files is a device write: run it only with the user's approval.
"""
import argparse,struct,sys,zlib
from pathlib import Path
MAGIC=0x46355047;FIRM=0x4d434646;HEADER=64;SECTOR=0x1000
BASE=0x13f000;SECTORS=33;END=BASE+SECTORS*SECTOR
RECORD=400;CHUNK=20;COMMAND=0x7f48
STATUS,BEGIN,WRITE,COMMIT,CLEAR=range(5)

def model_name(index):
    if not 0<=index<64:raise ValueError('MODEL index must be 0..63')
    return 'amp-%02d'%index

# ---- GP5F files -------------------------------------------------------------
def header(seq,firm,name,data):
    raw=name.encode('ascii');assert len(raw)<32
    sectors=(HEADER+len(data)+SECTOR-1)//SECTOR
    h=struct.pack('<5IHH32s',MAGIC,seq,firm,len(data),zlib.crc32(data)&0xffffffff,sectors,0,raw)
    return h+struct.pack('<I',zlib.crc32(h)&0xffffffff)+b'\xff'*4
def parse_header(h,sector):
    """A valid header at `sector` (64 bytes) as a dict, else None (as the pedal reads it)."""
    if len(h)<HEADER:return None
    magic,seq,firm,length,crc,sectors,flags,name=struct.unpack_from('<5IHH32s',h)
    if magic!=MAGIC or zlib.crc32(h[:56])&0xffffffff!=struct.unpack_from('<I',h,56)[0]:return None
    if not sectors or sector+sectors>SECTORS or HEADER+length>sectors*SECTOR:return None
    return dict(sector=sector,seq=seq,firm=firm,length=length,crc=crc,sectors=sectors,name=name.split(b'\0')[0].decode('ascii','replace'))
def newest(files):
    """{(firm,name): header} keeping the highest sequence, as the pedal does."""
    out={}
    for f in files:
        k=(f['firm'],f['name'])
        if k not in out or f['seq']>out[k]['seq']:out[k]=f
    return out

# ---- DSP transfer frames (firmware/protocol/fcm_wire.h) ----------------------
def request(token,op,index=0,crc=0,offset=0,words=()):
    words=list(words);assert len(words)<=CHUNK
    return struct.pack('<HH7I',COMMAND,32+4*len(words),1,token,op,index,crc,offset,len(words))+struct.pack('<%dI'%len(words),*words)
def decode(reply):
    if len(reply)!=48:raise ValueError('invalid AMP reply length')
    cmd,n,version,token,status,held_index,held_crc,state,index,crc,nxt,epoch,_=struct.unpack('<HH11I',reply)
    if (cmd,n,version)!=(COMMAND,48,1):raise ValueError('invalid AMP reply header')
    return dict(token=token,status=status,held_index=held_index,held_crc=held_crc,state=state,index=index,crc=crc,next=nxt,epoch=epoch)

# ---- records ---------------------------------------------------------------
def record_from(path,name=None):
    data=Path(path).read_bytes()
    if Path(path).suffix=='.npz':
        sys.path.insert(0,str(Path(__file__).resolve().parent));import fcm_bank
        return fcm_bank.pack_record(name or Path(path).parent.name[:15],fcm_bank.model_from_npz(path))
    if len(data)!=RECORD:raise ValueError('a record file must be exactly %d bytes'%RECORD)
    if zlib.crc32(data[:-4])&0xffffffff!=struct.unpack_from('<I',data,RECORD-4)[0]:raise ValueError('record CRC mismatch')
    if name:raise ValueError('--name applies to .npz models only (a record carries its own name)')
    return data
def record_label(data):return data[:16].split(b'\0')[0].decode('ascii','replace')

# ---- device ----------------------------------------------------------------
class Pedal:
    """7E 05/07/08/0D/0E through an `ask(payload)->reply payload` callable."""
    def __init__(self,ask,tries=3):
        # Every command here is safe to repeat (reads; erasing a sector; programming the
        # same bytes again clears no further bits), so a lost reply is simply asked again.
        def again(payload):
            for _ in range(tries):
                r=ask(payload)
                if r is not None:return r
            return None
        self.ask=again
    def who(self):
        r=self.ask([0x7e,0x0d])
        if not r or len(r)<7 or r[2]!=1:raise RuntimeError('the pedal does not answer 7E 0D (not a Frost build with AMP files)')
        return tuple(r[3:6]),bytes(r[6:]).split(b'\0')[0].decode('ascii','replace')
    def storage(self):
        """Frost storage version: 2 = packed group files + presets (stage 2), 1 = single amp-NN files."""
        r=self.ask([0x7e,0x0d])
        if not r or len(r)<7:return 1
        e=r.index(0,6) if 0 in r[6:] else len(r)
        return r[e+1] if e+1<len(r) else 1
    def read(self,addr,n):
        out=bytearray()
        while len(out)<n:
            k=min(64,n-len(out));a=addr+len(out)
            r=self.ask([0x7e,0x05,*struct.pack('<I',a),k])
            if not r or len(r)!=3+k or r[2]!=1:raise RuntimeError('flash read failed at 0x%x'%a)
            out+=bytes(r[3:])
        return bytes(out)
    def erase(self,addr):
        r=self.ask([0x7e,0x07,*struct.pack('<I',addr)])
        if not r or len(r)<3 or r[2]!=1:raise RuntimeError('erase failed at 0x%x'%addr)
    def program(self,addr,data):
        assert len(data)%4==0
        for o in range(0,len(data),56):
            piece=data[o:o+56]
            if piece==b'\xff'*len(piece):continue
            r=self.ask([0x7e,0x08,*struct.pack('<I',addr+o),*piece])
            if not r or len(r)<3 or r[2]!=1:raise RuntimeError('program failed at 0x%x'%(addr+o))
    def changed(self):self.ask([0x7e,0x0e,*struct.pack('<I',FIRM)])
    def files(self):
        out=[]
        for s in range(SECTORS):
            h=parse_header(self.read(BASE+s*SECTOR,HEADER),s)
            if h:out.append(h)
        return out
    def models(self):
        return {f['name']:f for (firm,_),f in newest(self.files()).items() if firm==FIRM}
    def add(self,index,record,say=print):
        name=model_name(index);files=self.files();used=set()
        for f in files:used.update(range(f['sector'],f['sector']+f['sectors']))
        free=[s for s in range(SECTORS) if s not in used]
        if not free:raise RuntimeError('no free sector (33 in all): remove a model first')
        old=[f for f in files if f['firm']==FIRM and f['name']==name]
        seq=max([f['seq'] for f in files]+[0])+1;s=free[0];addr=BASE+s*SECTOR
        say('writing %s (%s) into sector %d'%(name,record_label(record),s))
        self.erase(addr);self.program(addr+HEADER,record)
        if self.read(addr+HEADER,RECORD)!=record:raise RuntimeError('read-back of the data failed; the old copy is untouched')
        h=header(seq,FIRM,name,record);self.program(addr,h[:60])            # the header last
        if parse_header(self.read(addr,HEADER),s) is None:raise RuntimeError('read-back of the header failed')
        for f in old:self.erase(BASE+f['sector']*SECTOR)                     # then forget older copies
        self.changed();return s
    def remove(self,index):
        name=model_name(index);hit=[f for f in self.files() if f['firm']==FIRM and f['name']==name]
        for f in hit:self.erase(BASE+f['sector']*SECTOR)
        if hit:self.changed()
        return len(hit)

def open_pedal(stage=None):
    root=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(Path(stage)/'tools' if stage else root/'build/gp5-line31s-fcmfiles-adc/tools'))
    import gp5_link
    link=gp5_link.open_link()
    return Pedal(lambda payload:gp5_link.ask(link,list(payload),timeout=3.0))

def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--stage',help='staged build whose tools/ hold gp5_link.py');sub=p.add_subparsers(dest='cmd',required=True)
    sub.add_parser('list')
    a=sub.add_parser('add');a.add_argument('index',type=int);a.add_argument('model');a.add_argument('--name')
    r=sub.add_parser('remove');r.add_argument('index',type=int)
    sub.add_parser('presets');n=sub.add_parser('rename');n.add_argument('number',type=int);n.add_argument('name')
    b=sub.add_parser('backup');b.add_argument('file');b=sub.add_parser('restore');b.add_argument('file')
    args=p.parse_args()
    if args.cmd=='add':record=record_from(args.model,args.name);model_name(args.index)
    pedal=open_pedal(args.stage);version,who=pedal.who()
    if who!='frost':raise SystemExit('the pedal runs %r, not Frost'%who)
    if pedal.storage()>=2:
        import frost_store
        st=frost_store.Store(pedal);moved=st.migrate(print)
        if moved:print('moved',moved,'AMP model(s) into the packed storage')
        if args.cmd=='list':
            m=st.amps()
            for i in sorted(m):print('MODEL %02d  %s'%(i,m[i]))
            print('%d model(s); %d of %d sectors used'%(len(m),sum(f['sectors'] for f in pedal.files()),SECTORS))
        elif args.cmd=='add':st.amp_put(args.index,record,print);print('done')
        elif args.cmd=='remove':st.amp_remove(args.index);print('removed')
        elif args.cmd=='presets':
            ps=st.presets()
            for i in sorted(ps):print('%02d  %s'%(i+1,ps[i]))
            print('%d of %d presets used'%(len(ps),frost_store.PRESETS))
        elif args.cmd=='rename':st.preset_rename(args.number-1,args.name);print('renamed')
        elif args.cmd=='backup':Path(args.file).write_text(st.backup());print('wrote',args.file)
        elif args.cmd=='restore':st.restore(Path(args.file).read_text(),print);print('restored')
        return
    if args.cmd in('presets','rename','backup','restore'):raise SystemExit('this firmware has no presets yet (needs Frost 31u or later)')
    if args.cmd=='list':
        m=pedal.models()
        for name in sorted(m):
            f=m[name];print('%s  sector %2d  %s'%(name,f['sector'],record_label(pedal.read(BASE+f['sector']*SECTOR+HEADER,16))))
        print('%d model(s); %d of %d sectors used'%(len(m),sum(f['sectors'] for f in pedal.files()),SECTORS))
    elif args.cmd=='add':print('done, sector',pedal.add(args.index,record))
    else:print('removed',pedal.remove(args.index),'copies')

if __name__=='__main__':main()
