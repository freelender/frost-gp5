from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audible_monitor as am

class FakePedal:
    """Replies like the line-test DSP; state is mutated per poll by a script."""
    def __init__(self,script=None):
        self.state=dict(gain=0,requested=0,flags=0,late=0,faults=0,rails=0,invalid=0,dropped=0,crc=0,mode=1,muted_marker=0)
        self.script=script or (lambda n,s:None);self.polls=0;self.requests=[]
    def __call__(self,p):
        self.requests.append(p);s=self.state;cmd=struct.unpack_from('<H',p)[0]
        if cmd==0x7f3b:
            self.script(self.polls,s);self.polls+=1
            token=struct.unpack_from('<I',p,8)[0];closed=1 if s['flags'] or not s['gain'] else 0
            return struct.pack('<HH11I',0x7f3b,48,1,token,0,s['mode'],16384 if s['mode'] else 0,s['gain'],s['flags'],s['faults'],s['requested'],2,closed)
        if cmd==0x7f30:
            return bytes.fromhex('307f2c00')+struct.pack('<10I',0x46584332,190000,240000,s['late'],0,s['faults'],0,0,s['muted_marker'],1)
        token=struct.unpack_from('<I',p,8)[0]
        if cmd==0x7f35:
            return struct.pack('<HH11I',0x7f35,48,1,token,0,1000,struct.unpack('<I',struct.pack('<f',0.01))[0],0,s['rails'],s['invalid'],s['faults'],0,2)
        if cmd==0x7f3a:
            return struct.pack('<HH11I',0x7f3a,48,1,token,0,0,10,s['dropped'],100,0,40,0,2)
        if cmd==0x7f37:
            return struct.pack('<HH11I',0x7f37,48,1,token,0,0,10,s['crc'],0,0,0,40,2)
        raise AssertionError('unexpected write or command 0x%04x'%cmd)

class Clock:
    def __init__(self):self.t=0.0
    def __call__(self):return self.t
    def sleep(self,d):self.t+=d

class AudibleMonitorTests(unittest.TestCase):
    def go(self,pedal,seconds=3,max_gain=16384):
        clock=Clock();log=[]
        out=am.run(pedal,log.append,seconds,0.5,max_gain,sleep=clock.sleep,clock=clock,say=lambda s:None)
        return out,log
    def test_only_read_commands_are_sent(self):
        pedal=FakePedal();self.go(pedal)
        self.assertEqual({struct.unpack_from('<H',p)[0] for p in pedal.requests},{0x7f3b,0x7f30,0x7f35,0x7f3a,0x7f37})
    def test_silence_phase_and_ramp_summary(self):
        def script(n,s):
            if n>=2:s['requested']=32768;s['gain']=min(327,100*n)
        out,log=self.go(FakePedal(script))
        self.assertEqual((out['max_requested'],out['max_target'],out['max_gain']),(32768,8192,327))
        self.assertEqual(out['samples'],7);self.assertEqual(log[-1]['event'],'phase_end')
    def test_gain_above_phase_limit_stops_with_instruction(self):
        def script(n,s):
            if n>=3:s['gain']=200
        with self.assertRaises(am.Stop) as e:self.go(FakePedal(script),max_gain=150)
        self.assertIn('Turn VOL down',str(e.exception))
    def test_each_hazard_stops(self):
        for key,value,word in (('flags',16,'latched late'),('faults',1,'fault'),('rails',1,'rail'),
                               ('invalid',1,'rail/invalid'),('dropped',1,'Link'),('crc',1,'Link')):
            def script(n,s,key=key,value=value):
                if n>=2:s[key]=value
            with self.subTest(key=key),self.assertRaises(am.Stop) as e:self.go(FakePedal(script))
            self.assertIn(word.split()[0].lower(),str(e.exception).lower())
    def test_preexisting_late_count_is_baseline_not_hazard(self):
        pedal=FakePedal();pedal.state['late']=3
        out,_=self.go(pedal);self.assertEqual((out['late_start'],out['late_end']),(3,3))
    def test_isolated_late_is_reported_not_a_stop(self):
        def script(n,s):
            if n>=2:s['late']=1
        out,_=self.go(FakePedal(script));self.assertEqual((out['late_start'],out['late_end']),(0,1))
    def test_muted_profile_or_unbounded_phase_rejected(self):
        pedal=FakePedal();pedal.state['muted_marker']=1
        with self.assertRaises(ValueError):self.go(pedal)
        for seconds,gain in ((0,10),(181,10),(5,65537),(5,-1)):
            with self.assertRaises(ValueError):self.go(FakePedal(),seconds=seconds,max_gain=gain)
    def test_light_mode_reads_only_line_and_metrics(self):
        pedal=FakePedal();clock=Clock()
        am.run(pedal,lambda v:None,3,0.5,16384,sleep=clock.sleep,clock=clock,say=lambda s:None,light=True)
        self.assertEqual({struct.unpack_from('<H',p)[0] for p in pedal.requests},{0x7f3b,0x7f30})
    def test_target_mirrors_dsp_cap(self):
        self.assertEqual([am.target(v) for v in (0,1,771,65536,65537)],[0,1,771,65536,0])

if __name__=='__main__':unittest.main()
