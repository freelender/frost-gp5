"""Read-only 0x7f3c main-loop servicing attribution codec. Caller supplies transport; no IO."""
import struct
COMMAND=0x7f3c
NAMES={0xfff1:'tx-service',0xfff2:'platform-report',0xfff3:'rx-drain',0xfff4:'slot-reply-job',0:'none'}

def name(identifier):
    return NAMES.get(identifier,'command 0x%04x'%identifier)

def request(index,token=1):
    if type(index) is not int or not 0<=index<=11:raise ValueError('invalid service index')
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid service token')
    return struct.pack('<HHIII',COMMAND,16,1,token,index)

def decode(data,index,token=1):
    if len(data)!=48:raise ValueError('invalid service length')
    cmd,n,version,actual,status,slot,ordinal,gap,pending,ids,cycles,iteration,worst=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual)!=(COMMAND,48,1,token):raise ValueError('mismatched service reply')
    if status==18:
        if any((slot,ordinal,gap,pending,ids,cycles,iteration,worst)):raise ValueError('invalid unavailable reply')
        return dict(index=index,available=False)
    if status or slot!=index or not ordinal or pending>15 or cycles>iteration and iteration or cycles>worst:
        raise ValueError('invalid service observation')
    return dict(index=slot,available=True,block=ordinal,gap_cycles=gap,dma_pending=pending,
                service_id=ids&0xffff,service=name(ids&0xffff),service_cycles=cycles,
                iteration_cycles=iteration,worst_id=ids>>16,worst=name(ids>>16),worst_cycles=worst)

def read(exchange,index,token=1):return decode(exchange(request(index,token)),index,token)
