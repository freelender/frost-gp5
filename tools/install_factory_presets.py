"""Install factory presets into slots 01–20, on explicit instruction.

Default is offline. --write performs preset storage writes, never a firmware
flash or an audition. Existing presets, AMP/IR assets, settings and setlists
are preserved. --replace-unmodified upgrades only exact original factory records.
Slots 21–60 are copied unchanged. Readback is part of writing.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import sys
import time
import zlib
import fcm_files as F
from frost_store import Store,GROUPS

ROOT=Path(__file__).resolve().parents[1]

def may_replace(old,new,original_hash=None):
    """An upgrade replaces only exact original factory bytes; edits/deletions stay."""
    if original_hash is not None:
        return old!=new and hashlib.sha256(old).hexdigest()==original_hash
    return not any(old) or all(v==255 for v in old)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write',action='store_true')
    parser.add_argument('--inspect',action='store_true',help='read-only upgrade plan; no storage/settings writes')
    parser.add_argument('--replace-unmodified',action='store_true',help='upgrade only exact v1 factory records; preserve edits, deletions and custom slots')
    parser.add_argument('--stage',type=Path,default=ROOT/'build/gp5-line31z6-setlists-adc')
    parser.add_argument('--bank',type=Path,default=ROOT/'build/factory-presets')
    parser.add_argument('--evidence',type=Path,default=ROOT/'build/factory-presets/install')
    args=parser.parse_args()
    manifest=json.loads((args.bank/'manifest.json').read_text(encoding='utf-8'))
    records=[(args.bank/('%02d.fpst'%i)).read_bytes() for i in range(1,21)]
    originals=json.loads((ROOT/'presets/factory-v1-hashes.json').read_text(encoding='utf-8'))['presets'] if args.replace_unmodified else None
    if originals and [r['number'] for r in originals]!=list(range(1,21)):raise ValueError('invalid factory upgrade baseline')
    for row,r in zip(manifest['presets'],records):
        if len(r)!=264 or hashlib.sha256(r).hexdigest()!=row['sha256']:raise ValueError('factory bank bytes differ from manifest')
    if not args.write and not args.inspect:print('20 factory records ready. Offline only; use --write with approval.');return 0
    if args.evidence.exists():raise RuntimeError('Choose fresh evidence directory; never repeat an installation implicitly.')
    sys.path.insert(0,str(args.stage/'tools'));import gp5tool
    ports=gp5tool.MidiPorts();names=[n for n in ports.names() if gp5tool.OPEN_PORT in n]
    if len(names)!=1:raise RuntimeError('Expected exactly one running Frost pedal. Close other MIDI connections if needed.')
    args.evidence.mkdir(parents=True);link=ports.open(names[0]);time.sleep(.3)
    report=dict(started=datetime.datetime.now(datetime.timezone.utc).isoformat(),scope='preset groups only; no firmware/settings/assets/setlist writes or audio/load tests',written=[],skipped=[])
    try:
        pedal=F.Pedal(lambda q:gp5tool.ask(link,bytes(q),bytes(q[:2]),2.0))
        _,who=pedal.who()
        if who!='frost' or pedal.storage()<2:raise RuntimeError('Packed Frost preset storage required')
        store=Store(pedal);files=pedal.files();before={};groups={}
        for g in range(4):
            h=store.newest(files,GROUPS['pst']['firm'],'pst-'+str(g));data=store.read_group('pst',g,files)
            if h and data is None:raise RuntimeError('Unreadable existing preset group; refusing replacement')
            before[str(g)]=data.hex() if data is not None else None
            if g<2:groups[g]=bytearray(data or bytes(3960))
        (args.evidence/'before.json').write_text(json.dumps(dict(files=files,preset_groups=before),indent=2)+'\n',encoding='utf-8')
        changed=set()
        for i,record in enumerate(records):
            group=i//15;at=i%15*264;old=groups[group][at:at+264]
            if not may_replace(old,record,originals[i]['sha256'] if originals else None):report['skipped'].append(i+1);continue
            groups[group][at:at+264]=record;changed.add(group);report['written'].append(i+1)
        if args.write:
            for g in sorted(changed):
                pedal.changed() # establish the controller SAVE guard before allocating a spare
                store.write_group('pst',g,bytes(groups[g]),say=print)
        else:
            report['scope']='read-only preset upgrade plan; no firmware/preset/settings/assets/setlist writes'
            report['planned']=report.pop('written')
        report['presets_after']=store.presets()
        report['completed']=datetime.datetime.now(datetime.timezone.utc).isoformat();report['success']=True
        print(json.dumps(report,indent=2))
    except Exception as exc:
        report.update(success=False,error=repr(exc));raise
    finally:
        link.close();(args.evidence/'install.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return 0

if __name__=='__main__':raise SystemExit(main())
