"""Read-only 0x7f3b line-output status codec. Caller supplies transport; no IO."""
import struct

COMMAND=0x7f3b
CAP_Q16=65536
LEGACY_CAP_Q16=16384
FLAGS={1:'numeric',2:'rail',4:'level',8:'route',16:'late',32:'invalid'}

def request(token=1):
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid line status token')
    return struct.pack('<HHII',COMMAND,12,1,token)

def decode(data,token):
    if len(data)!=48:raise ValueError('invalid line status reply length')
    cmd,n,version,actual,status,mode,cap,gain,flags,faults,requested,source,closed=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual,status)!=(COMMAND,48,1,token,0):raise ValueError('line status reply rejected or mismatched')
    if mode not in (0,1) or source not in (1,2) or closed not in (0,1) or flags&~63:
        raise ValueError('invalid line status observation')
    if mode==0 and (cap or gain or flags or requested or not closed):raise ValueError('muted profile reported line state')
    if mode==1 and (cap not in (LEGACY_CAP_Q16,CAP_Q16) or gain>cap or closed!=(1 if flags or not gain else 0)):
        raise ValueError('line status violates cap/closed contract')
    return dict(mode='line-test' if mode else 'muted',cap_q16=cap,gain_q16=gain,flags=flags,
                flag_names=[v for k,v in FLAGS.items() if flags&k],chain_faults=faults,
                last_requested_q16=requested,source='synthetic' if source==1 else 'platform-mono',closed=bool(closed))

def read(exchange,token=1):
    """exchange(request12)->reply48. No arm, clear, reset or write exists."""
    return decode(exchange(request(token)),token)
