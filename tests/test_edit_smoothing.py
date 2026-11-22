"""Edit optimization preserves audio, final parameters and wet ramps exactly."""
import ctypes as ct,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
class EditSmoothingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/edit_smoothing.dll' if os.name=='nt' else 'build/edit_smoothing.so'),ROOT/'tests/edit_smoothing_host.c')))
    def test_amp_edits_keep_original_smoothing_and_audio(self):
        for kind in [*range(45,52),33,19,31,18,20]:
            for seed in [1,17,9021]:
                with self.subTest(effect=kind,seed=seed):self.assertEqual(self.c.edit_smoothing_matches(kind,seed),-1)
if __name__=='__main__':unittest.main()
