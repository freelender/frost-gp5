"""Read-only preset capture codec/collector. No built-in device or filesystem IO.

The caller supplies exchange(request)->reply, serializes link ownership and
handles link timeouts. Returned words preserve binary32 IRs exactly; converting
these to PCM16 assets would lose precision. No implicit retry/restart or save.
"""
import struct
from preset_wire import WORDS,MAX_WORDS,checksum

DESCRIBE,READ=range(2)

def request(token,op,revision=0,index=0):
    return struct.pack('<HH5I',0x7f44,24,1,token,op,revision,index)

def decode(data):
    if len(data)!=48:raise ValueError('invalid preset download reply length')
    cmd,n,version,token,status,revision,index,count,*values=struct.unpack('<HH11I',data)
    if (cmd,n,version)!=(0x7f44,48,1):raise ValueError('invalid preset download reply header')
    if status==0:
        if not revision or count>5 or index>=MAX_WORDS or index+count>MAX_WORDS:raise ValueError('invalid preset download observation')
    elif index or count or any(values):raise ValueError('invalid preset download error payload')
    return dict(token=token,status=status,revision=revision,index=index,count=count,data=tuple(values))

def download(exchange,first_token=1):
    """Return (revision, immutable canonical words) after a fenced CRC check.

    Reject a lost/malformed/error reply or any observed revision change. Reads
    are idempotent; callers may retry an exchange with identical bytes. Discard
    partial captures on failure/reboot; no persistent boot epoch is available.
    """
    if type(first_token) is not int or not 1<=first_token<=0xffffffff-216:raise ValueError('invalid/exhausted download token range')
    token=first_token
    def send(op,revision=0,index=0):
        nonlocal token
        r=decode(exchange(request(token,op,revision,index)))
        if r['token']!=token or r['status']!=0:raise ValueError('preset download rejected or token mismatch')
        token+=1
        if revision and r['revision']!=revision:raise ValueError('preset changed during download')
        return r
    def describe(revision=0):
        r=send(DESCRIBE,revision)
        if r['index'] or r['count'] or r['data'][1] not in (WORDS,MAX_WORDS) or r['data'][2:]!=(0,0,0):raise ValueError('invalid preset download descriptor')
        return r
    first=describe();revision=first['revision'];crc=first['data'][0];total=first['data'][1];words=[]
    for index in range(0,total,5):
        r=send(READ,revision,index);count=min(5,total-index)
        if r['index']!=index or r['count']!=count or any(r['data'][count:]):raise ValueError('invalid preset download chunk')
        words.extend(r['data'][:count])
    last=describe(revision)
    if last['data'][1]!=total or (words[0]==2)!=(total==MAX_WORDS):raise ValueError('preset download length/version mismatch')
    if last['data'][0]!=crc or checksum(words)!=crc:raise ValueError('preset download checksum mismatch')
    return revision,tuple(words)
