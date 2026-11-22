"""web/store.js (the site's stage-2 storage code) against the real controller C
code on a simulated flash: legacy AMP files migrated into packed groups and
still found by the pedal, AMP add/remove, presets saved on the pedal listed /
renamed / swapped / deleted by the site and seen by the pedal, backup/restore.
Skips without Node.js."""
import ctypes as ct,json,os,shutil,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
import fcm_bank as B
import fcm_files as F
from test_fcm import model
@unittest.skipUnless(shutil.which('node'),'Node.js not installed')
class WebStoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.lib=ct.CDLL(str(build(ROOT/('build/web_store_editor.dll' if os.name=='nt' else 'build/web_store_editor.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c.ge_host.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
        c.ge_fcm_label.restype=ct.c_char_p;c.ge_fcm_label.argtypes=[ct.c_int];c.ge_preset_label.restype=ct.c_char_p
    def setUp(self):self.lib.ge_init();self.settle()
    def settle(self,n=300):
        for _ in range(n):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():return
        self.fail('controller did not settle')
    def host(self,q):
        a=(ct.c_uint8*len(q))(*q);o=(ct.c_uint8*100)();n=self.lib.ge_host(a,len(q),o);return bytes(o[:n]) if n else None
    def js(self,*ops):
        p=subprocess.Popen(['node',str(ROOT/'tests/js/store_proxy.mjs'),json.dumps(ops)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        while True:
            line=p.stdout.readline()
            if not line:raise AssertionError('proxy ended early: '+p.stderr.read())
            m=json.loads(line)
            if 'done' in m:p.stdin.close();p.wait(10);p.stdout.close();p.stderr.close();self.lib.ge_advance(2500);return m['done']
            r=self.host(bytes.fromhex(m['q']));p.stdin.write(json.dumps({'r':r.hex() if r is not None else None})+'\n');p.stdin.flush()
    def rec(self,name,gain):
        p=ROOT/f'build/store_{name}.fcm';r=B.pack_record(name,model(gain=gain));p.write_bytes(r);return str(p)
    def files(self):return sorted(f['name'] for f in F.Pedal(lambda q:self.host(bytes(q))).files())
    def test_legacy_models_migrate_and_pedal_still_finds_them(self):
        c=self.lib;ped=F.Pedal(lambda q:self.host(bytes(q)))
        for i,n in ((1,'JCM800'),(2,'FENDER'),(3,'IVORY')):ped.add(i,B.pack_record(n,model(gain=1+i/10)),say=lambda *_:None)
        self.assertEqual(self.files(),['amp-01','amp-02','amp-03'])
        moved,=self.js(['migrate']);self.assertEqual(moved,3);self.assertEqual(self.files(),['amp-0'])
        self.assertEqual([c.ge_fcm_label(i) for i in (1,2,3,4)],[b'01 JCM800',b'02 FENDER',b'03 IVORY',b'04 DEFAULT'])
        put,amps=self.js(['ampPut',45,self.rec('WEB',2.0)],['amps'])
        self.assertEqual({k:v['label'] for k,v in amps.items()},{'1':'JCM800','2':'FENDER','3':'IVORY','45':'WEB'})
        self.assertEqual(c.ge_fcm_label(45),b'45 WEB');self.assertEqual(self.files(),['amp-0','amp-4'])
        self.js(['ampRemove',45]);self.assertEqual(c.ge_fcm_label(45),b'45 DEFAULT');self.assertEqual(self.files(),['amp-0'])
    def test_pedal_presets_are_managed_by_the_site(self):
        c=self.lib
        for i in (0,1,20):c.ge_queue_raw(1,0,1000+100*i);self.settle();self.assertEqual(c.ge_flash_save(i),1);self.settle()
        before=self.js(['get',0])[0]
        listed,_,_,_,after=self.js(['presets'],['rename',0,'Clean Lead'],['swap',1,20],['delete',20],['presets'])
        self.assertEqual(listed,{'0':{'name':'PRESET 01'},'1':{'name':'PRESET 02'},'20':{'name':'PRESET 21'}})
        self.assertEqual(after,{'0':{'name':'Clean Lead'},'1':{'name':'PRESET 21'}})             # swap 02<->21, then delete 21 (the old 02)
        self.assertEqual(c.ge_preset_label(0),b'01 CLEAN LEAD');self.assertEqual(c.ge_store_preset(1),1)
        self.assertNotEqual(self.js(['get',0])[0],before)
        self.assertEqual(c.ge_flash_load(0),1);self.settle()                                      # the renamed preset still loads
    def test_backup_and_restore_everything(self):
        c=self.lib;self.js(['ampPut',7,self.rec('BAK',1.7)]);c.ge_flash_save(5);self.settle()
        bak=str(ROOT/'build/store_test.frostbackup');self.js(['backup',bak])
        self.js(['ampRemove',7],['delete',5]);self.assertEqual(c.ge_store_preset(5),0);self.assertEqual(c.ge_fcm_label(7),b'07 DEFAULT')
        self.js(['restore',bak]);self.assertEqual(c.ge_store_preset(5),1);self.assertEqual(c.ge_fcm_label(7),b'07 BAK')
        self.assertEqual(self.files(),['amp-0','glb-0','pst-0'])
if __name__=='__main__':unittest.main()
