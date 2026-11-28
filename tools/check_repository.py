"""Check source layout, guide allowlist, local Markdown links and private/generated files."""
import argparse,os,re
from pathlib import Path
from urllib.parse import unquote
ROOT=Path(__file__).resolve().parents[1]
GUIDES={'Installation Guide.MD','Build Firmware.md','Build effect.MD','Website Guide.md','User Guide.md','Midi Command.md','Effects list.md'}
ROOT_ITEMS={'.git','.github','.gitattributes','.gitignore','.deps','.venv','AGENTS.md','CONTRIBUTING.md','README.md','LICENSE','THIRD_PARTY_NOTICES.md','CMakeLists.txt','dependencies.lock.json','assets','build','community','docs','effects','engine','firmware','presets','tests','tools','web'}
GENERATED={'.bin','.elf','.dll','.so','.pyc','.wav','.nam','.fcm','.fcap','.fbank','.frostbackup','.npz','.zip','.txz','.o','.obj','.a','.lib','.map','.log'}

def check(empty=False):
    errors=[]
    for p in ROOT.iterdir():
        if p.name not in ROOT_ITEMS:errors.append('Unexpected root item: '+p.name)
    actual={p.name for p in (ROOT/'docs').iterdir()}
    if actual!=GUIDES:errors.append('docs/ must contain only the seven maintained guides')
    if empty and (ROOT/'build').exists() and any((ROOT/'build').iterdir()):errors.append('build/ is not empty')
    for folder,dirs,names in os.walk(ROOT):
        dirs[:]=[d for d in dirs if d not in ('.git','.deps','.venv','build','.local')]   # .local: community/dev.mjs test data
        for name in names:
            p=Path(folder)/name;relative=p.relative_to(ROOT).as_posix()
            if p.suffix.lower() in GENERATED or name in ('model_bank.h','model_bank.json','fast_model.h','user_bank.h'):errors.append('Generated/private file: '+relative)
            if p.suffix.lower() not in ('.md','.py','.c','.h','.inc','.js','.json','.yml','.txt','.sh'):continue
            text=p.read_text(encoding='utf-8')
            if re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',text):errors.append('Private key marker: '+relative)
            if p.suffix.lower()=='.md':
                for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)',text):
                    target=unquote(target.split('#',1)[0].split(' "',1)[0]).strip('<>')
                    if not target or re.match(r'^[a-z]+:',target):continue
                    if not (p.parent/target).exists():errors.append('Broken local link: '+relative+' -> '+target)
    return errors

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--require-empty-build',action='store_true');a=p.parse_args()
    errors=check(a.require_empty_build)
    if errors:print('\n'.join(errors));raise SystemExit(1)
    print('Repository layout, guides, local links and source hygiene pass.'+(' build/ is empty.' if a.require_empty_build else ''))
