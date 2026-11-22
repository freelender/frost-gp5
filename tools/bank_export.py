"""Pure controller-bank read codec/collector; no built-in device IO.

Export is the effective saved bank (RAM overlays or ROM), not unsaved live DSP
edits. All entries/names/body words are fenced by one controller epoch and CRC.
"""
import binascii
from dataclasses import dataclass
import struct
from preset_bank import Capture,save_bank
from preset_wire import WORDS,MAX_WORDS,checksum

STATUS,INFO,READ_WORDS,READ_NAME=range(4)
UNUSED=0xffffffff

def request(token,op,epoch=0,entry=UNUSED,index=0):
    return struct.pack('<HH7I',0x7f46,32,1,token,op,epoch,entry,index,0)

def decode(data):
    if len(data)!=60:raise ValueError('invalid bank reply length')
    cmd,n,version,token,status,epoch,count,mask,entry,index,length,*values=struct.unpack('<HH14I',data)
    if (cmd,n,version)!=(0x7f46,60,1):raise ValueError('invalid bank reply header')
    if status==0:
        if not token or not epoch or not 1<=count<=16 or mask>>count:raise ValueError('invalid bank observation')
    elif length or any(values):raise ValueError('invalid bank error payload')
    return dict(token=token,status=status,epoch=epoch,count=count,mask=mask,entry=entry,index=index,length=length,data=tuple(values))

def whole_crc(captures,mask):
    crc=binascii.crc32(struct.pack('<3I',1,len(captures),mask))
    for c in captures:
        name=c.name.encode('utf-8');crc=binascii.crc32(struct.pack('<I',len(name))+name,crc)
        crc=binascii.crc32(struct.pack('<'+'I'*len(c.words),*c.words),crc)
    return crc&0xffffffff

@dataclass(frozen=True)
class BankExport:
    epoch:int
    ram_mask:int
    crc:int
    captures:tuple

def download_bank(exchange,first_token=1):
    """Caller supplies serialized exchange(inner32)->reply60. Failures discard
    partial state; identical individual reads may be retried. No implicit save.
    Epoch is boot-local; discard on reconnect, CRC is not authentication.
    """
    # STATUS twice +16*(INFO +up to4 NAME chunks +215 WORD chunks).
    if type(first_token) is not int or not 1<=first_token<=UNUSED-3521:raise ValueError('invalid/exhausted bank token range')
    token=first_token;identity=None
    def send(op,epoch=0,entry=UNUSED,index=0):
        nonlocal token
        r=decode(exchange(request(token,op,epoch,entry,index)))
        if r['token']!=token or r['status']!=0:raise ValueError('bank read rejected or token mismatch')
        token+=1
        if r['entry']!=entry or r['index']!=index:raise ValueError('bank reply identity mismatch')
        if identity is not None and (r['epoch'],r['count'],r['mask'])!=identity:raise ValueError('bank changed during export')
        return r
    def status(epoch=0):
        r=send(STATUS,epoch)
        if r['length'] or r['data'][1] not in (WORDS,MAX_WORDS) or r['data'][2:]!=(64,0,0):raise ValueError('invalid bank descriptor')
        return r
    first=status();identity=(first['epoch'],first['count'],first['mask']);epoch,count,mask=identity;captures=[]
    longest=first['data'][1]
    for entry in range(count):
        info=send(INFO,epoch,entry);crc,name_length,is_ram,word_count,zero=info['data']
        if info['length'] or not 1<=name_length<=64 or is_ram!=((mask>>entry)&1) or word_count not in (WORDS,MAX_WORDS) or word_count>longest or zero:
            raise ValueError('invalid bank entry descriptor')
        name=bytearray()
        for offset in range(0,name_length,20):
            r=send(READ_NAME,epoch,entry,offset);n=min(20,name_length-offset);data=struct.pack('<5I',*r['data'])
            if r['length']!=n or any(data[n:]):raise ValueError('invalid bank name chunk')
            name.extend(data[:n])
        try:name=name.decode('utf-8')
        except UnicodeError:raise ValueError('invalid bank UTF-8 name') from None
        words=[]
        for index in range(0,word_count,5):
            r=send(READ_WORDS,epoch,entry,index);n=min(5,word_count-index)
            if r['length']!=n or any(r['data'][n:]):raise ValueError('invalid bank word chunk')
            words.extend(r['data'][:n])
        if checksum(words)!=crc:raise ValueError('bank entry checksum mismatch')
        # Controller bank epoch is NOT a DSP revision; provenance remains0.
        captures.append(Capture(name,tuple(words),source_revision=0))
    last=status(epoch);captures=tuple(captures)
    if last['data'][0]!=first['data'][0] or whole_crc(captures,mask)!=first['data'][0]:raise ValueError('whole bank checksum mismatch')
    if len({c.name for c in captures})!=count:raise ValueError('duplicate bank names')
    return BankExport(epoch,mask,first['data'][0],captures)

def export_bank(exchange,destination,first_token=1):
    result=download_bank(exchange,first_token)
    save_bank(destination,result.captures)
    return result

def bridge_request(inner):
    if len(inner)!=32:raise ValueError('invalid bank request length')
    cmd,n,version,token,op,epoch,entry,index,zero=struct.unpack('<HH7I',inner)
    if (cmd,n,version)!=(0x7f46,32,1) or not token or op>READ_NAME or zero:raise ValueError('only bank reads are permitted')
    return b'\x7e\x46'+inner

def bridge_reply(payload):
    if len(payload)!=62 or payload[:2]!=b'\x7e\x46':raise ValueError('invalid controller-bank bridge reply')
    inner=payload[2:];decode(inner);return inner
