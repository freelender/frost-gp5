"""Read-only DSP flash map check (31e, command 0x7f47). Never writes to the pedal.

Hashes DSP flash in 1 KB chunks with FNV-1a and compares each chunk with the bytes
expected there:
  tail      0x10348..0x14000  the installed package's DSP section, if 64 KB (31e only)
  dsp-data  0x14000..         the package's own 0x67 if it has one (31g+), else stock 0x67 (mapping)
  models    0x114000..        stock section 0x66 from the recovery image
Only hashes are exchanged and recorded; no stock bytes are copied. One pass, no retry.
Usage: python flash_hash.py --stage <stage> --recovery <stock.bin> --output <new dir> --report <new json>
"""
import argparse,datetime,hashlib,json,struct,sys,time
from pathlib import Path

COMMAND=0x7f47;CHUNK=1024;LOW=0x4000;HIGH=0x157000

def fnv(b):
    h=0x811c9dc5
    for x in b:h=((h^x)*0x01000193)&0xffffffff
    return h

def request(token,address,length):
    if not (LOW<=address<HIGH and 0<length<=CHUNK and length<=HIGH-address):raise ValueError('range outside the hash window')
    return struct.pack('<HHIII',COMMAND,16,token,address,length)

def decode(data,token):
    if len(data)!=16:raise ValueError('invalid flash hash reply length')
    cmd,n,tok,status,h=struct.unpack('<HHIII',data)
    if (cmd,n,tok)!=(COMMAND,16,token):raise ValueError('flash hash reply mismatched')
    if status!=0:raise ValueError('flash hash request rejected')
    return h

def expected_chunks(base,data):
    return [(base+o,len(data[o:o+CHUNK]),fnv(data[o:o+CHUNK])) for o in range(0,len(data),CHUNK)]

def regions(dsp_section,recovery_sections,frost_dsp_data=None):
    """frost_dsp_data: the package's own section 0x67 (31g+), compared instead of stock's."""
    plan=[]
    tail=dsp_section[0x10348-0x4000:0x14000-0x4000]
    if tail:plan.append(('tail',expected_chunks(0x10348,tail)))  # only a 64 KB DSP section has one
    return plan+[('dsp-data',expected_chunks(0x14000,frost_dsp_data if frost_dsp_data else recovery_sections[0x67])),
            ('models',expected_chunks(0x114000,recovery_sections[0x66]))]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True);p.add_argument('--recovery',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    a=p.parse_args()
    if a.report.exists():p.error('report exists; choose a new path')
    sys.path.insert(0,str(a.stage/'tools'))
    import gp5tool,gp5_link,mido
    pkg=a.stage/'build/GP5_Frost_line_test.bin'
    secs=dict(gp5tool.inspect(pkg.read_bytes())[0]);dsp=secs[0x63]
    rec=dict(gp5tool.inspect(a.recovery.read_bytes())[0])
    plan=regions(dsp,rec,secs.get(0x67))
    ins=[n for n in mido.get_input_names() if 'GP-5' in n];outs=[n for n in mido.get_output_names() if 'GP-5' in n]
    if len(ins)!=1 or len(outs)!=1:raise SystemExit('expected exactly one GP-5 MIDI input/output')
    a.output.mkdir(parents=True,exist_ok=False)
    journal=(a.output/'observations.jsonl').open('x',encoding='utf-8')
    link=gp5tool.MidiLink(mido,mido.open_output(outs[0]),mido.open_input(ins[0]));time.sleep(.3)
    token=31000;result={};error=None
    try:
        for name,chunks in plan:
            match=0;first_mismatch=None;done=0
            for addr,length,want in chunks:
                token+=1;r=gp5_link.raw(link,request(token,addr,length),wait_ms=500)
                if r is None or r[0]!=1:raise RuntimeError('read failed at 0x%x; no retry'%addr)
                got=decode(bytes(r[1]),token);done+=1
                journal.write(json.dumps(dict(region=name,address=addr,length=length,got=got,want=want))+'\n')
                if got==want:match+=1
                elif first_mismatch is None:first_mismatch=hex(addr)
            result[name]=dict(chunks=len(chunks),read=done,matching=match,first_mismatch=first_mismatch,
                              start=hex(chunks[0][0]),end=hex(chunks[-1][0]+chunks[-1][1]))
            print(name,result[name])
    except BaseException as exc:
        error=repr(exc);print('stopped:',error)
    finally:
        journal.close()
    report=dict(date=datetime.datetime.now(datetime.timezone.utc).isoformat(),kind='physical read-only DSP flash hash map',
                package_sha256=hashlib.sha256(pkg.read_bytes()).hexdigest(),
                recovery_sha256=hashlib.sha256(a.recovery.read_bytes()).hexdigest(),
                chunk=CHUNK,regions=result,error=error,
                journal=str(a.output/'observations.jsonl'),
                scope='Hashes only; no writes, resets or retries. Stock bytes are not copied.')
    a.report.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
