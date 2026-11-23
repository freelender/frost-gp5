import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from preset_io import catalog,validate,load,save,render
from chain_wav import run

class PresetTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=ROOT/'build');self.folder=Path(self.temp.name)
        self.path=self.folder/'preset.json';self.source=self.folder/'source.wav';self.out=self.folder/'output.wav'
        self.write_wav(self.source,[0]*6000+[10000,-10000]*1500)
        self.write_wav(self.folder/'ir.wav',[20000,1000,-200])
    def tearDown(self):self.temp.cleanup()
    def write_wav(self,path,values,rate=44100):
        with wave.open(str(path),'wb') as w:w.setparams((1,2,rate,0,'NONE',''));w.writeframes(struct.pack('<'+'h'*len(values),*values))
    def data(self):
        types=['gain','gain','eq','ir','delay','empty']
        slots=[dict(id=i,type=t,enabled=t!='empty',parameters={}) for i,t in enumerate(types)]
        slots[0]['parameters']={'MULTIPLIER':1.75};slots[1]['parameters']={'MULTIPLIER':.5}
        slots[2]['parameters']={'LOW':.75,'MID':1.25,'HIGH':.5};slots[3]['asset']='ir.wav'
        slots[4]['parameters']={'TIME':7.5,'FEEDBACK':.45,'MIX':.25};slots[4]['bypass']='cut'
        return dict(format='frost-preset',version=1,name='Existing blocks',sample_rate=44100,slots=slots,order=[1,0,4,2,3,5],pre_count=2,tail_seconds=.1)
    def test_metadata_roundtrip_preserves_independent_values_order_and_assets(self):
        p=validate(self.data(),self.folder);save(self.path,p);q=load(self.path)
        self.assertEqual(p.data,q.data);self.assertEqual(p.assets,q.assets)
        self.assertEqual(q.data['slots'][4]['parameters']['TIME'],7.5)
        self.assertEqual(q.data['order'],[1,0,4,2,3,5]);self.assertEqual(q.data['pre_count'],2)
        self.assertEqual(q.data['slots'][0]['parameters']['MULTIPLIER'],1.75)
        self.assertEqual(q.data['slots'][1]['parameters']['MULTIPLIER'],.5)
        self.assertEqual(len(q.data['slots'][3]['asset_sha256']),64)
        self.assertEqual(len(catalog()),60) # 59 registered effects plus empty
    def test_offline_recall_matches_existing_renderer_and_reports_levels(self):
        d=self.data();d['order']=list(range(6));d['slots'][1]['parameters']['MULTIPLIER']=1.75
        d['slots'][4]['bypass']='keep';d['slots'][4]['parameters']={'TIME':250.,'FEEDBACK':.35,'MIX':.3}
        save(self.path,validate(d,self.folder));metrics=render(self.path,self.source,self.out)
        reference=self.folder/'reference.wav'
        run(self.source,reference,gain=1.75,eq=(.75,1.25,.5),effects=['gain','gain','eq','ir','delay'],ir=self.folder/'ir.wav',tail_seconds=.1)
        self.assertEqual(reference.read_bytes(),self.out.read_bytes())
        self.assertEqual(metrics['containment_faults'],0)
    def test_slot_order_and_enable_state_change_recalled_audio(self):
        d=self.data();d['slots'][0]['type']='overdrive';d['slots'][0]['parameters']={'DRIVE':8.,'LEVEL':.5}
        d['slots'][1]['parameters']={'MULTIPLIER':3.};d['order']=list(range(6))
        save(self.path,validate(d,self.folder));render(self.path,self.source,self.out);first=self.out.read_bytes()
        d['order']=[1,0,2,3,4,5];save(self.path,validate(d,self.folder));render(self.path,self.source,self.out)
        self.assertNotEqual(first,self.out.read_bytes());second=self.out.read_bytes()
        d['slots'][0]['enabled']=False;save(self.path,validate(d,self.folder));render(self.path,self.source,self.out)
        self.assertNotEqual(second,self.out.read_bytes())
    def test_unknown_fields_versions_types_nonfinite_and_capacity_reject(self):
        mutations=[lambda d:d.update(version=3),lambda d:d['slots'][0].update(type='ts'),lambda d:d.update(sample_rate=48000),lambda d:d.update(extra=1),
            lambda d:d.update(order=[0]*6),lambda d:d.update(pre_count=True),lambda d:d['slots'][1].update(id=0),
            lambda d:d['slots'][0].update(type='drop_tune'),lambda d:d['slots'][0].update(enabled=1),
            lambda d:d['slots'][0]['parameters'].update(MULTIPLIER=float('nan')),
            lambda d:d['slots'][0]['parameters'].update(MULTIPLIER=True),
            lambda d:d['slots'][0]['parameters'].update(GAIN=1.),
            lambda d:d['slots'][5].update(type='delay',enabled=True),
            lambda d:d['slots'][0].update(asset='ir.wav'),lambda d:d['slots'][5].update(enabled=True)]
        for mutate in mutations:
            d=self.data();mutate(d)
            with self.assertRaises(ValueError):validate(d,self.folder)
        for bad in ('{"format":1,"format":2}',json.dumps(self.data())+'x','x'*32769):
            self.path.write_text(bad,encoding='utf-8')
            with self.assertRaises(ValueError):load(self.path)
    def test_asset_hash_missing_invalid_format_and_escape_reject(self):
        p=validate(self.data(),self.folder);save(self.path,p)
        self.write_wav(self.folder/'ir.wav',[1,2,3])
        with self.assertRaises(ValueError):load(self.path)
        for asset in ('../ir.wav','C:/private/ir.wav','..\\ir.wav','missing.wav'):
            d=self.data();d['slots'][3]['asset']=asset
            with self.assertRaises(ValueError):validate(d,self.folder)
        self.write_wav(self.folder/'ir.wav',[1000],48000)
        with self.assertRaises(ValueError):validate(self.data(),self.folder)
    def test_atomic_save_failure_preserves_original_and_removes_temp(self):
        p=validate(self.data(),self.folder);save(self.path,p);before=self.path.read_bytes()
        p.data['name']='Updated'
        with patch('preset_io.os.replace',side_effect=OSError('simulated interrupted replacement')):
            with self.assertRaises(OSError):save(self.path,p)
        self.assertEqual(self.path.read_bytes(),before)
        self.assertEqual(set(x.name for x in self.folder.iterdir()),{'ir.wav','source.wav','preset.json'})
    def test_failed_or_unsafe_render_never_overwrites_source_assets_or_output(self):
        save(self.path,validate(self.data(),self.folder))
        for destination in (self.path,self.source,self.folder/'ir.wav'):
            with self.assertRaises(ValueError):render(self.path,self.source,destination)
        d=self.data()
        for slot in d['slots']:slot.update(type='gain',enabled=True,parameters={'MULTIPLIER':4.});slot.pop('asset',None)
        save(self.path,validate(d,self.folder));self.out.write_bytes(b'previous output')
        with self.assertRaises(ValueError):render(self.path,self.source,self.out)
        self.assertEqual(self.out.read_bytes(),b'previous output')

if __name__=='__main__':unittest.main()
