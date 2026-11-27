"""Keep browser updates from mixing cached storage and setlist module versions."""
import shutil
import subprocess
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class WebModuleTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_combined_model_library(self):
        subprocess.run(['node',str(ROOT/'tests/js/model_library.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_named_capture_selector(self):
        subprocess.run(['node',str(ROOT/'tests/js/live_model.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_storage_write_recovery(self):
        subprocess.run(['node',str(ROOT/'tests/js/storage_retries.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_chocolate_controller_protocol(self):
        subprocess.run(['node',str(ROOT/'tests/js/chocolate.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_interface_language_tables(self):
        subprocess.run(['node',str(ROOT/'tests/js/i18n.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_versioned_module_graph_and_storage_contract(self):
        subprocess.run(['node',str(ROOT/'tests/js/web_modules.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_live_reorder_and_save_preset(self):
        subprocess.run(['node',str(ROOT/'tests/js/live_order_save.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)
