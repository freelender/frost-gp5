"""Pure little-endian codec/packet plan for preset transfer. No device access.

Plans can contain private IR coefficients; keep generated plans under build/ or
private/. One outstanding mutation, exact-byte retries, shared mutation owner.
"""
import binascii
import struct
WORDS=1071 # version-1 body (original nine types, three parameters)
MAX_WORDS=1101 # version-2 body: version-1 words + parameters 3..7 per slot
def words_for(version):return MAX_WORDS if version==2 else WORDS
STATUS,BEGIN,WRITE,COMMIT,ABORT=range(5)

def bits(value):return struct.unpack('<I',struct.pack('<f',value))[0]

def snapshot_words(preset):
    from preset_io import TYPES
    d=preset.data;slots=d['slots']
    version=2 if any(TYPES[s['type']] not in (255,) and TYPES[s['type']]>=9 for s in slots) else 1
    values=[version,44100,d['pre_count']]
    values += [TYPES[s['type']] for s in slots]+d['order']
    values += [int(s['enabled']) for s in slots]+[int(s['bypass']=='cut') for s in slots]
    extension=[]
    for s in slots:
        p=list(s['parameters'].values());values += [bits(v) for v in p[:3]]+[0]*(3-len(p[:3]))
        extension += [bits(v) for v in p[3:]]+[0]*(5-len(p[3:]))
    irs=[]
    for s in slots:
        if s['type']=='ir':irs.append(list(preset.assets.get(s['id'],(1.,))))
    irs += [[] for _ in range(2-len(irs))]
    values += [len(ir) for ir in irs]
    for ir in irs:values += [bits(v) for v in ir]+[0]*(512-len(ir))
    if version==2:values += extension
    elif any(extension):raise ValueError('version-1 preset cannot carry extended parameters')
    if len(values)!=words_for(version):raise ValueError('preset word count differs from protocol')
    return values

def checksum(words):return binascii.crc32(struct.pack('<'+'I'*len(words),*words))&0xffffffff

def request(token,op,session=0,revision=0,index=0,data=()):
    if len(data)>5:raise ValueError('preset chunk exceeds five words')
    return struct.pack('<HH11I',0x7f43,48,1,token,op,session,revision,index,*data,*([0]*(5-len(data))))

def decode(data):
    if len(data)!=48:raise ValueError('invalid preset reply length')
    cmd,n,version,token,status,revision,session,state,next_word,token_floor,phase,committed,crc=struct.unpack('<HH11I',data)
    if (cmd,n,version)!=(0x7f43,48,1):raise ValueError('invalid preset reply header')
    if state>5 or next_word>MAX_WORDS or phase>2:raise ValueError('invalid preset reply observation')
    if status in (0,1,15):
        if not revision:raise ValueError('invalid successful preset reply')
        if state==0 and (session or next_word):raise ValueError('invalid empty transfer observation')
        if state!=0 and (not session or not token_floor):raise ValueError('missing transfer identity')
        if state==3 and (phase or not committed or committed!=revision):raise ValueError('invalid completion observation')
    return dict(token=token,status=status,revision=revision,session=session,state=state,next_word=next_word,
        token_floor=token_floor,phase=phase,committed_revision=committed,prefix_crc=crc)

def packet_plan(preset,session,revision,first_token=1):
    if not 1<=session<=0xffffffff or not 1<=revision<0xffffffff or not 1<=first_token<=0xffffffff-216:raise ValueError('invalid/exhausted session/revision/token range')
    words=snapshot_words(preset);crc=checksum(words);token=first_token
    yield request(token,BEGIN,session,revision,data=[crc]);token+=1
    for index in range(0,len(words),5):
        yield request(token,WRITE,session,revision,index,words[index:index+5]);token+=1
    yield request(token,COMMIT,session,revision,len(words),[crc])
