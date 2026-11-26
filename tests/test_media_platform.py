"""Pinned optional upstream driver integration; never fetches or accesses hardware."""
import ctypes as ct
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools')]
from host import build
from prepare_gp5 import stage
from export_bank_midi import frame

class MediaPlatformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=Path(os.environ.get('FROST_MEDIA_PLATFORM',ROOT/'.deps/OpenGP5Media'))
        base=ROOT/'.deps/OpenGP5'
        if not source.exists() or not base.exists():raise unittest.SkipTest('both locked platform checkouts required')
        cls.tmp=tempfile.TemporaryDirectory(prefix='frost-media-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.stage=stage(base,Path(cls.tmp.name)/'stage',audio_input='adc',audio_output='line-test',
            control_scheduler=True,large_code=True,xflash=True,overlays=True,reverb_int16=True,
            dsp_usage=True,ir_link=True,media_platform=source)
    def test_staging_keeps_base_and_media_pins_and_single_spi_owner(self):
        m=json.loads((self.stage/'frost-input.json').read_text())
        lock=json.loads((ROOT/'dependencies.lock.json').read_text())
        self.assertEqual(m['platform_commit'],lock['OpenGP5']['commit'])
        self.assertEqual(m['media']['commit'],lock['OpenGP5Media']['commit'])
        self.assertFalse(m['media']['physical_validation'])
        self.assertIn('hal/usb.c',m['media']['source_sha256'])
        app=self.stage/'platform/nds32'
        script=(app/'build.sh').read_text()
        self.assertNotIn('audio_link.c',script)
        self.assertIn('FROST_USB_AUDIO=1',script)
        self.assertEqual((app/'start/irq.c').read_text().count('[13] = H(gp5_link_irq)'),1)
        self.assertIn('"GP-5 Open FW", "F144"',(app/'usb_midi.c').read_text())
        for p in self.stage.rglob('*.sh'):self.assertNotIn(b'\r\n',p.read_bytes())
    def test_bluetooth_waits_for_saved_preference_and_can_resume_without_rf_restart(self):
        app=self.stage/'platform/nds32'
        driver=(app/'bt/ble.c').read_text()
        start=driver[driver.index('int ble_start('):driver.index('#include "media_bluetooth.inc"')]
        self.assertNotIn('adv_schedule(slot_now())',start)
        self.assertIn('frost_ble_ready = 1',start)
        self.assertIn('if (was == BLE_OFF) return;',driver)
        self.assertIn('if (ble.state == BLE_OFF) return;',driver)
        adapter=(self.stage/'firmwares/frost/nds32/media_bluetooth.inc').read_text()
        self.assertNotIn('radio_start(',adapter)
        self.assertNotIn('gp5_bt_start(',adapter)
        self.assertIn('void gp5_bt_set_enabled(int on);',(app/'bt/ble.h').read_text())

    def test_interleaved_usb_ble_sysex_and_usb_only_loader(self):
        s=(self.stage/'platform/nds32/proto.c').read_text()
        s=s[s.index('volatile uint32_t proto_frames'):s.index('void loader_handover(void)')]
        s=s.replace('(void)ev; (void)n; return 0; }','(void)ev; (void)n; ++ble_writes; return 1; }')
        prefix='''#include <stdint.h>
#define PROTO_REQ_MAX 64
#define PROTO_REPLY_MAX 100
#define PROTO_NONE 0
#define PROTO_TO_LOADER 1
#define PROTO_DSP 2
static unsigned usb_writes,ble_writes;
static int usb_midi_write(uint8_t *e,unsigned n){(void)e;(void)n;++usb_writes;return 1;}
'''
        api='#define API '+('__declspec(dllexport)' if os.name=='nt' else '__attribute__((visibility("default")))')+'\n'
        suffix='''
API unsigned feed(unsigned port,const uint8_t *e){proto_port=(uint8_t)port;return proto_feed(e);}
API unsigned result(unsigned i){return i==0?proto_frames:i==1?proto_bad:i==2?usb_writes:i==3?ble_writes:proto_req[i-4];}
API void reply(unsigned port){uint8_t p[2]={0x7e,0x21};proto_port=(uint8_t)port;proto_reply(1,0,p,2);}
'''
        folder=self.stage.parent
        p=folder/'proto-test.c';p.write_text(prefix+api+s+suffix)
        try:c=ct.CDLL(str(build(ROOT/('build/media_proto.dll' if os.name=='nt' else 'build/media_proto.so'),p)))
        except subprocess.CalledProcessError as exc:self.fail(exc.stderr)
        c.feed.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8)]
        def events(payload):
            raw=frame(payload);out=[]
            for pos in range(0,len(raw),3):
                b=raw[pos:pos+3];out.append((ct.c_uint8*4)(4 if len(raw)-pos>3 else 4+len(b),*b,*([0]*(3-len(b)))))
            return out
        a,b=events(b'\x7e\x18'),events(b'\x7e\x19')
        for i in range(len(a)):
            ra=c.feed(0,a[i]);rb=c.feed(1,b[i])
        self.assertEqual((ra,rb),(2,2));self.assertEqual(c.result(0),2);self.assertEqual(c.result(1),0)
        c.reply(0);c.reply(1);self.assertGreater(c.result(2),0);self.assertGreater(c.result(3),0)
        for e in events(b'\x11\x6f'):r=c.feed(1,e)
        self.assertEqual(r,0)
        for e in events(b'\x11\x6f'):r=c.feed(0,e)
        self.assertEqual(r,1)

if __name__=='__main__':unittest.main()
