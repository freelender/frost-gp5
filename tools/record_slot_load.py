"""Record read-only slot/load windows, plus line state. Never writes to the pedal.

Run separate labeled phases (baseline-silence, reverb-silence, reverb-playing).
Raw observations stay in a fresh build folder; --report writes the hardware
trial summary outside the repository. Stop on a read failure, late block, fault or latch;
preserve completed observations. No reset, configuration, or flash commands.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import sys
import time
import struct

import line_meter
import load_meter
import slot_load_meter

ROOT=Path(__file__).resolve().parents[1]

def verify_ir_taps(exchange,expected,token):
    """Two read-only fenced queries; READ index 42 carries parameters/tap
    counts only. Avoid DESCRIBE's whole-preset CRC and IR coefficient capture."""
    import preset_download
    reply=exchange(struct.pack('<HH8I',0x7f40,36,1,token,0,0,0,255,0xffffffff,0))
    if len(reply)!=48:raise ValueError('invalid slot-get reply for IR verification')
    w=struct.unpack('<HH11I',reply)
    if w[:5]!=(0x7f40,48,1,token,0) or not w[5] or w[6]!=0:
        raise ValueError('slot-get identity/status mismatch for IR verification')
    revision=w[5]
    r=preset_download.decode(exchange(preset_download.request(token+1,preset_download.READ,revision,42)))
    if (r['token'],r['status'],r['revision'],r['index'],r['count'])!=(token+1,0,revision,42,5):
        raise ValueError('IR metadata identity/status/revision mismatch')
    taps=list(r['data'][3:5])
    if taps[0]!=expected:raise ValueError('expected first IR %d taps, observed %s'%(expected,taps))
    return dict(revision=revision,ir_taps=taps)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True,help='fresh private log directory')
    p.add_argument('--report',type=Path,required=True,help='fresh external hardware-trial JSON')
    p.add_argument('--phase',required=True)
    p.add_argument('--seconds',type=float,default=30)
    p.add_argument('--expected-ir-taps',type=int,choices=(128,256,512),help='verify active first IR length before recording; read-only, no CRC/capture')
    a=p.parse_args()
    if not 0<a.seconds<=600:p.error('seconds must be in (0,600]')
    if a.report.exists():p.error('report exists; choose a new name')
    stage=a.stage.resolve()
    image=stage/'build/GP5_Frost_line_test.bin'
    image_sha=hashlib.sha256(image.read_bytes()).hexdigest()
    sys.path.insert(0,str(stage/'tools'))
    import gp5tool,gp5_link,mido
    ins=[n for n in mido.get_input_names() if 'GP-5' in n]
    outs=[n for n in mido.get_output_names() if 'GP-5' in n]
    if len(ins)!=1 or len(outs)!=1:raise RuntimeError('expected exactly one GP-5 MIDI input/output')
    a.output.mkdir(parents=True,exist_ok=False)
    journal=a.output/'observations.jsonl'
    windows={};loads={};states=[];token=700000;error=None;link=None;ir_configuration=None
    import re
    wire=stage/'firmwares/frost/dsp/firmware/protocol/timing_wire.h'
    m=re.search(r'#define FROST_LOAD_WINDOWS (\d+)u',wire.read_text(encoding='utf-8')) if wire.exists() else None
    load_windows=int(m.group(1)) if m else 16
    start=time.monotonic()
    def exchange(payload):
        reply=gp5_link.raw(link,payload,wait_ms=500)
        if reply is None or reply[0]!=1:raise RuntimeError('read failed; no retry/reset performed')
        return bytes(reply[1])
    with journal.open('x',encoding='utf-8') as log:
        def note(kind,value):
            log.write(json.dumps(dict(elapsed_s=round(time.monotonic()-start,3),kind=kind,value=value))+'\n')
            log.flush()
        try:
            link=gp5tool.MidiLink(mido,mido.open_output(outs[0]),mido.open_input(ins[0]))
            time.sleep(.3)
            if a.expected_ir_taps is not None:
                ir_configuration=verify_ir_taps(exchange,a.expected_ir_taps,token+1);token+=2
                note('ir-configuration',ir_configuration)
            while True:
                for index in range(load_windows):
                    token+=1;s=slot_load_meter.read(exchange,index,token)
                    note('slot',s)
                    if s['available']:windows[s['seq']]=s
                    token+=1;l=load_meter.read(exchange,index,token)
                    note('load',l)
                    if l['available']:loads[l['seq']]=l
                    if l['available'] and l['late_blocks']:
                        raise RuntimeError('late blocks observed; stopped without retry/reset')
                    # Stop scanning once older windows are already paired.
                    if s['available'] and l['available'] and s['seq']==l['seq'] and s['seq']<max(windows):
                        if s['seq']-1 in windows and s['seq']-1 in loads:break
                token+=1;state=line_meter.read(exchange,token);states.append(state);note('line',state)
                if state['flags'] or state['chain_faults']:
                    raise RuntimeError('line flags or chain faults observed; stopped without retry/reset')
                if time.monotonic()-start>=a.seconds:break
                time.sleep(2)
        except BaseException as exc:
            error=repr(exc);note('error',error)
        finally:
            if link is not None:link.close()
    pairs=[dict(slot=windows[s],load=loads[s]) for s in sorted(windows.keys()&loads.keys())]
    seqs=sorted(windows)
    result=dict(date=datetime.datetime.now(datetime.timezone.utc).isoformat(),kind='physical per-slot cycle profiling',
                phase=a.phase,expected_package_sha256=image_sha,
                identity_note='Expected artifact only; installation must be independently confirmed.',
                elapsed_s=round(time.monotonic()-start,3),error=error,ir_configuration=ir_configuration,
                paired_windows=pairs,unpaired_slot_sequences=sorted(windows.keys()-loads.keys()),
                unpaired_load_sequences=sorted(loads.keys()-windows.keys()),
                missing_slot_sequences=[s for s in range(min(seqs),max(seqs)+1) if s not in windows] if seqs else [],
                line_states=states,journal=str(journal.resolve()),
                journal_sha256=hashlib.sha256(journal.read_bytes()).hexdigest(),
                interpretation='Window maxima are inclusive elapsed DWT cycles, not additive or isolated kernel costs. Initial retained windows may predate this phase; fades are unprofiled; latches/skipped blocks have no slot timings.')
    a.report.parent.mkdir(parents=True,exist_ok=True)
    with a.report.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2);f.write('\n')
    print('Recorded %d paired windows; report %s; error %s'%(len(pairs),a.report,error))
    if error:raise SystemExit(1)

if __name__=='__main__':main()
