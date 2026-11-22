"""Host checks for the EXPERIMENTAL AMP/FCM block (effects/fcm). Converted
models are approximations of NAM; these tests cover the engine contract only:
record transfer (BEGIN/WRITE/COMMIT/CLEAR, CRC, replay, order), default
fallback, model selection, block/per-sample identity and chunk invariance.
Not a listening or timing qualification."""
import ctypes as ct
import math
import os
from pathlib import Path
import struct
import sys
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
import fcm_bank as B
import fcm_files as F
APPLIED,REPLAYED,BAD_FRAME,BAD_VERSION,BAD_REQUEST_ID,BAD_OPERATION=0,1,2,3,4,5
BAD_VALUE,BAD_SESSION,BAD_CHECKSUM,INCOMPLETE,BAD_OFFSET=10,16,17,18,19

def sinc_aa(n=20,cut=0.45):
    """Hamming-windowed sinc, same family as scipy firwin (symmetric)."""
    m=(n-1)/2;h=[]
    for k in range(n):
        t=k-m;s=2*cut if t==0 else math.sin(2*math.pi*cut*t)/(math.pi*t)
        h.append(s*(0.54-0.46*math.cos(2*math.pi*k/(n-1))))
    g=sum(h);return [v/g for v in h]
def model(gain=1.0,drive=4.0,tilt=0.0):
    ident=[1.0,0.0,0.0,0.0,0.0];lp=[0.2+tilt,0.2,0.0,-0.6,0.0]
    return dict(pre=[ident],mid=[ident],post=[lp],gain=gain,Ap=[0.8,0.8],An=[0.7,0.7],Kp=[drive,drive],Kn=[drive,drive],
                env_a=0.01,env_k=[0.1,0.0],aa=sinc_aa())
