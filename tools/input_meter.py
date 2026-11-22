"""Read-only platform-mono input telemetry codec; caller supplies transport."""
import struct
COMMAND=0x7f36

def request(token=1):
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid input meter token')
    return struct.pack('<HHII',COMMAND,12,1,token)

def decode(data,token):
    if len(data)!=48:raise ValueError('invalid input meter length')
    cmd,n,version,actual,status,frames,lo,hi,peak,rails,nonzero,mute,source=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual,status)!=(COMMAND,48,1,token,0):raise ValueError('input meter rejected or mismatched')
    lo=lo if lo<0x80000000 else lo-0x100000000;hi=hi if hi<0x80000000 else hi-0x100000000
    if lo>hi or peak!=max(abs(lo),abs(hi)) or not rails<=nonzero<=frames or mute not in (0,1) or source not in (1,2):
        raise ValueError('invalid input meter observation')
    if (not frames and (lo or hi or peak)) or (not nonzero and peak):raise ValueError('invalid empty input observation')
    return dict(frames=frames,minimum=lo,maximum=hi,absolute_peak=peak,raw_rail_frames=rails,
                nonzero_frames=nonzero,hard_muted=bool(mute),chain_source='synthetic' if source==1 else 'platform-mono')

def read(exchange,token=1):return decode(exchange(request(token)),token)
