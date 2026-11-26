"""Fetch only the locked platform source. Does not build or access a pedal."""
import json
import argparse
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
locks=json.loads((ROOT/'dependencies.lock.json').read_text())
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--component',choices=('platform','media'),default='platform',help='base platform or optional USB audio/BLE driver source')
p.add_argument('--destination',type=Path,default=None,help='fresh platform checkout; may be outside the source repository')
a=p.parse_args();lock=locks['OpenGP5Media' if a.component=='media' else 'OpenGP5']
destination=(a.destination or ROOT/lock['path']).resolve()
if destination.exists():raise SystemExit('Platform folder exists; inspect it rather than overwriting')
destination.parent.mkdir(exist_ok=True)
subprocess.run(['git','clone','--no-checkout',lock['url'],str(destination)],check=True)
subprocess.run(['git','-C',str(destination),'checkout','--detach',lock['commit']],check=True)
print(destination)
