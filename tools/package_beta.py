"""Prepare a local firmware/documentation/website bundle. Never flashes or deploys."""
import argparse,datetime,hashlib,json,re,shutil,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def package(stage,baseline,out,notes=None,candidate=False):
    stage,baseline,out=map(lambda p:Path(p).resolve(),(stage,baseline,out))
    if out.is_relative_to(ROOT.resolve()) or ROOT.resolve().is_relative_to(out):
        raise ValueError("Choose an output directory outside the source repository and its parents")
    if candidate and not notes:raise ValueError("Candidate packaging requires release notes with validation limits")
    info=json.loads((stage/'frost-input.json').read_text(encoding='utf-8'))
    if not info.get('media'):raise ValueError('Release firmware must include USB audio and BLE MIDI (--media-platform)')
    version=info['release_version']
    assert version==re.search(r'#define FROST_VERSION "([^"]+)"',(ROOT/'firmware/version.h').read_text(encoding='utf-8'))[1]
    assert re.fullmatch(r'\d+\.\d+\.\d+(?:-beta\.\d+)?',version)
    sys.path.insert(0,str(stage/'tools'));import gp5tool
    image=stage/'build/GP5_Frost_line_test.bin';data=image.read_bytes()
    parts=dict(gp5tool.inspect(data)[0]);old=dict(gp5tool.inspect(baseline.read_bytes())[0])
    assert set(parts)=={98,99,103}
    changed=sorted(k for k in set(parts)|set(old) if parts.get(k)!=old.get(k))
    dsp_identical=parts[99]==old.get(99) and parts[103]==old.get(103)
    assert candidate or dsp_identical, 'Release branding must preserve accepted DSP bytes; use --candidate for unqualified DSP changes'
    assert all(hashlib.sha256((ROOT/'tools'/n).read_bytes()).hexdigest()==v for n,v in info.get('staging_tools',{}).items()), 'Stage/tool mismatch'
    assert all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==v for n,v in info['inputs'].items()), 'Stage/source mismatch'
    digest=hashlib.sha256(data).hexdigest();web_version=re.search(r"export const VERSION = '([^']+)'",(ROOT/'web/app.js').read_text(encoding='utf-8'))[1]
    assert not out.exists(), 'Choose a fresh output directory'
    testers=out/'testers';testers.mkdir(parents=True)
    profile='USB_Audio_BLE'
    name='Frost_GP5_'+version
    (testers/(name+'.bin')).write_bytes(data)
    (testers/'SHA256SUMS.txt').write_text(digest+'  '+name+'.bin\n',encoding='utf-8',newline='\n')
    for filename in ('README.md','CONTRIBUTING.md'):shutil.copy2(ROOT/filename,testers/filename)
    shutil.copytree(ROOT/'docs',testers/'docs')
    for folder in ('branding','docs'):
        shutil.copytree(ROOT/'assets'/folder,testers/'assets'/folder)
    for filename in ('LICENSE','THIRD_PARTY_NOTICES.md'):shutil.copy2(ROOT/filename,testers/filename)
    if notes:shutil.copy2(notes,testers/'RELEASE_NOTES.md')   # tester notes, kept outside the repository
    site=out/'website';shutil.copytree(ROOT/'web',site)
    assert not any(site.rglob('*.bin'))
    assert all(not row.get('file') for row in json.loads((site/'firmware/manifest.json').read_text(encoding='utf-8')))
    for filename in ('LICENSE','THIRD_PARTY_NOTICES.md'):shutil.copy2(ROOT/filename,site/filename)
    hashes=lambda folder:{p.relative_to(folder).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.rglob('*')) if p.is_file()}
    display=re.search(r'#define FROST_VERSION_DISPLAY "([^"]+)"',(ROOT/'firmware/version.h').read_text(encoding='utf-8'))[1]
    assert display==version.upper(), 'Display/release versions disagree'
    manifest=dict(version=version,display=display,website_version=web_version,firmware_sha256=digest,firmware_bytes=len(data),baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest(),profile=profile,qualification='candidate-pending-hardware-qualification' if candidate else 'branding-only-against-accepted-baseline',dsp_byte_identical_to_baseline=dsp_identical,changed_sections=changed,source_inputs=info['inputs'],build_tools=info.get('staging_tools',{}),platform_commit=info['platform_commit'],media=info.get('media'),cab_ir=info.get('cab_ir'),prepared=datetime.date.today().isoformat(),flashed=False,published=False,testers=hashes(testers),website=hashes(site))
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
    for folder,filename in [(testers,name+'.zip'),(site,'Frost_Website_'+web_version+'.zip')]:
        with zipfile.ZipFile(out/filename,'w',zipfile.ZIP_DEFLATED) as z:
            for p in sorted(folder.rglob('*')):
                if p.is_file():z.write(p,p.relative_to(folder).as_posix())
        with zipfile.ZipFile(out/filename) as z:assert z.testzip() is None
    print(json.dumps(dict(output=str(out),firmware=name+'.bin',sha256=digest,version=version,profile=profile,candidate=candidate),indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=Path);p.add_argument('--baseline',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
    p.add_argument('--notes',type=Path,help='release notes file (outside the repository) added to the tester ZIP')
    p.add_argument('--candidate',action='store_true',help='Allow changed DSP sections and mark this package pending hardware qualification; requires --notes')
    a=p.parse_args();package(a.stage,a.baseline,a.out,a.notes,a.candidate)
