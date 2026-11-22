import ctypes as ct
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from qualify_chain import Qualification

class QualificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.q=Qualification();cls.lib=cls.q.lib
    def process(self,x,amp=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        if amp:self.lib.fx_amp_process(0,a,b,len(x),0)
        else:self.lib.fx_process(0,a,b,len(x))
        return list(b)
    def test_all_distinct_five_block_combinations_include_full_ir(self):
        r=self.q.matrix()
        self.assertEqual((r['distinct_five_effect_combinations'],r['cases'],r['streams']),(70,140,280))
        self.assertTrue(r['all_partition_checks_bit_identical'])
        self.assertEqual(r['normal_unity_exceedance_frames'],0)
        self.assertGreater(r['control_unity_exceedance_frames'],0) # report valid-but-clipping extreme edits honestly
        self.assertTrue(all(v['ir_taps']==512 for v in r['results']))
        self.assertTrue(all(run['invalid_outputs']==0 for v in r['results'] for run in v['runs']))
    def test_nonfinite_between_blocks_does_not_reach_amp_or_later_effects(self):
        c=self.lib;self.q.setup(0,['eq','delay','reverb']);self.process([.1]*5000)
        c.qa_boundary(0,1);before=c.fx_amp_calls(0);c.qa_poison_eq(0,0)
        self.assertEqual(self.process([.1],True),[0.])
        self.assertEqual(c.fx_amp_calls(0),before);self.assertEqual(c.fx_faults(0),1)
        self.assertEqual(c.qa_delay_valid(0),0);self.assertEqual(c.qa_reverb_valid(0),0)
        self.assertTrue(all(abs(v)<=16 for v in self.process([.1]*1000,True)))
        self.assertEqual(c.fx_faults(0),1)
    def test_large_intermediate_cannot_be_hidden_by_later_attenuation(self):
        c=self.lib;self.q.setup(0,['gain','gain']);c.fx_set(0,0,0,4.);c.fx_set(0,1,0,0.)
        self.process([0.]*5000)
        # Final-only guards would miss this: stage1 exceeds16, stage2 silences.
        self.assertEqual(self.process([8.]),[0.]);self.assertEqual(c.fx_faults(0),1)
        self.assertAlmostEqual(self.process([.1])[0],0.,places=5)
    def test_final_stage_fault_counter_saturates_and_transition_completes(self):
        c=self.lib;self.q.setup(0,['gain']);c.fx_set(0,0,0,4.);self.process([0.]*5000)
        c.qa_saturate_faults(0)
        types=(ct.c_uint*6)(0,255,255,255,255,255);order=(ct.c_uint*6)(*range(6))
        self.assertEqual(c.fx_request(0,types,order,0),1)
        self.assertEqual(self.process([8.]*256),[0.]*256)
        self.assertEqual(c.fx_phase(0),0);self.assertEqual(c.fx_faults(0),0xffffffff)
        self.assertTrue(all(abs(v)<=16 for v in self.process([.1]*1000)))

if __name__=='__main__':unittest.main()
