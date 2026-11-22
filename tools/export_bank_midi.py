"""Read-only bank export over the pinned platform's USB-MIDI SysEx bridge.

No loader, DSP mutation, reboot, patch or flash commands. Ports are explicit.
Requires optional mido/python-rtmidi, and firmware with controller bridge7E46.
"""
import argparse
from contextlib import ExitStack,closing
import math
from pathlib import Path
import struct
import time
from bank_export import bridge_request,bridge_reply,export_bank

def crc8(data):
    crc=0
    for byte in data:
        crc^=byte
        for _ in range(8):crc=((crc<<1)^0x07)&255 if crc&128 else (crc<<1)&255
    return crc

def frame(payload):
    if len(payload)>64:raise ValueError('SysEx request too large')
    body=bytes((0,1,0,len(payload)))+payload
    body=bytes((crc8(body),))+body[1:]
    return b'\xf0'+bytes(n for b in body for n in (b>>4,b&15))+b'\xf7'

def unframe(raw):
    if not 10<=len(raw)<=210 or raw[0]!=0xf0 or raw[-1]!=0xf7 or len(raw)%2:raise ValueError('invalid SysEx frame')
    nibbles=raw[1:-1]
    if any(n>15 for n in nibbles):raise ValueError('invalid SysEx nibble')
    data=bytes((nibbles[i]<<4)|nibbles[i+1] for i in range(0,len(nibbles),2))
    if data[1:3]!=b'\x01\x00' or data[3]+4!=len(data) or crc8(b'\0'+data[1:])!=data[0]:raise ValueError('invalid SysEx envelope/CRC')
    return data[4:]

def limits(timeout,attempts):
    if type(timeout) not in (int,float) or not math.isfinite(timeout) or not .1<=timeout<=10 or type(attempts) is not int or not 1<=attempts<=3:
        raise ValueError('invalid MIDI timeout/retry bounds')

class MidiBankLink:
    def __init__(self,mido,inp,out,timeout=2.,attempts=2,clock=time.monotonic,sleep=time.sleep):
        limits(timeout,attempts)
        self.mido=mido;self.inp=inp;self.out=out;self.timeout=timeout;self.attempts=attempts;self.clock=clock;self.sleep=sleep
    def exchange(self,inner):
        # Validate before any port traffic. This link cannot send arbitrary SysEx.
        raw=frame(bridge_request(inner));token=struct.unpack_from('<I',inner,8)[0]
        for _ in range(self.attempts):
            deadline=self.clock()+self.timeout;seen=0
            for ignored in self.inp.iter_pending():
                seen+=1
                if seen>1024 or self.clock()>=deadline:raise TimeoutError('MIDI input queue did not drain')
            self.out.send(self.mido.Message.from_bytes(list(raw)))
            while self.clock()<deadline:
                for msg in self.inp.iter_pending():
                    seen+=1
                    if seen>1024 or self.clock()>=deadline:raise TimeoutError('MIDI input flood/timeout')
                    if msg.type!='sysex' or len(msg.data)>208:continue
                    try:reply=bridge_reply(unframe(b'\xf0'+bytes(msg.data)+b'\xf7'))
                    except ValueError:continue
                    if struct.unpack_from('<I',reply,8)[0]==token:return reply
                self.sleep(.001)
        raise TimeoutError('No valid controller-bank reply; verify matching Frost firmware and explicit ports')

def export_ports(input_name,output_name,destination,timeout=2.,attempts=2,mido=None):
    destination=Path(destination)
    limits(timeout,attempts)
    if destination.suffix.lower()!='.fbank' or not destination.parent.is_dir():raise ValueError('destination needs .fbank in an existing folder')
    # Fail missing compiler/metadata before opening ports, not after a long read.
    from preset_io import catalog
    catalog()
    if mido is None:
        try:import mido
        except ImportError:raise RuntimeError('install optional mido and python-rtmidi to use USB-MIDI export') from None
    with ExitStack() as resources:
        inp=resources.enter_context(closing(mido.open_input(input_name)))
        out=resources.enter_context(closing(mido.open_output(output_name)))
        return export_bank(MidiBankLink(mido,inp,out,timeout,attempts).exchange,destination)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--list',action='store_true');p.add_argument('--input-port');p.add_argument('--output-port')
    p.add_argument('--destination',type=Path);p.add_argument('--timeout',type=float,default=2.)
    p.add_argument('--attempts',type=int,default=2);a=p.parse_args()
    try:
        if a.list:
            import mido
            print('Inputs:',mido.get_input_names());print('Outputs:',mido.get_output_names());return
        if not a.input_port or not a.output_port or not a.destination:p.error('explicit input/output ports and destination are required')
        result=export_ports(a.input_port,a.output_port,a.destination,a.timeout,a.attempts)
        print(f'Exported effective saved bank: {len(result.captures)} entries, epoch {result.epoch}, RAM mask {result.ram_mask:04x}')
    except (ValueError,OSError,RuntimeError,ImportError) as error:p.exit(2,str(error)+'\n')

if __name__=='__main__':main()
