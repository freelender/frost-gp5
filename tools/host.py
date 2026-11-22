"""Host compiler helper for Frost's effect and controller fixtures."""
import ctypes as ct
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
INCLUDE_ROOT = ROOT

def compiler():
    candidate = os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc')
    if not candidate and os.name == 'nt':
        roots = list(Path('C:/Program Files/Microsoft Visual Studio').glob('*/Community/VC/Tools/Llvm/x64/bin/clang.exe'))
        candidate = str(sorted(roots)[-1]) if roots else None
    if not candidate: raise RuntimeError('set CC to a Clang/GCC compiler')
    return candidate

def build(output=None, source=None, includes=(), flags=()):
    output = Path(output) if output else ROOT/('build/nam_host.dll' if os.name == 'nt' else 'build/nam_host.so')
    output.parent.mkdir(parents=True, exist_ok=True)
    cmd = [compiler(), '-std=c99', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-shared']
    if os.name != 'nt': cmd += ['-fPIC']
    cmd += ['-I'+str(p) for p in [*includes, INCLUDE_ROOT]]
    cmd += list(flags)+[str(source or ROOT/'tests/chain_host.c'), '-o', str(output)]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    return output

