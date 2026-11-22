"""Read-only DSP line-output status (0x7F3B): latch flags, gain, chain faults.
Usage: probe_line.py --stage <stage dir>"""
import argparse,json,struct,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--stage',type=Path,required=True);a=p.parse_args()
sys.path.insert(0,str((a.stage/'tools').resolve()));import gp5tool
ports=gp5tool.MidiPorts();names=[n for n in ports.names() if gp5tool.OPEN_PORT in n]
link=ports.open(names[0]);time.sleep(.3)
try:
    payload=bytes([0x3b,0x7f,12,0])+struct.pack('<II',1,77)
    r=gp5tool.ask(link,b'\x7e\x03\x00\x00'+payload,b'\x7e\x03',1.0)
    r=gp5tool.ask(link,bytes([0x7e,4,len(payload),0,50]),b'\x7e\x04',2.0)
    d=bytes(r[4:4+r[3]]);v=struct.unpack_from('<11I',d,4)
    print(json.dumps(dict(zip(['version','request','status','line_test','cap_q16','gain','flags','chain_faults','requested','source','muted'],v))))
finally:link.close()
