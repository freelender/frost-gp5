"""Put a staged Frost image on the website (web/firmware/) and fill its manifest entry.
The current stage builds bake the boot cabinet from private/IRs; never publish such an
image. Pass --public-assets only for a build made with public/licensed assets."""
import argparse,hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage');p.add_argument('version')
p.add_argument('--public-assets',action='store_true',help='confirm the image contains no private captures/IRs')
a=p.parse_args()
stage=ROOT/'build'/a.stage;info=json.loads((stage/'frost-input.json').read_text())
cab=(info.get('cab_ir') or {}).get('source','') or str(info.get('cab_ir'))
if not a.public_assets:raise SystemExit('Refusing: confirm with --public-assets that this build uses no private assets (cab IR: %s).'%cab)
img=stage/'build/GP5_Frost_line_test.bin';data=img.read_bytes();name='frost-%s.bin'%a.version.lower()
out=ROOT/'web/firmware';shutil.copy2(img,out/name)
m=json.loads((out/'manifest.json').read_text());e=next(x for x in m if x['id']=='frost')
e.update(version=a.version,file=name,sha256=hashlib.sha256(data).hexdigest())
(out/'manifest.json').write_text(json.dumps(m,indent=1)+'\n');print('published',name,e['sha256'])
