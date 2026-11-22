"""Fetch only the locked platform source. Does not build or access a pedal."""
import json
import argparse
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
lock=json.loads((ROOT/'dependencies.lock.json').read_text())['OpenGP5']
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--destination',type=Path,default=ROOT/'.deps/OpenGP5',help='fresh platform checkout; may be outside the source repository')
destination=p.parse_args().destination.resolve()
if destination.exists():raise SystemExit('Platform folder exists; inspect it rather than overwriting')
destination.parent.mkdir(exist_ok=True)
subprocess.run(['git','clone','--no-checkout',lock['url'],str(destination)],check=True)
subprocess.run(['git','-C',str(destination),'checkout','--detach',lock['commit']],check=True)
print(destination)
