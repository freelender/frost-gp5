"""Stage 4 IR link (docs/Build%20Firmware.md): the DSP half and the controller half
joined by a simulated SPI wire on the host. Checks the fixed-point kernel, the
2-block latency, link loss fade and fallback, coefficient resync and slave
byte lag. Host checks only: no pedal timing, no real SPI."""
import ctypes as ct,math,os,random,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
F64=ct.c_float*64
def cab(taps=512,seed=3):
    r=random.Random(seed);h=[r.gauss(0,1)*math.exp(-i/60.0) for i in range(taps)]
    s=sum(abs(v) for v in h);return [v/s for v in h]
def tone(n,seed=5):
    r=random.Random(seed);return [0.4*math.sin(2*math.pi*196.0*i/44100)+0.2*math.sin(2*math.pi*523.0*i/44100+1)+0.05*r.uniform(-1,1) for i in range(n)]
def conv(x,h):
    y=[0.0]*len(x)
    for n in range(len(x)):
        s=0.0
        for k in range(min(len(h),n+1)):s+=h[k]*x[n-k]
        y[n]=s
    return y
class IrLinkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/ir_link_host'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/ir_link_host.c',
            includes=[ROOT/'tests/stubs'],flags=['-DFROST_IR_FFT_PARTS=8u','-DFROST_IR_FFT_SPLIT_SPECTRUM=1'])))
        c.ln_level.restype=ct.c_float;c.ln_scale.restype=ct.c_float
    def setup(self,h=None,two=False):
        c=self.c;c.ln_init(int(two));h=h or cab()
        self.assertEqual(c.ln_load(0,(ct.c_float*len(h))(*h),len(h)),1);self.h=h
        return c
    def run_blocks(self,x,blocks=None):
        c=self.c;out=[];n=len(x)//64 if blocks is None else blocks
        for b in range(n):
            i=F64(*x[64*b:64*b+64]) if x else F64();o=F64();self.assertEqual(c.ln_block(i,o),1);out+=list(o)
        return out
    def test_kernel_matches_exact_rounding(self):
        c=self.c;r=random.Random(9);q=[r.randint(-32768,32767) for _ in range(512)]
        c.ln_kernel_reset((ct.c_int16*512)(*q));xs=[];ys=[]
        for _ in range(12):
            x=[r.randint(-(1<<27),(1<<27)) for _ in range(64)];o=(ct.c_int32*64)();c.ln_kernel((ct.c_int32*64)(*x),o);xs+=x;ys+=list(o)
        for n in range(len(xs)-64,len(xs)):   # one block, exact model of kmmawb/kmmawt with saturation
            acc=0
            for k in range(512):
                v=xs[n-k] if n-k>=0 else 0;acc=max(-2**31,min(2**31-1,acc+((v*q[k]+0x8000)>>16)))
            self.assertEqual(ys[n],acc)
    def test_switches_to_controller_and_adds_exactly_two_blocks(self):
        c=self.setup();x=tone(64*900);y=self.run_blocks(x)
        self.assertEqual(c.ln_remote(),1);self.assertGreaterEqual(c.ln_stat(3),1)
        ref=conv(x[-64*40-128:],self.h);lvl=c.ln_level(0)
        tail=y[-64*30:];want=[lvl*v for v in ref[-64*30-128:-128]]
        err=max(abs(a-b) for a,b in zip(tail,want));peak=max(abs(v) for v in want)
        self.assertLess(err/peak,2e-4,(err,peak))
        # not 1 or 3 blocks
        for d in (64,192):
            alt=[lvl*v for v in ref[-64*30-d:-d]];self.assertGreater(max(abs(a-b) for a,b in zip(tail,alt))/peak,0.05)
    def test_link_loss_fades_then_recovers_without_fallback(self):
        c=self.setup();x=tone(64*1400);self.run_blocks(x[:64*700]);self.assertEqual(c.ln_remote(),1)
        c.ln_set(0,0,0);y=self.run_blocks(x[64*700:64*800])        # 100 blocks lost (145 ms flash erase)
        dry_free=max(abs(v) for v in y[64*3:])
        self.assertLess(dry_free,1e-4)                               # IR output silent (only the existing ~4e-6 dry leak of the settled wet), no garbage
        self.assertEqual(c.ln_remote(),1)                            # under 1 s: stays remote
        c.ln_set(0,1,0);self.run_blocks(x[64*800:64*820]);ok=c.ln_stat(0);self.run_blocks(x[64*820:64*900])
        self.assertGreater(c.ln_stat(0),ok+70)
        y=self.run_blocks(x[64*900:64*1000]);self.assertGreater(max(abs(v) for v in y),0.01)
    def test_long_loss_falls_back_to_dsp_ir(self):
        c=self.setup();x=tone(64*1700);self.run_blocks(x[:64*700]);self.assertEqual(c.ln_remote(),1)
        c.ln_set(0,0,0);self.run_blocks(x[64*700:64*1420]);self.assertEqual(c.ln_remote(),0)
        y=self.run_blocks(x[64*1420:64*1500]);lvl=c.ln_level(0)
        ref=conv(x[64*1420-512:64*1500],self.h[:128])[512:];tail=y[-64*20:];want=[lvl*v for v in ref[-64*20:]]   # fallback: first 128 taps
        peak=max(abs(v) for v in want);self.assertLess(max(abs(a-b) for a,b in zip(tail,want))/peak,1e-4)  # local, undelayed
    def test_slave_byte_lag_is_found(self):
        for lag in (1,3,5,8):
            c=self.setup();c.ln_set(lag,1,0);x=tone(64*600);self.run_blocks(x)
            self.assertEqual(c.ln_remote(),1,lag);self.assertEqual(c.ln_stat(9),0,lag)
    def test_a_late_controller_frame_is_refused_not_played(self):
        c=self.setup();x=tone(64*900);self.run_blocks(x[:64*700]);bad=c.ln_stat(1)
        c.ln_set(0,1,1);y=self.run_blocks(x[64*700:64*706])           # one exchange without the controller's work
        self.assertGreater(c.ln_stat(1),bad)
        self.assertEqual(c.ln_remote(),1)
        for v in y:self.assertTrue(math.isfinite(v) and abs(v)<2.0)
    def test_new_coefficients_resync_and_play(self):
        c=self.setup();x=tone(64*1300);self.run_blocks(x[:64*700]);v0=c.ln_stat(6)
        h2=cab(300,seed=11);self.assertEqual(c.ln_load(0,(ct.c_float*300)(*h2),300),1)
        self.run_blocks(x[64*700:64*760]);self.assertNotEqual(c.ln_stat(6),v0)
        y=self.run_blocks(x[64*760:64*860]);lvl=c.ln_level(0)
        ref=conv(x[64*760-640:64*860],h2)[640:];tail=y[-64*20:];want=[lvl*v for v in ref[-64*20-128:-128]]
        peak=max(abs(v) for v in want);self.assertLess(max(abs(a-b) for a,b in zip(tail,want))/peak,2e-4)
    def test_two_ir_blocks_use_two_streams(self):
        c=self.setup(two=True);h2=cab(512,seed=21);self.assertEqual(c.ln_load(1,(ct.c_float*512)(*h2),512),1)
        x=tone(64*900);y=self.run_blocks(x);self.assertEqual(c.ln_remote(),1)
        l0=c.ln_level(0);l1=c.ln_level(1);a=conv(x[-64*40-256-1024:],self.h);b=conv([l0*v for v in a],h2)
        tail=y[-64*20:];want=[l1*v for v in b[-64*20-256:-256]]    # 2 + 2 blocks
        peak=max(abs(v) for v in want);self.assertLess(max(abs(p-q) for p,q in zip(tail,want))/peak,3e-4)
    def test_controller_stall_shift_recovers_by_pausing(self):
        # 31y9 on the pedal: after a controller flash erase every frame stayed
        # shifted while the DSP kept exchanging. The DSP now pauses, the
        # controller's 20 ms quiet re-arm fires and frames align again.
        c=self.setup();x=tone(64*1400);self.run_blocks(x[:64*700]);self.assertEqual(c.ln_remote(),1)
        c.ln_shift();self.run_blocks(x[64*700:64*900])
        self.assertGreaterEqual(c.ln_stat(14),1);self.assertGreaterEqual(c.ln_stat(15),1)
        ok=c.ln_stat(0);self.run_blocks(x[64*900:64*1000]);self.assertGreaterEqual(c.ln_stat(0)-ok,100)
        self.assertEqual(c.ln_remote(),1)   # recovered well inside the 1 s fallback
        y=self.run_blocks(x[64*1000:64*1100]);self.assertGreater(max(abs(v) for v in y),0.01)
    def test_route_commit_runs_in_its_own_silent_block(self):
        c=self.setup();c.ln_commit_alone(1);x=tone(64*40)
        self.run_blocks(x[:64*10])
        types=(ct.c_uint*6)(3,1,255,255,255,255);self.assertEqual(c.ln_route(types),1)
        seen=[]
        for b in range(10,40):
            o=F64();c.ln_block(F64(*x[64*b:64*b+64]),o);seen.append((c.ln_transition(),max(abs(v) for v in o)))
        pend=[i for i,(tr,_) in enumerate(seen) if tr==3]   # FROST_ROUTE_COMMIT after the last fade-out block
        self.assertEqual(len(pend),1)
        k=pend[0]+1
        self.assertEqual(seen[k],(2,0.0))        # commit-only block: silent, then fade-in
        self.assertEqual(c.ln_type(1),1);c.ln_commit_alone(0)
