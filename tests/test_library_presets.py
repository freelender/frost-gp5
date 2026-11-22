"""Host tooling for version-2 (pedal-inspired model) presets: JSON files,
lossless capture files, mixed banks and C-header export. No device IO."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import preset_bank as pb
from preset_io import validate,save,load,render
from preset_wire import snapshot_words,WORDS,MAX_WORDS,checksum

def slots(types):
    return [dict(id=i,type=t,enabled=t!='empty') for i,t in enumerate(types)]
def data(types,version=2,**extra):
    d=dict(format='frost-preset',version=version,name='LIB',sample_rate=44100,slots=slots(types),order=list(range(6)),pre_count=6)
    d.update(extra);return d

class LibraryPresetTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.folder=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def test_v2_json_words_and_v1_compatibility(self):
        legacy=validate(data(['gain','gate','eq','overdrive','empty','empty'],version=1),self.folder)
        w=snapshot_words(legacy);self.assertEqual((len(w),w[0]),(WORDS,1))
        d=data(['ge7','rv6','ts','dd3','vb2','empty']);d['slots'][0]['parameters']={'6.4K':-7.5,'LEVEL':2.}
        d['slots'][1]['parameters']={'MODE':4.};d['slots'][3]['parameters']={'FOOT':1.}
        p=validate(d,self.folder);w=snapshot_words(p);self.assertEqual((len(w),w[0]),(MAX_WORDS,2))
        f=lambda v:struct.unpack('<I',struct.pack('<f',v))[0]
        self.assertEqual(w[WORDS+3],f(-7.5));self.assertEqual(w[WORDS+4],f(2.)) # GE-7 params 6,7
        self.assertEqual(w[27+3*1:27+3*1+3][0],f(25.)) # RV-6 E.LEVEL default in the v1 region
        # Explicit older preset values remain valid despite subtler defaults.
        d['slots'][1]['parameters']={'E.LEVEL':35.,'TIME':50.,'MODE':4.}
        old=snapshot_words(validate(d,self.folder))
        self.assertEqual(old[30:33],[f(35.),f(50.),f(50.)])
        for bad in (data(['ts']+['empty']*5,version=1),):
            with self.assertRaises(ValueError):validate(bad,self.folder)
        d2=data(['rv6']+['empty']*5);d2['slots'][0]['parameters']={'MODE':1.5}
        with self.assertRaises(ValueError):validate(d2,self.folder) # stepped mode must be integral
        d3=data(['dd3','dm2']+['empty']*4)
        with self.assertRaises(ValueError):validate(d3,self.folder) # one delay-class workspace
        d4=data(['ce2','m117','pog']+['empty']*3)
        with self.assertRaises(ValueError):validate(d4,self.folder) # two modulation workspaces
    def test_v2_capture_bank_header_roundtrip_and_mixed_lengths(self):
        legacy=snapshot_words(validate(data(['gain','gate','eq','empty','empty','empty'],version=1),self.folder))
        modern=snapshot_words(validate(data(['ge7','spring','pog','klon','ns2','empty']),self.folder))
        a=pb.Capture('OLD',tuple(legacy));b=pb.Capture('NEW',tuple(modern))
        self.assertEqual(pb.decode_capture(pb.encode_capture(b)),b)
        self.assertEqual(len(pb.encode_capture(a)),pb.CAP_BYTES) # version-1 files unchanged
        bank=pb.encode_bank([a,b]);self.assertEqual(pb.decode_bank(bank),(a,b))
        with self.assertRaises(ValueError):pb.decode_bank(bank[:-4])
        h=pb.header([a,b]).decode()
        self.assertIn('frost_user_words_0[1071]',h);self.assertIn('frost_user_words_1[1101]',h)
        packets=pb.recall_packets(b,1,1);self.assertEqual(len(packets),1+221+1)
        self.assertEqual(struct.unpack_from('<I',packets[-1],24)[0],1101)
        with self.assertRaises(ValueError):pb.Capture('BAD',tuple(modern[:1071]))
        bad=list(modern);bad[0]=1
        with self.assertRaises(ValueError):pb.Capture('BAD',tuple(bad))
    def test_v2_json_renders_offline(self):
        import wave
        src=self.folder/'in.wav'
        with wave.open(str(src),'wb') as w:
            w.setparams((1,2,44100,0,'NONE','not compressed'))
            w.writeframes(struct.pack('<'+'h'*4410,*[int(8000*((i//50)%2*2-1)) for i in range(4410)]))
        d=data(['ts','ce2','dm2','rv6','ge7','empty']);d['tail_seconds']=.2
        path=self.folder/'lib.json';save(path,validate(d,self.folder))
        self.assertEqual(json.loads(path.read_text())['version'],2)
        r=render(path,src,self.folder/'out.wav');self.assertGreater(r['frames'],4410)
        self.assertEqual(load(path).data['slots'][3]['parameters']['MODE'],1.)

if __name__=='__main__':unittest.main()
