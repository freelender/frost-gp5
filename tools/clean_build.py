"""Archive transient build output and Python caches outside Frost; never deletes data."""
import argparse,os,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def clean(destination):
    destination=destination.resolve()
    if destination.is_relative_to(ROOT) or ROOT.is_relative_to(destination):
        raise ValueError('Choose an archive outside the repository, not one of its parents')
    build=ROOT/'build';moves=[]
    if build.exists():
        for source in build.iterdir():moves.append((source,destination/'build'/source.name))
    for folder,dirs,_ in os.walk(ROOT):
        dirs[:]=[d for d in dirs if d not in ('.git','.deps','.venv','build')]
        if '__pycache__' in dirs:
            source=Path(folder)/'__pycache__'
            moves.append((source,destination/'python-caches'/source.relative_to(ROOT)))
            dirs.remove('__pycache__')
    # Preflight every path before any move; move links as links, never walk them.
    for source,target in moves:
        if not source.absolute().is_relative_to(ROOT) or not target.is_relative_to(destination):raise ValueError('Path escapes intended roots')
        if target.exists() or target.is_symlink():raise FileExistsError(target)
    for source,target in moves:
        target.parent.mkdir(parents=True,exist_ok=True);shutil.move(str(source),str(target))
    build.mkdir(exist_ok=True)
    print(f'Archived {len(moves)} items to {destination}; build/ is empty.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True)
    clean(p.parse_args().archive)
