"""beta.14 PING stereo ping-pong delay (effects/pingpong) and the stereo side
signal (engine/stereo.h): echo pattern in mono and stereo, WIDTH, the chain's
fold rule and fades. Host evidence only; pedal DSP cost is measured separately."""
import ctypes as ct,json,math,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
PING,TAPE,DD3,RV6,CE2,TS,EQ,EMPTY=60,42,17,19,22,9,2,255
FS=44100
AT=10   # the delay reads only samples written since reset (as DIGI): not the very first
def impulse(n,at=AT,a=.5):return [a if i==at else 0.0 for i in range(n)]
def peaks(y,thr=1e-3):
    """(index, value) of isolated echo peaks above thr."""
    out=[]
    for i in range(1,len(y)-1):
        v=abs(y[i])
        if v>thr and v>=abs(y[i-1]) and v>abs(y[i+1]):out.append((i,y[i]))
    return out
def energy(x):return sum(v*v for v in x)
class Kernel(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/pingpong_kernel.dll' if os.name=='nt' else 'build/pingpong_kernel.so'),ROOT/'tests/new_effects_host.c')))
        c.ne_set.argtypes=[ct.c_uint,ct.c_float]
        c.ne_render_stereo.argtypes=[ct.POINTER(ct.c_float)]*3+[ct.c_uint]
        c.lib_bound.restype=ct.c_float;c.lib_name.restype=ct.c_char_p
    def tearDown(self):self.c.ne_stereo(0,1)
    def render(self,x,params,output,route_ok=1):
        c=self.c;c.ne_stereo(output,route_ok);c.ne_reset(PING)
        for i,v in params.items():self.assertEqual(c.ne_set(i,v),1,(i,v))
        a=(ct.c_float*len(x))(*x);l=(ct.c_float*len(x))();r=(ct.c_float*len(x))()
        c.ne_render_stereo(a,l,r,len(x));L,R=list(l),list(r)
        self.assertTrue(all(math.isfinite(v) for v in L+R));return L,R
    def test_registry_controls_and_workspace(self):
        c=self.c
        self.assertEqual(c.lib_name(PING),b'PING');self.assertEqual(c.lib_category(PING),2);self.assertEqual(c.lib_workspace(PING),2)
        self.assertEqual(c.lib_parameter_count(PING),5)
        self.assertEqual([c.lib_bound(PING,0,i) for i in range(3)],[20.,400.,300.])   # TIME up to 400 ms
        self.assertEqual([c.lib_bound(PING,3,i) for i in range(3)],[0.,100.,100.])    # WIDTH full by default
        self.assertEqual([c.lib_bound(PING,4,i) for i in range(3)],[0.,1.,0.])        # TAIL last, OFF
        d=json.loads((ROOT/'web/effects.json').read_text())
        self.assertEqual([p['name'] for p in d['effects'][str(PING)]['params']],['TIME','F.BACK','E.LEVEL','WIDTH','TAIL'])
        dly=d['picker'][2];self.assertEqual(dly.count(PING),1);self.assertEqual(dly.index(PING),dly.index(TAPE)+1)
    def test_mono_output_is_an_ordinary_delay_with_every_echo(self):
        T=100;n=int(4.6*T*44.1)
        L,R=self.render(impulse(n),{0:T,1:50,2:50,3:100},0)
        self.assertEqual(L,R)
        p=[i for i,_ in peaks(L[AT+1:])]   # i+1 = delay after the impulse
        hop=T*44.1
        self.assertEqual(len(p),4)                                            # T, 2T, 3T, 4T
        for k,i in enumerate(p,1):self.assertAlmostEqual(i+1,k*hop,delta=3)
        # each echo is F (0.97 x F.BACK) times the one before: a 200 Hz burst, which
        # the 7 kHz repeat filter leaves alone (a single-sample impulse would shrink)
        burst=[.3*math.sin(2*math.pi*200*i/FS) if AT<=i<AT+441 else 0. for i in range(n)]
        B,_=self.render(burst,{0:T,1:50,2:50,3:100},0)
        amps=[max(abs(v) for v in B[int(k*hop):int(k*hop)+500]) for k in range(1,5)]
        for a,b in zip(amps,amps[1:]):self.assertAlmostEqual(b/a,.97*.5,delta=.02)
    def test_stereo_echoes_alternate_left_and_right(self):
        T=100;n=int(4.6*T*44.1);hop=T*44.1
        L,R=self.render(impulse(n),{0:T,1:50,2:50,3:100},1)
        self.assertAlmostEqual(L[AT],.5,delta=1e-6);self.assertAlmostEqual(R[AT],.5,delta=1e-6)   # dry on both sides
        pl=[i+1 for i,_ in peaks(L[AT+1:])];pr=[i+1 for i,_ in peaks(R[AT+1:])]
        self.assertEqual(len(pl),2);self.assertEqual(len(pr),2)
        self.assertAlmostEqual(pl[0],hop,delta=3);self.assertAlmostEqual(pl[1],3*hop,delta=3)   # odd echoes left
        self.assertAlmostEqual(pr[0],2*hop,delta=3);self.assertAlmostEqual(pr[1],4*hop,delta=3) # even echoes right
        # the same echoes as the mono delay, just split between the sides
        M,_=self.render(impulse(n),{0:T,1:50,2:50,3:100},0)
        for a,b in zip(peaks(M[AT+1:]),sorted(peaks(L[AT+1:])+peaks(R[AT+1:]))):self.assertAlmostEqual(abs(a[1]),abs(b[1]),delta=2e-3)
    def test_width_zero_and_folded_play_mono_on_both_sides(self):
        n=int(3*100*44.1)
        M,_=self.render(impulse(n),{0:100,1:50,2:50},0)
        for params,route in (({0:100,1:50,2:50,3:0},1),({0:100,1:50,2:50,3:100},0)):
            L,R=self.render(impulse(n),params,1,route)
            self.assertEqual(L,R)
            for a,b in zip(L,M):self.assertAlmostEqual(a,b,delta=1e-6)
    def test_long_feedback_stays_bounded(self):
        x=[.9*math.sin(2*math.pi*220*i/FS) for i in range(4*FS)]
        L,R=self.render(x,{0:400,1:100,2:100,3:100},1)
        self.assertTrue(all(abs(v)<=8. for v in L+R))
class ChainStereo(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/pingpong_chain.dll' if os.name=='nt' else 'build/pingpong_chain.so'),ROOT/'tests/library_host.c')))
        c.lib_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        c.lib_process_stereo.argtypes=[ct.c_uint]+[ct.POINTER(ct.c_float)]*3+[ct.c_uint]
    def tearDown(self):self.c.lib_stereo(0)
    def chain(self,types,params=None):
        c=self.c;c.lib_init(0);a=(ct.c_uint*len(types))(*types)
        self.assertEqual(c.lib_configure(0,a,len(types)),1,types)
        for slot,t in enumerate(types):
            for p,v in (params or {}).get(slot,{}).items():self.assertEqual(c.lib_set(0,slot,p,v),1)
            if t!=EMPTY:self.assertEqual(c.lib_enable(0,slot,1),1)
    def run_(self,x):
        a=(ct.c_float*len(x))(*x);l=(ct.c_float*len(x))();r=(ct.c_float*len(x))()
        self.assertEqual(self.c.lib_process_stereo(0,a,l,r,len(x)),0);return list(l),list(r)
    def side_state(self):s=self.c.lib_side_state();return s&255,s>>8
    def test_fold_rule(self):
        self.chain([TS,PING,RV6,CE2,EMPTY,EMPTY]);self.assertEqual(self.side_state(),(1,1))    # reverb/modulation after it: stereo
        self.chain([PING,TS,EMPTY,EMPTY,EMPTY,EMPTY]);self.assertEqual(self.side_state(),(0,0))  # a drive after it: mono
        self.chain([PING,EQ,EMPTY,EMPTY,EMPTY,EMPTY]);self.assertEqual(self.side_state(),(0,0))  # utility after it: mono
        self.chain([TS,DD3,EMPTY,EMPTY,EMPTY,EMPTY]);self.assertEqual(self.side_state()[0],6)     # no stereo block
    def test_chain_output_is_stereo_only_in_stereo_mode(self):
        params={1:{0:100,1:40,2:60}}
        n=FS
        for mode in (0,1):
            self.c.lib_stereo(mode);self.chain([TS,PING,EMPTY,EMPTY,EMPTY,EMPTY],params)
            x=[(.4 if i<200 else 0.)*math.sin(2*math.pi*440*i/FS) for i in range(n)]
            L,R=self.run_(x)
            diff=energy([a-b for a,b in zip(L,R)])
            if mode:self.assertGreater(diff,1e-3)
            else:self.assertEqual(diff,0.)
    def test_switching_off_fades_the_side_too(self):
        self.c.lib_stereo(1);self.chain([PING,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY],{0:{0:50,1:90,2:80}})
        x=[.4*math.sin(2*math.pi*440*i/FS) if i<2000 else 0. for i in range(FS//2)]
        self.run_(x)                                     # echoes ringing on both sides
        self.assertEqual(self.c.lib_enable(0,0,0),1)     # TAIL OFF: switch off
        L,R=self.run_([0.]*(FS//2))
        side=[(a-b)/2 for a,b in zip(L,R)]
        self.assertLess(max(abs(v) for v in side[:64]),.6)                     # no jump at the switch
        self.assertLess(max(abs(v) for v in side[4096:]),1e-6)                 # then gone
if __name__=='__main__':unittest.main()
