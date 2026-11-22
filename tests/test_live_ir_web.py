import shutil
import subprocess
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class LiveIrWebTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_cabinet_selector_and_live_protocol(self):
        subprocess.run(['node',str(ROOT/'tests/js/live_ir.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)