class RequiredRemoteTests(unittest.TestCase):
    setup=IrLinkTests.setup
    run_blocks=IrLinkTests.run_blocks
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/ir_required'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/ir_link_host.c',
            includes=[ROOT/'tests/stubs'],flags=['-DFROST_IRL_REQUIRE_REMOTE=1'])))
        c.ln_level.restype=ct.c_float;c.ln_scale.restype=ct.c_float
    def test_previous_transfer_is_closed_even_when_dma_is_busy(self):
        c=self.setup()
        self.assertEqual(c.ln_finish_busy(),1)
        self.assertEqual(c.ln_stat(2),1)
    def test_flash_waits_for_master_pause_and_rearms_before_resume(self):
        c=self.setup();x=tone(64*1000);self.run_blocks(x[:64*700]);ok=c.ln_stat(0)
        self.assertEqual(c.ln_flash_begin(),1)
        self.assertEqual([c.ln_flash_state(i) for i in (0,1,4)],[2,1,0])
        y=self.run_blocks(x[64*700:64*740])
        self.assertEqual(c.ln_stat(0),ok)
        self.assertLess(max(map(abs,y[-64*10:])),1e-4)
        c.ln_flash_end()
        self.assertEqual([c.ln_flash_state(i) for i in (0,1,2,4)],[0,0,1,1])
        y=self.run_blocks(x[64*740:])
        self.assertGreater(c.ln_stat(0),ok+200)
        self.assertGreater(max(map(abs,y[-64*50:])),.01)
    def test_unacknowledged_pause_refuses_flash(self):
        c=self.setup();c.ln_control_fail(1,0)
        self.assertEqual(c.ln_flash_begin(),0)
        self.assertEqual([c.ln_flash_state(i) for i in (0,1,2,4)],[0,0,0,1])
    def test_nested_flash_only_resumes_after_outer_end(self):
        c=self.setup();self.assertEqual(c.ln_flash_begin(),1);self.assertEqual(c.ln_flash_begin(),1)
        self.assertEqual(c.ln_flash_state(3),1)
        c.ln_flash_end();self.assertEqual(c.ln_flash_state(0),2);self.assertEqual(c.ln_flash_state(1),1)
        c.ln_flash_end();self.assertEqual(c.ln_flash_state(0),0);self.assertEqual(c.ln_flash_state(3),2)
    def test_lost_resume_retries_via_recovery_poll(self):
        c=self.setup();x=tone(64*1000);self.run_blocks(x[:64*700])
        self.assertEqual(c.ln_flash_begin(),1);c.ln_control_fail(0,1);c.ln_flash_end()
        self.assertEqual(c.ln_flash_state(0),2)
        c.ln_control_fail(0,0);y=self.run_blocks(x[64*700:])
        self.assertEqual(c.ln_flash_state(0),0);self.assertGreater(max(map(abs,y[-64*50:])),.01)
    def test_flash_resume_never_undoes_loader_quiet(self):
        c=self.setup();c.ln_loader_quiet();c.ln_control(2);c.ln_control(3)
        self.assertEqual(c.ln_flash_state(0),1)
        self.assertEqual(c.ln_flash_begin(),0)
    def test_duplicate_pause_resume_preserves_current_stream(self):
        c=self.setup();c.ln_control(2);c.ln_control(2);c.ln_control(3)
        n=c.ln_stat(14);c.ln_control(3);self.assertEqual(c.ln_stat(14),n)
    def test_stuck_master_dma_is_rearmed_and_audio_returns(self):
        c=self.setup();x=tone(64*1200);self.run_blocks(x[:64*700]);ok=c.ln_stat(0)
        c.ln_stick_master();self.run_blocks(x[64*700:64*1000])
        self.assertGreater(c.ln_stat(14),0)
        self.assertGreater(c.ln_stat(0),ok+100)
        self.assertEqual(c.ln_remote(),1)
        y=self.run_blocks(x[64*1000:]);self.assertGreater(max(map(abs,y)),.01)
    def test_long_link_loss_never_enables_expensive_local_fallback(self):
        c=self.setup();x=tone(64*1800);self.run_blocks(x[:64*700])
        c.ln_set(0,0,0);y=self.run_blocks(x[64*700:64*1500])
        self.assertEqual(c.ln_remote(),1)
        self.assertLess(max(map(abs,y[-64*100:])),1e-4)
        c.ln_set(0,1,0);y=self.run_blocks(x[64*1500:])
        self.assertGreater(max(map(abs,y[-64*100:])),.01)

    def test_upload_scratch_never_changes_mirrored_coefficients(self):
        c=self.setup();self.run_blocks([],blocks=700);version=c.ln_stat(6)
        c.ln_upload_busy(1)
        h=cab(seed=19);self.assertEqual(c.ln_load(0,F64(*h[:64]) if len(h)==64 else (ct.c_float*len(h))(*h),len(h)),1)
        ok=c.ln_stat(0);self.run_blocks([],blocks=100)
        self.assertEqual(c.ln_stat(6),version)
        self.assertGreater(c.ln_stat(0),ok+80)
        c.ln_upload_busy(0);self.run_blocks([],blocks=500)
        self.assertNotEqual(c.ln_stat(6),version)
        self.assertEqual(c.ln_remote(),1)

    def test_bypassed_ir_upload_starts_exchange_before_chain_processing(self):
        c=self.setup();self.run_blocks([],blocks=700)
        self.assertEqual(c.ln_begin_without_ir(1),1)
        self.assertEqual(c.ln_end_without_ir(),0)  # exactly one DMA exchange
    def test_active_ir_still_waits_for_current_block_input(self):
        c=self.setup();self.run_blocks([],blocks=700)
        self.assertEqual(c.ln_begin_without_ir(0),0)
    def test_empty_chain_also_starts_exchange_early(self):
        c=self.setup();types=(ct.c_uint*6)(*([255]*6))
        self.assertEqual(c.ln_route(types),1);self.run_blocks([],blocks=700)
        self.assertEqual(c.ln_begin_without_ir(0),1)
        self.assertEqual(c.ln_end_without_ir(),0)

if __name__=='__main__':unittest.main()
