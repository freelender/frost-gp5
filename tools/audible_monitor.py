"""Read-only monitor for the first capped audible line test. Never writes settings.

Run one bounded phase at a time while the person changes VOL by hand:

    python tools/audible_monitor.py --stage <line-test stage> --phase silence --seconds 20 --max-gain 0
    python tools/audible_monitor.py --stage <stage> --phase step1 --seconds 30 --max-gain 82

Each poll reads 0x7f3b line status, 0x7f30 metrics, 0x7f35 output, 0x7f3a TX and
0x7f37 RX. The phase stops early, printing the reason, on: a line latch flag,
a chain fault, a new PCM rail/invalid frame, TX drops, RX
errors, a wrong profile, or gain above --max-gain. The DSP reports its maximum
gain (unity from 31z9; 0.25 on older builds) and silences on latches; this tool
only observes and tells the person to turn VOL down. It has no write command.
"""
import argparse
import datetime
import json
import struct
import sys
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import line_meter,output_meter,link_tx_meter,link_rx_meter  # noqa: E402

CAP=line_meter.CAP_Q16

def metrics(data):
    if len(data)!=44 or data[:4]!=bytes.fromhex('307f2c00'):raise ValueError('invalid metrics frame')
    v=struct.unpack_from('<10I',data,4)
    if v[0]!=0x46584332 or v[8]!=0:raise ValueError('not the line-test profile (muted marker set)')
    return dict(cycles_last=v[1],cycles_worst=v[2],late_blocks=v[3],faults=v[5])

def target(requested,cap=CAP):
    """DSP target gain for a raw Q16 request (mirrors firmware/dsp/line_output.h)."""
    return 0 if requested>65536 else (requested*cap)>>16

class Stop(Exception):
    """Hazard or limit; the message is what the person should do."""

def poll(exchange,token,light=False):
    t=token
    line=line_meter.decode(exchange(line_meter.request(t)),t)
    m=metrics(exchange(bytes.fromhex('307f0400')))
    if light:return dict(line=line,metrics=m,output=None,tx=None,rx=None)
    out=output_meter.decode(exchange(output_meter.request(t+1)),t+1)
    tx=link_tx_meter.decode(exchange(link_tx_meter.request(t+2)),t+2)
    rx=link_rx_meter.decode(exchange(link_rx_meter.request(t+3)),t+3)
    return dict(line=line,metrics=m,output=out,tx=tx,rx=rx)

def check(sample,base,max_gain):
    line,m,out,tx,rx=(sample[k] for k in ('line','metrics','output','tx','rx'))
    if line['mode']!='line-test' or line['source']!='platform-mono':raise Stop('Wrong profile; turn VOL down and stop')
    if line['flags']:raise Stop('DSP latched %s: output is now silent until reboot. Turn VOL down.'%'+'.join(line['flag_names']))
    if m['faults'] or line['chain_faults']:raise Stop('Chain fault: output silenced. Turn VOL down.')
    if out is None:return
    if out['pcm_rail_frames']!=base['rails'] or out['invalid_boundary_frames']!=base['invalid']:raise Stop('Output rail/invalid frames. Turn VOL down.')
    if tx['dropped_frames'] or any(rx[k] for k in ('crc_failures','discarded_bytes','expired_buffers','overflows')):raise Stop('Link errors. Turn VOL down.')
    if line['gain_q16']>max_gain:raise Stop('Gain %d above this phase limit %d. Turn VOL down now.'%(line['gain_q16'],max_gain))

def run(exchange,emit,seconds,interval,max_gain,sleep=time.sleep,clock=time.monotonic,say=print,light=False):
    """Bounded phase. Returns a summary dict; raises Stop on any hazard (after logging it)."""
    if not 0<seconds<=180 or not 0.2<=interval<=5 or not 0<=max_gain<=CAP:raise ValueError('unbounded or invalid phase')
    token=150000;first=poll(exchange,token,light);emit(dict(event='sample',**first))
    base=dict(late=first['metrics']['late_blocks'],rails=None if light else first['output']['pcm_rail_frames'],invalid=None if light else first['output']['invalid_boundary_frames'])
    summary=dict(samples=0,max_gain=0,max_requested=0,max_target=0,worst_cycles=0,late_start=base['late'])
    start=clock();sample=first
    try:
        while True:
            check(sample,base,max_gain)
            line=sample['line'];summary['samples']+=1
            summary['max_gain']=max(summary['max_gain'],line['gain_q16'])
            summary['max_requested']=max(summary['max_requested'],line['last_requested_q16'])
            summary['max_target']=max(summary['max_target'],target(line['last_requested_q16'],line['cap_q16']))
            summary['worst_cycles']=max(summary['worst_cycles'],sample['metrics']['cycles_worst'])
            say('gain %3d/%d  requested %5d  closed %-5s  worst %6d  late %d'%(line['gain_q16'],CAP,line['last_requested_q16'],line['closed'],sample['metrics']['cycles_worst'],sample['metrics']['late_blocks']))
            if clock()-start>=seconds:break
            sleep(interval);token+=4
            sample=poll(exchange,token,light);emit(dict(event='sample',**sample))
    except Stop as e:
        emit(dict(event='stopped',reason=str(e),summary=summary));say('STOP: '+str(e));raise
    summary['late_end']=sample['metrics']['late_blocks'];summary['final_gain']=sample['line']['gain_q16']
    emit(dict(event='phase_end',summary=summary));return summary

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--stage',type=Path,required=True,help='staged OpenGP5 folder providing tools/gp5tool.py')
    p.add_argument('--phase',required=True)
    p.add_argument('--seconds',type=float,default=20)
    p.add_argument('--interval',type=float,default=0.5)
    p.add_argument('--max-gain',type=int,required=True,help='phase gain limit in Q16, 0..65536')
    p.add_argument('--journal-dir',type=Path,required=True)
    p.add_argument('--light',action='store_true',help='read only line status and metrics (2 commands per poll)')
    a=p.parse_args(argv)
    sys.path.insert(0,str(a.stage/'tools'))
    import gp5tool,gp5_link,mido
    journal=a.journal_dir/('audible-%s.jsonl'%a.phase)
    with journal.open('x',encoding='utf-8') as f:
        def emit(v):f.write(json.dumps(dict(time_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),phase=a.phase,**v))+'\n');f.flush()
        ins=[n for n in mido.get_input_names() if 'GP-5' in n];outs=[n for n in mido.get_output_names() if 'GP-5' in n]
        emit(dict(event='ports',inputs=ins,outputs=outs))
        if ins!=['GP-5 Open FW 0'] or outs!=['GP-5 Open FW 1']:raise SystemExit('Expected unique open GP-5 ports')
        link=gp5tool.MidiLink(mido,mido.open_output(outs[0]),mido.open_input(ins[0]));time.sleep(.3)
        def exchange(payload):
            r=gp5_link.raw(link,payload,wait_ms=500)
            emit(dict(event='rpc',request=payload.hex(),status=None if r is None else r[0],reply=None if r is None else r[1].hex()))
            if r is None or r[0]!=1:raise Stop('Missing/rejected read (no retry). Turn VOL down.')
            return bytes(r[1])
        try:
            print(json.dumps(run(exchange,emit,a.seconds,a.interval,a.max_gain,light=a.light)))
        except Stop:
            raise SystemExit(2)
        finally:
            link.close()

if __name__=='__main__':main()
