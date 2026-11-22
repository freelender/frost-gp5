import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
CMD=0x7f40
UNUSED=0xffffffff
EMPTY=255
GET,PARAMETER,ENABLE=0,1,2
OK,REPLAY,BAD_FRAME,BAD_VERSION,BAD_ID,BAD_OP,BAD_SLOT,STALE,WRONG_TYPE,BAD_PARAMETER,BAD_VALUE,EXHAUSTED=range(12)
BUSY,BAD_ROUTE=12,13

def bits(value):return struct.unpack('<I',struct.pack('<f',value))[0]
def request(id=1,op=GET,revision=0,slot=0,type=EMPTY,parameter=UNUSED,value=0,version=1):
    return struct.pack('<HH8I',CMD,36,version,id,op,revision,slot,type,parameter,value)
def decode(data):
    cmd,length,version,id,status,revision,slot,type,enabled,count,*values=struct.unpack('<HH8I3f',data)
    assert (cmd,length,version)==(CMD,48,1)
    return dict(id=id,status=status,revision=revision,slot=slot,type=type,enabled=enabled,count=count,values=values)

class SlotProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/slot_protocol.dll' if os.name=='nt' else 'build/slot_protocol.so'),
            ROOT/'tests/slot_protocol_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.lib.protocol_configure.argtypes=[ct.POINTER(ct.c_uint),ct.c_uint]
        cls.lib.protocol_audio.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        cls.lib.protocol_inplace.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.lib.protocol_current.restype=ct.c_float
        cls.lib.protocol_revision.restype=ct.c_uint32
        cls.lib.protocol_set_revision.argtypes=[ct.c_uint32]
        cls.lib.protocol_route.argtypes=[ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint,ct.c_uint32]
    def setUp(self):self.lib.protocol_init()
    def send(self,payload):
        # Deliberately unaligned wire data; endpoint must not cast to structs.
        buffer=(ct.c_uint8*(len(payload)+1))(0,*payload)
        p=ct.cast(ct.byref(buffer,1),ct.POINTER(ct.c_uint8))
        self.assertEqual(self.lib.protocol_send(CMD,p,len(payload)),1)
        self.assertEqual(self.lib.protocol_reply_bytes(),48)
        return decode(bytes(self.lib.protocol_reply(i) for i in range(48)))
    def snapshot(self,slot=0):return self.send(request(slot=slot))
    def write(self,**kwargs):
        kwargs.setdefault('op',PARAMETER);kwargs.setdefault('revision',self.lib.protocol_revision())
        kwargs.setdefault('type',0);kwargs.setdefault('parameter',0)
        return self.send(request(**kwargs))
    def configure(self,types):
        self.assertEqual(self.lib.protocol_configure((ct.c_uint*len(types))(*types),len(types)),1)
    def audio(self,x):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        self.lib.protocol_audio(a,b,len(x));return list(b)
    def test_byte_contract_and_default_snapshot(self):
        payload=request(id=0x12345678)
        self.assertEqual(payload[:16],bytes.fromhex('407f2400010000007856341200000000'))
        for slot,type,count in ((0,0,1),(1,1,1),(2,2,3),(3,3,1),(4,EMPTY,0),(5,EMPTY,0)):
            reply=self.snapshot(slot)
            self.assertEqual((reply['status'],reply['revision'],reply['type'],reply['count']),(OK,1,type,count))
            self.assertEqual(reply['values'][count:],[0.]*(3-count))
        self.assertEqual(self.lib.protocol_commands(),0)
    def test_atomic_enable_mask_validates_before_mutating_and_replays(self):
        for value in (16,64,255):
            result=self.send(request(id=10,op=3,revision=1,value=value))
            self.assertIn(result['status'],(BAD_FRAME,BAD_VALUE))
            self.assertEqual(self.lib.protocol_revision(),1)
            self.assertEqual([self.snapshot(s)['enabled'] for s in range(4)],[1]*4)
        payload=request(id=11,op=3,revision=1,value=5)
        first=self.send(payload);self.assertEqual((first['status'],first['revision'],first['enabled']),(OK,2,5))
        self.assertEqual([self.snapshot(s)['enabled'] for s in range(6)],[1,0,1,0,0,0])
        second=self.send(payload);self.assertEqual((second['status'],second['revision'],second['enabled']),(REPLAY,2,5))
    def test_generic_slots_duplicate_types_and_legacy_readback(self):
        self.configure([0,0,2,3,1,0])
        r=self.write(id=11,slot=1,value=bits(2.5))
        self.assertEqual((r['status'],r['revision'],r['values'][0]),(OK,2,2.5))
        self.assertEqual(self.snapshot(0)['values'][0],1.)
        r=self.write(id=12,slot=5,value=bits(3.))
        self.assertEqual(r['values'][0],3.)
        self.lib.protocol_init()
        self.write(id=13,value=bits(1.2345))
        old=struct.pack('<HH',0x7f31,4)
        a=(ct.c_uint8*4)(*old);self.lib.protocol_send(0x7f31,a,4)
        reply=bytes(self.lib.protocol_reply(i) for i in range(48))
        self.assertEqual(struct.unpack_from('<I',reply,8)[0],1235)
        self.assertEqual(self.snapshot()['values'][0],ct.c_float(1.2345).value)
    def test_lost_reply_exact_retry_and_request_id_conflict(self):
        payload=request(id=19,op=PARAMETER,revision=1,type=0,parameter=0,value=bits(2.))
        self.assertEqual(self.send(payload)['status'],OK) # pretend this reply was lost
        self.snapshot() # reads do not evict last accepted write
        r=self.send(payload)
        self.assertEqual((r['status'],r['revision'],r['values'][0]),(REPLAY,2,2.))
        self.assertEqual(self.lib.protocol_commands(),1)
        self.assertEqual(self.write(id=19,value=bits(3.))['status'],BAD_ID)
        self.assertEqual(self.write(id=20,value=bits(1.5))['status'],OK)
        self.assertEqual(self.send(payload)['status'],STALE)
        self.assertEqual(self.snapshot()['values'][0],1.5)
    def test_legacy_interleave_invalidates_retry_and_resynchronizes(self):
        payload=request(id=9,op=PARAMETER,revision=1,type=0,parameter=0,value=bits(2.))
        self.send(payload);first=self.snapshot()
        legacy=struct.pack('<HHII',0x7f10,12,1,1500)
        a=(ct.c_uint8*len(legacy))(*legacy)
        self.assertEqual(self.lib.protocol_send(0x7f10,a,len(a)),1)
        second=self.snapshot(1)
        self.assertNotEqual(first['revision'],second['revision']) # discard torn multi-slot read
        self.assertEqual(self.send(payload)['status'],STALE)
        snapshots=[self.snapshot(i) for i in range(6)]
        self.assertEqual({s['revision'] for s in snapshots},{3})
        self.assertEqual(snapshots[0]['values'][0],1.5)
        self.assertEqual(self.write(id=10,revision=3,value=bits(1.75))['status'],OK)
    def test_rejects_invalid_edits_atomically(self):
        invalid=[(dict(version=3),BAD_VERSION),(dict(id=0),BAD_ID),(dict(op=99),BAD_OP),
            (dict(slot=6),BAD_SLOT),(dict(slot=UNUSED),BAD_SLOT),(dict(revision=0),STALE),
            (dict(type=1),WRONG_TYPE),(dict(type=99),WRONG_TYPE),(dict(slot=4),WRONG_TYPE),
            (dict(parameter=3),BAD_PARAMETER),(dict(parameter=UNUSED),BAD_PARAMETER),
            (dict(value=bits(-.1)),BAD_VALUE),(dict(value=bits(4.1)),BAD_VALUE),
            (dict(value=bits(float('nan'))),BAD_VALUE),(dict(value=bits(float('inf'))),BAD_VALUE),
            (dict(op=ENABLE,parameter=0,value=1),BAD_PARAMETER),
            (dict(op=ENABLE,parameter=UNUSED,value=2),BAD_VALUE)]
        for edits,status in invalid:
            with self.subTest(edits=edits):
                r=self.write(**edits);self.assertEqual(r['status'],status)
                self.assertEqual(self.lib.protocol_revision(),1)
                self.assertEqual(self.snapshot()['values'][0],1.)
        self.assertEqual(self.lib.protocol_commands(),0)
    def test_malformed_frames_and_canonical_queries(self):
        original=request()
        for n in list(range(36))+list(range(37,50)):
            r=self.send((original+b'\0'*50)[:n]);self.assertEqual(r['status'],BAD_FRAME)
        for offset,value in ((0,0),(1,0),(2,35),(3,1)):
            p=bytearray(original);p[offset]=value
            self.assertEqual(self.send(p)['status'],BAD_FRAME)
        for kwargs in (dict(revision=1),dict(type=0),dict(parameter=0),dict(value=1)):
            self.assertEqual(self.send(request(**kwargs))['status'],BAD_FRAME)
        self.assertEqual(self.lib.protocol_revision(),1)
        self.assertEqual(self.lib.protocol_commands(),0)
    def test_targets_commit_between_blocks_and_bypass_stays_smooth(self):
        self.configure([0])
        self.audio([.1]*64)
        r=self.write(id=8,value=bits(2.));self.assertEqual(r['status'],OK)
        self.assertEqual(self.lib.protocol_current(0,0),1.)
        self.write(id=9,op=ENABLE,parameter=UNUSED,value=1)
        y=self.audio([.1]*5000)
        self.assertAlmostEqual(y[-1],.2,places=5)
        self.assertLess(max(abs(a-b) for a,b in zip(y,y[1:])),.001)
        self.write(id=10,op=ENABLE,parameter=UNUSED,value=0)
        y=self.audio([.1]*5000)
        self.assertAlmostEqual(y[-1],.1,places=5)
        self.assertLess(max(abs(a-b) for a,b in zip(y,y[1:])),.001)
        self.assertTrue(self.lib.protocol_callback_has_no_reply())
    def test_revision_exhaustion_never_wraps_or_reapplies(self):
        self.lib.protocol_set_revision(UNUSED-1)
        payload=request(id=7,op=PARAMETER,revision=UNUSED-1,type=0,parameter=0,value=bits(2.))
        self.assertEqual(self.send(payload)['revision'],UNUSED)
        self.assertEqual(self.send(payload)['status'],REPLAY)
        self.assertEqual(self.write(id=8,value=bits(3.))['status'],EXHAUSTED)
        legacy=struct.pack('<HHII',0x7f10,12,1,1000);a=(ct.c_uint8*len(legacy))(*legacy)
        self.lib.protocol_send(0x7f10,a,len(a))
        self.assertEqual(self.lib.protocol_revision(),UNUSED)
        self.assertEqual(self.snapshot()['values'][0],2.)
    def test_request_and_reply_may_share_buffer(self):
        p=request(id=3,op=PARAMETER,revision=1,type=0,parameter=0,value=bits(2.))
        a=(ct.c_uint8*48)(*p)
        self.assertEqual(self.lib.protocol_inplace(a,36),48)
        r=decode(bytes(a));self.assertEqual((r['status'],r['values'][0]),(OK,2.))

    def test_all_registered_parameter_types_and_enable_snapshots(self):
        for id,(slot,type,param,value) in enumerate(((0,0,0,1.7),(1,1,0,.025),
                (2,2,0,.5),(2,2,1,1.6),(2,2,2,.2),(3,3,0,.7)),start=10):
            r=self.write(id=id,slot=slot,type=type,parameter=param,value=bits(value))
            self.assertEqual(r['status'],OK)
            self.assertEqual(r['values'][param],ct.c_float(value).value)
        for slot in range(4):
            r=self.write(id=20+slot,slot=slot,type=slot,op=ENABLE,parameter=UNUSED,value=0)
            self.assertEqual((r['status'],r['enabled']),(OK,0))
            self.assertEqual(self.snapshot(slot)['enabled'],0)

    def test_command_boundaries_are_independent_of_audio_chunk_size(self):
        def render(chunk):
            self.lib.protocol_init();self.configure([0,2])
            self.write(id=10,value=bits(1.7))
            self.write(id=11,op=ENABLE,parameter=UNUSED,value=1)
            self.write(id=14,slot=1,type=2,op=ENABLE,parameter=UNUSED,value=1)
            output=[]
            for phase in range(3):
                if phase==1:self.write(id=12,slot=1,type=2,parameter=2,value=bits(.2))
                if phase==2:self.write(id=13,op=ENABLE,parameter=UNUSED,value=0)
                for begin in range(0,2300,chunk):
                    x=[((i*47)%1023-511)/8192 for i in range(begin,min(begin+chunk,2300))]
                    output.extend(self.audio(x))
            return output
        self.assertEqual(render(37),render(64))

    def test_native_route_reserves_revision_and_blocks_torn_snapshots(self):
        payload=request(id=7,op=PARAMETER,revision=1,type=0,parameter=0,value=bits(2.))
        self.send(payload)
        types=(ct.c_uint*6)(2,1,2,3,EMPTY,EMPTY);order=(ct.c_uint*6)(*range(6))
        self.assertEqual(self.lib.protocol_route(types,order,2,2),OK)
        self.assertEqual(self.lib.protocol_revision(),3)
        self.assertEqual(self.snapshot()['status'],BUSY)
        self.assertEqual(self.send(payload)['status'],BUSY)
        self.assertEqual(self.write(id=8,value=bits(1.5))['status'],BUSY)
        self.assertEqual(self.lib.protocol_route(types,order,2,3),BUSY)
        # Legacy writes also reject without advancing the reserved revision.
        legacy=struct.pack('<HHII',0x7f10,12,1,1000);a=(ct.c_uint8*len(legacy))(*legacy)
        self.lib.protocol_send(0x7f10,a,len(a));self.assertEqual(self.lib.protocol_revision(),3)
        self.audio([0.]*128)
        mid=self.snapshot();self.assertEqual((mid['status'],mid['type']),(BUSY,2))
        self.audio([0.]*128)
        final=self.snapshot();self.assertEqual((final['status'],final['revision'],final['type']),(OK,3,2))
        self.assertEqual(self.send(payload)['status'],STALE)
        self.assertEqual(self.write(id=8,value=bits(1.5))['status'],WRONG_TYPE)
        self.assertEqual(self.write(id=9,type=2,value=bits(1.5))['status'],OK)

    def test_invalid_native_route_preserves_revision_and_replay(self):
        payload=request(id=7,op=PARAMETER,revision=1,type=0,parameter=0,value=bits(2.))
        self.send(payload)
        types=(ct.c_uint*6)(0,1,2,3,EMPTY,EMPTY);bad_order=(ct.c_uint*6)(0,0,2,3,4,5)
        self.assertEqual(self.lib.protocol_route(types,bad_order,3,2),BAD_ROUTE)
        self.assertEqual(self.lib.protocol_route(types,bad_order,3,1),STALE)
        self.assertEqual(self.lib.protocol_revision(),2)
        self.assertEqual(self.send(payload)['status'],REPLAY)
        self.lib.protocol_set_revision(UNUSED)
        self.assertEqual(self.lib.protocol_route(types,bad_order,3,UNUSED),EXHAUSTED)

if __name__=='__main__':unittest.main()
