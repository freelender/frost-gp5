"""Read-only stack watermark telemetry codec; no device IO or memory writes."""
import struct
COMMAND=0x7f38
BOTTOM,TOP=0x20005c00,0x20006c00

def request(token=1):
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid stack token')
    return struct.pack('<HHII',COMMAND,12,1,token)

def decode(data,token):
    if len(data)!=48:raise ValueError('invalid stack reply length')
    cmd,n,version,actual,status,used,free,sp,bottom,top,flags,mute,source=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual,status)!=(COMMAND,48,1,token,0):raise ValueError('stack reply rejected or mismatched')
    if (bottom,top)!=(BOTTOM,TOP) or used+free!=4096 or used%4 or free%4 or used>4096 or flags&~3 or mute not in (0,1) or source not in (1,2):raise ValueError('invalid stack observation')
    if bool(flags&1)!=(free>=16):raise ValueError('invalid bottom guard flag')
    if bool(flags&2)!=(BOTTOM<=sp<=TOP and not sp&7):raise ValueError('invalid MSP range flag')
    return dict(touched_bytes=used,untouched_prefix_bytes=free,current_msp=sp,bottom=bottom,top=top,
                bottom_guard_intact=bool(flags&1),msp_in_range=bool(flags&2),hard_muted=bool(mute),
                source='synthetic' if source==1 else 'platform-mono',scope='empirical watermark; not formal maximum stack proof')

def read(exchange,token=1):return decode(exchange(request(token)),token)
