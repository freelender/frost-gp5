import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

def write_png(path,pixels,scale=3):
    rows=[]
    for y in range(80):
        row=bytearray()
        for x in range(160):
            color=pixels[y*160+x];r=(color>>11)&31;g=(color>>5)&63;b=color&31
            row.extend(bytes((r*255//31,g*255//63,b*255//31))*scale)
        rows.extend([b'\0'+bytes(row)]*scale)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',160*scale,80*scale,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b''))

class ChainUITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suffix='.dll' if os.name=='nt' else '.so'
        cls.lib=ct.CDLL(str(build(ROOT/('build/chain_ui'+suffix),ROOT/'tests/chain_ui_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.ui_pixel.restype=ct.c_uint16
        cls.lib.ui_turn.argtypes=[ct.c_int]
    def setUp(self):self.lib.ui_init()
    def test_selection_edit_bypass_and_back(self):
        lib=self.lib;self.assertEqual(lib.ui_selected(),1)
        lib.ui_turn(1);lib.ui_foot()
        self.assertEqual(lib.ui_selected(),2);self.assertEqual(lib.ui_enabled(2),0)
        self.assertTrue(all(lib.ui_enabled(i) for i in range(6) if i!=2))
        lib.ui_bounce();self.assertEqual(lib.ui_enabled(2),0)
        lib.ui_press();self.assertEqual(lib.ui_view(),1)
        lib.ui_press();lib.ui_turn(1);self.assertEqual(lib.ui_value(2,0),2)
        lib.ui_foot();self.assertEqual(lib.ui_view(),2);self.assertEqual(lib.ui_enabled(2),1)
        lib.ui_press();lib.ui_turn(-1);self.assertEqual(lib.ui_parameter(),2)
        lib.ui_press();self.assertEqual(lib.ui_view(),0);self.assertEqual(lib.ui_selected(),2)
        self.assertTrue(lib.ui_safe())
    def test_bounds_and_no_dsp_effect_settings(self):
        lib=self.lib
        lib.ui_turn(2147483647);self.assertLess(lib.ui_selected(),6)
        lib.ui_turn(-2147483648);self.assertLess(lib.ui_selected(),6)
        lib.ui_init();lib.ui_press();lib.ui_press()
        lib.ui_turn(-2147483648);self.assertEqual(lib.ui_value(1,0),0)
        lib.ui_turn(2147483647);self.assertEqual(lib.ui_value(1,0),4000)
        self.assertTrue(lib.ui_safe())
    def test_bounded_render_and_panel_pixel_format(self):
        lib=self.lib;self.assertEqual(lib.ui_paint(),9);self.assertEqual(lib.ui_blits(),10)
        lib.ui_paint();self.assertEqual(lib.ui_blits(),10);self.assertEqual(lib.ui_bad(),0)
        pixels=[lib.ui_pixel(x,y) for y in range(80) for x in range(160)]
        self.assertGreater(len(set(pixels)),3)
        write_png(ROOT/'build/frost-chain-ui.png',pixels)
        lib.ui_press();lib.ui_paint()
        write_png(ROOT/'build/frost-chain-ui-edit.png',[lib.ui_pixel(x,y) for y in range(80) for x in range(160)])
        suffix='.dll' if os.name=='nt' else '.so'
        panel=ct.CDLL(str(build(ROOT/('build/chain_ui_panel'+suffix),ROOT/'tests/chain_ui_host.c',
            includes=[ROOT/'tests/stubs'],flags=['-DUI_PANEL'])))
        panel.ui_pixel.restype=ct.c_uint16;panel.ui_init();panel.ui_paint()
        panel.ui_color.restype=ct.c_uint16
        # Actual byte stream sent by gp5_blit, independently specified RGB565.
        for rgb,wire in (((255,0,0),b'\xf8\x00'),((0,255,0),b'\x07\xe0'),((0,0,255),b'\x00\x1f'),((40,156,255),b'\x2c\xff')):
            self.assertEqual(struct.pack('<H',panel.ui_color(*rgb)),wire)
        for x,y in ((0,0),(59,16),(60,17),(8,30)):
            raw=panel.ui_pixel(x,y);rgb=((raw>>8)|(raw<<8))&65535
            self.assertEqual(rgb,pixels[y*160+x])

if __name__=='__main__':unittest.main()
