import ctypes as ct
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class ConnectedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/chain_connected.dll' if os.name=='nt' else 'build/chain_connected.so'),
            ROOT/'tests/chain_connected_host.c',includes=[ROOT,ROOT/'tests/stubs'])))
        cls.lib.connected_set.argtypes=[ct.c_uint32,ct.c_uint32]
    def setUp(self):self.lib.connected_init()
    def test_startup_resync_and_readback(self):
        c=self.lib;self.assertEqual(c.connected_drain(),11);self.assertEqual(c.connected_drain(),0)
        self.assertEqual([c.connected_get(i) for i in range(11)],[1,1000,1,1000,1,1000,1000,1000,1,128,1000])
        c.connected_ready();self.assertEqual(c.connected_drain(),11)
        self.assertEqual(c.connected_metric(0),0x46584332)
        self.assertEqual(c.connected_metric(7),22)
        c.connected_retry(3);self.assertEqual(c.connected_drain(),1)
        c.connected_retry(11);self.assertEqual(c.connected_drain(),0)
    def test_edit_and_selected_only_bypass(self):
        c=self.lib;c.connected_drain()
        # Starts at gate: footswitch changes only gate; editor adjusts threshold.
        c.connected_foot();c.connected_drain()
        self.assertEqual([c.connected_get(i) for i in (0,2,4)],[1,0,1])
        c.connected_press();c.connected_press();c.connected_turn(3);c.connected_drain()
        self.assertEqual(c.connected_get(3),1300)
        c.connected_press();c.connected_turn(1);c.connected_press() # Back to chain
        c.connected_turn(-1);c.connected_press();c.connected_press();c.connected_turn(2);c.connected_drain()
        self.assertEqual(c.connected_get(1),1050)
        c.connected_press();c.connected_turn(1);c.connected_press();c.connected_turn(2) # EQ
        c.connected_press()
        for parameter in range(3):
            c.connected_press();c.connected_turn(parameter+1);c.connected_press();c.connected_drain()
            self.assertEqual(c.connected_get(5+parameter),1000+25*(parameter+1))
            c.connected_turn(1)
        self.assertTrue(c.connected_muted())
    def test_reject_invalid_and_keep_hard_mute(self):
        c=self.lib;c.connected_drain()
        for id,value in [(0,2),(1,4001),(3,0),(3,100001),(7,2001),(8,2),(0xffffffff,1)]:
            self.assertEqual(c.connected_set(id,value),1) # ACK transport, reject setting
        self.assertEqual(c.connected_metric(6),7)
        self.assertEqual(c.connected_get(1),1000)
        self.assertTrue(c.connected_muted());self.assertEqual(c.connected_metric(5),0)
        c.connected_turn(2);c.connected_foot() # disabled NAM placeholder
        self.assertEqual(c.connected_drain(),0)

    def test_ir_editor_and_length_validation(self):
        c=self.lib;c.connected_drain();c.connected_turn(3) # Gate -> IR
        c.connected_press();c.connected_press();c.connected_turn(2);c.connected_drain()
        self.assertEqual(c.connected_get(9),160)
        c.connected_foot();c.connected_drain();self.assertEqual(c.connected_get(8),0)
        c.connected_press();c.connected_turn(1);c.connected_press();c.connected_turn(-2);c.connected_drain()
        self.assertEqual(c.connected_get(10),950)
        for n in (0,15,17,513):c.connected_set(9,n)
        self.assertEqual(c.connected_get(9),160);self.assertEqual(c.connected_metric(6),4)
        self.assertTrue(c.connected_muted())

if __name__=='__main__':unittest.main()
