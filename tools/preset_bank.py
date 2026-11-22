"""Lossless host capture files and bounded ROM-bank preparation. No device IO.

Generated captures/banks/headers may contain private IR data: keep them under
build/ or private/. This tool neither changes firmware nor programs flash.
"""
import argparse
import ctypes as ct
from dataclasses import dataclass
import hashlib
import hmac
import os
from pathlib import Path
import struct
import tempfile

from preset_download import download
from preset_wire import WORDS,MAX_WORDS,checksum,request,BEGIN,WRITE,COMMIT,snapshot_words

CAP_MAGIC=b'FRSTCAP\0'
BANK_MAGIC=b'FRSTBNK\0'
VERSION=1
MAX_PRESETS=16
CAP_PREFIX=struct.Struct('<8s4I64s')
BANK_PREFIX=struct.Struct('<8s2I')
BODY=struct.Struct('<'+'I'*WORDS)
# Capture files record their body length: 1071 (version-1) or 1101 (version-2).
def cap_bytes(words):return CAP_PREFIX.size+32+4*words
CAP_BYTES=cap_bytes(WORDS)
MAX_CAP_BYTES=cap_bytes(MAX_WORDS)
MAX_BANK_BYTES=BANK_PREFIX.size+32+MAX_PRESETS*MAX_CAP_BYTES

def _uint(value,lo=0,hi=0xffffffff):
    return type(value) is int and lo<=value<=hi

def _float(word):return struct.unpack('<f',struct.pack('<I',word))[0]

def validate_words(words):
    """Validate the canonical body against shared native metadata/resources.

    Padding is bitwise positive zero; active float words, including signed zero,
    are never normalized. IR absolute-sum accumulation rounds as native binary32.
    This admits resources/settings, not physical CPU or audio headroom.
    """
    if not isinstance(words,(tuple,list)) or len(words) not in (WORDS,MAX_WORDS) or any(not _uint(v) for v in words):
        raise ValueError('capture needs1071 (v1) or1101 (v2) unsigned32-bit words')
    if words[0] not in (1,2) or len(words)!=(MAX_WORDS if words[0]==2 else WORDS) or words[1]!=44100 or words[2]>6:
        raise ValueError('unsupported capture version/rate/boundary')
    from preset_io import catalog,TYPES,check_capacity
    from chain_host import Chain
    meta={TYPES[name]:tuple(parameters.values()) for name,parameters in catalog().items()}
    flags=Chain.library().chain_parameter_flags
    types=words[3:9];order=words[9:15];enabled=words[15:21];policy=words[21:27]
    if sorted(order)!=list(range(6)):raise ValueError('invalid capture route order')
    counts={}
    for slot,t in enumerate(types):
        if t not in meta or enabled[slot]>1 or policy[slot]>1 or (t==255 and enabled[slot]) or (words[0]==1 and t!=255 and t>=9):
            raise ValueError('invalid capture type/enable/bypass')
        counts[t]=counts.get(t,0)+1
        for p in range(8 if words[0]==2 else 3):
            word=words[27+3*slot+p] if p<3 else words[WORDS+5*slot+p-3];value=_float(word)
            if p>=len(meta[t]):
                if word:raise ValueError('unused capture parameters must be zero')
            elif word&0x7f800000==0x7f800000 or not meta[t][p][0]<=value<=meta[t][p][1] or (flags(t,p)&1 and value!=int(value)):
                raise ValueError('invalid capture parameter')
    try:check_capacity(types)
    except ValueError:raise ValueError('capture exceeds static workspace capacity') from None
    for bank in range(2):
        taps=words[45+bank];coeffs=words[47+512*bank:559+512*bank]
        if bank>=counts.get(3,0):
            if taps or any(coeffs):raise ValueError('unused IR bank must be zero')
            continue
        if not 1<=taps<=512:raise ValueError('invalid capture IR length')
        total=0.
        for i,word in enumerate(coeffs):
            if i>=taps:
                if word:raise ValueError('unused IR coefficients must be zero')
            else:
                value=_float(word)
                if word&0x7f800000==0x7f800000:raise ValueError('nonfinite capture IR')
                total=ct.c_float(total+abs(value)).value
                if total>8.:raise ValueError('capture IR absolute sum exceeds8')
    return tuple(words)

