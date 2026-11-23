"""Website song storage and real controller navigation on simulated flash."""
import ctypes as ct
import os
import copy
import json
import shutil
import subprocess
import unittest
from pathlib import Path
import test_web_store as W
import settings_nav

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('node'),'Node.js needed')
class SetlistTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.lib=ct.CDLL(str(W.build(ROOT/('build/setlist_editor.dll' if os.name=='nt' else 'build/setlist_editor.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c.ge_host.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
        c.ge_fcm_label.restype=ct.c_char_p;c.ge_fcm_label.argtypes=[ct.c_int];c.ge_preset_label.restype=ct.c_char_p
        for name in ('ge_song_name','ge_part_name'):
            getattr(cls.lib,name).restype=ct.c_char_p
    js=W.WebStoreTests.js
    host=W.WebStoreTests.host
    files=W.WebStoreTests.files
    def settle(self,n=500):
        c=self.lib
        for _ in range(n):
            c.ge_pump(1);c.ge_tick_draw()
            if c.ge_ready() and not self.host([0x7e,0x1b])[7]:return
        self.fail('controller did not settle')
    def setUp(self):
        c=self.lib;c.ge_init();self.settle()
        for i in (0,1,2):self.assertEqual(c.ge_flash_save(i),1);self.settle()
        self.data={'format':'frost-setlist','version':1,'name':'Friday gig','songs':[
            {'id':3,'name':'First song','parts':[{'name':'Intro','preset':0},{'name':'Solo','preset':1}]},
            {'id':7,'name':'Second song','parts':[{'name':'Verse','preset':2}]}], 'order':[3,7,3]}
    def install(self):
        self.js(['setPut',self.data]);self.settle();self.lib.ge_set_global(0,3);self.lib.ge_reboot();self.settle()
        self.assertEqual(self.lib.ge_view(),15)
    def test_website_roundtrip_settings_preserve_and_reboot(self):
        c=self.lib;self.install()
        self.assertEqual(self.js(['setGet'])[0],self.data)
        self.assertEqual(c.ge_set_count(),3);self.assertEqual(c.ge_current_preset(),0)
        self.assertEqual(c.ge_song_name(),b'FIRST SONG');self.assertEqual(c.ge_part_name(),b'INTRO');self.assertEqual(c.ge_paint(),0)
        c.ge_set_global(1,0);c.ge_reboot();self.settle();self.assertEqual(c.ge_set_hold(),0)
        self.assertEqual(self.js(['setGet'])[0],self.data)
        self.assertEqual(self.files(),['glb-0','pst-0'])
    def test_next_previous_parts_cross_songs_and_stop_at_ends(self):
        c=self.lib;self.install();self.assertEqual(c.ge_set_hold(),1)
        c.ge_foot_hold();self.settle();self.assertEqual((c.ge_set_order(),c.ge_set_part()),(0,0))
        for want in ((0,1,1),(1,0,2),(2,0,0),(2,1,1),(2,1,1)):
            c.ge_foot();self.settle();self.assertEqual((c.ge_set_order(),c.ge_set_part(),c.ge_current_preset()),want)
        c.ge_foot_hold();self.settle();self.assertEqual((c.ge_set_order(),c.ge_set_part()),(2,0))
        c.ge_foot_hold();self.settle();self.assertEqual((c.ge_set_order(),c.ge_set_part()),(1,0));self.assertEqual(c.ge_current_preset(),2)
    def test_upload_while_home_is_painting_refreshes_without_reboot(self):
        c=self.lib;self.install();old_host=self.host
        updated=copy.deepcopy(self.data);updated['name']='New live set';updated['order'].append(7)
        writes=[]
        def interleaved(q):
            reply=old_host(q);c.ge_tick_draw()
            if q[:2] in (b'\x7e\x07',b'\x7e\x08'):
                writes.append(1)
                self.assertEqual(c.ge_set_count(),0)
                self.assertEqual(c.ge_song_name(),b'')
                c.ge_foot() # ignore navigation against an in-flight replacement
                self.assertEqual(c.ge_current_preset(),0)
            return reply
        self.host=interleaved
        try:self.js(['setPut',updated])
        finally:self.host=old_host
        self.assertTrue(writes);self.settle()
        self.assertEqual(c.ge_set_count(),4)
        self.assertEqual(c.ge_song_name(),b'FIRST SONG')
        self.assertEqual(self.js(['setGet'])[0],updated)
        self.assertEqual(c.ge_paint(),0)
    def test_para_song_selection_and_edit_return(self):
        c=self.lib;self.install();c.ge_turn(1);self.settle();self.assertEqual(c.ge_set_order(),1);self.assertEqual(c.ge_current_preset(),2)
        c.ge_turn(-1);self.settle();self.assertEqual(c.ge_set_order(),0)
        c.ge_press();self.assertEqual(c.ge_view(),0);c.ge_advance(16000);c.ge_tick_draw();self.assertEqual(c.ge_view(),15)
    def test_hold_can_open_tuner_without_advancing_part(self):
        c=self.lib;self.install();c.ge_set_global(1,0);c.ge_foot_hold();self.settle()
        self.assertEqual(c.ge_view(),14);self.assertEqual((c.ge_set_order(),c.ge_set_part()),(0,0))
        c.ge_foot();self.settle();self.assertEqual(c.ge_view(),15);self.assertEqual(c.ge_current_preset(),0)
        c.ge_foot();self.settle();self.assertEqual(c.ge_set_part(),1)
    def test_missing_preset_does_not_advance_or_replace_sound(self):
        c=self.lib;self.data['songs'][0]['parts'][1]['preset']=59;self.install()
        revision=c.ge_actual_revision();c.ge_foot();self.settle()
        self.assertEqual(c.ge_current_preset(),0);self.assertEqual(c.ge_set_part(),0);self.assertEqual(c.ge_set_error(),1);self.assertEqual(c.ge_actual_revision(),revision)
    def test_setlist_hold_setting_on_pedal_and_persistence(self):
        c=self.lib;self.install();c.ge_long_press();settings_nav.set_value(c,settings_nav.SETLIST_HOLD,1);settings_nav.leave(c);self.settle()
        self.assertEqual(c.ge_set_hold(),0);c.ge_reboot();self.settle();self.assertEqual(c.ge_set_hold(),0);self.assertEqual(c.ge_view(),15)
    def test_backup_restore_includes_songs(self):
        self.install();p=str(ROOT/'build/setlist-test.frostbackup');self.js(['backup',p]);changed=copy.deepcopy(self.data);changed['name']='Other set'
        self.js(['setPut',changed]);self.assertEqual(self.js(['setGet'])[0]['name'],'Other set');self.js(['restore',p]);self.assertEqual(self.js(['setGet'])[0],self.data)
    def test_global_save_power_cuts_keep_complete_song_library(self):
        c=self.lib
        for cut in (1,2,3,4):
            for partial in (0,60,2048):
                c.ge_init();self.settle()
                for i in (0,1,2):c.ge_flash_save(i);self.settle()
                self.install();c.ge_flash_cut(cut,partial);c.ge_set_global(1,0)
                c.ge_flash_cut(0,0);c.ge_reboot();self.settle()
                self.assertEqual(self.js(['setGet'])[0],self.data,(cut,partial))
                self.assertEqual(c.ge_set_count(),3)
                self.assertIn(c.ge_set_hold(),(0,1))
    def test_malformed_song_with_valid_crcs_is_not_activated(self):
        c=self.lib;self.install();self.js(['setDamage']);c.ge_reboot();self.settle()
        self.assertEqual(c.ge_set_count(),0);self.assertEqual(c.ge_view(),15);self.assertEqual(c.ge_paint(),0)
    def test_older_backup_does_not_erase_setlist(self):
        self.install();p=ROOT/'build/setlist-old-backup.frostbackup';self.js(['backup',str(p)])
        backup=json.loads(p.read_text());backup['files']=[f for f in backup['files'] if f['kind']!='glb'];p.write_text(json.dumps(backup),encoding='utf-8')
        self.js(['restore',str(p)]);self.assertEqual(self.js(['setGet'])[0],self.data)
    def test_format_validation_in_node(self):
        subprocess.run(['node',str(ROOT/'tests/js/setlist.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

if __name__=='__main__':unittest.main()
