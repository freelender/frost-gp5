"""Pure callback codec for nonblocking DSP transmit queue telemetry."""
import struct
COMMAND=0x7f3a
def request(token=1):
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid TX token')
    return struct.pack('<HHII',COMMAND,12,1,token)
def decode(data,token):
    if len(data)!=48:raise ValueError('invalid TX length')
    cmd,n,version,actual,status,queued,frames,dropped,written,blocked,high,deferred,source=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual,status)!=(COMMAND,48,1,token,0):raise ValueError('TX reply rejected or mismatched')
    if not queued<=high<=1024 or source not in (1,2):raise ValueError('invalid TX observation')
    return dict(queued_bytes=queued,enqueued_frames=frames,dropped_frames=dropped,bytes_written=written,
                blocked_passes=blocked,high_water=high,deferred_dispatches=deferred,
                source='synthetic' if source==1 else 'platform-mono')
def read(exchange,token=1):return decode(exchange(request(token)),token)