@dataclass(frozen=True)
class Capture:
    name:str
    words:tuple
    source_revision:int=0

    def __post_init__(self):
        if type(self.name) is not str or not self.name or not self.name.isprintable():raise ValueError('invalid capture name')
        try:name=self.name.encode('utf-8')
        except UnicodeError:raise ValueError('invalid UTF-8 capture name') from None
        if len(name)>64 or not _uint(self.source_revision):raise ValueError('invalid capture name/revision')
        object.__setattr__(self,'words',validate_words(self.words))

def capture_current(exchange,name,first_token=1):
    revision,words=download(exchange,first_token)
    return Capture(name,words,revision)

def encode_capture(capture):
    # Revalidate before emitting even if a caller bypassed frozen construction.
    c=Capture(capture.name,capture.words,capture.source_revision);name=c.name.encode('utf-8')
    prefix=CAP_PREFIX.pack(CAP_MAGIC,VERSION,c.source_revision,len(name),len(c.words),name.ljust(64,b'\0'))
    body=struct.pack('<'+'I'*len(c.words),*c.words)
    return prefix+hashlib.sha256(prefix+body).digest()+body

def decode_capture(payload):
    if len(payload) not in (CAP_BYTES,MAX_CAP_BYTES):raise ValueError('invalid capture file size')
    prefix=payload[:CAP_PREFIX.size];magic,version,revision,n,words,name=CAP_PREFIX.unpack(prefix)
    if magic!=CAP_MAGIC or version!=VERSION or words not in (WORDS,MAX_WORDS) or len(payload)!=cap_bytes(words) or not 1<=n<=64 or any(name[n:]):
        raise ValueError('invalid capture file header')
    body=payload[CAP_PREFIX.size+32:]
    if not hmac.compare_digest(payload[CAP_PREFIX.size:CAP_PREFIX.size+32],hashlib.sha256(prefix+body).digest()):
        raise ValueError('capture integrity check failed')
    try:name=name[:n].decode('utf-8')
    except UnicodeError:raise ValueError('invalid UTF-8 capture name') from None
    return Capture(name,struct.unpack('<'+'I'*words,body),revision)

def _bank(captures):
    if not isinstance(captures,(tuple,list)) or not 1<=len(captures)<=MAX_PRESETS:raise ValueError('bank needs1..16 captures')
    checked=tuple(Capture(c.name,c.words,c.source_revision) for c in captures)
    if len({c.name for c in checked})!=len(checked):raise ValueError('bank preset names must be unique')
    return checked

def encode_bank(captures):
    captures=_bank(captures);prefix=BANK_PREFIX.pack(BANK_MAGIC,VERSION,len(captures))
    body=b''.join(encode_capture(c) for c in captures)
    return prefix+hashlib.sha256(prefix+body).digest()+body

def decode_bank(payload):
    if not BANK_PREFIX.size+32<=len(payload)<=MAX_BANK_BYTES:raise ValueError('invalid bank file size')
    prefix=payload[:BANK_PREFIX.size];magic,version,n=BANK_PREFIX.unpack(prefix)
    if magic!=BANK_MAGIC or version!=VERSION or not 1<=n<=MAX_PRESETS:
        raise ValueError('invalid bank file header/count')
    body=payload[BANK_PREFIX.size+32:]
    if not hmac.compare_digest(payload[BANK_PREFIX.size:BANK_PREFIX.size+32],hashlib.sha256(prefix+body).digest()):
        raise ValueError('bank integrity check failed')
    captures=[];offset=0
    for _ in range(n):
        if offset+CAP_PREFIX.size>len(body):raise ValueError('invalid bank file header/count')
        size=cap_bytes(CAP_PREFIX.unpack(body[offset:offset+CAP_PREFIX.size])[4])
        if size not in (CAP_BYTES,MAX_CAP_BYTES):raise ValueError('invalid bank file header/count')
        captures.append(decode_capture(body[offset:offset+size]));offset+=size
    if offset!=len(body):raise ValueError('invalid bank file header/count')
    return _bank(captures)

def _read(path,limit):
    with Path(path).open('rb') as file:payload=file.read(limit+1)
    if len(payload)>limit:raise ValueError('preset file too large')
    return payload

def load_capture(path):return decode_capture(_read(path,MAX_CAP_BYTES))
def load_bank(path):return decode_bank(_read(path,MAX_BANK_BYTES))

def _write(path,payload,suffix,protected=()):
    path=Path(path)
    if path.suffix.lower()!=suffix:raise ValueError('unexpected output file extension')
    if path.resolve() in {Path(p).resolve() for p in protected}:raise ValueError('output must not overwrite input')
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(mode='wb',dir=path.parent,delete=False) as file:
            temporary=Path(file.name);file.write(payload);file.flush();os.fsync(file.fileno())
        os.replace(temporary,path)
    finally:
        if temporary is not None and temporary.exists():temporary.unlink()

