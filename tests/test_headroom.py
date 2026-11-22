import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools')]
from headroom import Headroom,HeadroomError
from preset_io import render,save,validate

class HeadroomTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=ROOT/'build');self.folder=Path(self.temp.name)
        self.preset=self.folder/'preset.json';self.source=self.folder/'dry.wav';self.out=self.folder/'wet.wav'
    def tearDown(self):self.temp.cleanup()
    def wav(self,values):
        with wave.open(str(self.source),'wb') as w:
            w.setparams((1,2,44100,0,'NONE',''));w.writeframes(struct.pack('<'+'h'*len(values),*values))
    def prepare(self,gains=(1.,),tail=0.):
        slots=[dict(id=i,type='gain' if i<len(gains) else 'empty',enabled=i<len(gains),
             parameters={'MULTIPLIER':gains[i]} if i<len(gains) else {}) for i in range(6)]
        data=dict(format='frost-preset',version=1,name='Headroom check',sample_rate=44100,
                  slots=slots,order=list(range(6)),pre_count=3,tail_seconds=tail)
        save(self.preset,validate(data,self.folder))
    def test_success_matches_legacy_bytes_and_includes_tail(self):
        self.wav([0]*1000+[8000,-8000]*1000);self.prepare((.5,),.02)
        legacy=render(self.preset,self.source,self.out);reference=self.out.read_bytes()
        metrics=render(self.preset,self.source,self.out,ceiling_dbfs=-6.)
        self.assertEqual(self.out.read_bytes(),reference);r=metrics['headroom']
        self.assertTrue(r['admitted']);self.assertEqual(r['analyzed_frames'],3882)
        self.assertEqual((r['pcm16_clipped_frames'],r['pcm16_rail_frames'],r['outside_unity_frames']),(0,0,0))
        self.assertEqual(metrics['float_peak'],legacy['float_peak'])
    def test_supported_overload_rejects_and_preserves_existing_or_absent_output(self):
        self.wav([0]*2048+[16000,-16000]*1000);self.prepare((4.,4.))
        for existing in (False,True):
            if existing:self.out.write_bytes(b'keep this output')
            before=set(self.folder.iterdir())
            with self.assertRaises(HeadroomError) as caught:render(self.preset,self.source,self.out,-1.)
            self.assertFalse(caught.exception.report['admitted'])
            self.assertGreater(caught.exception.report['first_rejected_frame'],2047)
            self.assertEqual(set(self.folder.iterdir()),before)
            if existing:self.assertEqual(self.out.read_bytes(),b'keep this output')
            else:self.assertFalse(self.out.exists())
    def test_same_trace_fixed_with_existing_final_gain_slot(self):
        self.wav([0]*2048+[16000,-16000]*1000);self.prepare((4.,4.,.05))
        r=render(self.preset,self.source,self.out,-1.)['headroom']
        self.assertTrue(r['admitted']);self.assertLess(r['float_peak'],10**(-1/20))
        self.assertEqual(r['pcm16_clipped_frames'],0)
    def test_zero_dbfs_handles_pcm16_asymmetry_and_rounding(self):
        meter=Headroom(0);meter.observe([-1.,32767/32768,0.,-0.])
        self.assertEqual((meter.report()['pcm16_rail_frames'],meter.report()['outside_unity_frames']),(2,0))
        for value in (1.,32767.75/32768):
            with self.assertRaises(HeadroomError) as caught:Headroom(0).observe([value])
            self.assertEqual(caught.exception.report['failure'],'PCM16 clipping')
        meter=Headroom();meter.observe([1.,-1.,1.1,-1.1])
        self.assertEqual((meter.outside,meter.clipped,meter.rails),(2,3,4))
        self.assertIsNone(meter.report()['admitted'])
    def test_threshold_and_first_index_are_chunk_independent(self):
        limit=10**(-6/20);values=[0.]*17+[limit]+[limit+1e-8]*10
        reports=[]
        for chunk in (1,7,64):
            meter=Headroom(-6)
            try:
                for i in range(0,len(values),chunk):meter.observe(values[i:i+chunk])
            except HeadroomError as error:reports.append(error.report)
        self.assertEqual([r['first_rejected_frame'] for r in reports],[18]*3)
        self.assertTrue(all(r['failure']=='headroom ceiling exceeded' for r in reports))
    def test_invalid_limits_and_nonfinite_output_fail_before_commit(self):
        for limit in (True,'-1',float('nan'),float('inf'),-60.001,.001,1<<2000):
            with self.assertRaises(ValueError):render('missing','missing','missing',limit)
        for y in (float('nan'),float('inf'),-float('inf'),16.01):
            with self.assertRaises(HeadroomError):Headroom().observe([y])
        self.wav([0]*64);self.prepare();self.out.write_bytes(b'preserve')
        with patch('preset_io.Chain.process',return_value=[float('nan')]*64):
            with self.assertRaises(HeadroomError):render(self.preset,self.source,self.out,-1.)
        self.assertEqual(self.out.read_bytes(),b'preserve')
    def test_downstream_attenuation_cannot_hide_numeric_containment_fault(self):
        self.wav([0]*2048+[32767,-32768]*1000);self.prepare((4.,4.,4.,.01))
        self.out.write_bytes(b'keep')
        with self.assertRaisesRegex(ValueError,'containment fault'):render(self.preset,self.source,self.out,-1.)
        self.assertEqual(self.out.read_bytes(),b'keep')
    def test_empty_trace_is_not_admitted(self):
        meter=Headroom(-1);self.assertIsNone(meter.report()['admitted'])
        meter.observe([.1]);self.assertIsNone(meter.report()['admitted'])
        self.assertTrue(meter.finish()['admitted'])
        with self.assertRaises(ValueError):meter.observe([.1])
        self.wav([]);self.prepare()
        with self.assertRaises(HeadroomError) as caught:render(self.preset,self.source,self.out,-1.)
        self.assertEqual(caught.exception.report['failure'],'empty trace')
        self.assertFalse(caught.exception.report['admitted']);self.assertFalse(self.out.exists())
    def test_tail_is_checked_and_failed_tail_preserves_destination(self):
        self.wav([0]*64);self.prepare(tail=.002);self.out.write_bytes(b'previous')
        with patch('preset_io.Chain.process',side_effect=[[.1]*64,[.95]*64]):
            with self.assertRaises(HeadroomError) as caught:render(self.preset,self.source,self.out,-1.)
        self.assertEqual(caught.exception.report['first_rejected_frame'],64)
        self.assertEqual(self.out.read_bytes(),b'previous')
    def test_truncated_trace_cannot_be_admitted(self):
        self.wav([100]*128);self.prepare();complete=self.source.read_bytes()
        for missing in (1,2,64):
            self.source.write_bytes(complete[:-missing]);self.out.write_bytes(b'keep')
            with self.assertRaisesRegex(ValueError,'truncated'):render(self.preset,self.source,self.out,-1.)
            self.assertEqual(self.out.read_bytes(),b'keep')
    def test_cli_returns_failure_with_evidence_and_no_output(self):
        self.wav([0]*2048+[16000,-16000]*1000);self.prepare((4.,4.))
        r=subprocess.run([sys.executable,str(ROOT/'tools/preset_io.py'),str(self.preset),str(self.source),str(self.out),'--ceiling-dbfs','-1'],capture_output=True,text=True)
        self.assertEqual(r.returncode,2);self.assertIn('first_rejected_frame',r.stderr)
        self.assertIn('output not committed',r.stderr);self.assertFalse(self.out.exists())

if __name__=='__main__':unittest.main()
