"""Release packaging metadata, baseline fencing and portable documentation."""
import contextlib,hashlib,io,json,sys,tempfile,types,unittest,zipfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import package_beta as pkg

class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.work=Path(self.tmp.name);self.root=self.work/'src';self.root.mkdir()
        def put(name,text):
            p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8')
        put('firmware/version.h','#define FROST_VERSION "0.1.0-beta.17"\n#define FROST_VERSION_DISPLAY "0.1.0-BETA.17"\n')
        put('README.md','[Guide](docs/User%20Guide.md)');put('CONTRIBUTING.md','Contribution guide')
        for name in ('Installation Guide.MD','User Guide.md','Effects list.md','Website Guide.md','Build Firmware.md','Build effect.MD','Midi Command.md'):
            put('docs/'+name,'![Logo](../assets/branding/logo.svg)')
        put('assets/branding/logo.svg','<svg/>');put('assets/docs/example.svg','<svg/>')
        put('LICENSE','MIT');put('THIRD_PARTY_NOTICES.md','Notices')
        put('web/app.js',"export const VERSION = '1.9.0';")
        put('web/firmware/manifest.json','[{"file":""}]')
        put('web/index.html','<meta charset="utf-8">')
        put('tools/prepare_gp5.py','tool')
        self.stage=self.work/'stage';(self.stage/'build').mkdir(parents=True)
        self.baseline=self.work/'baseline.bin';self.image=self.stage/'build/GP5_Frost_line_test.bin'
        self.before={98:'app0',99:'dsp0',103:'data0'};self.after={98:'app1',99:'dsp0',103:'data0'}
        self.info=dict(release_version='0.1.0-beta.17',platform_commit='pinned',media={'commit':'media-pin'},
            inputs={'firmware/version.h':hashlib.sha256((self.root/'firmware/version.h').read_bytes()).hexdigest()},
            staging_tools={'prepare_gp5.py':hashlib.sha256((self.root/'tools/prepare_gp5.py').read_bytes()).hexdigest()})
        self.notes=self.work/'notes.md';self.notes.write_text('Candidate; audio qualification pending.',encoding='utf-8')
        self.out=self.work/'out'
        fake=types.ModuleType('gp5tool');fake.inspect=lambda b:({int(k):v for k,v in json.loads(b).items()},None)
        self.addCleanup(patch.stopall);patch.object(pkg,'ROOT',self.root).start();patch.dict(sys.modules,{'gp5tool':fake}).start()
    def run_package(self,candidate=False,notes=True):
        self.baseline.write_bytes(json.dumps(self.before).encode());self.image.write_bytes(json.dumps(self.after).encode())
        (self.stage/'frost-input.json').write_text(json.dumps(self.info),encoding='utf-8')
        with contextlib.redirect_stdout(io.StringIO()):pkg.package(self.stage,self.baseline,self.out,self.notes if notes else None,candidate)
        return json.loads((self.out/'manifest.json').read_text(encoding='utf-8'))
    def test_branding_mode_preserves_dsp_fence(self):
        self.after[99]='new DSP'
        with self.assertRaisesRegex(AssertionError,'preserve accepted DSP'):self.run_package()
        self.assertFalse(self.out.exists())
    def test_candidate_requires_notes(self):
        with self.assertRaisesRegex(ValueError,'requires release notes'):self.run_package(True,False)
    def test_changed_dsp_is_reported_and_not_qualified(self):
        self.after[99]='new DSP';m=self.run_package(True)
        self.assertEqual(m['changed_sections'],[98,99]);self.assertFalse(m['dsp_byte_identical_to_baseline'])
        self.assertEqual(m['qualification'],'candidate-pending-hardware-qualification')
        self.assertFalse(m['published']);self.assertFalse(m['flashed']);self.assertEqual(m['profile'],'USB_Audio_BLE')
    def test_branding_manifest_reports_actual_sections(self):
        m=self.run_package();self.assertEqual(m['changed_sections'],[98]);self.assertTrue(m['dsp_byte_identical_to_baseline'])
    def test_docs_images_midi_notices_and_checksum_are_in_zip(self):
        m=self.run_package(True);zpath=next(self.out.glob('Frost_GP5_0.1.0-beta.17.zip'))
        with zipfile.ZipFile(zpath) as z:
            self.assertIsNone(z.testzip())
            for name in ('README.md','docs/Midi Command.md','docs/User Guide.md','assets/branding/logo.svg','assets/docs/example.svg','LICENSE','THIRD_PARTY_NOTICES.md','RELEASE_NOTES.md'):
                self.assertIn(name,z.namelist())
            firmware=next(n for n in z.namelist() if n.endswith('.bin'))
            digest=hashlib.sha256(z.read(firmware)).hexdigest()
            self.assertEqual(digest,m['firmware_sha256']);self.assertEqual(z.read('SHA256SUMS.txt').decode(),digest+'  '+firmware+'\n')
    def test_release_rejects_a_build_without_usb_audio_and_ble(self):
        self.info['media']=None
        with self.assertRaisesRegex(ValueError,'must include USB audio and BLE MIDI'):self.run_package(True)
        self.assertFalse(self.out.exists())
    def test_source_and_staging_tool_changes_reject_package(self):
        for kind in ('inputs','staging_tools'):
            with self.subTest(kind=kind):
                key=next(iter(self.info[kind]));old=self.info[kind][key];self.info[kind][key]='bad'
                with self.assertRaises(AssertionError):self.run_package(True)
                self.info[kind][key]=old
        self.assertFalse(self.out.exists())
    def test_output_inside_source_is_rejected(self):
        self.out=self.root/'release'
        with self.assertRaisesRegex(ValueError,'outside the source'):self.run_package(True)

if __name__=='__main__':unittest.main()
