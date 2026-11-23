"""Stage 3 host checks. Website -> simulated controller flash -> DSP workspace.
No USB/device access. Conversion is checked against tools/cab_ir.py."""
import ctypes as ct,json,math,os,shutil,struct,subprocess,sys,unittest,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
import cab_ir
import test_web_store as ws

def wav(rate=48000,bits=24,channels=2,tag=1):
    values=[0.0]*20+[math.exp(-i/70)*math.cos(i*.24) for i in range(900)]
    def pack(v):
        if tag==3:return struct.pack('<f' if bits==32 else '<d',v)
        if bits==8:return bytes([max(0,min(255,round(v*127)+128))])
        return round(v*((1<<(bits-1))-1)).to_bytes(bits//8,'little',signed=True)
    data=b''.join(pack(v*.7) for v in values for _ in range(channels))
    fmt=struct.pack('<HHIIHH',tag,channels,rate,rate*channels*(bits//8),channels*(bits//8),bits)
    body=b'WAVEfmt '+struct.pack('<I',len(fmt))+fmt+b'data'+struct.pack('<I',len(data))+data
    return b'RIFF'+struct.pack('<I',len(body))+body

@unittest.skipUnless(shutil.which('node'),'Node.js needed')
class IrFilesTests(unittest.TestCase):
    js=ws.WebStoreTests.js;host=ws.WebStoreTests.host;files=ws.WebStoreTests.files
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/ir_files_editor.dll' if os.name=='nt' else 'build/ir_files_editor.so'),ROOT/'tests/ir_files_host.c',includes=[ROOT/'tests/stubs'],flags=['-DFROST_IR_FFT_PARTS=8u'])))
        cls.lib.ge_host.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
        cls.lib.ge_ir_coefficient.restype=ct.c_float
    def setUp(self):self.lib.ge_init();self.pump()
    def pump(self):self.lib.ge_pump(350)
    def convert(self,data=None,name='WEB CAB'):
        p=ROOT/'build/ir_test.wav';p.write_bytes(data if data is not None else wav())
        return json.loads(subprocess.check_output(['node',str(ROOT/'tests/js/ir_convert.mjs'),str(p),name],text=True))
    def put(self,i=0,name='WEB CAB'):
        r=self.convert(name=name);p=ROOT/'build/ir_test.fir';p.write_bytes(bytes.fromhex(r['record']));self.js(['irPut',i,str(p)]);return r
    def test_wav_encodings_and_rates_match_python_shape(self):
        for rate,bits,channels,tag in [(44100,16,1,1),(48000,24,2,1),(96000,32,4,1),(32000,32,2,3),(22050,64,1,3)]:
            data=wav(rate,bits,channels,tag);r=self.convert(data);h,info=cab_ir.convert(data,taps=512)
            self.assertEqual(r['info']['trimmed'],info['trimmed_leading_samples'])
            self.assertLess(max(abs(a-b) for a,b in zip(h,r['h'])),max(abs(v) for v in h)*.00004)
            rec=bytes.fromhex(r['record']);self.assertEqual(zlib.crc32(rec[:-4]),struct.unpack_from('<I',rec,1044)[0]);self.assertLessEqual(r['info']['l1'],8.0)
        self.assertEqual(self.convert(wav(11025,8,3))['info']['taps'],512)
    def test_js_storage_capacity_names_and_backup(self):
        r=self.put(62);listed,got=self.js(['irs'],['irGet',62]);self.assertEqual(listed,{'62':{'label':'WEB CAB'}});self.assertEqual(got,r['record']);self.assertEqual(self.lib.irf_record(62),1);self.assertIn('ir-20',self.files())
        p=str(ROOT/'build/ir_test.frostbackup');self.js(['backup',p],['irRemove',62]);self.assertEqual(self.lib.irf_record(62),0);self.js(['restore',p]);self.assertEqual(self.lib.irf_record(62),1)
    def test_website_ir_reaches_dsp_and_preset_recall(self):
        c=self.lib;r=self.put();c.irf_cab(3,1);self.pump();self.assertEqual(c.irf_busy(),0);self.assertEqual(c.ge_ir_taps(3),512)
        for i in range(512):self.assertAlmostEqual(c.ge_ir_coefficient(3,i),r['h'][i],places=8)
        self.assertEqual(c.ge_dsp_global(1),0);self.assertEqual(c.ge_flash_save(0),1);self.pump();self.assertEqual(c.irf_ref(0,0),0)
        c.irf_cab(3,0);self.pump();self.assertEqual(c.ge_flash_load(0),1);self.pump();self.assertEqual(c.irf_cab_get(3),1);self.assertEqual(c.ge_ir_taps(3),512)
        c.ge_reboot();self.pump();self.assertEqual(c.ge_flash_load(0),1);self.pump();self.assertAlmostEqual(c.ge_ir_coefficient(3,35),r['h'][35],places=8)
    def test_remove_selected_ir_falls_back_and_replacement_reloads(self):
        c=self.lib;r=self.put();c.irf_cab(3,1);self.pump();self.js(['irRemove',0]);self.pump();self.assertEqual(c.ge_dsp_global(1),0);self.assertNotEqual(c.ge_ir_coefficient(3,35),r['h'][35]);self.put(name='REPLACED');self.pump();self.assertEqual(c.ge_ir_taps(3),512)
    def test_two_ir_references(self):
        c=self.lib;self.put(0);self.put(62);c.irf_two();self.pump();c.irf_cab(3,1);c.irf_cab(4,63);self.pump();self.assertEqual(c.ge_flash_save(4),1);self.pump();self.assertEqual([c.irf_ref(4,i) for i in range(2)],[0,62]);self.assertEqual(c.ge_ir_taps(4),128);self.assertEqual(c.ge_dsp_global(1),0);self.assertEqual(c.ge_flash_load(4),1);self.pump();self.assertEqual([c.irf_cab_get(i) for i in (3,4)],[1,63])
    def test_cab_press_edit_and_selected_only_bypass(self):
        c=self.lib;self.put();c.irf_value_view();c.ge_press();c.ge_turn(1);c.ge_press();self.pump();self.assertEqual(c.irf_cab_get(3),1);before=[c.ge_enabled(i) for i in range(6)];c.ge_foot();self.assertEqual(c.ge_view(),0);c.ge_foot();self.pump();after=[c.ge_enabled(i) for i in range(6)];self.assertEqual([i for i in range(6) if before[i]!=after[i]],[3]);self.assertEqual(c.ge_paint(),0)
    def test_transfer_retries_and_reconnect_recover_partial_workspace(self):
        c=self.lib;r=self.put();c.irf_cab(3,1);c.irf_lost_reply(3);self.pump();self.assertEqual(c.irf_busy(),0);self.assertAlmostEqual(c.ge_ir_coefficient(3,10),r['h'][10],places=8)
        c.irf_cab(3,0);self.pump();c.irf_cab(3,1);c.ge_pump(5);c.irf_client_restart();self.pump();self.assertEqual(c.irf_busy(),0);self.assertEqual(c.ge_dsp_global(1),0);self.assertAlmostEqual(c.ge_ir_coefficient(3,10),r['h'][10],places=8)
    def test_sector_power_cut_preserves_old_or_new_and_neighbors(self):
        c=self.lib;a=bytes.fromhex(self.convert(name='OLD')['record']);b=bytes.fromhex(self.convert(name='NEW')['record'])
        aa=(ct.c_uint8*1048).from_buffer_copy(a);bb=(ct.c_uint8*1048).from_buffer_copy(b)
        # The COW primitive is shared by all groups. Partial data, header, and
        # old-sector erase each leave one usable version and both neighbor slots.
        for op,n in [(1,0),(1,1024),(2,0),(2,28),(2,60),(3,0),(3,2048),(4,0)]:
            c.ge_init();self.pump();self.assertEqual(c.irf_store_write(0,aa),1);self.assertEqual(c.irf_store_write(1,aa),1);self.assertEqual(c.irf_store_write(2,aa),1)
            c.ge_flash_cut(op,n);c.irf_store_write(1,bb);c.ge_flash_cut(0,0);c.ge_reboot();self.pump()
            got=self.js(['irGet',0],['irGet',1],['irGet',2]);self.assertEqual(got[0],a.hex());self.assertIn(got[1],[a.hex(),b.hex()]);self.assertEqual(got[2],a.hex());self.assertEqual(c.irf_store_write(1,bb),1)
    def test_malformed_record_restores_builtin_without_latching_override(self):
        c=self.lib;r=self.put();rec=bytearray.fromhex(r['record']);struct.pack_into('<f',rec,16,float('nan'));struct.pack_into('<I',rec,1044,zlib.crc32(rec[:1044]));arr=(ct.c_uint8*1048).from_buffer_copy(rec)
        self.assertEqual(c.irf_store_write(0,arr),1);c.irf_cab(3,1);self.pump();self.assertEqual(c.irf_busy(),0);self.assertEqual(c.ge_dsp_global(1),0);self.assertNotEqual(c.ge_ir_coefficient(3,10),r['h'][10])
    def test_new_ir_block_installs_builtin_instead_of_leaving_identity(self):
        c=self.lib
        for t in (3,255,3):
            self.assertEqual(c.ge_type_change(4,t),1);self.pump()
            if t==3:
                self.assertEqual(c.irf_cab_get(4),0)
                self.assertGreater(c.ge_ir_taps(4),1)
                self.assertNotAlmostEqual(c.ge_ir_coefficient(4,0),1.0)
                self.assertEqual(c.irf_busy(),0)
    def test_legacy_quiet_ir_is_calibrated_without_rewriting_stored_record(self):
        c=self.lib;r=self.convert();rec=bytearray.fromhex(r['record'])
        scale=struct.unpack_from('<f',rec,16)[0];struct.pack_into('<f',rec,16,scale/8)
        struct.pack_into('<I',rec,1044,zlib.crc32(rec[:1044]));a=(ct.c_uint8*1048).from_buffer_copy(rec)
        self.assertEqual(c.irf_store_write(0,a),1);c.irf_cab(3,1);self.pump()
        self.assertEqual(c.irf_busy(),0);self.assertEqual(self.js(['irGet',0])[0],rec.hex())
        for i in range(512):self.assertAlmostEqual(c.ge_ir_coefficient(3,i),r['h'][i],delta=2e-5)
        self.pump();self.assertEqual(c.irf_busy(),0)
if __name__=='__main__':unittest.main()
