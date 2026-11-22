import ctypes as ct
import math
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]

class SlotEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/generic_editor.dll' if os.name=='nt' else 'build/generic_editor.so'),
            ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.ge_turn.argtypes=[ct.c_int]
        cls.lib.ge_queue_raw.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        cls.lib.ge_wire.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.lib.ge_actual_value.restype=ct.c_float
    def setUp(self):self.lib.ge_init()
    def settle(self,n=100):
        for _ in range(n):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():return
        self.fail('editor did not become coherent/idle')
    def wire(self,cmd,p):
        a=(ct.c_uint8*len(p))(*p);self.assertEqual(self.lib.ge_wire(cmd,a,len(p)),1)
    def remote_route(self,types=(0,1,2,3,255,255),order=(0,1,2,3,4,5),pre=3,token=8000):
        self.wire(0x7f42,struct.pack('<HH4I12B',0x7f42,32,1,token,self.lib.ge_actual_revision(),pre,*types,*order))
    def remote_parameter(self,slot,type,p,value,token=9000):
        self.wire(0x7f40,struct.pack('<HH8I',0x7f40,36,1,token,1,self.lib.ge_actual_revision(),slot,type,p,bits(value)))
    def select(self,slot):
        c=self.lib
        self.assertEqual(c.ge_view(),0)
        for _ in range(6):
            if c.ge_selected()==slot:return
            c.ge_turn(1)
        self.fail('slot not selectable')
    def test_initial_snapshot_is_coherent_and_does_not_overwrite_dsp(self):
        c=self.lib;c.ge_pump(1)
        self.assertFalse(c.ge_ready());self.assertEqual([c.ge_type(i) for i in range(6)],[255]*6)
        self.settle()
        self.assertEqual([c.ge_type(i) for i in range(6)],[0,1,2,3,255,255])
        self.assertEqual([c.ge_enabled(i) for i in range(6)],[1,1,1,1,0,0])
        self.assertEqual(c.ge_actual_revision(),1);self.assertEqual(c.ge_mutations(),0)
        self.assertEqual(c.ge_selected(),1);self.assertEqual(c.ge_view(),0)
        self.assertEqual(c.ge_paint(),0)
    def test_para_parameter_edit_and_foot_only_selected_slot(self):
        c=self.lib;self.settle();c.ge_press();c.ge_press();c.ge_turn(3)
        self.settle();self.assertAlmostEqual(c.ge_actual_value(1,0),10**(-5+4*.53),places=9)
        self.assertEqual(c.ge_value(1,0),53)
        before=[c.ge_actual_enabled(i) for i in range(6)]
        c.ge_foot();self.settle()
        after=[c.ge_actual_enabled(i) for i in range(6)]
        self.assertEqual(after,[before[i] if i!=1 else 0 for i in range(6)])
        self.assertTrue(c.ge_muted());self.assertEqual(c.ge_paint(),0)
    def pick(self,category,model,drop=None):
        """Category-then-model picker gesture from an EMPTY slot (UTILITY/CAB,
        EMPTY is entry 4 there): TYPE row, category list, model list."""
        c=self.lib;c.ge_press();c.ge_press();c.ge_turn(category-6);c.ge_press()
        c.ge_turn(model-(4 if category==6 else 0))
        if drop:c.ge_drop(*drop)
        c.ge_press()
    def test_empty_slot_type_picker_and_new_type_defaults(self):
        c=self.lib;self.settle();self.select(4)
        self.pick(6,0) # EMPTY -> UTILITY / CAB: GAIN
        self.assertEqual(c.ge_type(4),255) # no optimistic replacement before confirmation
        self.settle();self.assertEqual((c.ge_type(4),c.ge_actual_type(4)),(0,0))
        self.assertEqual(c.ge_actual_enabled(4),0)
        c.ge_foot();self.settle();self.assertEqual(c.ge_actual_enabled(4),1)
        self.assertEqual(c.ge_value_raw(4,0),1000)
        self.assertEqual(c.ge_paint(),0)
    def test_position_reorders_display_but_keeps_stable_selected_identity(self):
        c=self.lib;self.settle();c.ge_press();c.ge_turn(2);c.ge_press();c.ge_turn(3);c.ge_press()
        self.settle()
        self.assertEqual([c.ge_order(i) for i in range(6)],[0,2,3,4,1,5])
        self.assertEqual(c.ge_selected(),1)
        c.ge_foot();self.settle();self.assertEqual(c.ge_actual_enabled(1),0)
        self.assertEqual(c.ge_actual_enabled(2),1)
        c.ge_turn(2);c.ge_press() # POSITION -> REMOVE EFFECT -> BACK -> chain
        self.assertEqual(c.ge_view(),0);c.ge_turn(-1)
        self.assertEqual(c.ge_selected(),4) # adjacent route position, not slot ID arithmetic
        mutations=c.ge_mutations();c.ge_foot();c.ge_pump(10)
        self.assertEqual(c.ge_mutations(),mutations)
    ROW_PARAM,ROW_CAB,ROW_TYPE,ROW_POSITION,ROW_REMOVE,ROW_BACK,ROW_PRESETS=range(7)
    def test_remove_effect_row_asks_then_empties_only_the_selected_slot(self):
        c=self.lib;self.settle();c.ge_press()                 # PARA menu of slot 1
        n=c.ge_human_count(c.ge_type(1));self.assertEqual(c.ge_row_total(),n+5)
        self.assertEqual([c.ge_row_kind(n+k) for k in range(5)],[self.ROW_TYPE,self.ROW_POSITION,self.ROW_REMOVE,self.ROW_BACK,self.ROW_PRESETS])
        c.ge_turn(n+2);c.ge_press();self.assertEqual(c.ge_view(),2);self.assertEqual(c.ge_paint(),0)   # CONFIRM?
        c.ge_turn(1);self.assertEqual(c.ge_view(),1);c.ge_pump(10);self.assertEqual(c.ge_type(1),1)        # turning cancels
        before=[c.ge_type(i) for i in range(6)]
        c.ge_press();c.ge_press();self.settle()
        self.assertEqual([c.ge_type(i) for i in range(6)],[255 if i==1 else before[i] for i in range(6)])
        self.assertEqual(c.ge_view(),1);self.assertEqual(c.ge_parameter(),0);self.assertEqual(c.ge_paint(),0)
        self.assertNotIn(self.ROW_REMOVE,[c.ge_row_kind(i) for i in range(c.ge_row_total())])  # an empty slot has nothing to remove
    def test_ir_cab_is_the_first_row(self):
        c=self.lib;self.settle();self.select(3);c.ge_press()
        n=c.ge_human_count(c.ge_type(3));self.assertEqual(c.ge_row_total(),n+6)
        self.assertEqual(c.ge_row_kind(0),self.ROW_CAB);self.assertEqual(c.ge_row_kind(1),self.ROW_PARAM)
        self.assertEqual(c.ge_row_kind(c.ge_row_total()-1),self.ROW_PRESETS);self.assertEqual(c.ge_paint(),0)
        c.ge_press();self.assertEqual(c.ge_view(),2);c.ge_turn(1);self.assertEqual(c.ge_paint(),0);c.ge_press();self.assertEqual(c.ge_view(),1)
    def test_remote_route_duplicates_and_foreign_values_are_imported(self):
        c=self.lib;self.settle()
        self.remote_route(types=(2,0,2,3,255,255),order=(3,2,1,0,4,5),pre=1)
        c.ge_pump(40);self.settle()
        self.assertEqual([c.ge_order(i) for i in range(6)],[3,2,1,0,4,5])
        self.assertEqual(c.ge_type(0),2);self.assertEqual(c.ge_type(2),2)
        self.assertEqual(c.ge_queue_raw(0,2,250),1);self.settle()
        self.assertAlmostEqual(c.ge_actual_value(0,2),.25)
        self.assertEqual(c.ge_actual_value(2,2),1.)
        self.remote_parameter(0,2,2,.5);c.ge_pump(30);self.settle()
        self.assertEqual(c.ge_value_raw(0,2),500)
    def test_raw_snapshot_values_are_not_quantized_by_bypass_edits(self):
        c=self.lib;self.settle();value=1.2345
        self.remote_parameter(3,3,0,value);c.ge_pump(30);self.settle()
        self.assertEqual(c.ge_raw(3,0),bits(value));self.assertEqual(c.ge_value_raw(3,0),1235)
        self.select(3);c.ge_foot();self.settle()
        self.assertEqual(bits(c.ge_actual_value(3,0)),bits(value))
    def test_lost_route_request_reply_and_wrong_token_recover_without_double_apply(self):
        c=self.lib
        for mode in (1,2,3):
            c.ge_init();self.settle();revision=c.ge_actual_revision()
            c.ge_drop(1,mode);self.assertEqual(c.ge_type_change(4,0),1)
            self.settle(150)
            self.assertEqual(c.ge_type(4),0)
            self.assertEqual(c.ge_actual_revision(),revision+1)
            self.assertEqual(c.ge_mutations(),2)
    def test_coherent_refresh_discards_torn_epoch_and_malformed_snapshots(self):
        c=self.lib
        for poison in (1,2,3):
            c.ge_init();c.ge_poison(poison);c.ge_pump(2)
            self.assertFalse(c.ge_ready())
            self.assertEqual([c.ge_type(i) for i in range(6)],[255]*6)
            self.settle();self.assertEqual(c.ge_type(0),0)
        c.ge_init();c.ge_pump(2) # route plus slot 0, still no published snapshot
        self.remote_parameter(0,0,0,2.)
        self.settle();self.assertEqual(c.ge_value_raw(0,0),2000)
    def test_newer_intent_survives_ack_and_type_changes_drop_old_parameter_intent(self):
        c=self.lib;self.settle();c.ge_queue_raw(0,0,1500)
        self.assertEqual(c.ge_prepare(),36)
        c.ge_queue_raw(0,0,1750);c.ge_deliver();self.settle()
        self.assertAlmostEqual(c.ge_actual_value(0,0),1.75)
        c.ge_queue_raw(0,0,1500);self.assertEqual(c.ge_prepare(),36)
        self.remote_route(types=(2,1,2,3,255,255),token=8001);c.ge_deliver()
        c.ge_pump(40);self.settle()
        self.assertEqual(c.ge_type(0),2)
        self.assertEqual(c.ge_actual_value(0,0),1.) # no old GAIN edit applied as EQ LOW
    def test_route_conflict_is_not_blindly_rebased_and_capacity_is_bounded(self):
        c=self.lib;self.settle();c.ge_queue_raw(0,0,1500);c.ge_type_change(4,0)
        self.assertEqual(c.ge_prepare(),36) # flush prior parameter before route
        self.remote_route(pre=0);c.ge_deliver();c.ge_pump(50);self.settle()
        self.assertEqual(c.ge_type(4),255);self.assertEqual(c.ge_route_state(),0)
        self.assertEqual(c.ge_error(),2)
        c.ge_init();self.settle();self.assertEqual(c.ge_type_change(4,3),1);self.settle()
        mutations=c.ge_mutations();self.assertEqual(c.ge_type_change(5,3),0)
        c.ge_pump(10);self.assertEqual(c.ge_mutations(),mutations)
        self.assertEqual(c.ge_error(),3)
    def test_noop_route_and_lost_parameter_reply_complete_once(self):
        c=self.lib;self.settle();revision=c.ge_actual_revision()
        self.assertEqual(c.ge_type_change(0,0),1);self.settle()
        self.assertEqual(c.ge_actual_revision(),revision+1)
        for mode in (1,2,3):
            c.ge_init();self.settle();revision=c.ge_actual_revision()
            c.ge_drop(1,mode);c.ge_queue_raw(3,0,1250);self.settle()
            self.assertEqual(c.ge_actual_revision(),revision+1)
            self.assertEqual(c.ge_value_raw(3,0),1250);self.assertEqual(c.ge_mutations(),2)
            self.assertAlmostEqual(c.ge_actual_value(3,0),1.25)
    def test_reconnect_discards_uncertain_mutation_at_boot_boundary(self):
        c=self.lib;self.settle();c.ge_drop(1,1);c.ge_queue_raw(0,0,1750);c.ge_tick()
        self.assertFalse(c.ge_ready());self.assertEqual(c.ge_mutations(),1)
        c.ge_dsp_reset();self.settle()
        self.assertEqual(c.ge_value_raw(0,0),1000);self.assertEqual(c.ge_actual_revision(),1)
        self.assertEqual(c.ge_mutations(),1) # old uncertain write is never replayed into the new boot
    def test_overdrive_picker_parameters_and_selected_bypass(self):
        c=self.lib;self.settle();self.select(4)
        self.pick(0,5);self.settle() # OVERDRIVE category, generic DRIVE listed last
        self.assertEqual(c.ge_type(4),4);self.assertEqual(c.ge_enabled(4),0)
        self.assertEqual([c.ge_value(4,p) for p in range(3)],[16,50,25])
        c.ge_foot();self.settle()
        for p,value in enumerate((20,54,29)):
            c.ge_press();c.ge_turn(4)
            if p==2:c.ge_drop(1,1)
            self.settle();c.ge_press()
            self.assertEqual(c.ge_value(4,p),value)
            self.assertAlmostEqual(c.ge_actual_value(4,p),(4.8,.54,.58)[p],places=6)
            self.assertEqual(c.ge_paint(),0)
            c.ge_turn(1)
        c.ge_foot();self.settle();self.assertEqual(c.ge_enabled(4),0)
        self.assertEqual([c.ge_enabled(i) for i in range(4)],[1]*4)
        self.assertTrue(c.ge_muted())
    def test_delay_picker_parameters_retries_and_capacity(self):
        c=self.lib;self.settle();self.select(4)
        self.pick(2,2,drop=(1,1));self.settle() # DELAY category, generic DELAY last
        self.assertEqual(c.ge_type(4),5);self.assertEqual(c.ge_enabled(4),0)
        self.assertEqual([c.ge_value(4,p) for p in range(3)],[250,37,30])
        c.ge_foot();self.settle()
        for p,value in enumerate((270,41,34)):
            c.ge_press();c.ge_turn(4);self.settle();c.ge_press()
            self.assertEqual(c.ge_value(4,p),value)
            self.assertAlmostEqual(c.ge_actual_value(4,p),(270.,.3895,.34)[p],places=6)
            self.assertEqual(c.ge_paint(),0);c.ge_turn(1)
        before=c.ge_mutations();self.assertEqual(c.ge_type_change(5,5),0)
        c.ge_pump(10);self.assertEqual(c.ge_mutations(),before);self.assertEqual(c.ge_error(),3)
        c.ge_foot();self.settle();self.assertEqual(c.ge_enabled(4),0)
        self.assertEqual([c.ge_enabled(i) for i in range(4)],[1]*4);self.assertTrue(c.ge_muted())
    def test_chorus_flanger_picker_controls_and_negative_feedback_retry(self):
        c=self.lib
        for type,defaults,desired in ((6,[80,50,30],[100,54,34]),(7,[30,50,56],[50,54,-56])):
            c.ge_init();self.settle();self.select(4)
            self.pick(4,type+1,drop=(1,2));self.settle() # MODULATION: generic CHORUS/FLANGER last
            self.assertEqual(c.ge_type(4),type);self.assertEqual(c.ge_enabled(4),0)
            self.assertEqual([c.ge_value(4,p) for p in range(3)],defaults)
            c.ge_foot();self.settle()
            for p,value in enumerate(desired):
                c.ge_press();c.ge_turn(-112 if type==7 and p==2 else 4)
                if p==2:c.ge_drop(1,1)
                self.settle();c.ge_press()
                self.assertEqual(c.ge_value(4,p),value)
                self.assertAlmostEqual(c.ge_actual_value(4,p),((1.,.54,.34) if type==6 else (.5,.54,-.504))[p],places=6)
                self.assertEqual(c.ge_paint(),0);c.ge_turn(1)
            c.ge_foot();self.settle();self.assertEqual(c.ge_enabled(4),0)
            self.assertEqual([c.ge_enabled(i) for i in range(4)],[1]*4);self.assertTrue(c.ge_muted())
    def test_modulation_shared_capacity_is_two_across_types(self):
        c=self.lib;self.settle()
        self.assertEqual(c.ge_type_change(4,6),1);self.settle()
        self.assertEqual(c.ge_type_change(5,7),1);self.settle()
        before=c.ge_mutations();self.assertEqual(c.ge_type_change(0,6),0)
        c.ge_pump(10);self.assertEqual(c.ge_mutations(),before);self.assertEqual(c.ge_error(),3)
        self.assertEqual([c.ge_type(i) for i in range(6)],[0,1,2,3,6,7])
    def test_reverb_picker_parameters_retry_and_selected_bypass(self):
        c=self.lib;self.settle();self.select(4)
        self.pick(3,2,drop=(1,1));self.settle() # REVERB category, generic REVERB last
        self.assertEqual(c.ge_type(4),8);self.assertEqual(c.ge_enabled(4),0)
        self.assertEqual([c.ge_value(4,p) for p in range(3)],[79,50,25])
        c.ge_foot();self.settle()
        for p,value in enumerate((83,54,29)):
            c.ge_press();c.ge_turn(4)
            if p==2:c.ge_drop(1,2)
            self.settle();c.ge_press()
            self.assertEqual(c.ge_value(4,p),value)
            self.assertAlmostEqual(c.ge_actual_value(4,p),(.7885,.46,.29)[p],places=6)
            self.assertEqual(c.ge_paint(),0);c.ge_turn(1)
        c.ge_foot();self.settle();self.assertEqual(c.ge_enabled(4),0)
        self.assertEqual([c.ge_enabled(i) for i in range(4)],[1]*4);self.assertTrue(c.ge_muted())
    def test_reverb_capacity_rejects_second_instance_without_wire_mutation(self):
        c=self.lib;self.settle();self.assertEqual(c.ge_type_change(4,8),1);self.settle()
        before=c.ge_mutations();self.assertEqual(c.ge_type_change(5,8),0)
        c.ge_pump(10);self.assertEqual(c.ge_mutations(),before);self.assertEqual(c.ge_error(),3)
        self.assertEqual([c.ge_type(i) for i in range(6)],[0,1,2,3,8,255])
    def test_reconnect_imports_actual_state_and_ui_bounds_are_safe(self):
        c=self.lib;self.settle();c.ge_queue_raw(0,0,1750);self.settle()
        c.ge_dsp_reset();self.settle();self.assertEqual(c.ge_value_raw(0,0),1000)
        self.assertEqual(c.ge_actual_revision(),1)
        c.ge_press();c.ge_press();c.ge_turn(2147483647);self.settle()
        self.assertEqual(c.ge_value_raw(1,0),100000)
        c.ge_turn(-2147483648);self.settle();self.assertEqual(c.ge_value_raw(1,0),10)
        c.ge_press();c.ge_turn(4);c.ge_press();c.ge_turn(-1) # BACK (after REMOVE EFFECT) to chain, select gain
        self.assertEqual(c.ge_view(),0)
        c.ge_turn(2);c.ge_press() # EQ menu; scroll all rows and paint safely
        for _ in range(12):c.ge_turn(1);self.assertEqual(c.ge_paint(),0)
        self.assertTrue(c.ge_muted())

if __name__=='__main__':unittest.main()
