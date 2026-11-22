import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import sys
import struct
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class MetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/metadata.dll' if os.name=='nt' else 'build/metadata.so'),
            ROOT/'tests/chain_connected_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.connected_turn.argtypes=[ct.c_int]
        cls.lib.connected_set.argtypes=[ct.c_uint32,ct.c_uint32]
        cls.lib.connected_next.argtypes=[ct.POINTER(ct.c_uint32),ct.POINTER(ct.c_uint32)]
        cls.lib.connected_registry_meta.restype=ct.c_float
        cls.lib.connected_target.restype=ct.c_float
    def setUp(self):self.lib.connected_init();self.lib.connected_drain()
    def test_editor_wire_and_registry_contract(self):
        c=self.lib
        # The 11 historical wire IDs, including UI row 4 mapping to DSP IR slot 3.
        expected=[(0,0,0,0,0,0),(0,0,1,0,0,0),(1,0,0,1,1,0),(1,0,1,1,1,0),
                  (2,0,0,2,2,0),(2,0,1,2,2,0),(2,1,1,2,2,1),(2,2,1,2,2,2),
                  (4,0,0,3,3,0),(4,0,2,3,3,0),(4,1,1,3,3,0)]
        for id,mapping in enumerate(expected):
            with self.subTest(id=id):
                self.assertEqual(tuple(c.connected_meta(id,i) for i in range(6)),mapping)
                if mapping[2]==0:continue
                wire=[c.connected_meta(id,i) for i in range(6,11)]
                self.assertEqual([c.connected_ui_meta(id,i) for i in range(5)],wire)
                if mapping[2]==2:continue # taps are an asset control, not a DSP parameter
                for field,index in enumerate((0,1,3)):
                    # Legacy muted IR row stays at unity; new IR instances
                    # have product default1.6 (VOL80). Bounds still agree.
                    expected=1.6 if id==10 and field==2 else wire[index]/wire[4]
                    self.assertEqual(c.connected_registry_meta(id,field),ct.c_float(expected).value)
    def test_every_editor_parameter_reaches_dsp_and_clamps(self):
        c=self.lib
        for id in (1,3,5,6,7,9,10):
            with self.subTest(id=id):
                c.connected_init();c.connected_drain()
                row=c.connected_meta(id,0);parameter=c.connected_meta(id,1)
                lo=c.connected_meta(id,6);hi=c.connected_meta(id,7);scale=c.connected_meta(id,10)
                c.connected_turn(row-1);c.connected_press();c.connected_turn(parameter);c.connected_press()
                for detents,expected in ((-2147483648,lo),(2147483647,hi)):
                    c.connected_turn(detents);self.assertGreater(c.connected_drain(),0)
                    self.assertEqual(c.connected_get(id),expected)
                    self.assertEqual(c.connected_target(id),ct.c_float(expected/scale).value)
                self.assertEqual(c.connected_metric(6),0)
        self.assertTrue(c.connected_muted())
    def test_rejected_wire_bounds_preserve_targets(self):
        c=self.lib
        for id in range(11):
            before=c.connected_target(id);setting=c.connected_get(id)
            lo=c.connected_meta(id,6);hi=c.connected_meta(id,7)
            bad=[hi+1,0xffffffff]+([lo-1] if lo else [])
            if id==9:bad.append(17)
            for value in bad:
                c.connected_set(id,value)
                self.assertEqual(c.connected_target(id),before)
                self.assertEqual(c.connected_get(id),setting)
        # Editor steps must not accidentally quantize valid continuous wire values.
        for id,value in ((1,1001),(3,11),(5,1001),(10,1001)):
            c.connected_set(id,value);self.assertEqual(c.connected_get(id),value)
    def test_coalesced_retry_and_reconnect_use_latest_values(self):
        c=self.lib
        c.connected_turn(-1);c.connected_press();c.connected_press() # gain value
        for _ in range(25):c.connected_turn(1)
        c.connected_retry(1);c.connected_retry(0xffffffff)
        items=[];id=ct.c_uint32();value=ct.c_uint32()
        while c.connected_next(ct.byref(id),ct.byref(value)):items.append((id.value,value.value))
        self.assertEqual(items,[(0,1),(1,1625)])
        c.connected_ready();self.assertEqual(c.connected_drain(),11)
        self.assertEqual(c.connected_get(1),1625)
        # Both inactive rows queue nothing, even after selected-only foot presses.
        for row in (3,5):
            c.connected_init();c.connected_drain();c.connected_turn(row-1);c.connected_foot()
            self.assertEqual(c.connected_drain(),0)

    def test_diagnostic_frames_preserve_previous_controller(self):
        reference=json.loads((ROOT/'tests/fixtures/diagnostic-ui-reference.json').read_text(encoding='utf-8'))
        c=self.lib
        for frame in reference['frames']:
            with self.subTest(action=frame['action'],argument=frame['argument']):
                fn=getattr(c,'connected_'+frame['action'])
                if frame['action']=='turn':fn(frame['argument'])
                else:fn()
                c.connected_paint()
                pixels=struct.pack('<12800H',*(c.connected_pixel(i) for i in range(12800)))
                self.assertEqual(hashlib.sha256(pixels).hexdigest(),frame['pixel_sha256'])

if __name__=='__main__':unittest.main()
