import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
import preset_bank as bank
from preset_download import download
from preset_wire import decode as upload_decode,bits,checksum
import test_preset_read as read_tests

class PresetBankTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/preset_bank.dll' if os.name=='nt' else 'build/preset_bank.so'),
            ROOT/'tests/preset_bank_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.protocol_send.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.lib.protocol_audio.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        cls.lib.bank_admit_words.argtypes=[ct.POINTER(ct.c_uint32)]
    exchange=read_tests.PresetReadTests.exchange
    audio=read_tests.PresetReadTests.audio
    body=read_tests.PresetReadTests.body
    upload=read_tests.PresetReadTests.upload
    def setUp(self):
        self.lib.protocol_init();self.temp=tempfile.TemporaryDirectory(dir=ROOT/'build');self.folder=Path(self.temp.name)
        self.capture=bank.Capture('Float test',self.body(),0xffffffff)
    def tearDown(self):self.temp.cleanup()
    def test_capture_save_bank_load_and_actual_recall_preserve_float_ir_audio(self):
        c=self.lib;self.upload(self.body());self.audio()
        captured=bank.capture_current(self.exchange,'Float IR / edited',first_token=1)
        self.assertEqual(captured.source_revision,2);self.assertEqual(captured.words,tuple(self.body()))
        first=self.audio(4096,True);path=self.folder/'edited.fcap';bank.save_capture(path,captured)
        self.assertEqual(path.stat().st_size,4404);self.assertEqual(bank.load_capture(path),captured)
        output=self.folder/'bank.fbank';bank.build_bank([path],output);loaded=bank.load_bank(output)[0]
        packets=bank.recall_packets(loaded,session=2,revision=2,first_token=2000)
        self.assertEqual(len(packets),217);self.assertEqual(loaded.words[47],0x80000000)
        for p in packets:
            r=upload_decode(self.exchange(p));self.assertIn(r['status'],(0,15))
        self.audio();self.assertEqual(self.audio(4096,True),first)
        self.assertEqual(download(self.exchange)[1],captured.words);self.assertEqual(c.protocol_revision(),3)
        self.assertTrue(c.protocol_callback_has_no_reply())
    def test_native_validation_bounds_types_padding_params_ir_and_resource_limits(self):
        bad=[(0,2),(1,48000),(2,7),(3,99),(9,0),(15,2),(21,2),(27,bits(float('nan'))),
             (28,bits(1.)),(45,0),(45,513),(47,bits(float('inf'))),(47,bits(9.)),(8,5)]
        for i,v in bad:
            words=self.body();words[i]=v
            # Route slot0 is IR: order index9 already3, setting0 duplicates position1.
            with self.subTest(index=i,value=v),self.assertRaises(ValueError):bank.Capture('Bad',words)
        words=self.body();words[3]=0;words[45]=0;words[47:559]=[0]*512 # Only stable slot3 remains IR; unused bank1 must zero.
        with self.assertRaises(ValueError):bank.Capture('Unused bank',words)
        words=self.body();words[45]=1;words[48]=bits(-0.)
        with self.assertRaises(ValueError):bank.Capture('Unused tail',words)
        words=self.body();words[47:559]=[bits(1/64)]*512
        self.assertEqual(bank.Capture('Exact sum8',words).words,tuple(words))
        words[47]=bits(1/64+1/1024)
        with self.assertRaises(ValueError):bank.Capture('Excess sum',words)
        for badwords in (self.body()[:-1],self.body()+[0],[-1]*1071,[True]*1071):
            with self.assertRaises(ValueError):bank.Capture('Bad',badwords)
    def test_capacity_matrix_matches_actual_native_recall_admission(self):
        # Build otherwise-valid duplicated workspace types from native defaults.
        from preset_io import catalog,TYPES
        meta=catalog()
        for types,valid in ((['delay','delay'],False),(['chorus','flanger'],True),
                (['chorus','flanger','chorus'],False),(['reverb','reverb'],False),
                (['ir','ir'],True),(['ir','ir','ir'],False)):
            names=types+['empty']*(6-len(types));w=[1,44100,6]+[TYPES[t] for t in names]+list(range(6))+[int(t!='empty') for t in names]+[0]*6
            for t in names:
                values=[bits(bounds[2]) for bounds in meta[t].values()];w+=values+[0]*(3-len(values))
            n=names.count('ir');w += [1 if i<n else 0 for i in range(2)]
            for i in range(2):w += ([bits(1.)]+[0]*511 if i<n else [0]*512)
            self.lib.protocol_init()
            self.assertEqual(self.lib.bank_admit_words((ct.c_uint32*1071)(*w)),15 if valid else 13)
            if valid:self.assertEqual(bank.Capture('Valid',w).words,tuple(w))
            else:
                with self.assertRaises(ValueError):bank.Capture('Invalid',w)
    def test_integrity_headers_sizes_and_semantics_are_checked_before_loading(self):
        payload=bank.encode_capture(self.capture)
        for changed in (payload[:-1],payload+b'x',b'x'*4404):
            with self.assertRaises(ValueError):bank.decode_capture(changed)
        for offset in (0,8,12,16,20,24,88,130,4403):
            p=bytearray(payload);p[offset]^=1
            with self.assertRaises(ValueError):bank.decode_capture(p)
        # A matching hash does not bypass semantic validation.
        p=bytearray(payload);struct.pack_into('<I',p,120+27*4,bits(float('nan')))
        p[88:120]=hashlib.sha256(p[:88]+p[120:]).digest()
        with self.assertRaises(ValueError):bank.decode_capture(p)
        p=bytearray(payload);p[24]=0xff;p[88:120]=hashlib.sha256(p[:88]+p[120:]).digest()
        with self.assertRaises(ValueError):bank.decode_capture(p)
    def test_bank_bounds_order_names_nested_integrity_and_deterministic_encoding(self):
        captures=[bank.Capture('Preset '+str(i),self.body(),i) for i in range(16)]
        p=bank.encode_bank(captures);self.assertEqual(len(p),70512);self.assertEqual(bank.decode_bank(p),tuple(captures))
        self.assertEqual(bank.encode_bank(captures),p)
        for entries in ([],captures+[captures[0]],[self.capture,self.capture]):
            with self.assertRaises(ValueError):bank.encode_bank(entries)
        for changed in (p[:-1],p+b'x',p[:8]+struct.pack('<I',2)+p[12:]):
            with self.assertRaises(ValueError):bank.decode_bank(changed)
        corrupt=bytearray(p);corrupt[48+200]^=1
        corrupt[16:48]=hashlib.sha256(corrupt[:16]+corrupt[48:]).digest()
        with self.assertRaises(ValueError):bank.decode_bank(corrupt)
        too_large=self.folder/'large.fbank';too_large.write_bytes(b'x'*(bank.MAX_BANK_BYTES+1))
        with self.assertRaises(ValueError):bank.load_bank(too_large)
    def test_name_utf8_bounds_immutability_and_revision_provenance(self):
        for name in ('', 'x'*65,'x\n','\ud800','\0'):
            with self.assertRaises(ValueError):bank.Capture(name,self.body())
        c=bank.Capture('Čist / "test" \\',self.body(),1)
        self.assertEqual(bank.decode_capture(bank.encode_capture(c)),c)
        words=self.body();c=bank.Capture('Independent',words);words[27]=0
        self.assertNotEqual(c.words[27],0)
        for revision in (-1,0x100000000,True):
            with self.assertRaises(ValueError):bank.Capture('Bad',self.body(),revision)
        packets=bank.recall_packets(self.capture,session=1,revision=99)
        self.assertEqual(struct.unpack_from('<I',packets[0],20)[0],99) # Never use captured source revision.
        for args in ((0,1,1),(1,0xffffffff,1),(1,1,0),(True,1,1),(1,1,0xffffffff-215)):
            with self.assertRaises(ValueError):bank.recall_packets(c,*args)
        self.assertEqual(struct.unpack_from('<I',bank.recall_packets(c,1,1,0xffffffff-216)[-1],8)[0],0xffffffff)
    def test_failed_and_interrupted_saves_preserve_existing_capture_and_clean_temp(self):
        path=self.folder/'saved.fcap';bank.save_capture(path,self.capture);original=path.read_bytes();files=set(self.folder.iterdir())
        for operation in ('replace','fsync'):
            with patch('preset_bank.os.'+operation,side_effect=OSError('simulated failure')):
                with self.assertRaises(OSError):bank.save_capture(path,bank.Capture('Changed',self.body()))
            self.assertEqual(path.read_bytes(),original);self.assertEqual(set(self.folder.iterdir()),files)
        with self.assertRaises(ValueError):bank.save_capture(self.folder/'wrong.json',self.capture)
    def test_bank_rejects_invalid_input_without_replacing_output_or_inputs(self):
        source=self.folder/'source.fcap';bank.save_capture(source,self.capture)
        output=self.folder/'bank.fbank';bank.build_bank([source],output);original=output.read_bytes()
        bad=self.folder/'bad.fcap';bad.write_bytes(b'broken')
        with self.assertRaises(ValueError):bank.build_bank([source,bad],output)
        self.assertEqual(output.read_bytes(),original)
        with self.assertRaises(ValueError):bank.save_bank(output,[self.capture],protected=[output])
        with patch('preset_bank.os.replace',side_effect=OSError('interrupted')):
            with self.assertRaises(OSError):bank.build_bank([source],output)
        self.assertEqual(output.read_bytes(),original)
        with self.assertRaises(ValueError):bank.export_header(output,output)
        self.assertEqual(bank.load_capture(source),self.capture)
    def test_failed_download_does_not_publish_a_capture(self):
        path=self.folder/'capture.fcap';bank.save_capture(path,self.capture);original=path.read_bytes()
        def exchange(p):raise OSError('link lost')
        with self.assertRaises(OSError):
            bank.save_capture(path,bank.capture_current(exchange,'New'))
        self.assertEqual(path.read_bytes(),original)
    def test_existing_json_import_is_self_contained_after_assets_change(self):
        ir=self.folder/'ir.wav'
        with wave.open(str(ir),'wb') as f:f.setparams((1,2,44100,0,'NONE',''));f.writeframes(struct.pack('<3h',20000,1000,-200))
        slots=[dict(id=i,type=t,enabled=t!='empty') for i,t in enumerate(['gain','gate','eq','ir','delay','empty'])]
        slots[3]['asset']='ir.wav';slots[4]['parameters']={'TIME':23.5};slots[4]['bypass']='cut'
        source=self.folder/'old.json';source.write_text(json.dumps(dict(format='frost-preset',version=1,name='Imported',sample_rate=44100,
            slots=slots,order=[1,0,2,4,3,5],pre_count=2)),encoding='utf-8')
        dest=self.folder/'imported.fcap';c=bank.import_preset(source,dest);ir.unlink();source.unlink()
        loaded=bank.load_capture(dest);self.assertEqual(c,loaded);self.assertEqual(c.source_revision,0)
        self.assertEqual(c.words[36],bits(1.));self.assertEqual(c.words[39],bits(23.5));self.assertEqual(c.words[25],1)
        self.assertEqual(c.words[47:50],tuple(bits(v/32768) for v in (20000,1000,-200)))
    def test_generated_c_header_compiles_and_matches_all_words_crc_and_escaped_names(self):
        captures=[self.capture,bank.Capture('Čist / "quote" \\',self.body())]
        header=self.folder/'user_bank.h';header.write_bytes(bank.header(captures))
        source=self.folder/'bank_host.c';source.write_text('''#include "user_bank.h"
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API unsigned bank_count(void){return FROST_USER_PRESET_COUNT;}
API unsigned bank_word(unsigned p,unsigned i){return frost_user_presets[p].words[i];}
API unsigned bank_crc(unsigned p){return frost_user_presets[p].crc;}
API const char *bank_name(unsigned p){return frost_user_presets[p].name;}
''',encoding='utf-8')
        # Windows keeps loaded DLLs locked; use persistent ignored build output.
        out=ROOT/('build/generated_user_bank.dll' if os.name=='nt' else 'build/generated_user_bank.so')
        c=ct.CDLL(str(build(out,source,includes=[self.folder])));c.bank_name.restype=ct.c_char_p
        c.bank_word.restype=c.bank_crc.restype=ct.c_uint
        self.assertEqual(c.bank_count(),2)
        for p,capture in enumerate(captures):
            self.assertEqual(c.bank_name(p),capture.name.encode('utf-8'));self.assertEqual(c.bank_crc(p),checksum(capture.words))
            self.assertEqual(tuple(c.bank_word(p,i) for i in range(1071)),capture.words)
    def test_cli_import_build_inspect_and_header_without_device_access(self):
        source=self.folder/'source.fcap';bank.save_capture(source,self.capture)
        output=self.folder/'user.fbank';header=self.folder/'user_bank.h'
        for args in (['build',str(output),str(source)],['header',str(output),str(header)],['inspect',str(output)]):
            p=subprocess.run([sys.executable,str(ROOT/'tools/preset_bank.py'),*args],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
        self.assertIn('FROST_USER_PRESET_COUNT 1u',header.read_text())
        p=subprocess.run([sys.executable,str(ROOT/'tools/preset_bank.py'),'build',str(output),str(source),str(source)],capture_output=True,text=True)
        self.assertEqual(p.returncode,2);self.assertEqual(bank.load_bank(output),(self.capture,))

if __name__=='__main__':unittest.main()