def save_capture(path,capture):_write(path,encode_capture(capture),'.fcap')
def save_bank(path,captures,protected=()):_write(path,encode_bank(captures),'.fbank',protected)

def import_preset(source,destination):
    from preset_io import load
    p=load(source);c=Capture(p.data['name'],snapshot_words(p))
    protected=[source]+[Path(source).parent/s['asset'] for s in p.data['slots'] if 'asset' in s]
    _write(destination,encode_capture(c),'.fcap',protected)
    return c

def build_bank(sources,destination):
    if not isinstance(sources,(tuple,list)) or not 1<=len(sources)<=MAX_PRESETS:raise ValueError('bank needs1..16 input files')
    captures=tuple(load_capture(p) for p in sources)
    save_bank(destination,captures,protected=sources)
    return captures

def recall_packets(capture,session,revision,first_token=1):
    c=Capture(capture.name,capture.words,capture.source_revision)
    if not _uint(session,1) or not _uint(revision,1,0xfffffffe) or not _uint(first_token,1,0xffffffff-216):
        raise ValueError('invalid/exhausted recall session/revision/token range')
    # Source revision is provenance only; always require a fresh live revision.
    crc=checksum(c.words);packets=[request(first_token,BEGIN,session,revision,data=[crc])]
    chunks=range(0,len(c.words),5)
    for chunk,index in enumerate(chunks,1):
        packets.append(request(first_token+chunk,WRITE,session,revision,index,c.words[index:index+5]))
    packets.append(request(first_token+len(chunks)+1,COMMIT,session,revision,len(c.words),[crc]))
    return tuple(packets)

def header(captures):
    captures=_bank(captures)
    lines=['/* Generated private preset data; no executable DSP. Do not publish captures. */',
        '#ifndef FROST_USER_BANK_H','#define FROST_USER_BANK_H','#include <stdint.h>',
        'typedef struct {const char *name;const uint32_t *words;uint32_t crc;} FrostUserPreset;',
        '#define FROST_USER_PRESET_COUNT '+str(len(captures))+'u']
    for i,c in enumerate(captures):
        lines.append('static const uint32_t frost_user_words_'+str(i)+'['+str(len(c.words))+']={')
        for start in range(0,len(c.words),8):lines.append('    '+','.join('0x%08xu'%v for v in c.words[start:start+8])+',')
        lines.append('};')
    lines.append('static const FrostUserPreset frost_user_presets[FROST_USER_PRESET_COUNT]={')
    for i,c in enumerate(captures):
        # Fixed three-digit octal escapes cannot inject C or consume following hex.
        name=''.join('\\%03o'%v for v in c.name.encode('utf-8'))
        lines.append('    {"'+name+'",frost_user_words_'+str(i)+',0x%08xu},'%checksum(c.words))
    lines.extend(['};','#endif',''])
    return '\n'.join(lines).encode('ascii')

def export_header(source,destination):
    _write(destination,header(load_bank(source)),'.h',protected=[source])

def main():
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='op',required=True)
    p=commands.add_parser('import-json',help='convert existing validated JSON/PCM16 preset to a self-contained capture')
    p.add_argument('source',type=Path);p.add_argument('destination',type=Path)
    p=commands.add_parser('build',help='build a bank in the supplied capture order')
    p.add_argument('destination',type=Path);p.add_argument('sources',nargs='+',type=Path)
    p=commands.add_parser('header',help='emit a validated bank as immutable C data; does not install it')
    p.add_argument('source',type=Path);p.add_argument('destination',type=Path)
    p=commands.add_parser('inspect',help='validate a capture or bank and list names/body CRCs')
    p.add_argument('source',type=Path)
    a=parser.parse_args()
    try:
        if a.op=='import-json':import_preset(a.source,a.destination)
        elif a.op=='build':build_bank(a.sources,a.destination)
        elif a.op=='header':export_header(a.source,a.destination)
        else:
            captures=(load_capture(a.source),) if a.source.suffix.lower()=='.fcap' else load_bank(a.source)
            for i,c in enumerate(captures):print(f'{i+1}: {c.name} | CRC32 {checksum(c.words):08x} | source revision {c.source_revision}')
    except (ValueError,OSError) as error:parser.exit(2,str(error)+'\n')

if __name__=='__main__':main()
