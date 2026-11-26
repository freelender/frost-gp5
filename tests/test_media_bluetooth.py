"""Bounded BLE stop/resume, queue reset and interrupt-state preservation."""
import ctypes as ct
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
class BluetoothDriverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/media_bluetooth.dll' if os.name=='nt' else 'build/media_bluetooth.so'),ROOT/'tests/media_bluetooth_host.c')))
    def values(self):return [self.lib.get(i) for i in range(14)]
    def test_disable_connected_clears_queues_and_events(self):
        c=self.lib;c.init(1,2,1);c.enable(0);v=self.values()
        self.assertEqual(v,[0,1,0x16,1,0,0,0,1,0,0,0,0,0,0])
        c.enable(0);self.assertEqual(self.values(),v)
    def test_resume_advertising_and_preserve_masked_interrupts(self):
        c=self.lib;c.init(1,1,0);c.enable(0);c.enable(1)
        self.assertEqual(self.values(),[1,0,0,0,1,1,1,2,0,1,0,256,0x33A,0])
        before=self.values();c.enable(1);self.assertEqual(self.values(),before)
    def test_uninitialized_or_failed_radio_does_not_touch_registers(self):
        c=self.lib;c.init(0,0,1);before=self.values();c.enable(1);c.enable(0)
        self.assertEqual(self.values(),before)
if __name__=='__main__':unittest.main()
