import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
OK,REPLAY,BAD_FRAME,BAD_VERSION,BAD_ID,STALE,EXHAUSTED,BUSY,BAD_ROUTE,ACCEPTED=0,1,2,3,4,7,11,12,13,15
DEFAULT=(0,1,2,3,255,255)
ORDER=tuple(range(6))
def request(token,revision,types=DEFAULT,order=ORDER,pre=3,version=1):
    return struct.pack('<HH4I12B',0x7f42,32,version,token,revision,pre,*types,*order)
def decode(r):
    cmd,length,version,token,status,revision,phase,position,pre=struct.unpack_from('<HH7I',r)
    assert (cmd,length,version)==(0x7f42,44,1)
    return dict(token=token,status=status,revision=revision,phase=phase,position=position,pre=pre,
        types=tuple(r[32:38]),order=tuple(r[38:44]))

class RouteWireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/route_wire.dll' if os.name=='nt' else 'build/route_wire.so'),
            ROOT/'tests/chain_connected_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.connected_wire.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.lib.connected_route_inplace.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.lib.connected_force_revision.argtypes=[ct.c_uint32]
        cls.lib.connected_revision.restype=ct.c_uint32
        cls.lib.connected_set.argtypes=[ct.c_uint32,ct.c_uint32]
    def setUp(self):self.lib.connected_init();self.lib.connected_drain()
    def wire(self,cmd,p):
        a=(ct.c_uint8*(len(p)+1))(0,*p)
        self.assertEqual(self.lib.connected_wire(cmd,ct.cast(ct.byref(a,1),ct.POINTER(ct.c_uint8)),len(p)),1)
        return bytes(self.lib.connected_reply_byte(i) for i in range(self.lib.connected_reply_bytes()))
    def send(self,p):
        r=self.wire(0x7f42,p);self.assertEqual(len(r),44);return decode(r)
    def status(self):return self.wire(0x7f41,struct.pack('<HHII',0x7f41,12,1,99999))
    def test_accepted_is_not_complete_and_retries_never_restart_fade(self):
        c=self.lib;rev=c.connected_revision();count=c.connected_metric(7)
        desired=(3,1,2,0,255,255);order=(1,0,2,3,5,4)
        p=request(0x12345678,rev,desired,order,2)
        self.assertEqual(p[:12],bytes.fromhex('427f20000100000078563412'))
        first=self.send(p)
        self.assertEqual((first['status'],first['revision'],first['phase'],first['position']),(ACCEPTED,rev+1,1,0))
        self.assertEqual(first['types'],DEFAULT) # old route until silent commit
        c.connected_audio(1);self.status()
        again=self.send(p)
        self.assertEqual((again['status'],again['phase'],again['position']),(REPLAY,1,64))
        c.connected_audio(1);mid=self.send(p)
        self.assertEqual((mid['status'],mid['phase'],mid['position']),(REPLAY,2,0))
        self.assertEqual((mid['types'],mid['order'],mid['pre']),(desired,order,2))
        c.connected_audio(2);done=self.send(p)
        self.assertEqual((done['status'],done['phase'],done['position']),(REPLAY,0,0))
        self.assertEqual(c.connected_revision(),rev+1)
        self.assertEqual(c.connected_metric(7),count+1)
        self.assertTrue(c.connected_muted())
    def test_busy_request_collision_and_stale_after_other_mutation(self):
        c=self.lib;rev=c.connected_revision();p=request(1000,rev,pre=0)
        self.send(p)
        self.assertEqual(self.send(request(1000,rev,pre=1))['status'],BAD_ID)
        self.assertEqual(self.send(request(1001,rev+1,pre=2))['status'],BUSY)
        c.connected_audio(4)
        self.assertEqual(self.send(request(1001,rev,pre=2))['status'],STALE)
        # Slot writes remain usable on an idle non-diagnostic route.
        generic=struct.pack('<HH8I',0x7f40,36,1,1002,1,rev+1,0,0,0,0x40000000)
        self.assertEqual(struct.unpack_from('<I',self.wire(0x7f40,generic),12)[0],OK)
        self.assertEqual(self.send(p)['status'],STALE)
        self.assertEqual(c.connected_revision(),rev+2)
    def test_noop_completes_immediately_without_second_revision_on_retry(self):
        c=self.lib;rev=c.connected_revision();p=request(2000,rev)
        first=self.send(p)
        self.assertEqual((first['status'],first['phase'],first['revision']),(OK,0,rev+1))
        self.assertEqual(self.send(p)['status'],REPLAY)
        self.assertEqual(c.connected_revision(),rev+1)
    def test_invalid_routes_are_atomic_and_do_not_evict_previous_cache(self):
        c=self.lib;rev=c.connected_revision()
        prior=struct.pack('<HH5I',0x7f11,24,1,3000,rev,1,1250)
        self.wire(0x7f11,prior);rev+=1
        invalid=[dict(types=(52,1,2,3,255,255)),dict(types=(254,1,2,3,255,255)),
            dict(types=(3,3,3,255,255,255)),dict(types=(5,5,2,3,255,255)),dict(types=(6,7,6,3,255,255)),dict(order=(0,0,2,3,4,5)),
            dict(types=(8,8,2,3,255,255)),dict(order=(0,1,2,3,4,255)),dict(pre=7),dict(pre=0xffffffff)]
        for i,fields in enumerate(invalid):
            with self.subTest(fields=fields):
                r=self.send(request(3010+i,rev,**fields))
                self.assertEqual((r['status'],r['phase'],r['types']),(BAD_ROUTE,0,DEFAULT))
                self.assertEqual(c.connected_revision(),rev)
        self.assertEqual(self.send(request(3000,rev,pre=0))['status'],BAD_ID)
        self.assertEqual(struct.unpack_from('<I',self.wire(0x7f11,prior),12)[0],REPLAY)
    def test_malformed_envelopes_versions_and_ids_never_schedule(self):
        c=self.lib;rev=c.connected_revision();p=request(4000,rev,pre=0)
        for n in list(range(32))+[33,44,80]:
            r=self.send((p+b'\0'*80)[:n]);self.assertEqual(r['status'],BAD_FRAME)
            self.assertEqual(c.connected_revision(),rev)
        for offset,value in ((0,0),(1,0),(2,31),(3,1)):
            q=bytearray(p);q[offset]=value
            self.assertEqual(self.send(q)['status'],BAD_FRAME)
        self.assertEqual(self.send(request(4001,rev,version=2))['status'],BAD_VERSION)
        self.assertEqual(self.send(request(0,rev))['status'],BAD_ID)
        self.assertEqual(c.connected_revision(),rev)
    def test_controller_pauses_real_wire_route_and_restores_queued_intent(self):
        c=self.lib;c.connected_force_value(1,2000);c.connected_drain();c.connected_force_value(1,1500)
        rev=c.connected_revision()
        self.assertEqual(self.send(request(5000,rev,types=(3,1,2,0,255,255)))['status'],ACCEPTED)
        self.assertEqual([c.connected_get(i)&0xffffffff for i in range(11)],[0xffffffff]*11)
        c.connected_poll();self.assertEqual(c.connected_sync_state(),2)
        self.assertEqual(c.connected_drain(),0)
        c.connected_audio(4);c.connected_poll()
        self.assertEqual(c.connected_sync_state(),3)
        self.assertEqual(c.connected_drain(),0)
        self.assertEqual([c.connected_get(i)&0xffffffff for i in range(11)],[0xffffffff]*11)
        restore=request(5001,c.connected_revision())
        self.assertEqual(self.send(restore)['status'],ACCEPTED)
        c.connected_audio(4)
        self.assertEqual(c.connected_get(1),1000) # real new-instance default, not stale 2000
        c.connected_poll()
        self.assertEqual(c.connected_sync_state(),1)
        self.assertEqual(c.connected_drain(),11)
        self.assertEqual(c.connected_get(1),1500)
        # Confirmation cache can be evicted by controller resynchronization;
        # an old route must then fail stale, never trigger a fresh fade.
        self.assertEqual(self.send(restore)['status'],STALE)
        self.assertEqual(struct.unpack_from('<I',self.status(),20)[0],0)
        self.assertTrue(c.connected_muted())
    def test_revision_exhaustion_and_reboot_deduplication_boundary(self):
        c=self.lib;c.connected_force_revision(0xfffffffe)
        p=request(6000,0xfffffffe,pre=0)
        self.assertEqual(self.send(p)['revision'],0xffffffff)
        self.assertEqual(self.send(p)['status'],REPLAY)
        c.connected_audio(4)
        self.assertEqual(self.send(request(6001,0xffffffff))['status'],EXHAUSTED)
        self.assertEqual(c.connected_revision(),0xffffffff)
        c.connected_dsp_reset()
        self.assertEqual(self.send(p)['status'],STALE)
        self.assertEqual(c.connected_revision(),1)
    def test_request_and_snapshot_can_share_storage(self):
        c=self.lib;p=request(7000,c.connected_revision(),pre=0)
        a=(ct.c_uint8*44)(*p);c.connected_route_inplace(a,32)
        r=decode(bytes(a));self.assertEqual((r['status'],r['phase']),(ACCEPTED,1))
        c.connected_audio(4)
        self.assertEqual(struct.unpack_from('<I',self.status(),28)[0],0)

if __name__=='__main__':unittest.main()
