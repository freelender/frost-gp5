"""Host checks for the browser/Node FCM converter (web/converter, EXPERIMENTAL).
Converted models are approximations of NAM; these check the pipeline's parts:
record bytes identical to tools/fcm_bank.py, biquad fit, hand-written gradients
against finite differences, resampler, NAM block render, a short smoke run.
Skips when Node.js is not installed."""
import json,math,shutil,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import fcm_bank as B
from test_fcm import model
NAM=ROOT.parent/'upstream/NeuralAmpModelerCore/example_models/A2.nam'
@unittest.skipUnless(shutil.which('node'),'Node.js not installed')
class ConverterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        m=model(gain=1.7,drive=9.0,tilt=0.05);sec=lambda s:[[r[0],r[1],r[2],1.0,r[3],r[4]] for r in s]
        js=dict(pre=sec(m['pre']),mid=sec(m['mid']),post=sec(m['post']),g=m['gain'],A=[[m['Ap'][0],m['An'][0]],[m['Ap'][1],m['An'][1]]],
                K=[[m['Kp'][0],m['Kn'][0]],[m['Kp'][1],m['Kn'][1]]],ea=m['env_a'],ek=m['env_k'],aa=m['aa'])
        cls.py=B.pack_record('TEST AMP',m);p=ROOT/'build/converter_model.json';p.parent.mkdir(exist_ok=True);p.write_text(json.dumps(js))
        r=subprocess.run(['node',str(ROOT/'tests/js/converter_check.mjs'),str(p),str(NAM)],capture_output=True,text=True,timeout=900)
        if r.returncode:raise AssertionError(r.stderr)
        cls.r=json.loads(r.stdout.strip().splitlines()[-1])
    def test_record_bytes_match_the_python_packer(self):
        self.assertEqual(bytes.fromhex(self.r['record']),self.py)
        import zlib;self.assertEqual(self.r['crc'],zlib.crc32(b'FrostCaptureModel'))
    def test_biquad_fit_recovers_a_known_cascade(self):
        i=self.r['iir'];self.assertEqual(i['sections'],3);self.assertTrue(i['stable']);self.assertLess(i['esr'],1e-6)
    def test_gradients_match_finite_differences(self):self.assertLess(self.r['grad_worst_rel'],5e-3)
    def test_resampler_and_antialias_filter(self):
        self.assertEqual(self.r['resample']['length'],44100);self.assertLess(self.r['resample']['max_err'],1e-3)
        self.assertAlmostEqual(self.r['firwin_sum'],1.0,places=12)
    @unittest.skipUnless(NAM.exists(),'NAM Core example A2 not present')
    def test_nam_render_and_smoke_conversion(self):
        n=self.r['nam'];self.assertEqual((n['info']['channels'],n['info']['weights']),(8,12146));self.assertLess(n['block_vs_scalar'],1e-12)
        s=self.r['smoke'];self.assertEqual((s['bytes'],s['crc_ok'],s['sections']),(400,True,[3,1,3]))
if __name__=='__main__':unittest.main()
