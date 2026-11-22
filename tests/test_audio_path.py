"""Actual Frost ADC adapter + patched platform processing block, modeled IO.

Fault injections exist only in this native fixture, never in pedal firmware.
"""
import ctypes as ct
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from platform_compat import patch_audio

class AudioPathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        platform=next((p for p in (ROOT/'.deps/OpenGP5',ROOT/'build/platform-ce8417-review')
                       if (p/'platform/dsp/audio.h').exists()),None)
        if platform is None:raise unittest.SkipTest('locked platform checkout required; no network fetch')
        audio=patch_audio((platform/'platform/dsp/audio.h').read_text())
        start=audio.index('        int32_t g = (int32_t)ST[24];')
        end=audio.index('\n    }\n#else',start)
        prefix='''#define FROST_AUDIO_ADC 1
#include "tests/slot_protocol_host.c"
#define GP5_FIRMWARE 1
#define ADMA 0x40030000u
static uint32_t ST[32],clock_value,pending;
static unsigned ir_calls,reset_calls;
__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *function,void *caller)
{(void)caller;/* 31a: FFT replay steps return precomputed outputs; not kernel evaluations. */
if(function==(void *)frost_ir_sample && !frost_ir_fft_replay)++ir_calls;
/* Count evaluated IR samples, including two per paired-kernel entry. The
 * persistent-corrupt-IR assertion still requires exactly one scalar sample. */
if(function==(void *)frost_ir_pair)ir_calls+=2;
/* 25a block FIR: eight outputs per dot8 entry, one per dot1 entry. */
if(function==(void *)frost_ir_dot8)ir_calls+=8;
if(function==(void *)frost_ir_dot1)++ir_calls;
/* 31a: one FFT block call is one kernel evaluation (64 outputs at once). */
if(function==(void *)frost_ir_fft_block)++ir_calls;
if(function==(void *)frost_chain_reset_histories)++reset_calls;}
__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *function,void *caller)
{(void)function;(void)caller;}
static struct {uint32_t before,data[64],after;} buffers[4];
#define inL buffers[0].data
#define inR buffers[1].data
#define outL buffers[2].data
#define outR buffers[3].data
static uint32_t *reg(uint32_t addr){if(addr==0xE0001004u){clock_value+=100;return &clock_value;}return &pending;}
#define REG(a) (*reg(a))
static void platform_block(void){
'''
        suffix='''
}
API void ap_init(void){unsigned i;protocol_init();fw_prepare();clock_value=pending=ir_calls=reset_calls=0;
for(i=0;i<32;++i)ST[i]=0;
for(i=0;i<4;++i){buffers[i].before=0x12345678;buffers[i].after=0xabcdef01;}}
API unsigned ap_block(int32_t left,int32_t right,uint32_t level,uint32_t flags)
{unsigned i,before=reply_count;ST[24]=level;pending=flags;
for(i=0;i<64;++i){inL[i]=(uint32_t)left;inR[i]=(uint32_t)right;outL[i]=outR[i]=0xdeadbeef;}
platform_block();
for(i=0;i<64;++i)if(outL[i] || outR[i])return 0;
for(i=0;i<4;++i)if(buffers[i].before!=0x12345678 || buffers[i].after!=0xabcdef01)return 0;
return reply_count==before && frost_bench_mute;}
API uint32_t ap_value(unsigned i)
{union {float f;uint32_t u;} peak;peak.f=frost_output_meter.peak;
uint32_t v[]={frost_chain.faults,frost_input_meter.frames,frost_output_meter.frames,
frost_output_meter.outside_unity,frost_output_meter.pcm_rails,frost_output_meter.invalid,
peak.u,slot_protocol.revision,commands,(uint32_t)frost_input_meter.minimum,
(uint32_t)frost_input_meter.maximum,ST[31]};return i<12?v[i]:0;}
API void ap_fault(unsigned kind,uint32_t bits)
{union {float f;uint32_t u;} v;v.u=bits;
if(kind==0)frost_chain.slots[2].state.eq.low=v.f;
else {FrostIR *ir=frost_chain.slots[3].state.ir;ir->coefficients[0]=v.f;
/* 31a: the FFT path convolves with the spectra; corrupt the state it uses. */
{FrostIRFFT *f=frost_ir_fft_of(ir);if(f && f->parts)f->spectrum[0][0]=v.f;}}}
API void ap_eq_dry(void){frost_chain.slots[2].wet=frost_chain.slots[2].wet_target=0;}
API unsigned ap_fix_ir(void){float identity=1.0f;return frost_chain_load_ir(&frost_chain,3,&identity,1);}
API unsigned ap_overload(void){unsigned types[2]={FROST_GAIN,FROST_GAIN},i;
if(!frost_chain_configure(&frost_chain,types,2))return 0;
for(i=0;i<2;++i){if(!frost_chain_enable(&frost_chain,i,1) || !frost_chain_set(&frost_chain,i,0,4.0f))return 0;}return 1;}
API void ap_saturate(void){frost_chain.faults=frost_input_meter.frames=frost_output_meter.frames=
frost_output_meter.outside_unity=frost_output_meter.pcm_rails=UINT32_MAX;}
API unsigned ap_calls(unsigned which){return which?reset_calls:ir_calls;}
'''
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            source=Path(folder)/'audio_path.c';source.write_text(prefix+audio[start:end]+suffix)
            try:cls.c=ct.CDLL(str(build(ROOT/('build/audio_path'+('.dll' if os.name=='nt' else '.so')),
                                      source,includes=[ROOT/'tests/stubs'],flags=['-finstrument-functions'])))
            except subprocess.CalledProcessError as error:raise RuntimeError(error.stderr) from error
        cls.c.ap_block.argtypes=[ct.c_int32,ct.c_int32,ct.c_uint32,ct.c_uint32]
        cls.c.ap_value.restype=ct.c_uint32
        cls.c.ap_fault.argtypes=[ct.c_uint,ct.c_uint32]
        cls.c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
    def setUp(self):self.c.ap_init()
    def block(self,left=1073741824,right=0,level=65536,flags=0):
        self.assertEqual(self.c.ap_block(left,right,level,flags),1)
    def test_q31_extremes_and_all_output_level_signs_still_write_literal_zero(self):
        for left in (-2147483648,0,2147483647):
            for right in (-2147483648,0,2147483647):
                for level in (0,65535,65536,0x7fffffff,0x80000000,0xffffffff):
                    self.c.ap_init();self.block(left,right,level)
                    expected=(left>>1)-(right>>1)
                    self.assertEqual(ct.c_int32(self.c.ap_value(9)).value,expected)
                    self.assertEqual(ct.c_int32(self.c.ap_value(10)).value,expected)
                    self.assertEqual((self.c.ap_value(1),self.c.ap_value(2)),(64,64))
    def test_eq_nan_and_infinity_are_contained_and_recover_even_when_dry(self):
        for dry in (False,True):
            for bits in (0x7fc00000,0x7f800000,0xff800000):
                self.c.ap_init()
                if dry:self.c.ap_eq_dry()
                self.c.ap_fault(0,bits);self.block()
                self.assertEqual(self.c.ap_value(0),1);self.assertEqual(self.c.ap_value(5),0)
                for _ in range(3):self.block()
                self.assertEqual(self.c.ap_value(0),1)
    def test_persistent_invalid_ir_resets_once_and_latches_until_initialization(self):
        self.c.ap_fault(1,0x7fc00000);self.block();self.assertEqual(self.c.ap_value(0),1)
        self.assertEqual((self.c.ap_calls(0),self.c.ap_calls(1)),(1,1))
        for _ in range(4):self.block()
        self.assertEqual((self.c.ap_calls(0),self.c.ap_calls(1)),(1,1))
        self.assertEqual(self.c.ap_fix_ir(),1);self.block()
        self.assertEqual(self.c.ap_value(0),1);self.assertEqual(self.c.ap_value(5),0)
        self.assertEqual((self.c.ap_calls(0),self.c.ap_calls(1)),(1,1))
        p=(ct.c_uint8*4)(0x33,0x7f,4,0);self.c.protocol_send(0x7f33,p,4);self.block()
        self.assertEqual((self.c.ap_calls(0),self.c.ap_calls(1)),(1,1))
        self.c.ap_init();self.block()
        # 31a: the cabinet IR runs on the FFT workspace: one block call per
        # 64-sample block (25a's direct block FIR made 65 evaluations).
        self.assertEqual(self.c.ap_value(0),0);self.assertEqual(self.c.ap_calls(0),1)
    def test_valid_overload_is_reported_without_becoming_an_engine_fault(self):
        self.assertEqual(self.c.ap_overload(),1)
        for _ in range(20):self.block()
        self.assertGreater(self.c.ap_value(3),0);self.assertGreater(self.c.ap_value(4),0)
        peak=struct.unpack('<f',struct.pack('<I',self.c.ap_value(6)))[0]
        self.assertGreater(peak,1);self.assertEqual((self.c.ap_value(0),self.c.ap_value(5)),(0,0))
    def test_nonfinite_parameter_write_rejected_before_audio_or_revision_change(self):
        p=struct.pack('<HH8I',0x7f40,36,1,99,1,1,0,0,0,0x7fc00000)
        self.assertEqual(self.c.protocol_send(0x7f40,(ct.c_uint8*len(p))(*p),len(p)),1)
        reply=bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
        self.assertEqual(struct.unpack_from('<I',reply,12)[0],10)
        self.assertEqual((self.c.ap_value(7),self.c.ap_value(8)),(1,0))
        self.block();self.assertEqual(self.c.ap_value(0),0)
    def test_saturated_fault_and_meter_counters_never_wrap_or_unmute(self):
        self.c.ap_saturate();self.c.ap_fault(1,0x7fc00000);self.block(flags=15)
        for i in (0,1,2,3,4):self.assertEqual(self.c.ap_value(i),0xffffffff)
        self.assertEqual(self.c.ap_value(5),0)
        self.assertEqual(self.c.ap_value(11),1) # Modeled DMA pending mask, not hardware evidence.

if __name__=='__main__':unittest.main()
