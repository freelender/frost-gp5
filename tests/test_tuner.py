"""Stage 2c tuner pitch detector (effects/tuner) on synthetic material at
44.1 kHz in 64-sample blocks: sines and Karplus-Strong plucked strings with
harmonics and decay, guitar E2..E4 and bass B0, detuned by known cents. Host
check of the algorithm only (no pedal timing)."""
import ctypes as ct,math,os,random,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
NOTES={'B0':30.868,'E1':41.203,'E2':82.407,'A2':110.0,'D3':146.832,'G3':195.998,'B3':246.942,'E4':329.628,'E5':659.255}
def pluck(f,seconds=1.5,fs=44100,seed=1,amp=0.5):
    """Karplus-Strong with a first-order allpass for the fractional delay, so the pitch is exact."""
    r=random.Random(seed);L=fs/f-0.5;n=int(L);frac=L-n
    line=[r.uniform(-1,1)*amp for _ in range(n)];a=(1-frac)/(1+frac);ap_x=ap_y=0.0;prev=0.0;out=[];i=0
    for _ in range(int(seconds*fs)):
        x=line[i];y=0.997*0.5*(x+prev);prev=x
        z=a*y+ap_x-a*ap_y;ap_x=y;ap_y=z
        line[i]=z;out.append(x);i=(i+1)%n
    return out
class TunerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/tuner_host.dll' if os.name=='nt' else 'build/tuner_host.so'),ROOT/'tests/tuner_host.c')))
        c.tu_freq.restype=ct.c_float;c.tu_clarity.restype=ct.c_float
    def run_(self,x):
        c=self.c;c.tu_start();est=[];last=0
        for i in range(0,len(x)-63,64):
            a=(ct.c_float*64)(*x[i:i+64]);c.tu_block(a,64)
            if c.tu_seq()!=last:last=c.tu_seq();est.append(c.tu_freq())
        return est
    def cents(self,f,ref):return 1200*math.log2(f/ref)
    def test_sines_are_within_half_a_cent(self):
        for name,f in NOTES.items():
            for detune in (-23.0,0.0,17.0):
                ft=f*2**(detune/1200);x=[0.3*math.sin(2*math.pi*ft*i/44100) for i in range(44100)]
                est=[e for e in self.run_(x) if e][2:]
                self.assertGreater(len(est),5,name)
                worst=max(abs(self.cents(e,ft)) for e in est);self.assertLess(worst,1.0 if f>500 else 0.5,(name,detune,worst))   # E5 (12th fret): <1 cent
    def test_plucked_strings_read_the_right_note(self):
        for name,f in NOTES.items():
            if name=='E5':continue
            x=pluck(f,seed=sum(map(ord,name)));est=[e for e in self.run_(x) if e][1:8]
            self.assertGreater(len(est),4,name)
            ref=sorted(est)[len(est)//2]
            self.assertLess(abs(self.cents(ref,f)),50.0,(name,ref))                    # the right note: no octave errors
            self.assertLess(max(abs(self.cents(e,ref)) for e in est),1.0,(name,est))  # steady within 1 cent
    def test_readings_come_about_ten_times_a_second_and_silence_reads_nothing(self):
        x=[0.3*math.sin(2*math.pi*110*i/44100) for i in range(44100)];est=self.run_(x)
        self.assertGreaterEqual(len(est),10)
        self.assertTrue(all(e==0 for e in self.run_([0.0]*22050)))
        self.assertLessEqual(self.c.tu_bytes(),70000)                  # fits the borrowed delay line
if __name__=='__main__':unittest.main()