def words(record):return struct.unpack('<100I',record)
def crc_of(record):return struct.unpack_from('<I',record,396)[0]
class FcmTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ext='dll' if os.name=='nt' else 'so'
        cls.c=ct.CDLL(str(build(ROOT/f'build/fcm_host.{ext}',ROOT/'tests/fcm_host.c')))
        cls.c.fcm_crc.restype=ct.c_uint32;cls.c.fcm_loaded.restype=ct.c_uint
    def setUp(self):self.c.fcm_rx_reset();self.token=100
    def cmd(self,op,index=0,crc=0,offset=0,data=(),raw=None):
        self.token+=1;q=raw if raw is not None else F.request(self.token,op,index,crc,offset,data)
        a=(ct.c_uint8*len(q))(*q);r=(ct.c_uint8*48)();self.c.fcm_command(a,len(q),r)
        return F.decode(bytes(r))
    def send(self,index,record,chunk=20):
        crc=crc_of(record);w=words(record)
        self.assertEqual(self.cmd(F.BEGIN,index,crc)['status'],APPLIED)
        for o in range(0,100,chunk):self.assertEqual(self.cmd(F.WRITE,index,crc,o,w[o:o+chunk])['status'],APPLIED)
        return self.cmd(F.COMMIT,index,crc)
    def run_(self,x,p,chunk=64,per_sample=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))();pp=(ct.c_float*6)(*p)
        if per_sample:self.c.fcm_samples(a,b,len(x),pp)
        else:self.c.fcm_block(a,b,len(x),chunk,pp)
        return list(b)
    def fresh(self):self.c.fcm_reset_state()
    def x(self,n=2048):return [0.4*math.sin(2*math.pi*196*i/44100)*min(1,i/400) for i in range(n)]
    def test_layout_and_crc_match_the_packer(self):
        self.assertEqual(self.c.fcm_model_bytes(),B.MODEL_BYTES);self.assertEqual(self.c.fcm_record_bytes(),B.RECORD_BYTES)
        self.assertEqual(B.RECORD_BYTES,F.RECORD)
        data=b'FrostCaptureModel'*7;self.assertEqual(self.c.fcm_crc(data,len(data)),zlib.crc32(data)&0xffffffff)
    def test_default_model_without_record_is_finite_and_audible(self):
        self.fresh();y=self.run_(self.x(),[0,0,50])
        self.assertTrue(all(math.isfinite(v) and abs(v)<=8 for v in y));self.assertGreater(max(abs(v) for v in y[1000:]),1e-3)
        self.assertEqual(self.c.fcm_loaded(),1);self.assertEqual(self.c.fcm_from_rx(),0)
    def test_received_record_plays_only_for_its_model_index(self):
        self.fresh();d=self.run_(self.x(),[3,0,50])                       # default
        r=self.send(3,B.pack_record('HOT',model(gain=3.0,drive=20.0,tilt=0.1)))
        self.assertEqual((r['status'],r['held_index'],r['state']),(APPLIED,3,2))
        self.fresh();m3=self.run_(self.x(),[3,0,50]);self.assertNotEqual(m3,d);self.assertEqual(self.c.fcm_from_rx(),1)
        self.fresh();self.assertEqual(self.run_(self.x(),[4,0,50]),d)    # another MODEL: default
        self.assertEqual(self.cmd(F.CLEAR)['held_index'],0xffffffff)
        self.fresh();self.assertEqual(self.run_(self.x(),[3,0,50]),d)
    def test_live_commit_and_clear_switch_a_running_block(self):
        self.fresh();d=self.run_(self.x(),[2,0,50]);self.fresh();self.run_(self.x(512),[2,0,50])
        rec=B.pack_record('A',model(drive=15.0));self.send(2,rec)
        y=self.run_(self.x(),[2,0,50]);self.assertEqual(self.c.fcm_from_rx(),1)
        self.fresh();z=self.run_(self.x(),[2,0,50]);self.assertEqual(y,z)  # reloaded from a clean state
        self.cmd(F.CLEAR);self.run_(self.x(64),[2,0,50]);self.assertEqual(self.c.fcm_from_rx(),0)
        # a commit for ANOTHER index does not disturb a block playing the default
        self.fresh();a=self.run_(self.x(512),[2,0,50]);self.send(9,rec);b=self.run_(self.x(),[2,0,50])
        self.assertEqual(self.c.fcm_from_rx(),0)
        self.c.fcm_rx_reset();self.fresh();a2=self.run_(self.x(512),[2,0,50]);b2=self.run_(self.x(),[2,0,50])
        self.assertEqual((a,b),(a2,b2))                                     # no reload, no glitch
    def test_receiving_keeps_the_playing_copy_until_commit(self):
        rec=B.pack_record('A',model(drive=12.0));self.send(5,rec)
        self.fresh();self.run_(self.x(256),[5,0,50]);self.assertEqual(self.c.fcm_from_rx(),1)
        other=B.pack_record('B',model(drive=3.0));crc=crc_of(other)
        self.assertEqual(self.cmd(F.BEGIN,5,crc)['held_index'],0xffffffff)
        self.run_(self.x(256),[5,0,50]);self.assertEqual(self.c.fcm_from_rx(),1)   # still the old copy
    def test_order_replay_crc_and_frame_checks(self):
        rec=bytearray(B.pack_record('C',model()));crc=crc_of(rec);w=words(bytes(rec))
        self.assertEqual(self.cmd(F.WRITE,1,crc,0,w[:20])['status'],BAD_SESSION)
        self.assertEqual(self.cmd(F.BEGIN,1,0)['status'],BAD_VALUE)
        self.assertEqual(self.cmd(F.BEGIN,64,crc)['status'],BAD_VALUE)
        self.cmd(F.BEGIN,1,crc)
        self.assertEqual(self.cmd(F.WRITE,1,crc,20,w[20:40])['status'],BAD_OFFSET)   # out of order
        self.assertEqual(self.cmd(F.WRITE,1,crc,0,w[:20])['status'],APPLIED)
        self.assertEqual(self.cmd(F.WRITE,1,crc,0,w[:20])['status'],REPLAYED)          # lost reply resent
        self.assertEqual(self.cmd(F.COMMIT,1,crc)['status'],INCOMPLETE)
        for o in range(20,100,20):self.cmd(F.WRITE,1,crc,o,w[o:o+20])
        self.assertEqual(self.cmd(F.WRITE,1,crc,90,w[90:100]+(0,)*0)['status'],REPLAYED)
        self.assertEqual(self.cmd(F.COMMIT,1,crc)['status'],APPLIED)
        self.assertEqual(self.cmd(F.COMMIT,1,crc)['status'],REPLAYED)
        # corrupted words: COMMIT refuses and nothing is held
        bad=list(w);bad[30]^=1;self.cmd(F.BEGIN,1,crc)
        for o in range(0,100,20):self.cmd(F.WRITE,1,crc,o,bad[o:o+20])
        r=self.cmd(F.COMMIT,1,crc);self.assertEqual((r['status'],r['held_index'],r['state']),(BAD_CHECKSUM,0xffffffff,0))
        # an impossible section count is refused even with a matching CRC
        nb=bytearray(rec);struct.pack_into('<I',nb,16,7);nb[396:]=struct.pack('<I',zlib.crc32(bytes(nb[:396]))&0xffffffff)
        self.assertEqual(self.send(1,bytes(nb))['status'],BAD_CHECKSUM)
        q=F.request(999,F.STATUS)
        self.assertEqual(self.cmd(0,raw=q[:-4])['status'],BAD_FRAME)
        self.assertEqual(self.cmd(0,raw=q[:4]+struct.pack('<I',2)+q[8:])['status'],BAD_VERSION)
        self.assertEqual(self.cmd(0,raw=q[:8]+struct.pack('<I',0)+q[12:])['status'],BAD_REQUEST_ID)
        self.assertEqual(self.cmd(7)['status'],BAD_OPERATION)
        self.assertEqual(self.cmd(F.BEGIN,1,crc,0,(1,))['status'],BAD_FRAME)          # data on a non-WRITE op
    def test_block_per_sample_and_chunk_identity(self):
        self.send(0,B.pack_record('A',model(drive=12.0)))
        self.fresh();a=self.run_(self.x(),[0,3,60],chunk=64)
        self.fresh();b=self.run_(self.x(),[0,3,60],chunk=37)
        self.fresh();c=self.run_(self.x(),[0,3,60],per_sample=True)
        self.assertEqual(a,b);self.assertEqual(a,c)
    def ramp(self,x,p,p0):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))();pp=(ct.c_float*6)(*p);q=(ct.c_float*6)(*p0)
        self.c.fcm_block_ramp(a,b,len(x),pp,q);return list(b)
    def test_ramp_equals_constant_when_unchanged_and_glides_otherwise(self):
        self.fresh();a=self.run_(self.x(),[0,3,60]);self.fresh();b=self.ramp(self.x(),[0,3,60],[0,3,60])
        self.assertEqual(len(a),len(b));self.assertTrue(all(abs(u-v)<=1e-6*max(1,abs(u)) for u,v in zip(a,b)))
        # LEVEL 60 -> 90 over one block: the per-sample output gain rises monotonically, no step
        x=[0.0]*1024+[0.3]*64;self.fresh();lo=self.run_(x,[0,0,60]);self.fresh();hi=self.run_(x,[0,0,90])
        self.fresh();self.run_(x[:1024],[0,0,60]);r=self.ramp(x[1024:],[0,0,90],[0,0,60])
        ratio=[(r[i]-lo[1024+i])/(hi[1024+i]-lo[1024+i]) for i in range(64) if abs(hi[1024+i]-lo[1024+i])>1e-6]
        self.assertTrue(all(b>=a-1e-4 for a,b in zip(ratio,ratio[1:])));self.assertAlmostEqual(ratio[-1],1.0,places=3)
    def test_model_switch_reinitializes(self):
        self.send(1,B.pack_record('B',model(drive=15.0)))
        self.fresh();self.run_(self.x(512),[0,0,50]);y=self.run_(self.x(),[1,0,50])
        self.assertEqual(self.c.fcm_loaded(),2)
        self.fresh();z=self.run_(self.x(),[1,0,50]);self.assertEqual(y,z)  # switching restarts from clean state
    def test_tone_controls_are_post_model_and_chunk_independent(self):
        x=self.x(12000)
        self.fresh();flat=self.run_(x,[0,0,50,0,0,0])
        self.fresh();boost=self.run_(x,[0,0,50,6,6,6])
        # Equal band gains are a pure post-model gain, independent of the
        # model's nonlinear dynamics and harmonic generation.
        gain=10**(6/20)
        self.assertLess(max(abs(b-gain*a) for a,b in zip(flat,boost)),.001)
        p=[0,0,50,-6,4,8]
        self.fresh();a=self.run_(x,p,chunk=64)
        self.fresh();b=self.run_(x,p,chunk=37)
        self.fresh();c=self.run_(x,p,per_sample=True)
        self.assertEqual(a,b);self.assertEqual(a,c)
        self.assertNotEqual(flat,a)

if __name__=='__main__':unittest.main()
