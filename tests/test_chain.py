import math
from pathlib import Path
import sys
import unittest
import tempfile
import wave
import struct
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from chain_host import Chain
from chain_wav import run

class ChainTests(unittest.TestCase):
    def test_bypass_and_unity_eq(self):
        c=Chain();x=[math.sin(i*0.17)*0.2 for i in range(4096)]
        self.assertLess(max(abs(a-b) for a,b in zip(x,c.process(x))),1e-7)
        c.lib.chain_enable(2,1)
        self.assertLess(max(abs(a-b) for a,b in zip(x,c.process(x))),1e-7)

    def test_partition_invariance(self):
        c=Chain();x=[math.sin(i*.4)*.1 for i in range(12000)]
        def setup():
            c.lib.chain_init()
            for e in range(3):self.assertEqual(c.lib.chain_enable(e,1),1)
            for e,p,v in [(0,0,2.),(1,0,.02),(2,0,1.5),(2,1,.5),(2,2,.3)]:self.assertEqual(c.lib.chain_set(e,p,v),1)
        setup();expected=c.process(x);setup();actual=[]
        for i in range(0,len(x),37):actual.extend(c.process(x[i:i+37]))
        self.assertEqual(expected,actual)

    def test_gain_and_transitions(self):
        c=Chain();c.lib.chain_set(0,0,2.);c.lib.chain_enable(0,1)
        y=c.process([.1]*5000)
        self.assertAlmostEqual(y[-1],.2,places=5)
        self.assertLess(max(abs(a-b) for a,b in zip(y,y[1:])),.001)
        c.lib.chain_enable(0,0);y=c.process([.1]*5000)
        self.assertAlmostEqual(y[-1],.1,places=5)
        self.assertLess(max(abs(a-b) for a,b in zip(y,y[1:])),.001)

    def test_gate_closes_and_opens(self):
        c=Chain();c.lib.chain_enable(1,1);c.lib.chain_set(1,0,.01)
        y=c.process([.0001]*20000);self.assertLess(abs(y[-1]),1e-8)
        y=c.process([.1]*5000);self.assertAlmostEqual(y[-1],.1,places=5)
        y=c.process([.0001]*40000);self.assertLess(abs(y[-1]),1e-8)

    def test_validation_fault_recovery(self):
        c=Chain()
        for e,p,v in [(4,0,1.),(0,3,1.),(0,0,-1.),(0,0,float('nan')),(1,0,float('inf'))]:self.assertEqual(c.lib.chain_set(e,p,v),0)
        self.assertEqual(c.lib.chain_enable(0,2),0)
        self.assertEqual(c.process([float('nan'),float('inf'),17.,.1])[:3],[0.,0.,0.])
        self.assertEqual(c.lib.chain_faults(),3)
        self.assertAlmostEqual(c.process([.1])[0],.1,places=6)

    def test_wav_length_tail_and_rate(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/'build') as d:
            source=Path(d)/'in.wav';out=Path(d)/'out.wav'
            with wave.open(str(source),'wb') as w:
                w.setparams((1,2,44100,0,'NONE','not compressed'));w.writeframes(struct.pack('<129h',*([3000]*129)))
            run(source,out)
            with wave.open(str(out),'rb') as w:
                self.assertEqual(w.getnframes(),129);self.assertEqual(w.getframerate(),44100)
            with self.assertRaises(ValueError):run(source,source)
            with wave.open(str(source),'wb') as w:
                w.setparams((1,2,48000,0,'NONE','not compressed'));w.writeframes(b'\0\0')
            with self.assertRaises(ValueError):run(source,out)

    def test_wav_configurable_duplicate_gain_route(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/'build') as d:
            source=Path(d)/'in.wav';out=Path(d)/'out.wav'
            with wave.open(str(source),'wb') as w:
                w.setparams((1,2,44100,0,'NONE','not compressed'))
                w.writeframes(struct.pack('<6000h',*([1000]*6000)))
            run(source,out,gain=2.,effects=['gain','gain'])
            with wave.open(str(out),'rb') as w:
                data=struct.unpack('<6000h',w.readframes(6000))
                self.assertEqual(data[-1],4000)
                self.assertEqual(w.getnframes(),6000)
            for route in (['nam'],['gain']*7):
                with self.assertRaises(ValueError):run(source,out,effects=route)
            with self.assertRaises(ValueError):run(source,out,ir=source,effects=['gain'])

    def test_wav_cut_policy_and_invalid_policy(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/'build') as d:
            source=Path(d)/'in.wav';out=Path(d)/'out.wav'
            with wave.open(str(source),'wb') as w:
                w.setparams((1,2,44100,0,'NONE','not compressed'))
                w.writeframes(struct.pack('<6000h',*([1000]*6000)))
            run(source,out,gain=2.,effects=['gain','gain'],bypass_policy='cut')
            with wave.open(str(out),'rb') as w:
                self.assertEqual(w.getnframes(),6000)
                data=struct.unpack('<6000h',w.readframes(6000))
            self.assertEqual(data[-1],4000)
            self.assertTrue(all(1000<=value<=4000 for value in data))
            self.assertLess(max(abs(a-b) for a,b in zip(data,data[1:])),100)
            with self.assertRaises(ValueError):run(source,out,bypass_policy='unknown')

if __name__=='__main__':unittest.main()
