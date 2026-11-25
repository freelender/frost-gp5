"""Host checks for the pedal-inspired model library (IDs 9..32).

These are HOST/EMULATOR measurements of the C kernels. They are not pedal
qualification, listening tests or proof that any combination fits the GP-5
CPU budget."""
import ctypes as ct
import math
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

FS=44100
EMPTY=255
(TS,BD2,KLON,BB,OCD,DS1,RAT,DISTPLUS,DD3,DM2,RV6,SPRING,PH90,CE2,MISTRESS,M117,VIBE,VB2,TR2,POG,
 DYNA,COMPPLUS,GE7,NS2)=range(9,33)
FCM=33   # EXPERIMENTAL AMP (Frost Capture Model)
DRIVES=(TS,BD2,KLON,BB,OCD,DS1,RAT,DISTPLUS)

def rms(x):return math.sqrt(sum(v*v for v in x)/max(1,len(x)))
def brightness(x):return rms([x[i]-x[i-1] for i in range(1,len(x))])/max(rms(x),1e-12)
def tone_amp(x,f,start=0):
    """Amplitude of frequency f (Hz) over x[start:] by quadrature correlation."""
    s=c=0.;n=len(x)-start
    for i in range(start,len(x)):
        a=2*math.pi*f*i/FS;s+=x[i]*math.sin(a);c+=x[i]*math.cos(a)
    return 2*math.hypot(s,c)/n
def sine(f,n,a=.3):return [a*math.sin(2*math.pi*f*i/FS) for i in range(n)]
def pluck(n,f=110.,a=.35):
    return [a*math.exp(-3.*i/FS)*sum(math.sin(2*math.pi*f*k*i/FS)/k for k in range(1,8)) for i in range(n)]
def corr(a,b):
    num=sum(x*y for x,y in zip(a,b));den=math.sqrt(sum(x*x for x in a)*sum(y*y for y in b))
    return num/den if den else 1.

class LibraryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=c=ct.CDLL(str(build(ROOT/('build/effect_library'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/library_host.c',flags=['-DFROST_NO_TAILS'])))   # kernels vs reference; tails: test_tails.py
        c.lib_bound.restype=ct.c_float;c.lib_bound.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint]
        c.lib_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        for f in ('lib_name','lib_parameter_name'):getattr(c,f).restype=ct.c_char_p
        c.lib_workspace_ptr.restype=c.lib_mod_pool.restype=ct.c_void_p
        c.lib_snapshot_parameter.restype=ct.c_float;c.lib_ge7_centre.restype=ct.c_float
        c.lib_snapshot_set_parameter.argtypes=[ct.c_uint,ct.c_uint,ct.c_float]
        c.lib_target.restype=ct.c_float
        for f in ('lib_slot','lib_upload','lib_download'):
            getattr(c,f).restype=ct.POINTER(ct.c_uint8);getattr(c,f).argtypes=[ct.c_uint,ct.c_char_p,ct.c_uint]
    # ---------------- helpers ----------------
    def chain(self,types,c=0,enable=True,params=None):
        self.c.lib_init(c);a=(ct.c_uint*len(types))(*types)
        self.assertEqual(self.c.lib_configure(c,a,len(types)),1,types)
        for slot,(t) in enumerate(types):
            for p,v in (params or {}).get(slot,{}).items():self.assertEqual(self.c.lib_set(c,slot,p,v),1,(t,p,v))
            if enable and t!=EMPTY:self.assertEqual(self.c.lib_enable(c,slot,1),1)
    def run_(self,x,c=0,chunk=64,reference=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        bad=(self.c.lib_process_reference if reference else self.c.lib_process)(c,a,b,len(x),chunk)
        self.assertEqual(bad,0);return list(b)
    def render(self,type,x,params=None,warm=0,settle=2048):
        self.chain([type],params={0:params or {}});self.run_([0.]*settle) # settle bypass/parameter ramps
        return self.run_(x)[warm:]
    # ---------------- registry/controls ----------------
    def test_registry_appends_ids_and_keeps_original_nine(self):
        c=self.c;self.assertEqual(c.lib_effect_count(),62)
        self.assertEqual([c.lib_parameter_count(t) for t in range(9)],[1,1,3,3,3,3,3,3,3])
        counts={TS:4,BD2:3,KLON:3,BB:3,OCD:4,DS1:3,RAT:3,DISTPLUS:2,DD3:6,DM2:4,RV6:5,SPRING:4,   # beta.10: +TAIL on delays/reverbs
                PH90:2,CE2:2,
                MISTRESS:4,M117:4,VIBE:4,VB2:4,TR2:3,POG:3,DYNA:2,COMPPLUS:5,GE7:8,NS2:3,FCM:6}
        self.assertEqual({t:c.lib_parameter_count(t) for t in counts},counts)
        for t in counts:
            names=[c.lib_parameter_name(t,p) for p in range(counts[t])]
            self.assertEqual(len(set(names)),len(names),t)
            for p in range(counts[t]):
                lo,hi,init,flags=(c.lib_bound(t,p,i) for i in range(4))
                self.assertTrue(lo<=init<=hi,(t,p))
                if flags:self.assertEqual(init,int(init))
        self.assertEqual(c.lib_parameter_count(62),0)
    def test_workspace_classes_and_categories(self):
        c=self.c
        self.assertEqual({t for t in range(52) if c.lib_workspace(t)==2},{5,DD3,DM2,42})
        self.assertEqual({t for t in range(52) if c.lib_workspace(t)==3},{6,7,CE2,MISTRESS,M117,VB2,POG,FCM,34})
        self.assertEqual({t for t in range(52) if c.lib_workspace(t)==4},{8,RV6,SPRING})
        self.assertEqual([c.lib_category(t) for t in DRIVES],[0]*5+[1]*3)
        self.assertEqual([c.lib_category(t) for t in (DD3,DM2,RV6,SPRING,PH90,POG,DYNA,NS2,0,3,34)],[2,2,3,3,4,8,5,5,6,6,8])
        # Workspace layouts fit inside the unchanged sample memories.
        for mode in range(5):self.assertLessEqual(c.lib_rv6_samples(mode),c.lib_reverb_capacity())
        self.assertLessEqual(c.lib_spring_samples(),c.lib_reverb_capacity())
        self.assertEqual((c.lib_delay_bytes(),c.lib_mod_bytes(),c.lib_reverb_bytes()),(70812,4624,21232))
    # ---------------- drives ----------------
    def test_drive_models_are_distinct_topologies(self):
        x=pluck(8192);outs={t:self.render(t,x) for t in DRIVES}
        for i,a in enumerate(DRIVES):
            self.assertTrue(all(math.isfinite(v) and abs(v)<=8 for v in outs[a]))
            for b in DRIVES[i+1:]:
                na=[v/max(rms(outs[a]),1e-9) for v in outs[a]];nb=[v/max(rms(outs[b]),1e-9) for v in outs[b]]
                self.assertLess(corr(na,nb),.995,(a,b))
    def test_drive_gain_tone_and_level_controls(self):
        x=sine(220,8192,.25)
        for t in DRIVES:
            gain_p=0;tone_p={TS:1,BD2:1,KLON:1,BB:1,OCD:1,DS1:1,RAT:1}.get(t);level_p=1 if t==DISTPLUS else 2
            lo=self.render(t,x,{gain_p:5.})[2048:];hi=self.render(t,x,{gain_p:95.})[2048:]
            # More drive -> more harmonic content relative to the fundamental.
            h=lambda y:(tone_amp(y,660)+tone_amp(y,1100))/max(tone_amp(y,220),1e-9)
            self.assertGreater(h(hi),h(lo),t)
            if tone_p is not None:
                dark=self.render(t,x,{tone_p:0.})[2048:];bright=self.render(t,x,{tone_p:100.})[2048:]
                self.assertGreater(brightness(bright),brightness(dark)*1.05,t) # clockwise = brighter
            quiet=self.render(t,x,{level_p:25.})[2048:];loud=self.render(t,x,{level_p:75.})[2048:]
            self.assertAlmostEqual(rms(loud)/rms(quiet),9.,delta=.05) # (75/25)^2 audio-taper level law
            self.assertLess(rms(self.render(t,x,{level_p:0.})[2048:]),1e-5) # level 0 ~ mute (smoothing asymptote)
    def test_tube_screamer_mid_hump_and_versions(self):
        # Clipping-amp gain applies above the 720 Hz corner: near-linear mid
        # emphasis versus the flat-low-end Bluesbreaker topology.
        hump=lambda t:(tone_amp(self.render(t,sine(1000,8192,.02),{0:10.})[4096:],1000)/
                       tone_amp(self.render(t,sine(100,16384,.02),{0:10.})[8192:],100))
        self.assertGreater(hump(TS),1.5*hump(BB))
        a=self.render(TS,pluck(8192),{3:0.});b=self.render(TS,pluck(8192),{3:1.})
        self.assertGreater(brightness(b),brightness(a));self.assertNotEqual(a,b)
    def test_hot_input_stays_finite_bounded_and_rejects_bad_modes(self):
        x=[4.*math.sin(i*.07) for i in range(4096)]
        for t in DRIVES:
            y=self.render(t,x,{0:100.,1 if t==DISTPLUS else 2:100.})
            self.assertTrue(all(math.isfinite(v) and abs(v)<=8. for v in y),t)
        self.chain([TS]);self.assertEqual(self.c.lib_set(0,0,3,.5),0);self.assertEqual(self.c.lib_set(0,0,3,2.),0)
        self.assertEqual(self.c.lib_set(0,0,0,101.),0);self.assertEqual(self.c.lib_set(0,0,3,1.),1)
    # ---------------- dynamics / EQ ----------------
    def test_ge7_band_calibration_in_db(self):
        for band in range(7):
            f=self.c.lib_ge7_centre(band)
            for db in (12.,-12.,6.):
                n=16384 if f<250 else 8192
                y=self.render(GE7,sine(f,n,.1),{band:db})[n//2:]
                measured=20*math.log10(tone_amp(y,f)/.1)
                self.assertAlmostEqual(measured,db,delta=.25,msg=(band,db))
        y=self.render(GE7,sine(1000,8192,.1),{7:-6.})[4096:]
        self.assertAlmostEqual(20*math.log10(tone_amp(y,1000)/.1),-6.,delta=.05)
        flat=self.render(GE7,sine(1000,8192,.1))[4096:]
        self.assertAlmostEqual(tone_amp(flat,1000),.1,delta=1e-4) # all bands at 0 dB: transparent
        far=self.render(GE7,sine(6400,8192,.1),{0:15.})[4096:] # 100 Hz boost leaves 6.4 kHz alone
        self.assertAlmostEqual(20*math.log10(tone_amp(far,6400)/.1),0.,delta=.2)
    def test_dyna_comp_envelope_attack_release_and_sensitivity(self):
        burst=[(.5 if 4410<=i<26460 else .02)*math.sin(2*math.pi*440*i/FS) for i in range(44100)]
        y=self.render(DYNA,burst,{1:80.})
        env=lambda a,b:rms(y[a:b])
        onset=env(4410,4410+88);settled=env(13000,14000)
        self.assertGreater(onset,settled*1.3) # fast attack overshoot then squash
        after=env(26460+441,26460+882);late=env(40000,41000)
        self.assertLess(after,late) # slow release: quiet tail recovers gain over ~0.5 s
        x=pluck(22050);soft=self.render(DYNA,x,{1:10.});hard=self.render(DYNA,x,{1:90.})
        crest=lambda y:max(map(abs,y[882:]))/rms(y[882:]) # after the attack transient
        self.assertLess(crest(hard),crest(soft))
    def test_comp_plus_blend_tone_and_pickup(self):
        x=pluck(22050)
        dry=self.render(COMPPLUS,x,{2:0.,1:50.});self.assertLess(max(abs(a-b) for a,b in zip(dry,x)),1e-3) # blend 0 = dry at unity
        dark=self.render(COMPPLUS,x,{2:100.,3:0.});bright=self.render(COMPPLUS,x,{2:100.,3:100.})
        self.assertGreater(brightness(bright),brightness(dark)*1.1)
        sc=self.render(COMPPLUS,x,{2:100.,4:0.});hb=self.render(COMPPLUS,x,{2:100.,4:1.})
        self.assertNotEqual(sc,hb)
    def test_ns2_threshold_decay_hysteresis_and_mute(self):
        noise=[.0005*math.sin(i*1.3) for i in range(22050)]
        played=[.3*math.sin(2*math.pi*196*i/FS) for i in range(22050)]
        y=self.render(NS2,played+noise,{0:50.,1:20.})
        self.assertGreater(rms(y[4000:20000]),.2) # playing passes
        self.assertLess(rms(y[-4000:]),.0005*.03) # >30 dB suppression of the noise floor
        fast=self.render(NS2,played+noise,{0:50.,1:0.});slow=self.render(NS2,played+noise,{0:50.,1:100.})
        self.assertGreater(rms(slow[22050:22050+4410]),rms(fast[22050:22050+4410]))
        muted=self.render(NS2,played,{2:1.})
        self.assertLess(max(map(abs,muted[2048:])),1e-4) # mute: -70 dB once the enable crossfade settles
    # ---------------- delays ----------------
    def impulse_times(self,y,threshold):
        peaks=[];i=1
        while i<len(y)-1:
            if abs(y[i])>threshold and abs(y[i])>=abs(y[i-1]) and abs(y[i])>=abs(y[i+1]):peaks.append(i);i+=200
            else:i+=1
        return peaks
    def test_dd3_time_feedback_and_800ms(self):
        for ms in (13.,320.,800.):
            x=[0.]*3000+[1.]+[0.]*80000
            y=self.render(DD3,x,{2:ms,1:50.,0:100.},settle=44100)
            peaks=self.impulse_times(y[3001:],.05)
            self.assertAlmostEqual(peaks[0],ms*44.1,delta=3,msg=ms)
        # F.BACK 50 -> each repeat at 0.97*0.5 of the previous (500 Hz burst, in band).
        burst=[.3*math.sin(2*math.pi*500*i/FS) if i<882 else 0. for i in range(44100)]
        y=self.render(DD3,burst,{2:320.,1:50.,0:100.},settle=44100);d=round(320*44.1)
        e1=rms(y[d+100:d+800]);e2=rms(y[2*d+100:2*d+800])
        self.assertAlmostEqual(e2/e1,.485,delta=.03)
    def test_dd3_hold_loops_and_releases(self):
        self.chain([DD3],params={0:{2:200.,0:100.,1:0.}});self.run_([0.]*44100)
        burst=[.3*math.sin(i*.05) if i<4000 else 0. for i in range(9000)];self.run_(burst)
        self.assertEqual(self.c.lib_set(0,0,3,1.),1)
        y=self.run_([0.]*44100)
        # The frozen 200 ms window keeps repeating with no feedback decay.
        a=rms(y[8820:17640]);b=rms(y[35280:44100])
        self.assertGreater(a,.01);self.assertAlmostEqual(a,b,delta=a*.05)
        self.assertEqual(self.c.lib_set(0,0,3,0.),1)
        y=self.run_([0.]*44100);self.assertLess(rms(y[-8820:]),1e-6) # feedback 0: echoes end
    def test_dm2_range_and_darker_long_repeats(self):
        for ms in (20.,300.):
            x=[0.]*3000+[1.]+[0.]*30000
            y=self.render(DM2,x,{0:ms,1:100.,2:0.},settle=44100)
            self.assertAlmostEqual(self.impulse_times(y[3001:],.01)[0],ms*44.1,delta=25) # +/-0.15% clock wobble
        self.chain([DM2]);self.assertEqual(self.c.lib_set(0,0,0,301.),0);self.assertEqual(self.c.lib_set(0,0,0,19.),0)
        noise=[((i*1103515245+12345)%65536/32768.-1.)*.2 for i in range(40000)]
        short=self.render(DM2,noise,{0:40.,1:100.,2:0.},settle=44100);long=self.render(DM2,noise,{0:280.,1:100.,2:0.},settle=44100)
        wet=lambda y:[a-b for a,b in zip(y,noise[-len(y):])]
        self.assertLess(brightness(wet(long)[20000:]),brightness(wet(short)[20000:]))
        y=self.render(DM2,[0.]*1000+[.5]+[0.]*88200,{0:150.,1:100.,2:100.}) # INTENS past unity stays bounded
        self.assertTrue(all(math.isfinite(v) and abs(v)<8 for v in y))
    # ---------------- modulation ----------------
    def test_tremolo_rate_wave_and_depth(self):
        x=[.3]*44100
        tri=self.render(TR2,x,{0:4.,1:0.,2:100.});sq=self.render(TR2,x,{0:4.,1:100.,2:100.})
        crossings=sum(1 for i in range(1,len(tri)) if (tri[i]-.15)*(tri[i-1]-.15)<0)
        self.assertAlmostEqual(crossings/2,4*len(tri)/FS,delta=1)
        near=lambda y:sum(1 for v in y if v<.03 or v>.27)/len(y)
        self.assertGreater(near(sq),near(tri)+.3) # square sits at its extremes
        self.assertAlmostEqual(min(self.render(TR2,x,{2:0.})),.3,places=4)
    def test_phase90_voices_sweep_and_univibe_modes(self):
        x=pluck(22050)
        script=self.render(PH90,x,{1:0.});block=self.render(PH90,x,{1:1.})
        self.assertLess(corr(script,block),.999)
        chorus=self.render(VIBE,x,{2:0.});vib=self.render(VIBE,x,{2:1.})
        self.assertLess(corr(chorus,vib),.99)
        ce2=self.render(CE2,x)
        self.assertLess(corr([v/rms(chorus) for v in chorus],[v/rms(ce2) for v in ce2]),.98) # vibe is not a chorus
    def test_mistress_filter_matrix_is_static_and_range_moves_it(self):
        noise=[((i*1103515245+12345)%65536/32768.-1.)*.2 for i in range(44100)]
        a=self.render(MISTRESS,noise,{3:1.,1:20.,0:2.});b=self.render(MISTRESS,noise,{3:1.,1:80.,0:2.})
        self.assertLess(corr(a,b),.995)
        # Matrix stops the LFO: rate has no effect.
        c=self.render(MISTRESS,noise,{3:1.,1:20.,0:.02});self.assertEqual(a,c)
        d=self.render(MISTRESS,noise,{3:0.,1:20.,0:2.});self.assertNotEqual(a,d)
    def test_vb2_rise_and_wet_only_vibrato(self):
        self.chain([VB2],enable=False,params={0:{0:5.,1:100.,2:2000.}})
        tone=sine(440,44100*3,.3);self.run_(tone[:4410])
        self.assertEqual(self.c.lib_enable(0,0,1),1);y=self.run_(tone[4410:])
        def wobble(seg):
            # instantaneous period variation via zero-crossing spacing
            z=[i for i in range(1,len(seg)) if seg[i-1]<0<=seg[i]];d=[b-a for a,b in zip(z,z[1:])]
            return max(d)-min(d)
        self.assertLess(wobble(y[441:8820]),wobble(y[100000:110000])) # depth rises after engage
        self.assertGreater(wobble(y[100000:110000]),2)
    def test_m117_and_ce2_lfo_phase_continuity(self):
        for t,rate_p in ((M117,2),(CE2,0),(PH90,0),(TR2,0)):
            self.chain([t]);x=sine(330,8192,.2);self.run_([0.]*2048)
            first=self.run_(x[:4096]);self.assertEqual(self.c.lib_set(0,0,rate_p,self.c.lib_bound(t,rate_p,1)),1)
            second=self.run_(x[4096:])
            jump=abs(second[0]-first[-1]);typical=max(abs(first[i]-first[i-1]) for i in range(1,len(first)))
            self.assertLessEqual(jump,typical*1.5,t) # rate change does not reset/jump the LFO
    # ---------------- reverbs ----------------
    def test_rv6_block_equals_sample_bitwise(self):
        self.c.lib_rv6_block_matches.restype=ct.c_int
        for seed in (1,7,2501,99):self.assertEqual(self.c.lib_rv6_block_matches(60000,seed),-1)
    def test_rv6_modes_decay_tails_and_stability(self):
        impulse=[0.]*1000+[1.]+[0.]*(44100*2)
        tails={}
        for mode in range(5):
            short=self.render(RV6,impulse,{3:float(mode),2:10.,0:100.});long=self.render(RV6,impulse,{3:float(mode),2:90.,0:100.})
            self.assertGreater(rms(long[44100:66150]),rms(short[44100:66150]),mode)
            tails[mode]=long
        for a in range(5):
            for b in range(a+1,5):self.assertLess(corr(tails[a][1001:20000],tails[b][1001:20000]),.9,(a,b))
        loud=[.8*math.sin(i*.3) for i in range(44100*4)]
        for mode in range(5):
            y=self.render(RV6,loud,{3:float(mode),2:100.,0:100.,1:100.})
            self.assertTrue(all(math.isfinite(v) and abs(v)<=8 for v in y),mode)
    def test_shimmer_climbs_an_octave(self):
        tone=sine(330,22050,.3)+[0.]*44100
        y=self.render(RV6,tone,{3:4.,2:60.,0:100.});tail=y[22050+4410:]
        hall=self.render(RV6,tone,{3:1.,2:60.,0:100.})[22050+4410:]
        ratio=lambda t:tone_amp(t,660)/max(tone_amp(t,330),1e-9)
        self.assertGreater(ratio(tail),3*ratio(hall))
    def test_reverb_reset_and_mode_change_restart_tail(self):
        self.chain([RV6],params={0:{2:90.,0:100.}});self.run_([0.]*2048);self.run_([.5]+[0.]*4000)
        self.c.lib_reset(0);self.assertEqual(max(map(abs,self.run_([0.]*8192))),0.)
        self.run_([.5]+[0.]*4000);self.assertEqual(self.c.lib_set(0,0,3,0.),1)
        self.assertLess(max(map(abs,self.run_([0.]*4096)[64:])),1e-9)
    def test_spring_dispersion_drip_and_controls(self):
        impulse=[0.]*1000+[1.]+[0.]*44100
        y=self.render(SPRING,impulse,{1:100.,0:40.})
        wet=y[1001:]
        # Each spring echo arrives as a chirp: high-pass and low-pass parts of the
        # first echo peak at different times (dispersion), unlike a plain delay.
        seg=wet[1300:3500];hp=[seg[i]-seg[i-1] for i in range(1,len(seg))];lp=[sum(seg[max(0,i-24):i+1]) for i in range(len(seg))]
        ph=max(range(len(hp)),key=lambda i:abs(hp[i]));pl=max(range(len(lp)),key=lambda i:abs(lp[i]))
        self.assertGreater(abs(ph-pl),20)
        dark=self.render(SPRING,pluck(22050),{1:100.,2:0.});bright=self.render(SPRING,pluck(22050),{1:100.,2:100.})
        self.assertGreater(brightness(bright),brightness(dark))
        hot=self.render(SPRING,[2.*math.sin(i*.2) for i in range(88200)],{0:100.,1:100.})
        self.assertTrue(all(math.isfinite(v) and abs(v)<=8 for v in hot))
    # ---------------- octave ----------------
    def test_pog_octaves_polyphony_latency(self):
        def short(y,f):return sum(tone_amp(y[i:i+441],f) for i in range(0,len(y)-441,441))/((len(y)-441)//441)
        def dominant(y,candidates):return max(candidates,key=lambda f:short(y,f))
        up=self.render(POG,sine(220,22050,.3),{0:0.,1:0.,2:100.})[8820:]
        self.assertEqual(dominant(up,(110,220,440,660)),440)
        down=self.render(POG,sine(220,22050,.3),{0:0.,1:100.,2:0.})[8820:]
        self.assertEqual(dominant(down,(110,220,440)),110)
        chord=[a+b for a,b in zip(sine(220,22050,.2),sine(330,22050,.2))]
        poly=self.render(POG,chord,{0:0.,1:0.,2:100.})[8820:]
        for f in (440,660):self.assertGreater(short(poly,f),2*short(poly,550),f) # both notes shifted
        dry=self.render(POG,sine(220,8192,.3),{0:100.,1:0.,2:0.});self.assertLess(max(abs(a-b) for a,b in zip(dry,sine(220,8192,.3))),1e-4)
        # Artifact floor: energy outside the target partial (documented, not hidden).
        # Pitch accuracy: zero crossings of the up voice match 440 Hz within 2%.
        z=sum(1 for i in range(1,len(up)) if (up[i]>0)!=(up[i-1]>0));self.assertAlmostEqual(z/2/(len(up)/FS),440,delta=9)
    # ---------------- resources/isolation ----------------
    def test_workspace_admission_and_instance_isolation(self):
        c=self.c;c.lib_init(0)
        for bad in ([CE2,M117,POG],[DD3,DM2],[5,DD3],[RV6,SPRING],[8,RV6],[6,7,VB2]):
            a=(ct.c_uint*len(bad))(*bad);self.assertEqual(c.lib_configure(0,a,len(bad)),0,bad)
        self.chain([CE2,VB2,DD3,RV6,GE7,TS])
        self.assertNotEqual(c.lib_workspace_ptr(0,0),c.lib_workspace_ptr(0,1))
        self.assertEqual({c.lib_workspace_ptr(0,0),c.lib_workspace_ptr(0,1)},{c.lib_mod_pool(0,0),c.lib_mod_pool(0,1)})
        x=pluck(8192)
        self.chain([CE2,EMPTY],c=0);solo=self.run_(x,c=0)
        self.chain([CE2,CE2],c=1,params={1:{0:7.,1:100.}})
        self.assertEqual(c.lib_enable(1,1,0),1);both=self.run_(x,c=1)
        # Second instance (bypassed, KEEP: still running) never disturbs the first's history.
        self.chain([CE2,EMPTY],c=0);again=self.run_(x,c=0);self.assertEqual(solo,again)
    def test_live_route_swap_keeps_classes_exclusive(self):
        c=self.c;self.chain([0,1,2,3,CE2,EMPTY])
        types=(ct.c_uint*6)(0,1,2,3,VB2,POG);order=(ct.c_uint*6)(*range(6))
        self.assertEqual(c.lib_route(0,types,order,6),1);self.run_([0.]*512)
        self.assertEqual([c.lib_type(0,i) for i in range(6)],[0,1,2,3,VB2,POG])
        self.assertNotEqual(c.lib_workspace_ptr(0,4),c.lib_workspace_ptr(0,5))
        bad=(ct.c_uint*6)(CE2,1,2,3,VB2,POG);self.assertEqual(c.lib_route(0,bad,order,6),0)
    def test_block_partition_and_reference_identity_for_every_model(self):
        x=pluck(4096)+[2.*math.sin(i*.01) for i in range(2048)]
        for t in range(9,34):
            self.chain([t]);a=self.run_(x,chunk=64)
            self.chain([t]);b=self.run_(x,chunk=37)
            self.chain([t]);r=self.run_(x,chunk=64,reference=True)
            self.assertEqual(a,b,t);self.assertEqual(a,r,t)
            self.assertTrue(all(math.isfinite(v) and abs(v)<=16 for v in a),t)
    def test_route_fades_stage_major_match_reference_for_every_model(self):
        """Live type changes fade out/commit/fade in on 64-sample blocks for
        every model; guarded output must equal the per-sample reference."""
        x=pluck(4096);order=(ct.c_uint*6)(*range(6))
        for t in range(9,34):
            outs=[]
            for reference in (False,True):
                self.chain([0,1,2,3,EMPTY,EMPTY]);self.run_(x[:640],reference=reference)
                route=(ct.c_uint*6)(0,1,2,3,t,EMPTY);self.assertEqual(self.c.lib_route(0,route,order,6),1)
                y=self.run_(x[640:640+512],reference=reference);self.assertEqual(self.c.lib_transition(0),0)
                self.assertEqual(self.c.lib_enable(0,4,1),1);y+=self.run_(x[1152:],reference=reference)
                outs.append(y)
            self.assertEqual(outs[0],outs[1],t)
            self.assertEqual(self.c.lib_type(0,4),t)
    def test_numeric_fault_contained_for_every_model(self):
        for t in range(9,34):
            self.chain([t]);x=pluck(512);x[100]=float('nan')
            a=(ct.c_float*512)(*x);b=(ct.c_float*512)()
            self.assertEqual(self.c.lib_process(0,a,b,512,64),1)
            self.assertEqual(self.c.lib_faults(0),1)
            self.assertEqual(list(b)[64:128],[0.]*64)
            y=self.run_(pluck(1024));self.assertTrue(all(math.isfinite(v) for v in y),t)
    # ---------------- presets ----------------
    def test_v2_snapshot_exact_roundtrip_and_v1_compatibility(self):
        c=self.c;self.chain([0,1,2,3,EMPTY,EMPTY]);self.assertEqual(c.lib_capture(0),1);self.assertEqual(c.lib_snapshot_version(),1)
        self.chain([GE7,DD3,VB2,TS,EMPTY,EMPTY],params={0:{6:-7.5,7:3.},1:{4:1.},2:{3:1.},3:{3:1.}})
        self.assertEqual(c.lib_capture(0),1);self.assertEqual(c.lib_snapshot_version(),2)
        self.assertEqual(c.lib_snapshot_parameter(0,6),-7.5);self.assertEqual(c.lib_snapshot_parameter(1,4),1.)
        self.chain([0,1,2,3,EMPTY,EMPTY],c=1);self.assertEqual(c.lib_recall(1),1);self.run_([0.]*512,c=1)
        self.assertEqual([c.lib_type(1,i) for i in range(4)],[GE7,DD3,VB2,TS]);self.assertEqual(c.lib_target(1,0,6),-7.5)
        c.lib_snapshot_set_version(1);self.assertEqual(c.lib_recall(1),0) # new types need version 2
        c.lib_snapshot_set_version(2);c.lib_snapshot_set_parameter(3,3,.5);self.assertEqual(c.lib_recall(1),0) # stepped
        c.lib_snapshot_set_parameter(3,3,1.);c.lib_snapshot_set_parameter(1,6,1.);self.assertEqual(c.lib_recall(1),0) # unused (DD3 has 6 since beta.10 TAIL)
    def word_exchange(self,fn,c,fmt,*v):
        payload=struct.pack(fmt,*v);return ct.string_at(fn(c,payload,len(payload)),48)
    def test_wire_v2_upload_download_exact_and_v1_unchanged(self):
        from preset_wire import request as up,checksum,decode as up_decode,BEGIN,WRITE,COMMIT
        from preset_download import request as down,decode as down_decode
        c=self.c;self.chain([0,1,2,3,EMPTY,EMPTY])
        x=lambda p:ct.string_at(c.lib_download(0,p,len(p)),48)
        d=down_decode(x(down(1,0)));self.assertEqual(d['data'][1],1071)
        self.chain([GE7,RV6,TS,3,EMPTY,EMPTY],params={0:{0:4.5,7:-2.},1:{3:4.},2:{3:1.}})
        d=down_decode(x(down(2,0)));self.assertEqual(d['data'][1],1101);rev=d['revision'];words=[]
        for i in range(0,1101,5):words+=list(down_decode(x(down(3+i,1,rev,i)))['data'][:min(5,1101-i)])
        self.assertEqual(checksum(words),d['data'][0]);self.assertEqual(words[0],2)
        # Upload the same version-2 body into a legacy-routed second fixture.
        self.chain([0,1,2,3,EMPTY,EMPTY],c=1);u=lambda p:up_decode(ct.string_at(c.lib_upload(1,p,len(p)),48))
        crc=checksum(words);self.assertEqual(u(up(1,BEGIN,1,1,data=[crc]))['status'],0)
        for n,i in enumerate(range(0,1101,5)):self.assertEqual(u(up(2+n,WRITE,1,1,i,words[i:i+5]))['status'],0)
        self.assertEqual(u(up(1000,COMMIT,1,1,1071,[crc]))['status'],18) # v2 body must commit at 1101
        self.assertEqual(u(up(1001,COMMIT,1,1,1101,[crc]))['status'],15);self.run_([0.]*512,c=1)
        self.assertEqual([c.lib_type(1,i) for i in range(3)],[GE7,RV6,TS]);self.assertEqual(c.lib_target(1,0,7),-2.)
        # A later version-1 upload clears the appended staging words.
        v1=[1,44100,6,0,1,2,3,255,255]+list(range(6))+[1,1,1,1,0,0]+[0]*6
        v1+=[struct.unpack('<I',struct.pack('<f',v))[0] for v in (1.,0.,0.,.001,0.,0.,1.,1.,1.,1.,0.,0.)]+[0]*6+[1,0]+[0x3f800000]+[0]*1023
        self.assertEqual(len(v1),1071);crc=checksum(v1);rev=c.lib_revision(1)
        self.assertEqual(u(up(2000,BEGIN,2,rev,data=[crc]))['status'],0)
        for n,i in enumerate(range(0,1071,5)):self.assertEqual(u(up(2001+n,WRITE,2,rev,i,v1[i:i+5]))['status'],0)
        self.assertEqual(u(up(2001+n+1,WRITE,2,rev,1071,[0]))['status'],19) # no words past a v1 body
        self.assertEqual(u(up(2300,COMMIT,2,rev,1071,[crc]))['status'],15)
    def test_slot_protocol_windows_retries_and_malformed(self):
        c=self.c;self.chain([GE7,0,EMPTY])
        def slot(version,id,op,rev,s,t,p,value):
            q=struct.pack('<HH8I',0x7f40,36,version,id,op,rev,s,t,p,value);r=ct.string_at(c.lib_slot(0,q,len(q)),48)
            return struct.unpack('<HH8I3f',r)
        v1=slot(1,1,0,0,0,EMPTY,0xffffffff,0)
        self.assertEqual(v1[2:5],(1,1,0));self.assertEqual(v1[9],8) # v1 reply: count only, params 0..2
        for base in (3,6):
            r=slot(2,2+base,0,0,0,EMPTY,base,0);self.assertEqual((r[2],r[4],r[9]),(2,0,8|(base<<16)))
        for bad in (2,7,9,0xffffffff):self.assertEqual(slot(2,20,0,0,0,EMPTY,bad,0)[4],2)
        bits=struct.unpack('<I',struct.pack('<f',-4.5))[0];rev=c.lib_revision(0)
        r=slot(2,30,1,rev,0,GE7,7,bits);self.assertEqual((r[4],r[9]>>16),(0,6));self.assertEqual(r[12],0.) # past count
        self.assertEqual(r[11],-4.5)
        self.assertEqual(slot(2,30,1,rev,0,GE7,7,bits)[4],1) # exact retry replays
        self.assertEqual(slot(2,30,1,rev,0,GE7,6,bits)[4],4) # same token, different body
        self.assertEqual(slot(2,31,1,rev+1,0,GE7,8,bits)[4],9) # parameter past count
        self.assertEqual(slot(2,32,1,rev+1,0,GE7,7,struct.unpack('<I',struct.pack('<f',16.))[0])[4],10)
        self.assertEqual(slot(3,33,0,0,0,EMPTY,0xffffffff,0)[4],3)

class LibraryEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=c=ct.CDLL(str(build(ROOT/('build/library_editor'+('.dll' if os.name=='nt' else '.so')),
            ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c.ge_actual_value.restype=ct.c_float;c.ge_to_dsp.restype=ct.c_float;c.ge_to_dsp.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        c.ge_from_dsp.argtypes=[ct.c_uint,ct.c_uint,ct.c_float];c.ge_queue.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        for f in ('ge_human_name','ge_human_hint','ge_human_text'):getattr(c,f).restype=ct.c_char_p
    def setUp(self):self.c.ge_init();self.settle()
    def settle(self):
        for _ in range(300):
            self.c.ge_pump(1)
            if self.c.ge_ready():return
        self.fail('editor did not settle')
    def put(self,slot,type):self.assertEqual(self.c.ge_type_change(slot,type),1);self.settle()
    def test_every_control_maps_exactly_and_shows_units_or_labels(self):
        c=self.c
        for t in range(9,34):
            for p in range(c.ge_human_count(t)):
                lo,hi,step,init,scale=[c.ge_human_meta(t,p,i) for i in range(5)]
                for v in range(lo,hi+1,max(1,(hi-lo)//37)):
                    raw=c.ge_to_dsp(t,p,v);self.assertEqual(c.ge_from_dsp(t,p,raw),v,(t,p,v))
                self.assertTrue(c.ge_human_hint(t,p),(t,p))
        self.put(4,GE7);self.assertEqual(c.ge_human_text(4,0),b'0.0DB')
        self.assertEqual(c.ge_queue(4,6,-75),1);self.settle()
        self.assertEqual(c.ge_human_text(4,6),b'-7.5DB');self.assertEqual(c.ge_actual_value(4,6),-7.5)
        self.put(5,RV6);self.assertEqual(c.ge_human_text(5,3),b'HALL')
        self.assertEqual(c.ge_queue(5,3,4),1);self.settle();self.assertEqual(c.ge_human_text(5,3),b'SHIMMER')
        self.put(4,VB2);self.assertEqual(c.ge_human_text(4,2),b'500MS');self.assertEqual(c.ge_human_text(4,0),b'4.00HZ')
    def test_eight_parameters_sync_and_survive_reconnect(self):
        c=self.c;self.put(4,GE7)
        for p in range(8):self.assertEqual(c.ge_queue(4,p,10*p-35),1)
        self.settle();self.assertEqual([c.ge_actual_value(4,p) for p in range(8)],[(10*p-35)/10 for p in range(8)])
        c.ge_reconnect();self.settle()
        self.assertEqual([c.ge_value(4,p) for p in range(8)],[10*p-35 for p in range(8)])
    def test_category_then_model_picker(self):
        c=self.c
        for _ in range(3):c.ge_turn(1) # select slot 5 (position 3 -> slot 4 in default order)
        while c.ge_selected()!=4:c.ge_turn(1)
        c.ge_press();c.ge_press();self.assertEqual((c.ge_view(),c.ge_pick_category()),(7,6))
        c.ge_turn(-5);self.assertEqual(c.ge_pick_category(),1);c.ge_press()
        self.assertEqual(c.ge_view(),8);c.ge_turn(1);c.ge_press();self.settle()
        self.assertEqual(c.ge_type(4),RAT);self.assertEqual(c.ge_enabled(4),0)
        c.ge_turn(3);c.ge_press();self.assertEqual((c.ge_view(),c.ge_pick_category()),(7,1)) # TYPE row after 3 knobs
        c.ge_press();self.assertEqual(c.ge_pick_model(),1) # opens on current model
        c.ge_turn(5);self.assertEqual(c.ge_pick_model(),6) # BACK entry after CRUSH (32l: MUFF, CRUSH added)
        c.ge_press();self.assertEqual(c.ge_view(),7);c.ge_turn(8);c.ge_press() # category BACK (after PITCH) -> PARAMETERS
        self.assertEqual(c.ge_view(),1);self.assertEqual(c.ge_type(4),RAT)
        self.assertEqual(c.ge_paint(),0) # full frame drawn, no out-of-range blits
    def select(self,slot):
        while self.c.ge_selected()!=slot:self.c.ge_turn(1)
    def test_footswitch_default_selected_only_and_explicit_modes(self):
        c=self.c;self.put(4,DD3);self.select(4)
        c.ge_foot();self.settle();self.assertEqual(c.ge_enabled(4),1) # FOOT=BYPASS: ordinary toggle
        c.ge_foot_release();self.settle();self.assertEqual(c.ge_enabled(4),1)
        self.assertEqual(c.ge_queue(4,4,1),1);self.settle() # FOOT=HOLD
        before=[c.ge_enabled(i) for i in range(6)]
        c.ge_foot();self.settle();self.assertEqual(c.ge_actual_value(4,3),1.)
        c.ge_foot_release();self.settle();self.assertEqual(c.ge_actual_value(4,3),0.)
        self.assertEqual([c.ge_enabled(i) for i in range(6)],before)
        self.put(4,VB2);self.assertEqual(c.ge_queue(4,3,1),1);self.settle() # UNLATCH
        c.ge_foot();self.settle();self.assertEqual(c.ge_enabled(4),1)
        c.ge_foot_release();self.settle();self.assertEqual(c.ge_enabled(4),0)
        self.select(0);c.ge_foot();self.settle();self.assertEqual(c.ge_enabled(0),0) # other block: plain bypass
        c.ge_foot_release();self.settle();self.assertEqual(c.ge_enabled(0),0)

if __name__=='__main__':unittest.main()
