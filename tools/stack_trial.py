"""Bounded read-only muted stack trial. Never resets counters or writes settings.

Only run on the separately authorized/installed stack ADC candidate. Every
request/result is journaled before interpreting it; failed reads are not retried.
"""
import argparse
import datetime
import json
from pathlib import Path
import struct
import sys
import time
import input_meter
import link_rx_meter
import output_meter
import stack_meter

def metrics(data,token):
    del token
    if len(data)!=44 or data[:4]!=bytes.fromhex('307f2c00'):
        raise ValueError('invalid timing frame')
    v=struct.unpack_from('<10I',data,4)
    if v[0]!=0x46584332 or v[8]!=1:raise ValueError('invalid timing identity or hard mute')
    return dict(cycles_last=v[1],cycles_worst=v[2],late_blocks=v[3],faults=v[5],
                commands=v[7],hard_muted=bool(v[8]))

def collect(exchange,emit,rounds=31,interval=2,sleep=time.sleep):
    """Callback-only collector. emit must persist each record before returning.

    Thirty gaps of2seconds, plus request time, for the default observation.
    No automatic reset/retry. A pass means only this observation completed.
    """
    if type(rounds) is not int or not 1<=rounds<=31 or not 0<=interval<=2:
        raise ValueError('trial bounds exceeded')
    emit(dict(event='start',rounds=rounds,interval_seconds=interval,
              parameter_writes=False,counter_resets=False,audible_output_enabled=False))
    baseline=None;token=80000
    try:
        for sample in range(rounds):
            for name,codec in [('metrics',None),('stack',stack_meter),('input',input_meter),
                               ('output',output_meter),('receiver',link_rx_meter)]:
                token+=1
                p=bytes.fromhex('307f0400') if codec is None else codec.request(token)
                emit(dict(event='request',sample=sample,name=name,request=p.hex()))
                data=exchange(p)
                emit(dict(event='reply',sample=sample,name=name,payload=data.hex()))
                d=metrics(data,token) if codec is None else codec.decode(data,token)
                emit(dict(event='observation',sample=sample,name=name,values=d))
                if name=='metrics':
                    if d['faults']:raise ValueError('chain faults recorded')
                    if baseline is None:baseline=d['late_blocks']
                    elif d['late_blocks']!=baseline:raise ValueError('late counter changed during observation')
                if name=='stack' and not (d['bottom_guard_intact'] and d['msp_in_range']):
                    raise ValueError('stack guard or MSP check failed')
                if 'hard_muted' in d and not d['hard_muted']:raise ValueError('hard mute missing')
                source=d.get('source',d.get('chain_source'))
                if source is not None and source!='platform-mono':raise ValueError('expected ADC source')
                if name=='output' and any(d[k] for k in ('outside_unity_frames','pcm_rail_frames',
                                                        'invalid_boundary_frames','chain_faults')):
                    raise ValueError('output headroom or fault check failed')
                if name=='input' and d['raw_rail_frames']:raise ValueError('input rails recorded')
            if sample+1<rounds:sleep(interval)
    except Exception as error:
        emit(dict(event='stopped',error=type(error).__name__+': '+str(error)))
        raise
    emit(dict(event='completed',rounds=rounds,initial_late_blocks=baseline,
              qualification='observation only; startup and broader workloads remain open'))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path,help='New JSONL journal; refuses overwrite')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(root/'build/gp5-stack-adc/tools'))
    # Optional device dependencies are loaded only for explicit CLI invocation.
    import gp5tool,gp5_link,mido
    with args.output.open('x',encoding='utf-8') as journal:
        def emit(item):
            item=dict(time_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**item)
            journal.write(json.dumps(item)+'\n');journal.flush()
        link=None
        try:
            ins=[n for n in mido.get_input_names() if 'GP-5' in n]
            outs=[n for n in mido.get_output_names() if 'GP-5' in n]
            emit(dict(event='ports',inputs=ins,outputs=outs))
            if ins!=['GP-5 Open FW 0'] or outs!=['GP-5 Open FW 1']:
                raise ValueError('expected unique open-firmware GP-5 input/output')
            inp=mido.open_input(ins[0])
            try:out=mido.open_output(outs[0])
            except Exception:inp.close();raise
            link=gp5tool.MidiLink(mido,out,inp);time.sleep(.3)
            def exchange(p):
                r=gp5_link.raw(link,p,wait_ms=500)
                if r is None or r[0]!=1:
                    emit(dict(event='transport_failure',status=None if r is None else r[0],
                              payload=None if r is None else r[1].hex()))
                    raise ValueError('missing or rejected raw response; no retry')
                return bytes(r[1])
            collect(exchange,emit)
        except Exception as error:
            emit(dict(event='session_error',error=type(error).__name__+': '+str(error)))
            raise
        finally:
            if link is not None:link.close()

if __name__=='__main__':main()
