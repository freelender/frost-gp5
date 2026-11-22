import ctypes as ct
import math
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class Snapshot(ct.Structure):
    _fields_=[('types',ct.c_uint8*6),('order',ct.c_uint8*6),('enabled',ct.c_uint8*6),('bypass',ct.c_uint8*6),
        ('parameters',(ct.c_float*8)*6),('ir_taps',ct.c_uint*2),('ir_coefficients',(ct.c_float*512)*2),
        ('pre_count',ct.c_uint),('version',ct.c_uint),('sample_rate',ct.c_uint)]

class NativePresetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/native_presets.dll' if os.name=='nt' else 'build/native_presets.so'),ROOT/'tests/native_preset_host.c')))
        c=cls.lib;c.pn_ptr.restype=ct.POINTER(Snapshot)
        for name in ('pn_current','pn_wet','fx_target','fx_ir_coefficient'):getattr(c,name).restype=ct.c_float
        c.fx_configure.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        c.fx_ir.argtypes=[ct.c_uint,ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
        c.fx_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.fx_inplace.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
        c.fx_amp_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint,ct.c_uint]
        c.fx_faults.restype=ct.c_uint
    def setUp(self):
        for c in (0,1):self.lib.pn_init(c)
    def array(self,v):return (ct.c_uint*len(v))(*v)
    def configure(self,c,types):self.assertEqual(self.lib.fx_configure(c,self.array(types),len(types)),1)
    def process(self,c,x,inplace=False,amp=None):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        if inplace:self.lib.fx_inplace(c,a,len(x));return list(a)
        if amp is not None:self.lib.fx_amp_process(c,a,b,len(x),amp)
        else:self.lib.fx_process(c,a,b,len(x))
        return list(b)
    def snapshot(self,c=0,index=0):
        self.assertEqual(self.lib.pn_capture(c,index),1);return self.lib.pn_ptr(index).contents
    def test_target_capture_is_canonical_and_contains_independent_ir_assets(self):
        c=self.lib;self.assertEqual(c.pn_bytes(),ct.sizeof(Snapshot));self.assertEqual(c.pn_bytes(),4332) # 4212 + 6 slots x 5 appended parameters x 4 bytes
        self.configure(0,[3,0,3,2]);c.fx_set(0,1,0,2.5);c.fx_enable(0,1,1)
        for slot,coeff in ((0,[.7,.2]),(2,[1.,-.25,.125])):
            self.assertEqual(c.fx_ir(0,slot,(ct.c_float*len(coeff))(*coeff),len(coeff)),1)
        p=self.snapshot();self.assertEqual(p.parameters[1][0],2.5) # unsmoothed target
        self.assertEqual(c.pn_current(0,1,0),1.);self.assertEqual(p.enabled[1],1)
        self.assertEqual(list(p.ir_taps),[2,3]);self.assertAlmostEqual(p.ir_coefficients[0][0],.7,places=6)
        self.assertEqual(p.ir_coefficients[1][1],-.25)
        self.assertTrue(all(p.ir_coefficients[0][j]==0. for j in range(2,512)))
        self.assertEqual((p.version,p.sample_rate,p.pre_count),(1,44100,4))
    def test_recall_commits_targets_policy_enables_only_at_silence_and_copies_input(self):
        c=self.lib;self.configure(0,[0]);c.fx_set(0,0,0,2.);c.fx_enable(0,0,1);self.process(0,[.1]*5000)
        p=self.snapshot();p.parameters[0][0]=.5;p.bypass[0]=1
        self.assertEqual(c.pn_request(0,0,1),15);self.assertEqual(c.pn_revision(0),2)
        p.parameters[0][0]=4. # caller no longer owns pending bytes
        self.assertEqual(c.fx_target(0,0,0),2.);self.assertEqual(c.pn_get_policy(0,0),0)
        self.assertEqual(c.pn_query(0),12);out=self.process(0,[.1]*127)
        self.assertEqual(c.fx_target(0,0,0),2.);self.assertEqual(c.fx_phase(0),1)
        self.assertEqual(self.process(0,[.1]),[0.]);self.assertEqual(c.fx_phase(0),2)
        self.assertEqual(c.fx_target(0,0,0),.5);self.assertEqual(c.pn_current(0,0,0),.5)
        self.assertEqual(c.pn_wet(0,0),1.);self.assertEqual(c.pn_get_policy(0,0),1)
        fade=self.process(0,[.1]*128);self.assertAlmostEqual(fade[-1],.05,places=6)
        self.assertEqual(c.fx_phase(0),0);self.assertEqual(c.pn_query(0),0)
        self.assertLess(max(abs(a-b) for a,b in zip(out,out[1:])),.002)
    def test_two_full_ir_assets_restore_after_type_replacement_and_histories_cut(self):
        c=self.lib;self.configure(0,[3,3]);values=[]
        for slot in (0,1):
            coeff=[(.5 if slot==0 else .7)]+[.0001*(-1 if (j+slot)%2 else 1) for j in range(1,512)]
            values.append(coeff);c.fx_ir(0,slot,(ct.c_float*512)(*coeff),512);c.fx_enable(0,slot,1)
        self.snapshot();self.configure(0,[0,8,5,6,7,255])
        for slot in range(5):c.fx_enable(0,slot,1)
        self.process(0,[.2]*4000)
        self.assertEqual(c.pn_request(0,0,1),15);self.process(0,[0.]*128)
        self.assertEqual([c.fx_type(0,i) for i in range(6)],[3,3,255,255,255,255])
        for slot in (0,1):
            self.assertEqual(c.fx_ir_taps(0,slot),512);self.assertEqual(c.pn_ir_nonzero_history(0,slot),0)
            for j in (0,100,511):self.assertAlmostEqual(c.fx_ir_coefficient(0,slot,j),values[slot][j],places=6)
        self.assertEqual(self.process(0,[0.]*2000),[0.]*2000);self.assertTrue(c.fx_ir_distinct(0,0,1))
    def test_invalid_snapshots_reject_atomically_without_cache_or_revision_changes(self):
        c=self.lib;self.snapshot();base=bytes(c.pn_ptr(0).contents);c.pn_cache(0)
        mutations=[lambda p:setattr(p,'version',3),lambda p:setattr(p,'sample_rate',48000),
            lambda p:setattr(p,'pre_count',7),lambda p:p.order.__setitem__(1,0),
            lambda p:p.types.__setitem__(0,99),lambda p:p.enabled.__setitem__(0,2),
            lambda p:p.bypass.__setitem__(0,2),lambda p:p.enabled.__setitem__(4,1),
            lambda p:p.parameters[0].__setitem__(0,4.1),lambda p:p.parameters[0].__setitem__(1,.1),
            lambda p:p.parameters[0].__setitem__(0,float('nan')),lambda p:p.ir_taps.__setitem__(0,513),
            lambda p:p.ir_coefficients[0].__setitem__(0,9.),lambda p:p.ir_coefficients[0].__setitem__(1,.1),
            lambda p:p.ir_taps.__setitem__(1,1),lambda p:p.ir_coefficients[1].__setitem__(10,float('nan'))]
        for mutate in mutations:
            ct.memmove(c.pn_ptr(0),base,len(base));mutate(c.pn_ptr(0).contents)
            self.assertEqual(c.pn_request(0,0,1),13)
            self.assertEqual((c.pn_revision(0),c.pn_cache_kind(0),c.fx_phase(0)),(1,1,0))
            self.assertEqual([c.pn_cache_word(0,j) for j in range(8)],list(range(123,131)))
            self.assertEqual([c.fx_type(0,j) for j in range(6)],[0,1,2,3,255,255])
    def test_workspace_rejection_busy_stale_exhaustion_and_identical_recall(self):
        c=self.lib;self.configure(0,[5]);self.snapshot();c.pn_detach_large(1)
        self.assertEqual(c.pn_request(1,0,1),13)
        c.pn_init(0);self.snapshot();self.assertEqual(c.pn_request(0,0,2),7)
        c.pn_set_revision(0,0xffffffff);self.assertEqual(c.pn_request(0,0,0xffffffff),11)
        c.pn_set_revision(0,1);c.pn_cache(0);before=bytes(c.pn_ptr(0).contents)
        self.assertEqual(c.pn_request(0,0,1),15) # identical recall still resets/fades
        self.assertEqual(c.pn_cache_kind(0),0);self.assertEqual(c.pn_request(0,0,1),12)
        self.assertEqual(c.pn_capture(0,0),0);self.assertEqual(bytes(c.pn_ptr(0).contents),before)
        self.process(0,[0.]*256);self.assertEqual(c.pn_revision(0),2)
        self.assertEqual(c.pn_request(0,0,1),7) # native API is fenced, not replay-cached
    def test_six_block_recall_partition_inplace_amp_reset_and_fault_progress(self):
        c=self.lib;self.configure(0,[6,7,5,8,4,3])
        for slot in range(6):c.fx_enable(0,slot,1)
        c.fx_set(0,2,0,20.);c.fx_set(0,3,0,.9);c.pn_policy(0,3,1)
        p=self.snapshot();p.order[:]=[5,4,3,2,1,0];p.pre_count=2
        self.assertEqual(c.pn_request(0,0,1),15);self.assertEqual(c.pn_request(1,0,1),15)
        x=[math.sin(i*.11)*.1 for i in range(16000)];x[127]=float('nan');x[255]=17.
        # Different old default routes must first be identical for sample0..126.
        # Compare from the silent commit onward, where the snapshot is common.
        y=self.process(0,x);z=[]
        for i in range(0,len(x),37):z+=self.process(1,x[i:i+37],True)
        self.assertEqual(y[127:],z[127:]);self.assertEqual(c.fx_faults(0),2)
        self.assertEqual(c.fx_phase(0),0);self.assertEqual(c.pn_get_policy(0,3),1)
        # External amp reset occurs at recall commit, independent of tails.
        c.pn_init(0);self.snapshot();c.pn_request(0,0,1);self.process(0,[0.]*256,amp=4)
        self.assertEqual(c.fx_amp_resets(0),1)
    def test_disabled_cut_slots_resume_empty_and_no_stale_preset_after_plain_route(self):
        c=self.lib;self.configure(0,[8,5]);p=self.snapshot()
        p.bypass[0]=p.bypass[1]=1;p.enabled[0]=p.enabled[1]=0
        self.assertEqual(c.pn_request(0,0,1),15);self.process(0,[.1]*256)
        self.assertEqual([c.pn_suspended(0,i) for i in (0,1)],[1,1])
        for slot in (0,1):c.fx_enable(0,slot,1)
        self.assertEqual(self.process(0,[0.]*5000),[0.]*5000)
        types=self.array([0,255,255,255,255,255]);order=self.array(list(range(6)))
        self.assertEqual(c.fx_request(0,types,order,3),1);self.process(0,[0.]*256)
        self.assertEqual(c.fx_type(0,0),0);self.assertEqual(c.fx_target(0,0,0),1.)
        self.assertEqual(c.fx_enabled(0,0),0)
    def test_owned_staging_accepts_without_an_extra_full_snapshot(self):
        c=self.lib;self.configure(0,[8,5,3]);p=self.snapshot()
        p.parameters[0][0]=.9;p.parameters[1][0]=20.;p.enabled[0]=p.enabled[1]=1
        self.assertEqual(c.pn_request_owned(0,0,1),15)
        self.process(0,[0.]*256)
        self.assertAlmostEqual(c.fx_target(0,0,0),.9,places=6)
        self.assertEqual(c.fx_target(0,1,0),20.);self.assertEqual(c.fx_phase(0),0)

if __name__=='__main__':unittest.main()
