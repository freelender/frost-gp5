"""Local Pages staging and release-file verification; no deployment or device IO."""
import hashlib,json,os,re,shutil,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
IMAGE=os.environ.get('FROST_TEST_FIRMWARE')
VERSION=re.search(r'#define FROST_VERSION "([^"]+)"',(ROOT/'firmware/version.h').read_text(encoding='utf-8'))[1]
@unittest.skipUnless(shutil.which('node'),'Node.js needed')
class PagesTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.work=Path(self.tmp.name);self.out=self.work/'site'
    def run_stage(self,*args,ok=True):
        r=subprocess.run(['node',str(ROOT/'tools/prepare_pages.mjs'),'--out',str(self.out),*map(str,args)],cwd=ROOT,capture_output=True,text=True,timeout=30)
        if ok:self.assertEqual(r.returncode,0,r.stderr)
        else:self.assertNotEqual(r.returncode,0,r.stdout)
        return r
    def release_args(self,version=VERSION):
        image=self.work/f'Frost_GP5_{version}.bin';shutil.copy2(IMAGE,image)
        sums=self.work/'SHA256SUMS.txt';sums.write_text(hashlib.sha256(image.read_bytes()).hexdigest()+'  '+image.name+'\n',encoding='utf-8')
        return image,sums,('--firmware',image,'--checksums',sums,'--version',version)
    def test_editor_only_is_portable_and_cannot_advertise_firmware(self):
        self.run_stage();self.assertTrue((self.out/'.nojekyll').exists())
        self.assertTrue((self.out/'LICENSE').exists());self.assertTrue((self.out/'index.html').exists())
        row=json.loads((self.out/'firmware/manifest.json').read_text(encoding='utf-8'))[0]
        self.assertEqual((row['file'],row['sha256']),('',''));self.assertFalse(list(self.out.rglob('*.bin')))
    def test_existing_output_and_source_paths_are_rejected(self):
        self.out.mkdir();self.run_stage(ok=False)
        self.out=ROOT/'build/forbidden-pages';self.run_stage(ok=False);self.assertFalse(self.out.exists())
    def test_partial_firmware_options_are_rejected_before_copying(self):
        self.run_stage('--version',VERSION,ok=False);self.assertFalse(self.out.exists())
    @unittest.skipUnless(IMAGE,'FROST_TEST_FIRMWARE needed for release-image checks')
    def test_real_release_is_bundled_with_its_verified_manifest(self):
        image,sums,args=self.release_args();self.run_stage(*args)
        row=json.loads((self.out/'firmware/manifest.json').read_text(encoding='utf-8'))[0]
        self.assertEqual(row['file'],image.name);self.assertEqual(row['version'],VERSION)
        self.assertEqual((self.out/'firmware'/image.name).read_bytes(),image.read_bytes())
        self.assertEqual(row['sha256'],hashlib.sha256(image.read_bytes()).hexdigest())
    @unittest.skipUnless(IMAGE,'FROST_TEST_FIRMWARE needed for release-image checks')
    def test_wrong_checksum_or_duplicate_checksum_is_rejected(self):
        for duplicate in (False,True):
            image,sums,args=self.release_args()
            row=hashlib.sha256(image.read_bytes()).hexdigest()+'  '+image.name+'\n'
            sums.write_text(row+row if duplicate else '0'*64+'  '+image.name+'\n',encoding='utf-8')
            self.run_stage(*args,ok=False);self.assertFalse(self.out.exists())
    @unittest.skipUnless(IMAGE,'FROST_TEST_FIRMWARE needed for release-image checks')
    def test_wrong_embedded_version_and_corrupt_image_are_rejected(self):
        image,sums,args=self.release_args('0.1.0-beta.999');self.run_stage(*args,ok=False)
        self.assertFalse(self.out.exists())
        image,sums,args=self.release_args();data=bytearray(image.read_bytes());data[-1]^=1;image.write_bytes(data)
        sums.write_text(hashlib.sha256(data).hexdigest()+'  '+image.name+'\n',encoding='utf-8')
        self.run_stage(*args,ok=False);self.assertFalse(self.out.exists())

if __name__=='__main__':unittest.main()
