import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from timing_meter import request,decode

class TimingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/timing_host'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/timing_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.trace_record.argtypes=[ct.c_uint32]*5
        cls.c.trace_anchor.argtypes=[ct.c_uint32]
        cls.c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.c.trace_inplace.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
    def setUp(self):self.c.protocol_init()
    def exchange(self,p):
        a=(ct.c_uint8*len(p))(*p);self.assertEqual(self.c.protocol_send(0x7f39,a,len(p)),1)
        return bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
    def read(self,index):return decode(self.exchange(request(index,7)),index,7)
    def test_first_worst_firstlate_and_latest_survive_later_blocks(self):
        self.assertEqual(self.c.trace_size(),448)
        for i in range(20):
            begin=1000*i
            self.c.trace_record(begin,begin+10,begin+(500 if i==4 else 40),begin+(600 if i==4 else 100),1 if i in (2,7) else 0)
        self.assertEqual([self.read(i)['block'] for i in range(8)],list(range(1,9)))
        self.assertEqual(self.read(8)['block'],5);self.assertEqual(self.read(9)['block'],3)
        self.assertEqual(self.read(10)['block'],20)
        self.assertEqual(self.read(8)['total_cycles'],600)
        self.assertEqual(self.read(0)['gap_cycles'],0)
        self.assertEqual(self.read(1)['gap_cycles'],900)
    def test_wraparound_phases_gap_and_saturated_ordinal(self):
        self.c.trace_record(0xfffffff0,0xfffffff8,8,16,15)
        v=self.read(10);self.assertEqual((v['total_cycles'],v['input_cycles'],v['process_cycles'],v['output_cycles']),(32,8,16,8))
        self.c.trace_record(32,40,50,64,0);self.assertEqual(self.read(10)['gap_cycles'],16)
        self.c.trace_saturate();self.c.trace_record(100,110,120,130,0)
        self.assertEqual(self.read(10)['block'],0xffffffff);self.assertEqual(self.read(0)['block'],1)
    def test_absence_and_reinit_are_explicit(self):
        for index in range(11):self.assertFalse(self.read(index)['available'])
        self.c.trace_anchor(75);self.c.trace_record(100,110,120,130,0)
        self.assertEqual(self.read(0)['gap_cycles'],25)
        self.assertTrue(self.read(0)['available']);self.assertFalse(self.read(9)['available'])
        self.c.protocol_init();self.assertFalse(self.read(0)['available'])
    def test_legacy_counter_reset_does_not_erase_trace(self):
        self.c.trace_record(0,10,20,30,1);before=self.exchange(request(9,7))
        p=(ct.c_uint8*4)(0x33,0x7f,4,0);self.c.protocol_send(0x7f33,p,4)
        self.assertEqual(self.exchange(request(9,7)),before)
    def test_bad_requests_and_unaligned_inplace_never_mutate_settings(self):
        self.c.trace_record(0,10,20,30,1)
        before=(self.c.protocol_revision(),self.c.protocol_commands(),self.c.protocol_rejected())
        good=request(9,7)
        for p in [good[:i] for i in range(16)]+[good+b'\0',good[:4]+struct.pack('<III',2,7,9),good[:8]+bytes(4)+good[12:],good[:12]+struct.pack('<I',11)]:
            v=struct.unpack('<HH11I',self.exchange(p));self.assertNotEqual(v[4],0);self.assertEqual(v[5:],(0,)*8)
        expected=self.exchange(good);buf=(ct.c_uint8*50)(*[0xcc]*50)
        for i,b in enumerate(good):buf[i+1]=b
        self.c.trace_inplace(ct.cast(ct.byref(buf,1),ct.POINTER(ct.c_uint8)),16)
        # Native dispatch source1; direct reader source2: compare all but flags.
        self.assertEqual(bytes(buf[1:45]),expected[:44]);self.assertEqual((buf[0],buf[49]),(0xcc,0xcc))
        self.assertEqual(before,(self.c.protocol_revision(),self.c.protocol_commands(),self.c.protocol_rejected()))
        self.assertEqual(self.c.protocol_callback_has_no_reply(),1)
    def test_codec_rejects_mismatches_and_impossible_fields(self):
        for index,token in [(-1,1),(11,1),(True,1),(0,0),(0,True),(0,0x100000000)]:
            with self.assertRaises(ValueError):request(index,token)
        self.c.trace_record(0,10,20,30,1);good=self.exchange(request(0,7))
        for pos,value in [(0,0),(1,47),(2,2),(3,8),(4,2),(5,1),(6,0),(7,31),(12,128)]:
            v=list(struct.unpack('<HH11I',good));v[pos]=value
            with self.assertRaises(ValueError):decode(struct.pack('<HH11I',*v),0,7)
        with self.assertRaises(ValueError):decode(good[:-1],0,7)

class ServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/service_host'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/timing_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.trace_record.argtypes=[ct.c_uint32]*5;cls.c.trace_anchor.argtypes=[ct.c_uint32]
        cls.c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        for f in ('service_note',):getattr(cls.c,f).argtypes=[ct.c_uint32]*3
        cls.c.service_iteration.argtypes=[ct.c_uint32]*2
    def setUp(self):self.c.protocol_init()
    def read(self,index,token=9):
        import service_meter
        p=service_meter.request(index,token);a=(ct.c_uint8*len(p))(*p)
        self.assertEqual(self.c.protocol_send(0x7f3c,a,len(p)),1)
        raw=bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
        return service_meter.decode(raw,index,token),raw
    def test_longest_step_before_each_block_is_attached_then_cleared(self):
        import service_meter
        self.c.trace_anchor(0)
        self.c.service_note(0xfff1,100,140);self.c.service_note(0x7f40,140,70140);self.c.service_note(0xfff3,70140,70200)
        self.c.service_iteration(100,70300)
        self.c.trace_record(100000,100010,100100,100200,4)
        self.c.trace_record(200000,200010,200100,200200,0)
        self.c.service_note(0xfff2,200300,200900)
        self.c.trace_record(300000,300010,300100,300200,2)
        latest,_=self.read(11)
        self.assertEqual((latest['block'],latest['service'],latest['service_cycles'],latest['dma_pending']),(3,'platform-report',600,2))
        late,_=self.read(9);first,_=self.read(0);second,_=self.read(1)
        self.assertEqual((late['block'],late['service'],late['service_cycles'],late['iteration_cycles'],late['dma_pending']),(1,'command 0x7f40',70000,70200,4))
        self.assertEqual((second['service_id'],second['service_cycles'],second['iteration_cycles']),(0,0,0))
        self.assertEqual((first['worst'],first['worst_cycles']),('command 0x7f40',70000))
    def test_wraparound_unavailable_and_bad_requests(self):
        import service_meter
        self.c.trace_anchor(0xfffffff0)
        self.c.service_note(0xfff2,0xffffff00,0x100)
        self.c.trace_record(0xfffffff8,0x10,0x20,0x30,0)
        d,_=self.read(0);self.assertEqual((d['service'],d['service_cycles']),('platform-report',0x200))
        self.assertEqual(self.read(9)[0],dict(index=9,available=False))
        self.assertEqual(self.read(11)[0],dict(index=11,available=False))
        for bad in (struct.pack('<HHIII',0x7f3c,16,2,5,0),struct.pack('<HHIII',0x7f3c,16,1,0,0),struct.pack('<HHIII',0x7f3c,16,1,5,12),struct.pack('<HHII',0x7f3c,12,1,5)):
            a=(ct.c_uint8*len(bad))(*bad);self.c.protocol_send(0x7f3c,a,len(bad))
            raw=bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
            self.assertEqual(raw[:4],bytes.fromhex('3c7f3000'));self.assertNotEqual(struct.unpack_from('<I',raw,12)[0],0)
        with self.assertRaises(ValueError):service_meter.request(12)
        raw=self.read(0)[1]
        with self.assertRaises(ValueError):service_meter.decode(raw,0,10)
        bad=bytearray(raw);bad[36:40]=struct.pack('<I',0x100);bad[44:48]=struct.pack('<I',0x10)
        with self.assertRaises(ValueError):service_meter.decode(bytes(bad),0,9)
    def test_gate_allows_one_heavy_step_per_block_inside_window(self):
        o=self.c.service_open;o.argtypes=[ct.c_uint32]
        self.c.trace_anchor(1000)
        self.assertEqual(o(2000),0)                      # before first block
        self.c.trace_record(5000,5010,5100,190000,0)     # block ends at 190000
        self.assertEqual(o(190000+100),1);self.assertEqual(o(190000+16383),1)
        self.assertEqual(o(190000+16384),0)              # window closed: idle gap is ahead
        self.c.service_take();self.assertEqual(o(190000+200),0)   # one per block
        self.c.trace_record(280000,280010,280100,470000,0)
        self.assertEqual(o(470000+50),1)
    def test_gate_reopens_when_audio_stops_and_survives_wrap_and_saturation(self):
        o=self.c.service_open;o.argtypes=[ct.c_uint32]
        self.c.trace_anchor(0);self.c.trace_record(10,20,30,0xfffff000,0);self.c.service_take()
        self.assertEqual(o(0xfffff000+0x100),0)
        self.assertEqual(o((0xfffff000+557000)&0xffffffff),1)   # stale across DWT wrap
        self.c.service_epoch(0xffffffff);self.c.trace_saturate()
        self.c.trace_record(100,110,120,200,0);self.assertEqual(o(300),1)   # epoch wraps, ordinal saturated
    def test_load_windows_track_process_commands_and_lates(self):
        import load_meter
        def read(i,t=33):
            p=load_meter.request(i,t);a=(ct.c_uint8*len(p))(*p)
            self.assertEqual(self.c.protocol_send(0x7f3d,a,len(p)),1)
            return load_meter.decode(bytes(self.c.protocol_reply(k) for k in range(self.c.protocol_reply_bytes())),i,t)
        self.assertEqual(read(0),dict(index=0,available=False))
        self.c.trace_anchor(0);t=0
        for w in range(3):
            for b in range(256):
                if w==1 and b%8==0:self.c.service_note(0x7f40,t,t+50000)
                if w==2 and b==10:self.c.service_note(0xfff1,t,t+900)       # TX service is not a command
                proc=300000 if (w==1 and b%8==1) else 180000
                self.c.trace_record(t+100000,t+100010,t+100010+proc,t+100020+proc,1 if (w==1 and b==9) else 0);t+=600000
        newest,middle,oldest=read(0),read(1),read(2)
        self.assertEqual((oldest['commands'],oldest['process_max'],oldest['late_blocks']),(0,180000,0))
        self.assertEqual((middle['commands'],middle['process_max'],middle['late_blocks'],middle['command_cycles_mean']),(32,300000,1,50000))
        self.assertEqual(middle['process_mean'],(224*180000+32*300000)//256)
        self.assertEqual((newest['commands'],newest['seq']),(0,3))
        with self.assertRaises(ValueError):load_meter.request(16)
    def test_reinitialization_clears_service_state(self):
        self.c.service_note(0x7f40,0,5000);self.c.protocol_init();self.c.trace_anchor(0)
        self.c.trace_record(10,20,30,40,0)
        d,_=self.read(0);self.assertEqual((d['service_cycles'],d['worst_cycles']),(0,0))

if __name__=='__main__':unittest.main()
