"""Regenerate web/effects.json from the firmware control catalog (run after adding or changing an effect)."""
import ctypes as ct,os,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from host import build
lib=ct.CDLL(str(build(ROOT/('build/export_effects'+('.dll' if os.name=='nt' else '.so')),ROOT/'tools/export_effects.c',flags=['-Wno-unused-function'])))
out=ROOT/'web/effects.json'
if not lib.export_effects(str(out).encode()):raise SystemExit('could not write '+str(out))
import json;d=json.loads(out.read_text());print('wrote',out,len(d['effects']),'effects')
