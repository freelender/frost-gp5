"""Stage 5 drop tune (effects/drop_tune): pitch accuracy, cleanliness through
the splices (residual after fitting the expected tones), chords, latency and
bounded work. Host checks of the kernel; listening on the pedal decides."""
import ctypes as ct,math,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
FS=44100.0
def run(c,x,drop,mix=100.0):
    c.dt_init();n=len(x);a=(ct.c_float*n)(*x);y=(ct.c_float*n)();c.dt_run(a,y,n,ct.c_float(drop),ct.c_float(mix));return list(y)
def tone(freqs,n,amp=0.3):
    return [sum(amp*math.sin(2*math.pi*f*i/FS+0.7*k) for k,f in enumerate(freqs)) for i in range(n)]
def zc_freq(y):
    t=[];prev=y[0]
    for i in range(1,len(y)):
        if prev<0<=y[i]:t.append(i-1+(-prev)/(y[i]-prev))
        prev=y[i]
    return (len(t)-1)/((t[-1]-t[0])/FS)
def solve(A,b):
    n=len(b);M=[row[:]+[b[i]] for i,row in enumerate(A)]
    for i in range(n):
        p=max(range(i,n),key=lambda r:abs(M[r][i]));M[i],M[p]=M[p],M[i]
        for r in range(n):
            if r!=i:
                f=M[r][i]/M[i][i]
                for c_ in range(i,n+1):M[r][c_]-=f*M[i][c_]
    return [M[i][n]/M[i][i] for i in range(n)]
def snr_db(y,freqs):
    """Energy of y explained by sinusoids at freqs vs the residual."""
    basis=[]
    for f in freqs:
        w=2*math.pi*f/FS;basis.append([math.sin(w*i) for i in range(len(y))]);basis.append([math.cos(w*i) for i in range(len(y))])
    A=[[sum(p*q for p,q in zip(bi,bj)) for bj in basis] for bi in basis];b=[sum(p*q for p,q in zip(bi,y)) for bi in basis]
    co=solve(A,b);fit=[sum(co[k]*basis[k][i] for k in range(len(basis))) for i in range(len(y))]
    e=sum(v*v for v in fit);r=sum((v-f)**2 for v,f in zip(y,fit))
    return 10*math.log10(e/max(r,1e-20))
class DropTuneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/drop_tune_host'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/drop_tune_host.c')))
        cls.c.dt_distance.restype=ct.c_float
    def test_pitch_is_exact_for_half_and_whole_step(self):
        for f in (82.41,110.0,196.0,329.63):
            for k,semis in ((0,1),(1,2)):
                y=run(self.c,tone([f],44100),k)[11025:]
                got=zc_freq(y);want=f*2**(-semis/12)
                self.assertLess(abs(1200*math.log2(got/want)),1.0,(f,semis,got,want))
    def test_single_notes_stay_clean_through_splices(self):
        for f in (82.41,146.83,246.94):
            for k,semis in ((0,1),(1,2)):
                y=run(self.c,tone([f],22050),k)[8192:16384]
                self.assertGreater(self.c.dt_splices(),1,(f,semis))
                self.assertGreater(snr_db(y,[f*2**(-semis/12)]),50.0,(f,semis))
    def test_power_chord_and_triad(self):
        for chord in ([82.41,123.47,164.81],[196.0,246.94,293.66]):
            for k,semis in ((0,1),(1,2)):
                y=run(self.c,tone(chord,22050,0.2),k)[8192:12288]
                r=2**(-semis/12);db=snr_db(y,[f*r for f in chord])
                self.assertGreater(db,30.0,(chord,semis,db))
    def test_latency_is_bounded(self):
        for f,limit_ms in ((82.41,16.0),(196.0,9.0),(329.63,7.0)):
            self.c.dt_init();x=tone([f],44100);y=run(self.c,x,1)
            self.assertLess(self.c.dt_max_distance()/FS*1000,limit_ms,(f,self.c.dt_max_distance()))
    def test_mix_zero_is_dry_and_state_is_small(self):
        x=[float(ct.c_float(v).value) for v in tone([196.0],4096)];y=run(self.c,x,0,0.0)
        self.assertEqual(max(abs(a-b) for a,b in zip(x,y)),0.0)
        self.assertLessEqual(self.c.dt_state_bytes(),72)
if __name__=='__main__':unittest.main()
