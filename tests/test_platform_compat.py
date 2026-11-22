"""Locked platform integration checks; need an already fetched checkout, never network."""
import ctypes as ct
import importlib.util
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
from platform_compat import patch_main
from bank_export import bridge_request,bridge_reply,download_bank,request,STATUS
from export_bank_midi import frame

class PlatformCompatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        paths=[ROOT/'.deps/OpenGP5',ROOT/'build/platform-ce8417-review']
        cls.platform=next((p for p in paths if (p/'platform/nds32/gp5_store.c').exists()),None)
        if cls.platform is None:raise unittest.SkipTest('fetch locked OpenGP5 source to run platform integration checks')
        cls.original=(cls.platform/'platform/nds32/main.c').read_text()
    def test_profiles_preserve_confirmed_panel_and_remove_raw_flash_dispatch(self):
        for profile in ('generic','diagnostic'):
            s=patch_main(self.original,profile)
            self.assertIn('0x36, 1, 0xA8',s)
            self.assertIn('return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));',s)
            self.assertNotIn('gp5_flash_',s)
            self.assertNotIn('gp5_store_save(',s)
            self.assertEqual(s.count('#include "bank_host.inc"'),int(profile=='generic'))
            self.assertIn('else fw_foot_release();',s)
            self.assertIn('else fw_para_release();',s)
    def test_changed_flash_panel_and_transport_fail_closed(self):
        for old,new in [('cnt <= 64','cnt <= 63'),('0x36, 1, 0xA0','0x36, 1, 0xA1'),
                        ('uint32_t id, val;','uint32_t changed_id, val;')]:
            self.assertIn(old,self.original)
            with self.assertRaises(ValueError):patch_main(self.original.replace(old,new),'generic')
        with self.assertRaises(ValueError):patch_main(self.original,'unknown')
    def test_actual_patched_dispatch_exports_and_ignores_all_raw_flash_commands(self):
        for profile in ('generic','diagnostic'):
            s=patch_main(self.original,profile)
            a=s.index('static uint8_t dsp_raw[');b=s.index('/* gp5_firmware.h: optional',a)
            dispatch=s[a:b]
            prefix="""#include "tests/bank_export_host.c"
#define DSP_RAW_MAX 1024
#define PROTO_REPLY_MAX 100
static unsigned upstream_dsp_calls;
static unsigned dsp_rx_frames,dsp_unsol_frames,dsp_unsol_cmd,dsp_rx_errs,dsp_rx_bad,dsp_rx_lost;
static unsigned dsp_acks,dsp_naks,dsp_irq_off,dsp_rx_bytes,dsp_unsol_len;
static uint8_t dsp_unsol[64];
static unsigned sat8(unsigned n){return n>255?255:n;}
static unsigned dsp_link_query(uint16_t cmd,const uint8_t *p,unsigned n,uint8_t *r,unsigned max,unsigned wait)
{(void)cmd;(void)p;(void)n;(void)r;(void)max;(void)wait;++upstream_dsp_calls;return 0;}
static unsigned upstream_link_raw(const uint8_t *p,unsigned n,uint8_t *r,unsigned max,unsigned wait,uint8_t *status)
{(void)p;(void)n;(void)r;(void)max;(void)wait;*status=0;++upstream_dsp_calls;return 0;}
#define dsp_link_raw upstream_link_raw
"""
            suffix="""
API unsigned upstream_dispatch(const uint8_t *p,unsigned n)
{unsigned i;bank_host_reply_len=0;if(n<2 || n>64)return 0;proto_req_len=n;
for(i=0;i<n;++i)proto_req[i]=p[i];host_dsp_cmd();return bank_host_reply_len;}
API unsigned upstream_calls(void){return upstream_dsp_calls;}
"""
            with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
                source=Path(folder)/'host.c';source.write_text(prefix+dispatch+suffix)
                library=ROOT/('build/upstream_dispatch_'+profile+('.dll' if os.name=='nt' else '.so'))
                try:c=ct.CDLL(str(build(library,source,includes=[ROOT/'tests/stubs',ROOT/'firmware/controller'])))
                except subprocess.CalledProcessError as error:self.fail(error.stderr)
            c.upstream_dispatch.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
            c.ge_init();c.ge_pump(20)
            def dispatch(p):
                data=(ct.c_uint8*len(p))(*p);n=c.upstream_dispatch(data,len(p))
                return bytes(c.be_byte(i) for i in range(n))
            # Exercise every accepted outer length, including old Frost7E05.
            # 31s: the generic controller answers the OpenGP5 0.5 file commands
            # 7E 05/07/08 for run A only (fcm_files.h); address 0 is refused
            # without any flash operation. 7E 06/09 and every other length
            # stay ignored; the diagnostic profile still answers none.
            refused={(5,7):b'\x7e\x05\x00',(7,6):b'\x7e\x07\x00'}
            for command in range(5,10):
                for n in range(2,65):
                    want=b''
                    if profile=='generic':
                        want=refused.get((command,n),b'\x7e\x08\x00' if command==8 and n>=10 else b'')
                    self.assertEqual(dispatch(bytes((0x7e,command))+bytes(n-2)),want,(command,n))
            self.assertEqual(c.ge_flash_ops(),0)
            self.assertEqual(dispatch(b'\x7e\x05'+request(1,STATUS)),b'')
            self.assertEqual(c.upstream_calls(),0)
            if profile=='generic':
                result=download_bank(lambda p:bridge_reply(dispatch(bridge_request(p))))
                self.assertEqual((len(result.captures),result.ram_mask,result.epoch),(3,0,1))
            else:self.assertEqual(dispatch(bridge_request(request(1,STATUS))),b'')
            self.assertEqual(c.upstream_calls(),0);self.assertEqual(c.ge_muted(),1)
    def test_actual_patched_flash_range_allows_only_store_and_files_run_a(self):
        from platform_compat import patch_flash
        original=(self.platform/'platform/nds32/hal/flash.c').read_text()
        patched=patch_flash(original)
        a=patched.index('static int in_store(');b=patched.index('\n}\n',a)+3
        src="""#include <stdint.h>
#define GP5_FLASH_STORE 0x160000u
#define GP5_FLASH_STORE_SIZE 0x2000u
"""+patched[a:b]+"""
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int range_ok(uint32_t a,unsigned n){return in_store(a,n);}
"""
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            p=Path(folder)/'flash_range.c';p.write_text(src)
            c=ct.CDLL(str(build(ROOT/('build/flash_range'+('.dll' if os.name=='nt' else '.so')),p)))
        ok=lambda a,n:c.range_ok(ct.c_uint32(a),ct.c_uint(n))
        for a,n,want in [(0x160000,4096,1),(0x161000,4096,1),(0x162000,4,0),(0x13f000,4096,1),(0x15f000,4096,1),
                         (0x15fffc,4,1),(0x15fffc,8,0),(0x13efff,4,0),(0x13e000,4096,0),(0x10000,4,0),(0x170000,4,0),
                         (0x13f000,0x21000,1),(0x13f000,0x21004,0),(0xfffffffc,8,0),(0,4,0)]:
            self.assertEqual(ok(a,n),want,(hex(a),n))
        with self.assertRaises(ValueError):patch_flash(original.replace('GP5_FLASH_STORE_SIZE - len','GP5_FLASH_STORE_SIZE - len - 1'))
    def test_actual_original_receiver_reproduces_swallowed_frame_and_idle_stall(self):
        original=(self.platform/'platform/dsp/responder.c').read_text()
        begin=original.index('        while (have >= 4u) {');end=original.index('\n    }\n}\n\nvoid default_handler',begin)
        body=original[begin:end]
        prefix="""#include <stdint.h>
static uint8_t buf[512];static uint32_t have,stats[32],dispatches;
#define BUF_MAX 512u
#define ST stats
static uint8_t crc8(const uint8_t *p,uint32_t n){uint8_t c=0;while(n--){c^=*p++;for(unsigned b=0;b<8;++b)c=(uint8_t)((c&128)?(c<<1)^7:c<<1);}return c;}
static void handle(const uint8_t *p,uint32_t n){(void)p;(void)n;++dispatches;}
__declspec(dllexport) void old_init(void){have=dispatches=0;}
__declspec(dllexport) void old_feed(const uint8_t *p,unsigned n){while(n-- && have<512)buf[have++]=*p++;}
__declspec(dllexport) unsigned old_have(void){return have;}
__declspec(dllexport) unsigned old_poll(void){
"""
        # This optional integration fixture compiles the pinned parser verbatim.
        if os.name!='nt':prefix=prefix.replace('__declspec(dllexport)','__attribute__((visibility("default")))')
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            p=Path(folder)/'old_rx.c';p.write_text(prefix+body+'\nreturn dispatches;}\n')
            c=ct.CDLL(str(build(ROOT/('build/old_rx'+('.dll' if os.name=='nt' else '.so')),p)))
        c.old_feed.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        from test_link_rx import frame
        good=frame(bytes.fromhex('307f0400'));bad=bytes((0,16,0,1))+b'\xff'*4+good
        c.old_init();c.old_feed((ct.c_uint8*len(bad))(*bad),len(bad));self.assertEqual(c.old_poll(),0)
        c.old_init();partial=bytes((0,0,1,1))+b'\xff'*5
        c.old_feed((ct.c_uint8*len(partial))(*partial),len(partial))
        for _ in range(80):self.assertEqual(c.old_poll(),0)
        self.assertEqual(c.old_have(),9)
        from platform_compat import patch_responder
        patched=patch_responder(original);self.assertIn('#include "link_rx.inc"',patched)
        with self.assertRaises(ValueError):patch_responder(original.replace('const uint32_t BUF_MAX = 512u;','const uint32_t BUF_MAX = 511u;'))
    def test_new_bridge_golden_agrees_with_pinned_upstream_codec(self):
        spec=importlib.util.spec_from_file_location('pinned_gp5_proto',self.platform/'tools/gp5_proto.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        golden=module.build_sysex(1,0,b'\x7e\x46')
        self.assertEqual(frame(b'\x7e\x46'),bytes(golden))
        self.assertEqual(frame(b'\x7e\x46').hex(),'f00105000100000002070e0406f7')
        with self.assertRaises(ValueError):bridge_reply(b'\x7e\x05'+bytes(60))

    def test_stack_paint_precedes_c_start_and_changed_layout_fails_closed(self):
        from platform_compat import patch_responder,validate_stack_layout
        original=(self.platform/'platform/dsp/responder.c').read_text()
        layout=(self.platform/'platform/dsp/link.ld').read_text()
        validate_stack_layout(layout)
        patched=patch_responder(original)
        self.assertLess(patched.index('"msr msp, r0'),patched.index('#include "stack_boot.inc"'))
        self.assertLess(patched.index('#include "stack_boot.inc"'),patched.index('"b c_start'))
        for bad in (layout.replace('0x20005C00','0x20005D00'),layout.replace('0x20006C00','0x20006D00')):
            with self.assertRaises(ValueError):validate_stack_layout(bad)
        with self.assertRaises(ValueError):patch_responder(original.replace('ldr r0, =0x20006C00','ldr r0, =0x20006D00'))
        with self.assertRaises(ValueError):patch_responder(original.replace('msr msp, r0','msr msp, r1'))

    def test_every_non_audio_step_is_timed_after_audio_in_order(self):
        from platform_compat import patch_responder
        patched=patch_responder((self.platform/'platform/dsp/responder.c').read_text())
        order=['        audio_poll();','uint32_t frost_iteration=REG(0xE0001004u)','#include "link_tx.inc"','FROST_SERVICE_TX,frost_svc',
               'frost_service_open(&frost_service,frost_timing.last_end','(REG(0xE000E010u) & 0x10000u)','frost_service_take(&frost_service); send_report();','FROST_SERVICE_REPORT,frost_svc','#include "link_rx.inc"','frost_service_iteration(&frost_service,frost_iteration']
        at=[patched.index(x) for x in order];self.assertEqual(at,sorted(at))
        rx=(Path(__file__).resolve().parents[1]/'firmware/dsp/link_rx.inc').read_text()
        self.assertLess(rx.index('FROST_SERVICE_RX'),rx.index('handle(buf+4'))
        self.assertLess(rx.index('frost_cmd=(uint32_t)buf[4]'),rx.index('handle(buf+4'))
        self.assertLess(rx.index('frost_slot=frost_service_open'),rx.index('frost_rx_next'))
        self.assertLess(rx.index('frost_service_take(&frost_service)'),rx.index('handle(buf+4'))
        self.assertIn('frame=frost_slot?frost_rx_next(',rx)
        self.assertIn('if(frame && !frost_tx_dispatch_ready(',rx)
        self.assertLess(rx.index('handle(buf+4'),rx.index('frost_service_note(&frost_service,frost_cmd'))
    def test_dsp_tx_replacement_is_guarded_and_serviced_after_audio(self):
        from platform_compat import patch_responder
        original=(self.platform/'platform/dsp/responder.c').read_text()
        patched=patch_responder(original)
        self.assertNotIn('static void uart_put(',patched)
        self.assertIn('#include "link_tx_platform.inc"',patched)
        tx=patched.index('#include "link_tx.inc"')
        self.assertGreater(tx,patched.index('        audio_poll();'))
        self.assertLess(tx,patched.index('(REG(0xE000E010u) & 0x10000u)'))
        self.assertLess(tx,patched.index('#include "link_rx.inc"'))
        with self.assertRaises(ValueError):patch_responder(original.replace('>= 16u) {}','>= 15u) {}'))

    def test_actual_patched_audio_block_preserves_pcm_and_legacy_counters(self):
        from platform_compat import patch_audio
        original=(self.platform/'platform/dsp/audio.h').read_text()
        patched=patch_audio(original)
        self.assertLess(patched.index('REG(AIF + 0x00) = 0x8070741Bu;'),patched.index('frost_timing.last_end ='))
        with self.assertRaises(ValueError):patch_audio(original.replace('fw_process(blk, bl, br);','changed_process(blk, bl, br);'))
        a=patched.index('        int32_t g = (int32_t)ST[24];')
        b=patched.index('\n    }\n#else',a)
        prefix='''#include <stdint.h>
#include "firmware/dsp/timing_watch.h"
#define GP5_FIRMWARE 1
#define ADMA 0x40030000u
static uint32_t ST[32],clock_value,pending;
static int32_t inL[64],inR[64],outL[64],outR[64];
static uint32_t *reg(uint32_t addr){if(addr==0xE0001004u){clock_value+=100;return &clock_value;}return &pending;}
#define REG(a) (*reg(a))
static void fw_process(const int32_t *x,int32_t *l,int32_t *r)
{(void)x;clock_value+=400;for(unsigned i=0;i<64;++i)l[i]=r[i]=0;}
static void block(void){
'''
        suffix='''
}
API void run(unsigned flags){unsigned i;frost_timing_init(&frost_timing);clock_value=0;pending=flags;
for(i=0;i<32;++i)ST[i]=0;ST[24]=65536;for(i=0;i<64;++i){inL[i]=100;inR[i]=0;outL[i]=outR[i]=123;}block();}
API unsigned value(unsigned i){uint32_t v[]={ST[29],ST[30],ST[31],ST[26],frost_timing.records[0].total,
frost_timing.records[0].input,frost_timing.records[0].process,frost_timing.records[0].output,frost_timing.records[0].pending};return i<9?v[i]:0;}
API unsigned silent(void){for(unsigned i=0;i<64;++i)if(outL[i] || outR[i])return 0;return 1;}
'''
        api='#define API '+('__declspec(dllexport)' if os.name=='nt' else '__attribute__((visibility("default")))')+'\n'
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            p=Path(folder)/'audio.c';p.write_text(api+prefix+patched[a:b]+suffix)
            try:c=ct.CDLL(str(build(ROOT/('build/audio_trace'+('.dll' if os.name=='nt' else '.so')),p)))
            except subprocess.CalledProcessError as error:self.fail(error.stderr)
        for flags in (0,1,15):
            c.run(flags)
            self.assertEqual([c.value(i) for i in range(9)],[700,700,int(bool(flags)),50,700,100,500,100,flags])
            self.assertEqual(c.silent(),1)

    def test_actual_audio_init_prepares_before_hardware_or_dma_activity(self):
        from platform_compat import patch_audio
        patched=patch_audio((self.platform/'platform/dsp/audio.h').read_text())
        a=patched.index('static void audio_init(void) {');b=patched.index('\n#ifdef AUDIO_SPI1',a)
        body=patched[a:b]
        prefix='''#include <stdint.h>
#include "firmware/dsp/timing_watch.h"
#define GP5_FIRMWARE 1
#define ADMA 0x40030000u
#define AIF 0x50000000u
#define ABUF 0x1001F400u
static uint32_t regs[4096],phase,errors,arms,a_half,a_done;
static const uint32_t ACH_DESC[4]={0x40030140,0x40030160,0x40030100,0x40030120};
static uint32_t *reg(uint32_t addr){if(phase!=2)++errors;return &regs[(addr>>2)&4095];}
#define REG(a) (*reg(a))
static void fw_init(void){if(phase)++errors;phase=1;}
static void fw_prepare(void){if(phase!=1 || arms)++errors;phase=2;}
static void audio_arm(uint32_t half){if(phase!=2 || half)++errors;
for(unsigned i=0;i<512;++i)if(regs[((ABUF>>2)+i)&4095])++errors;++arms;}
'''
        api='#define API '+('__declspec(dllexport)' if os.name=='nt' else '__attribute__((visibility("default")))')+'\n'
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            p=Path(folder)/'init.c';p.write_text(api+prefix+body+'\nAPI unsigned run(void){for(unsigned i=0;i<4096;++i)regs[i]=0xa5a5a5a5;audio_init();return phase==2 && !errors && arms==1 && a_half==0 && a_done==0;}\n')
            try:c=ct.CDLL(str(build(ROOT/('build/audio_startup'+('.dll' if os.name=='nt' else '.so')),p)))
            except subprocess.CalledProcessError as error:self.fail(error.stderr)
        self.assertEqual(c.run(),1)

if __name__=='__main__':unittest.main()
