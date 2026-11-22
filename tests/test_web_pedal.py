"""web/pedal.js (the site's pedal code, EXPERIMENTAL) against the real controller
C code: every USB request the JS file layer sends is answered by the generic
controller's handler (fcm_files.h) on a simulated run-A flash; the controller's
client then sends the selected record to the DSP code. Also checks the SysEx
framing against tools' gp5_proto. Skips without Node.js."""
import ctypes as ct,json,os,shutil,struct,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
import fcm_bank as B
import fcm_files as F
from test_fcm import model
@unittest.skipUnless(shutil.which('node'),'Node.js not installed')
class WebPedalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/web_pedal_editor.dll' if os.name=='nt' else 'build/web_pedal_editor.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.ge_host.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
        for f in ('ge_dsp_rx','ge_fcm_client','ge_fcm_ws'):getattr(cls.lib,f).restype=ct.c_uint
        cls.lib.ge_fcm_ws_gain.restype=ct.c_float
    def setUp(self):self.lib.ge_init()
    def host(self,q):
        a=(ct.c_uint8*len(q))(*q);o=(ct.c_uint8*100)();n=self.lib.ge_host(a,len(q),o);return bytes(o[:n]) if n else None
    def run_js(self,*ops,drop_every=0):
        p=subprocess.Popen(['node',str(ROOT/'tests/js/files_proxy.mjs'),*ops],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        n=0
        while True:
            line=p.stdout.readline()
            if not line:raise AssertionError('proxy ended early')
            m=json.loads(line)
            if 'done' in m:p.stdin.close();p.wait(10);p.stdout.close();return m['done']
            n+=1;r=None if drop_every and n%drop_every==0 else self.host(bytes.fromhex(m['q']))
            p.stdin.write(json.dumps({'r':r.hex() if r is not None else None})+'\n');p.stdin.flush()
    def rec(self,name,gain):
        p=ROOT/f'build/web_{name}.fcm';r=B.pack_record(name,model(gain=gain));p.write_bytes(r);return p,r
    def test_js_add_list_remove_through_the_controller_code(self):
        pa,ra=self.rec('JCM800',2.0);pb,rb=self.rec('PLEXI',1.2)
        d=self.run_js(f'add:1:{pa}',f'add:2:{pb}',f'add:1:{pb}',drop_every=7)    # every 7th reply lost: retried
        self.assertEqual(d['who'],{'platform':[0,2,1],'firmware':'frost','storage':2})
        self.assertEqual({k:v['label'] for k,v in d['models'].items()},{'1':'PLEXI','2':'PLEXI'})
        self.assertEqual(d['used'],2);self.assertEqual(bytes.fromhex(d['download1']),rb)
        # the Python tool reads the same flash identically
        py=F.Pedal(lambda q:self.host(bytes(q)));self.assertEqual(sorted(py.models()),['amp-01','amp-02'])
        d=self.run_js('remove:2');self.assertEqual(list(d['models']),['1']);self.assertEqual(d['remove2'],1)
    def test_written_model_reaches_the_dsp_and_plays(self):
        c=self.lib;pa,ra=self.rec('JCM800',2.5);self.run_js(f'add:4:{pa}')
        for _ in range(100):
            c.ge_pump(1)
            if c.ge_ready():break
        self.assertEqual(c.ge_type_change(3,33),1)
        for _ in range(100):c.ge_pump(1)
        self.assertEqual(c.ge_queue(3,0,4),1)
        for _ in range(8):
            c.ge_advance(250)
            for _ in range(40):c.ge_pump(1)
        self.assertEqual([c.ge_dsp_rx(0),c.ge_dsp_rx(1),c.ge_dsp_rx(2)],[2,4,struct.unpack_from('<I',ra,396)[0]])
        self.assertEqual(c.ge_fcm_ws(3,2),1);self.assertAlmostEqual(c.ge_fcm_ws_gain(3),2.5,places=6)
    def test_sysex_framing_matches_the_python_protocol(self):
        tools=next(iter(sorted(ROOT.glob('build/gp5-line31*-adc/tools'))+sorted(ROOT.glob('.deps/OpenGP5/tools'))),None)
        if tools is None or not (tools/'gp5_proto.py').exists():self.skipTest('platform gp5_proto.py not staged')
        sys.path.insert(0,str(tools));import gp5_proto as G
        js="import {buildSysex,parseSysex} from './web/pedal.js';const a=buildSysex([0x7e,0x05,0,0xf0,0x13,0,64]);console.log(Buffer.from(a).toString('hex'),Buffer.from(parseSysex(a)).toString('hex'))"
        out=subprocess.run(['node','--input-type=module','-e',js],cwd=ROOT,capture_output=True,text=True,check=True).stdout.split()
        self.assertEqual(bytes.fromhex(out[0]),G.build_sysex(G.CLASS_CMD,0,[0x7e,0x05,0,0xf0,0x13,0,64]));self.assertEqual(out[1],'7e0500f0130040')
if __name__=='__main__':unittest.main()
