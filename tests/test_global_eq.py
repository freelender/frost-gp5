"""Global output EQ: frequency response, neutral bypass, glide and atomic protocol."""
import ctypes as ct,cmath,math,os,struct,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
class GlobalEqTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/global_eq.dll' if os.name=='nt' else 'build/global_eq.so'),ROOT/'tests/global_eq_host.c')))
        cls.c.eq_process.argtypes=[ct.POINTER(ct.c_float),ct.c_uint]
    def setUp(self):self.c.eq_init()
    def command(self,db=(0,0,0),op=1,token=1,version=1):
        q=struct.pack('<HHIIIiii',0x7f50,28,version,token,op,*db);a=(ct.c_uint8*28)(*q);b=(ct.c_uint8*32)()
        self.c.eq_command(a,28,b)
        return struct.unpack('<HHIIIiiiI',bytes(b))
    def run_audio(self,x):
        a=(ct.c_float*len(x))(*x);self.assertEqual(self.c.eq_process(a,len(x)),1);return list(a)
    def amplitude(self,x,hz):
        return 2*abs(sum(v*cmath.exp(-2j*math.pi*hz*i/44100) for i,v in enumerate(x)))/len(x)
    def test_neutral_is_exact_and_return_to_neutral_settles(self):
        x=[.2*math.sin(i*.19) for i in range(12000)]
        self.assertEqual(self.run_audio(x),[ct.c_float(v).value for v in x])
        self.command((12,-12,6));self.run_audio(x);self.command();self.run_audio([0.]*16384)
        self.assertEqual(self.run_audio(x),[ct.c_float(v).value for v in x])
    def test_frequency_response_matches_independent_complex_transfer(self):
        for band,hz in [(0,100),(1,800),(2,8000)]:
            for db in [-12,12]:
                with self.subTest(band=band,db=db):
                    self.c.eq_init();g=[1.]*3;g[band]=10**(db/20);knobs=[0]*3;knobs[band]=db;self.command(knobs)
                    self.run_audio([0.]*8192)
                    x=[.03*math.sin(2*math.pi*hz*i/44100) for i in range(44100)]
                    y=self.run_audio(x)
                    z=cmath.exp(-2j*math.pi*hz/44100)
                    lo=.034994/(1-(1-.034994)*z);hi=.29966/(1-(1-.29966)*z)
                    expected=abs(1+lo*(g[0]-1)+(hi-lo)*(g[1]-1)+(1-hi)*(g[2]-1))
                    measured=self.amplitude(y[22050:],hz)/self.amplitude(x[22050:],hz)
                    self.assertAlmostEqual(20*math.log10(measured/expected),0,delta=.03)
    def test_malformed_or_invalid_write_is_atomic(self):
        self.assertEqual(self.command((4,-3,2))[4],0)
        for db in [(13,0,0),(0,-13,0),(0,0,2147483647)]:
            self.assertEqual(self.command(db)[4],10)
            self.assertEqual(self.command(op=0)[5:8],(4,-3,2))
        self.assertEqual(self.command(token=0)[4],4)
        self.assertEqual(self.command(version=2)[4],3)
        self.assertEqual(self.command(op=8)[4],5)
    def test_flat_skips_kernel_and_steady_gains_stop_recomputing(self):
        self.assertEqual(self.c.eq_state_bytes(),24)
        self.assertEqual(self.c.eq_active(),0)
        self.command();self.assertEqual(self.c.eq_active(),0)
        self.command((6,-3,2));self.assertEqual(self.c.eq_active(),2)
        self.run_audio([0.]*16384);self.assertEqual(self.c.eq_active(),1)
        self.command((6,-3,2));self.assertEqual(self.c.eq_active(),1)
        self.command();self.assertEqual(self.c.eq_active(),2)
        self.run_audio([0.]*16384);self.assertEqual(self.c.eq_active(),0)

if __name__=='__main__':unittest.main()
