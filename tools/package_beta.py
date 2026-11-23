"""Prepare a local beta firmware/tester/website bundle. Never flashes or deploys."""
import argparse,datetime,hashlib,json,re,shutil,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def package(stage,baseline,out,notes=None):
    info=json.loads((stage/'frost-input.json').read_text())
    version=info['release_version']
    assert version==re.search(r'#define FROST_VERSION "([^"]+)"',(ROOT/'firmware/version.h').read_text())[1]
    assert re.fullmatch(r'\d+\.\d+\.\d+-beta\.\d+',version)
    sys.path.insert(0,str(stage/'tools'));import gp5tool
    image=stage/'build/GP5_Frost_line_test.bin';data=image.read_bytes()
    parts=dict(gp5tool.inspect(data)[0]);old=dict(gp5tool.inspect(baseline.read_bytes())[0])
    assert set(parts)=={98,99,103}
    assert parts[99]==old[99] and parts[103]==old[103], 'Beta branding must preserve accepted DSP bytes'
    assert all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==v for n,v in info['inputs'].items()), 'Stage/source mismatch'
    digest=hashlib.sha256(data).hexdigest();web_version=re.search(r"export const VERSION = '([^']+)'",(ROOT/'web/app.js').read_text(encoding='utf-8'))[1]
    assert not out.exists(), 'Choose a fresh output directory'
    testers=out/'testers';testers.mkdir(parents=True)
    name='Frost_GP5_'+version
    (testers/(name+'.bin')).write_bytes(data)
    (testers/'SHA256SUMS.txt').write_text(digest+'  '+name+'.bin\n',encoding='utf-8')
    for source,dest in [('Installation Guide.MD','Installation Guide.MD'),('User Guide.md','User Guide.md'),('Website Guide.md','Website Guide.md'),('Build Firmware.md','Build Firmware.md'),('Build effect.MD','Build effect.MD')]:
        shutil.copy2(ROOT/'docs'/source,testers/dest)
    for filename in ('LICENSE','THIRD_PARTY_NOTICES.md'):shutil.copy2(ROOT/filename,testers/filename)
    if notes:shutil.copy2(notes,testers/'RELEASE_NOTES.md')   # tester notes, kept outside the repository
    site=out/'website';shutil.copytree(ROOT/'web',site)
    assert not any(site.rglob('*.bin'))
    assert all(not row.get('file') for row in json.loads((site/'firmware/manifest.json').read_text()))
    index=site/'index.html';html=index.read_text(encoding='utf-8')
    if 'name="robots"' not in html:html=html.replace('<meta charset="utf-8">','<meta charset="utf-8">\n<meta name="robots" content="noindex, nofollow">')
    index.write_text(html,encoding='utf-8')
    (site/'_headers').write_text('/*\n  Cache-Control: no-store\n  X-Robots-Tag: noindex, nofollow\n  Permissions-Policy: midi=(self)\n')
    for filename in ('LICENSE','THIRD_PARTY_NOTICES.md'):shutil.copy2(ROOT/filename,site/filename)
    hashes=lambda folder:{p.relative_to(folder).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.rglob('*')) if p.is_file()}
    display=re.search(r'#define FROST_VERSION_DISPLAY "([^"]+)"',(ROOT/'firmware/version.h').read_text())[1]
    assert display==version.upper(), 'Display/release versions disagree'
    manifest=dict(version=version,display=display,website_version=web_version,firmware_sha256=digest,firmware_bytes=len(data),baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest(),dsp_byte_identical_to_baseline=True,changed_sections=[98],prepared=datetime.date.today().isoformat(),flashed=False,published=False,testers=hashes(testers),website=hashes(site))
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    for folder,filename in [(testers,name+'_Testers.zip'),(site,'Frost_Website_'+web_version+'.zip')]:
        with zipfile.ZipFile(out/filename,'w',zipfile.ZIP_DEFLATED) as z:
            for p in sorted(folder.rglob('*')):
                if p.is_file():z.write(p,p.relative_to(folder).as_posix())
        with zipfile.ZipFile(out/filename) as z:assert z.testzip() is None
    print(json.dumps(dict(output=str(out),firmware=name+'.bin',sha256=digest,version=version),indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=Path);p.add_argument('--baseline',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
    p.add_argument('--notes',type=Path,help='release notes file (outside the repository) added to the tester ZIP')
    a=p.parse_args();package(a.stage,a.baseline,a.out,a.notes)
