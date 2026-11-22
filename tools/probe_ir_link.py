"""Read-only stage 4 IR link status from the DSP (command 0x7F4E, op 0 =
status; op 1 would silence the link and is never sent here). Uses the
platform's USB DSP bridge (7E 03 stage, 7E 04 dispatch) like
probe_gp5_startup.py. No setting, counter or flash changes.
Usage: probe_ir_link.py --stage <stage dir> [--count N]"""
import argparse,json,struct,sys,time
from pathlib import Path
NAMES=['version','quiet','remote','frames_ok','frames_bad','busy','switches','ctl_cycles','block','ready','versions','sync_seen','magic_ok','last_lag','last_tag','rx_word0','rx_word1','resyncs']
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--count',type=int,default=3)
    a=p.parse_args();sys.path.insert(0,str((a.stage/'tools').resolve()));import gp5tool
    ports=gp5tool.MidiPorts();names=[n for n in ports.names() if gp5tool.OPEN_PORT in n]
    if len(names)!=1:raise SystemExit('expected exactly one GP-5 running the open firmware')
    link=ports.open(names[0]);time.sleep(.3);out=[]
    try:
        payload=bytes([0x4e,0x7f,12,0])+struct.pack('<II',1,0)   # version 1, op 0 = status (read-only)
        for i in range(a.count):
            r=gp5tool.ask(link,b'\x7e\x03\x00\x00'+payload,b'\x7e\x03',1.0)
            if r is None or bytes(r[:3])!=b'\x7e\x03\x01':out.append(dict(error='stage',reply=None if r is None else r.hex()));break
            r=gp5tool.ask(link,bytes([0x7e,4,len(payload),0,50]),b'\x7e\x04',2.0)
            if r is None or len(r)<4 or r[2]!=1:out.append(dict(error='dispatch',reply=None if r is None else r.hex()));break
            d=bytes(r[4:4+r[3]])
            if len(d)<48 or d[:2]!=b'\x4e\x7f':out.append(dict(error='reply',reply=d.hex()));break
            k=min(18,(len(d)-4)//4);v=struct.unpack_from('<%dI'%k,d,4);row=dict(zip(NAMES,v))
            for f in ('ready','versions','rx_word0','rx_word1'):
                if f in row:row[f]=hex(row[f])
            out.append(row)
            c=gp5tool.ask(link,b'\x7e\x13',b'\x7e\x13',1.0)
            if c is not None and len(c)>=70:
                cv=struct.unpack_from('<17I',bytes(c),2)
                row['controller']=dict(zip(['started','blocks','refused','lost','failed','waiting','waiting_max','notdone','disables','cur','cycles','worst','remote','dsp_cycles','dma_rx_status','dma_tx_status','spis_ctrl_dma_en'],cv))
            time.sleep(1.0)
    finally:link.close()
    print(json.dumps(out,indent=1))
if __name__=='__main__':main()
