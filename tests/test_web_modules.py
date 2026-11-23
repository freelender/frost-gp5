"""Keep browser updates from mixing cached storage and setlist module versions."""
import shutil
import subprocess
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class WebModuleTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_storage_write_recovery(self):
        subprocess.run(['node',str(ROOT/'tests/js/storage_retries.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_versioned_module_graph_and_storage_contract(self):
        subprocess.run(['node',str(ROOT/'tests/js/web_modules.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_live_reorder_and_save_preset(self):
        subprocess.run(['node',str(ROOT/'tests/js/live_order_save.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)
