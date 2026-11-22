"""Read-only bounded DSP receiver telemetry codec. No built-in device IO."""
import struct
COMMAND=0x7f37

def request(token=1):
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid receiver token')
    return struct.pack('<HHII',COMMAND,12,1,token)

def decode(data,token):
    if len(data)!=48:raise ValueError('invalid receiver reply length')
    cmd,n,version,actual,status,buffered,accepted,crc_bad,discarded,expired,overflows,high,source=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual,status)!=(COMMAND,48,1,token,0):raise ValueError('receiver reply rejected or mismatched')
    if not buffered<=high<=512 or source not in (1,2):raise ValueError('invalid receiver observation')
    return dict(buffered=buffered,accepted=accepted,crc_failures=crc_bad,discarded_bytes=discarded,
                expired_buffers=expired,overflows=overflows,high_water=high,
                source='synthetic' if source==1 else 'platform-mono')

def read(exchange,token=1):return decode(exchange(request(token)),token)
