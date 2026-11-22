"""Callback-only reader for bounded first/worst/late/last block timing records."""
import struct
COMMAND=0x7f39

def request(index,token=1):
    if type(index) is not int or not 0<=index<=10:raise ValueError('invalid timing index')
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid timing token')
    return struct.pack('<HHIII',COMMAND,16,1,token,index)

def decode(data,index,token=1):
    if len(data)!=48:raise ValueError('invalid timing length')
    cmd,n,version,actual,status,slot,ordinal,total,inp,process,out,gap,flags=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual)!=(COMMAND,48,1,token):raise ValueError('mismatched timing reply')
    if status==18:
        if any((slot,ordinal,total,inp,process,out,gap,flags)):raise ValueError('invalid unavailable reply')
        return dict(index=index,available=False)
    if status or slot!=index:raise ValueError('timing request rejected or index mismatch')
    if not ordinal or ((inp+process+out)&0xffffffff)!=total or flags&~0x7f or (flags>>5) not in (1,2):
        raise ValueError('invalid timing observation')
    return dict(index=slot,available=True,block=ordinal,total_cycles=total,input_cycles=inp,
                process_cycles=process,output_cycles=out,gap_cycles=gap,dma_pending=flags&15,
                hard_muted=bool(flags&16),source='synthetic' if flags>>5==1 else 'platform-mono')

def read(exchange,index,token=1):return decode(exchange(request(index,token)),index,token)
