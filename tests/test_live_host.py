"""Live editing over USB (firmware/controller/live_host.h): the website reads
the chain and changes type, enable and parameters through the controller's
editor queue; the DSP follows. Host check with the controller + DSP fixture."""
import ctypes as ct,os,struct,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
class LiveHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/live_host'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.ge_actual_value.restype=ct.c_float
    def setUp(self):self.c.ge_init();self.settle()
    def settle(self,n=300):
        for _ in range(n):self.c.ge_paint();self.c.ge_pump(1)
    def host(self,q):
        a=(ct.c_uint8*len(q))(*q);o=(ct.c_uint8*100)();n=self.c.ge_host(a,len(q),o);return bytes(o[:n]) if n else None
    def chain(self):
        r=self.host([0x7e,0x18]);self.assertEqual(len(r),28);self.assertEqual(r[2],1)
        return list(r[3:9]),list(r[9:15]),list(r[15:21]),struct.unpack_from('<I',r,21)[0],r[25]
    def slot(self,s):
        r=self.host([0x7e,0x14,s]);self.assertEqual(len(r),24)
        return dict(type=r[4],enabled=r[5],position=r[6],count=r[7],values=list(struct.unpack_from('<8h',r,8))[:r[7]])
    def test_read_chain_and_slots(self):
        types,order,enabled,rev,editable=self.chain()
        self.assertEqual(editable,1);self.assertEqual(sorted(order),list(range(6)))
        for s in range(6):
            d=self.slot(s);self.assertEqual(d['type'],types[s])
            if types[s]!=255:self.assertEqual(d['type'],self.c.ge_actual_type(s))
    def test_type_enable_and_parameter_reach_the_dsp(self):
        self.assertEqual(self.host([0x7e,0x17,4,34]),b'\x7e\x17\x01');self.settle()
        self.assertEqual(self.c.ge_actual_type(4),34)
        self.assertEqual(self.host([0x7e,0x16,4,1]),b'\x7e\x16\x01');self.settle()
        self.assertEqual(self.c.ge_actual_enabled(4),1)
        self.assertEqual(self.host([0x7e,0x15,4,0,*struct.pack('<h',1)]),b'\x7e\x15\x01');self.settle()   # DROP = 1 STEP
        self.assertEqual(self.slot(4)['values'][0],1);self.assertAlmostEqual(self.c.ge_actual_value(4,0),1.0,places=5)
        self.assertEqual(self.host([0x7e,0x15,4,1,*struct.pack('<h',999)]),b'\x7e\x15\x01');self.settle()  # clamped to 100
        self.assertEqual(self.slot(4)['values'][1],100)
    def test_chain_reply_reports_dsp_limit_refusals_for_the_website_popup(self):
        c=self.c;r=self.host([0x7e,0x18]);count=r[26];self.assertEqual(r[27],0)
        try:
            c.ge_load_budget(c.ge_load_chain()+1000)                         # POG (110k) no longer fits
            self.assertEqual(self.host([0x7e,0x17,4,28]),b'\x7e\x17\x01');self.settle()   # queued, then refused
            r=self.host([0x7e,0x18]);self.assertEqual(r[26],(count+1)&255);self.assertEqual(r[27],2)
            self.assertNotEqual(r[3+4],28)                                    # the block stays as it was
        finally:c.ge_load_budget(0xffffffff)
    def test_reorder_moves_blocks_and_keeps_their_settings(self):
        """32p 7E 1F: drag-and-drop on the website reorders the live chain."""
        ok=lambda c:bytes([0x7e,c,1])
        self.assertEqual(self.host([0x7e,0x17,4,34]),ok(0x17));self.settle()       # DROP in slot 4
        self.assertEqual(self.host([0x7e,0x15,4,1,*struct.pack('<h',60)]),ok(0x15));self.settle()
        types,order,_,_,_=self.chain()
        new=[order[4]]+[s for s in order if s!=order[4]]                             # position 5 to the front
        self.assertEqual(self.host([0x7e,0x1f,*new]),ok(0x1f));self.settle()
        t2,o2,_,_,_=self.chain();self.assertEqual(o2,new);self.assertEqual(t2,types)
        self.assertEqual([self.c.ge_actual_order(i) for i in range(6)],new)            # the DSP follows
        self.assertEqual(self.slot(4)['values'][1],60)                                  # settings stay with the block
        for bad in ([0,1,2,3,4,4],[0,1,2,3,4,6],[0,1,2,3,4]):
            r=self.host([0x7e,0x1f,*bad]);self.assertTrue(r is None or r[2]==0,bad)
        self.assertEqual(self.chain()[1],new)
    def test_save_live_sound_to_a_named_preset(self):
        """32p 7E 20: the website saves the live sound like PRESETS -> SAVE."""
        st=self.host([0x7e,0x20]);self.assertEqual(list(st[:5]),[0x7e,0x20,1,0,0]);self.assertEqual(len(st),8)
        self.assertEqual(self.host([0x7e,0x20,7,*b'MY SOUND']),bytes([0x7e,0x20,1]))
        for _ in range(400):
            self.c.ge_pump(1);st=self.host([0x7e,0x20])
            if not st[3]:break
        self.assertEqual((st[3],st[4],st[5]),(0,0,7))
        self.c.ge_store_name.restype=ct.c_char_p
        self.assertEqual(self.c.ge_store_preset(7),1);self.assertEqual(self.c.ge_store_name(7),b'MY SOUND')
        self.assertEqual(self.host([0x7e,0x20,60,*b'X'])[2],0)                         # preset 61 does not exist
        self.assertEqual(self.host([0x7e,0x20,8])[2],1)                                 # no name: default
        for _ in range(400):
            self.c.ge_pump(1)
            if not self.host([0x7e,0x20])[3]:break
        self.assertEqual(self.c.ge_store_name(8),b'PRESET 09')
    def save_and_wait(self,index,name):
        self.assertEqual(self.host([0x7e,0x20,index,*name])[2],1)
        for _ in range(600):
            self.c.ge_pump(1);st=self.host([0x7e,0x20])
            if not st[3]:return st
        self.fail('save did not finish')
    def test_save_retries_an_interrupted_read_back(self):
        """32q: a BUSY read-back (chain transition) restarts the capture instead
        of failing; after too many it fails with reason 2 and stores nothing."""
        self.c.ge_busy_reads(2);st=self.save_and_wait(9,b'RETRY')
        self.assertEqual((st[4],st[7]),(0,2));self.assertEqual(self.c.ge_store_preset(9),1)
        self.c.ge_busy_reads(50);st=self.save_and_wait(10,b'NEVER');self.c.ge_busy_reads(0)
        self.assertEqual((st[4],st[6],st[7]),(1,2,4));self.assertEqual(self.c.ge_store_preset(10),0)
    def test_bad_requests_are_refused(self):
        self.assertEqual(self.host([0x7e,0x15,9,0,0,0]),b'\x7e\x15\x00')     # bad slot
        self.assertEqual(self.host([0x7e,0x17,0,200]),b'\x7e\x17\x00')       # unknown type
        self.assertEqual(self.host([0x7e,0x15,0,40,0,0]),b'\x7e\x15\x00')     # bad parameter
    def test_cabinet_selection_uses_real_asset_client_and_preset_reference(self):
        r=self.host([0x7e,0x19,3]);self.assertEqual(len(r),25);self.assertEqual(r[2:5],bytes([1,3,0]))
        self.assertEqual(r[8:].split(b'\0')[0],b'BUILT IN')
        self.assertEqual(self.host([0x7e,0x1a,3,63]),b'\x7e\x1a\x01');self.settle()
        r=self.host([0x7e,0x19,3]);self.assertEqual(r[4],63);self.assertEqual(r[7],0)
        self.assertEqual(self.c.ge_flash_save(0),1);self.settle()
        self.assertEqual(self.host([0x7e,0x1a,3,0]),b'\x7e\x1a\x01');self.settle()
        self.assertEqual(self.c.ge_flash_load(0),1);self.settle()
        self.assertEqual(self.host([0x7e,0x19,3])[4],63)
    def test_cabinet_rejects_wrong_type_out_of_range_and_busy(self):
        self.assertEqual(self.host([0x7e,0x1a,0,1]),b'\x7e\x1a\x00')
        self.assertEqual(self.host([0x7e,0x19,0]),b'\x7e\x19\x00')
        self.assertEqual(self.host([0x7e,0x1a,3,64]),b'\x7e\x1a\x00')
        self.assertEqual(self.host([0x7e,0x1a,3,1]),b'\x7e\x1a\x01')
        self.c.ge_pump(2)
        self.assertEqual(self.host([0x7e,0x1a,3,2]),b'\x7e\x1a\x00')
if __name__=='__main__':unittest.main()
