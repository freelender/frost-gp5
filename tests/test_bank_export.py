import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from bank_export import request,decode,download_bank,export_bank,whole_crc,bridge_request,bridge_reply,STATUS,INFO,READ_WORDS,READ_NAME
from preset_bank import load_bank,encode_bank,Capture,header,recall_packets
from export_bank_midi import frame,unframe,MidiBankLink,export_ports
import test_preset_read as read_tests

class BankExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/bank_export.dll' if os.name=='nt' else 'build/bank_export.so'),
            ROOT/'tests/bank_export_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.be_read.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
        c.be_bridge.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint];c.ge_queue_raw.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        c.ge_turn.argtypes=[ct.c_int]
        c.be_epoch.restype=ct.c_uint
    def setUp(self):self.lib.ge_init();self.settle()
    def settle(self,n=900):
        for _ in range(n):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():return
        self.fail('controller did not settle')
    def exchange(self,p):
        a=(ct.c_uint8*len(p))(*p);r=(ct.c_uint8*60)();self.assertEqual(self.lib.be_read(a,len(p),r),60);return bytes(r)
    def bridge(self,p):
        payload=bridge_request(p);a=(ct.c_uint8*len(payload))(*payload);self.assertEqual(self.lib.be_bridge(a,len(payload)),62)
        self.assertEqual((self.lib.be_class(),self.lib.be_index()),(1,0))
        return bridge_reply(bytes(self.lib.be_byte(i) for i in range(62)))
    def test_full_bridge_export_is_exact_and_has_no_live_or_bank_mutation(self):
        c=self.lib;calls=c.ge_calls();revision=c.ge_actual_revision();result=download_bank(self.bridge)
        self.assertEqual((result.epoch,result.ram_mask),(1,0));self.assertEqual(len(result.captures),3)
        for p,capture in enumerate(result.captures):
            self.assertEqual(capture.words,tuple(c.ge_bank_word(p,i) for i in range(1071)));self.assertEqual(capture.source_revision,0)
        self.assertEqual(c.ge_calls(),calls);self.assertEqual(c.ge_actual_revision(),revision);self.assertEqual(c.be_epoch(),1)
        self.assertEqual(whole_crc(result.captures,0),result.crc);self.assertTrue(c.ge_muted())
    def test_ram_saved_entries_override_rom_unsaved_live_edits_are_excluded(self):
        c=self.lib;c.ge_queue_raw(0,0,1250);self.settle();c.ge_ram_save(1);self.settle();self.assertEqual(c.be_epoch(),2)
        c.ge_queue_raw(0,0,1750);self.settle();result=download_bank(self.exchange)
        self.assertEqual(result.ram_mask,2);self.assertEqual(result.captures[1].words[27],0x3fa00000)
        self.assertEqual(result.captures[0].words[27],c.ge_bank_word(0,27))
        c.ge_reconnect();self.settle();self.assertEqual(c.be_epoch(),2)
        c.ge_init();self.settle();self.assertEqual((c.be_epoch(),download_bank(self.exchange).ram_mask),(1,0))
    def test_exported_file_recall_restores_saved_targets_through_real_dsp_handler(self):
        c=self.lib;c.ge_queue_raw(0,0,1250);self.settle();c.ge_ram_save(1);self.settle()
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            path=Path(folder)/'saved.fbank';export_bank(self.bridge,path);saved=load_bank(path)[1]
            c.ge_queue_raw(0,0,1750);self.settle();revision=c.ge_actual_revision()
            for p in recall_packets(saved,session=1,revision=revision,first_token=5000):
                a=(ct.c_uint8*len(p))(*p);self.assertEqual(c.ge_wire(0x7f43,a,len(p)),1)
            c.ge_audio(4);c.ge_pump(30);self.settle()
            self.assertEqual(c.ge_raw(0,0),0x3fa00000);self.assertEqual(c.ge_actual_revision(),revision+1)
            self.assertEqual(c.be_epoch(),2);self.assertTrue(c.ge_muted())
    def test_save_during_export_invalidates_epoch_and_preserves_destination(self):
        c=self.lib
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            path=Path(folder)/'bank.fbank';export_bank(self.exchange,path);original=path.read_bytes()
            calls=0
            def exchange(p):
                nonlocal calls
                calls+=1
                if calls==100:c.ge_ram_save(0);self.settle()
                return self.exchange(p)
            with self.assertRaises(ValueError):export_bank(exchange,path)
            self.assertEqual(path.read_bytes(),original);self.assertEqual(len(list(Path(folder).iterdir())),1)
    def test_reboot_same_epoch_mixed_export_detected_by_whole_crc(self):
        c=self.lib;c.ge_queue_raw(0,0,1250);self.settle();c.ge_ram_save(0);self.settle();calls=0
        def exchange(p):
            nonlocal calls
            calls+=1
            if calls==230:
                c.ge_init();self.settle();c.ge_ram_save(0);self.settle() # Same epoch2, different effective body.
            return self.exchange(p)
        with self.assertRaises(ValueError):download_bank(exchange)
    def test_epoch_exhaustion_fails_closed_but_read_still_works(self):
        c=self.lib;c.be_set_epoch(0xffffffff);self.assertEqual(c.ge_ram_save(0),0)
        self.assertEqual(download_bank(self.exchange).epoch,0xffffffff)
        c.ge_init();self.settle();c.ge_ram_save(0);c.be_set_epoch(0xffffffff);self.settle()
        self.assertEqual(c.ge_capture_error(),1);self.assertEqual(c.ge_ram_valid(0),0);self.assertEqual(c.be_epoch(),0xffffffff)
    def test_frame_validation_random_retries_final_padding_and_inplace(self):
        c=self.lib
        for p,status in ((request(1,STATUS)[:-1],2),(request(0,STATUS),4),(request(1,8),5),
                (request(1,STATUS,entry=0),2),(request(1,INFO,entry=0),7),
                (request(1,INFO,1,9),6),(request(1,INFO,1,0,1),19),(request(1,READ_WORDS,1,0,1071),19),
                (request(1,READ_NAME,1,0,64),19),(request(1,STATUS,epoch=2),7)):
            r=decode(self.exchange(p));self.assertEqual(r['status'],status);self.assertEqual(r['length'],0);self.assertEqual(r['data'],(0,)*5)
        p=request(7,READ_WORDS,1,0,1070);r=decode(self.exchange(p))
        self.assertEqual(r['length'],1);self.assertEqual(r['data'][1:],(0,)*4);self.assertEqual(self.exchange(p),self.exchange(p))
        storage=(ct.c_uint8*61)();ptr=ct.cast(ct.byref(storage,1),ct.POINTER(ct.c_uint8))
        for i,v in enumerate(p):ptr[i]=v
        c.be_read(ptr,32,ptr);self.assertEqual(bytes(storage)[1:],self.exchange(p))
        for payload in (b'',b'\x7e',b'\x7e\x04'):
            a=(ct.c_uint8*len(payload))(*payload);self.assertEqual(c.be_bridge(a,len(payload)),0)
    def test_corrupt_collector_headers_names_metadata_chunks_padding_crc_and_tokens(self):
        for turn,offset,value in ((1,0,0),(1,20,17),(1,24,8),(1,44,42),
                (2,40,123),(2,44,0),(2,48,1),(3,36,19),(3,40,int.from_bytes(b'FAKE','little')),
                (4,28,1),(4,36,4),(218,44,1),(653,40,999)):
            calls=0
            def exchange(p):
                nonlocal calls
                calls+=1;r=bytearray(self.exchange(p))
                if calls==turn:struct.pack_into('<I',r,offset,value)
                return r
            with self.subTest(turn=turn,offset=offset),self.assertRaises(ValueError):download_bank(exchange)
        with self.assertRaises(ValueError):download_bank(lambda p:b'')
        for first in (0,True,0xffffffff):
            with self.assertRaises(ValueError):download_bank(self.exchange,first)
    def test_sysex_codec_golden_crc_and_malformed_nibbles(self):
        raw=frame(b'\x7e\x46');self.assertEqual(raw.hex(),'f00105000100000002070e0406f7')
        self.assertEqual(unframe(raw),b'\x7e\x46')
        for bad in (raw[:-1],b'\0'+raw[1:],raw[:4]+b'\x10'+raw[5:],raw[:-2]+b'\0\xf7'):
            with self.assertRaises(ValueError):unframe(bad)
        for inner in (b'',b'\x11\x6f',request(1,9)):
            with self.assertRaises(ValueError):bridge_request(inner)
    def test_mock_midi_transport_end_to_end_and_atomic_bank_file(self):
        owner=self
        class Message:
            @staticmethod
            def from_bytes(raw):return bytes(raw)
        class Input:
            def __init__(self):self.pending=[];self.closed=False
            def iter_pending(self):
                result=self.pending;self.pending=[];return iter(result)
            def close(self):self.closed=True
        class Output:
            def __init__(self,inp):self.inp=inp;self.sent=[];self.closed=False;self.drop=True
            def send(self,raw):
                self.sent.append(raw);payload=unframe(raw)
                owner.assertEqual(payload[:2],b'\x7e\x46')
                if self.drop:self.drop=False;return
                result=owner.bridge(payload[2:]);reply=frame(b'\x7e\x46'+result)
                stale=bytearray(result);struct.pack_into('<I',stale,8,999999)
                bad=bytearray(reply);bad[1]^=1
                self.inp.pending.extend([type('Noise',(),dict(type='note_on',data=()))(),
                    type('Stale',(),dict(type='sysex',data=frame(b'\x7e\x46'+stale)[1:-1]))(),
                    type('BadCRC',(),dict(type='sysex',data=bad[1:-1]))()])
                self.inp.pending.append(type('Reply',(),dict(type='sysex',data=reply[1:-1]))())
            def close(self):self.closed=True
        inp=Input();out=Output(inp)
        class Mido:
            def open_input(self,name):owner.assertEqual(name,'input');return inp
            def open_output(self,name):owner.assertEqual(name,'output');return out
        mido=Mido();mido.Message=Message
        tick=[0.]
        def sleep(n):tick[0]+=n
        link=MidiBankLink(mido,inp,out,timeout=.1,clock=lambda:tick[0],sleep=sleep)
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            path=Path(folder)/'export.fbank';result=export_bank(link.exchange,path)
            self.assertEqual(load_bank(path),result.captures);self.assertEqual(out.sent[0],out.sent[1])
            # Resource cleanup is exercised through the public port wrapper.
            out.drop=False
            with patch('export_bank_midi.MidiBankLink',return_value=link):export_ports('input','output',path,mido=mido)
            self.assertTrue(inp.closed and out.closed)
        with self.assertRaises(ValueError):link.exchange(b'\x11\x6f')
    def test_midi_timeouts_are_bounded_and_cannot_send_loader_commands(self):
        class Message:
            @staticmethod
            def from_bytes(raw):return bytes(raw)
        class Empty:
            def iter_pending(self):return iter(())
        class Out:
            def __init__(self):self.sent=[]
            def send(self,m):self.sent.append(m)
        mido=type('Mido',(),dict(Message=Message));out=Out();now=[0.]
        def sleep(n):now[0]+=n
        link=MidiBankLink(mido,Empty(),out,.1,2,lambda:now[0],sleep)
        with self.assertRaises(TimeoutError):link.exchange(request(1,STATUS))
        self.assertEqual(len(out.sent),2);self.assertEqual(out.sent[0],out.sent[1])
        for timeout,attempts in ((float('nan'),2),(.01,2),(1,4)):
            with self.assertRaises(ValueError):MidiBankLink(mido,Empty(),out,timeout,attempts)

class MaximumBankExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        w=read_tests.PresetReadTests.body(None)
        cls.captures=[Capture(('Č'*30)+f'{i:02}',w) for i in range(16)] #62 UTF-8 bytes, four NAME chunks.
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            folder=Path(folder);(folder/'user_bank.h').write_bytes(header(cls.captures))
            cls.lib=ct.CDLL(str(build(ROOT/('build/max_bank_export.dll' if os.name=='nt' else 'build/max_bank_export.so'),
                ROOT/'tests/bank_export_host.c',includes=[ROOT/'tests/stubs',folder],flags=['-DFROST_USER_BANK=1'])))
        cls.lib.be_read.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
    def test_full_maximum_bank_utf8_and_both_float_ir_banks(self):
        c=self.lib;c.ge_init();c.ge_pump(15);calls=[]
        def exchange(p):
            calls.append(p);a=(ct.c_uint8*len(p))(*p);r=(ct.c_uint8*60)();c.be_read(a,len(p),r);return bytes(r)
        result=download_bank(exchange,0xffffffff-3521)
        self.assertEqual(len(calls),3522);self.assertEqual(result.captures,tuple(self.captures))
        self.assertEqual(len(encode_bank(result.captures)),70512)

if __name__=='__main__':unittest.main()
