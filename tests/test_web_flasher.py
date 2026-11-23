"""The website's firmware install (web/firmware.js on OpenGP5's vendored flasher)
writes every section of a real Frost image intact into the simulated pedal."""
import json,shutil,subprocess,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
IMAGES=sorted((ROOT/'build').glob('gp5-line31y*-adc/build/GP5_Frost_line_test.bin'),key=lambda p:p.stat().st_mtime)  # newest last
@unittest.skipUnless(shutil.which('node') and IMAGES,'Node.js and a staged Frost image needed')
class WebFlasherTests(unittest.TestCase):
    def test_accepted_beta_identification(self):
        img=ROOT/'build/gp5-line31zc-ir-resync-setlist-final2-adc/build/GP5_Frost_line_test.bin'
        if not img.exists():self.skipTest('accepted 31zc image needed')
        r=subprocess.run(['node',str(ROOT/'tests/js/identify_release.mjs'),str(img)],cwd=ROOT,capture_output=True,text=True,timeout=60)
        self.assertEqual(r.returncode,0,r.stderr)

    def test_frost_image_flashes_intact(self):
        r=subprocess.run(['node',str(ROOT/'tests/js/flasher_frost.mjs'),str(IMAGES[-1])],cwd=ROOT,capture_output=True,text=True,timeout=600)
        self.assertEqual(r.returncode,0,r.stderr);d=json.loads(r.stdout.strip().splitlines()[-1])
        self.assertEqual(d['mode'],'open firmware');self.assertTrue(d['sections'] and all(s['same'] for s in d['sections']),d)
    def test_file_identification(self):
        img=ROOT/'build/gp5-line31y18-live-adc/build/GP5_Frost_line_test.bin';stock=ROOT.parent/'build/recovery/GP-5 Firmware V1.0.6.bin'
        if not img.exists():self.skipTest('31y18 stage image needed')
        junk=ROOT/'build/identify-junk.bin';junk.write_bytes(bytes(100))
        args=[str(img),str(junk)]+([str(stock)] if stock.exists() else [])
        r=subprocess.run(['node',str(ROOT/'tests/js/identify.mjs'),*args],cwd=ROOT,capture_output=True,text=True,timeout=60)
        junk.unlink();self.assertEqual(r.returncode,0,r.stderr);d=json.loads(r.stdout.strip().splitlines()[-1])
        self.assertEqual(d[0],{'kind':'frost','version':'31Y18'});self.assertIn('error',d[1])
        if stock.exists():self.assertEqual(d[2]['kind'],'stock')
    def test_effect_catalog_matches_firmware(self):
        d=json.loads((ROOT/'web/effects.json').read_text())
        self.assertEqual(len(d['effects']),59);self.assertEqual(d['effects']['34']['short'],'DROP')
if __name__=='__main__':unittest.main()

@unittest.skipUnless(shutil.which('node'),'Node.js needed')
class WebFlasherEraseTests(unittest.TestCase):
    def test_refused_and_silent_erase_messages(self):
        r=subprocess.run(['node',str(ROOT/'tests/js/flasher_erase_refused.mjs')],cwd=ROOT,capture_output=True,text=True,timeout=120)
        self.assertEqual(r.returncode,0,r.stderr);d=json.loads(r.stdout.strip().splitlines()[-1])
        ref,sil=d['refused'],d['silent']
        for c in (ref,sil):
            self.assertIn('Nothing was written',c['error']);self.assertEqual(c['written'],0)
            self.assertIn('Installation Guide',c['error']);self.assertNotIn('GETTING-STARTED',c['error'])
        self.assertEqual(ref['status'],1);self.assertIn('error code 01',ref['error']);self.assertIn('locked',ref['error'])
        self.assertIsNone(sil['status']);self.assertIn('no answer',sil['error']);self.assertNotIn('locked',sil['error'])
