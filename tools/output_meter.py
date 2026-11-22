"""Read-only output-boundary telemetry codec. Caller supplies transport; no IO."""
import math
import struct

COMMAND=0x7f35

def request(token=1):
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid output meter token')
    return struct.pack('<HHII',COMMAND,12,1,token)

def decode(data,token):
    if len(data)!=48:raise ValueError('invalid output meter reply length')
    cmd,n,version,actual,status,frames,peak_bits,outside,rails,invalid,faults,mute,source=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual,status)!=(COMMAND,48,1,token,0):raise ValueError('output meter reply rejected or mismatched')
    peak=struct.unpack('<f',struct.pack('<I',peak_bits))[0]
    if not math.isfinite(peak) or not 0<=peak<=16 or mute not in (0,1) or source not in (1,2):
        raise ValueError('invalid output meter observation')
    if max(outside,rails,invalid)>frames or outside>rails:raise ValueError('invalid output meter counters')
    return dict(frames=frames,peak=peak,outside_unity_frames=outside,pcm_rail_frames=rails,
                invalid_boundary_frames=invalid,chain_faults=faults,hard_muted=bool(mute),source='synthetic' if source==1 else 'platform-mono')

def read(exchange,token=1):
    """exchange(request12)->reply48, serialized outside audio. No reset or writes.
    Saturated counters are lower bounds; all values are cumulative since init.
    A repeated read can observe newer audio, even with the same token.
    """
    return decode(exchange(request(token)),token)
