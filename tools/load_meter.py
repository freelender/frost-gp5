"""Read-only 0x7f3d per-window load codec. Caller supplies transport; no IO."""
import struct
COMMAND=0x7f3d
WINDOWS=16
BLOCKS=256

def request(index,token=1):
    if type(index) is not int or not 0<=index<WINDOWS:raise ValueError('invalid load window index')
    if type(token) is not int or not 1<=token<=0xffffffff:raise ValueError('invalid load token')
    return struct.pack('<HHIII',COMMAND,16,1,token,index)

def decode(data,index,token=1):
    if len(data)!=48:raise ValueError('invalid load reply length')
    cmd,n,version,actual,status,seq,blocks,mean,peak,total,commands,late,cmd_mean=struct.unpack('<HH11I',data)
    if (cmd,n,version,actual)!=(COMMAND,48,1,token):raise ValueError('mismatched load reply')
    if status==18:
        if any((seq,blocks,mean,peak,total,commands,late,cmd_mean)):raise ValueError('invalid unavailable reply')
        return dict(index=index,available=False)
    if status or not seq or blocks!=BLOCKS or mean>peak or peak>total or late>blocks:raise ValueError('invalid load observation')
    return dict(index=index,available=True,seq=seq,blocks=blocks,process_mean=mean,process_max=peak,
                total_max=total,commands=commands,late_blocks=late,command_cycles_mean=cmd_mean)

def read(exchange,index,token=1):return decode(exchange(request(index,token)),index,token)
