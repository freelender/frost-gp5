"""Saved direct monitoring preference through the real PARA menu and flash record."""
import ctypes as ct
import os
import unittest
import test_global_settings as settings
from host import build
from test_global_settings import ROOT
import settings_nav as nav

class DirectMonitorTests(settings.GlobalSettingsTests):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/direct_monitor.dll' if os.name=='nt' else 'build/direct_monitor.so'),
            ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'],flags=['-DFROST_TEST_USB_MONITOR=1'])))
        cls.lib.ge_actual_value.restype=ct.c_float
        cls.lib.ge_preset_label.restype=ct.c_char_p
        cls.lib.ge_monitor_label.restype=ct.c_char_p

    def test_default_edit_persist_and_reset(self):
        c=self.lib
        self.assertEqual((c.ge_monitor_saved(),c.ge_monitor_now(),c.ge_monitor_label()),(1,1,b'ON'))
        self.open_settings();self.set_value(nav.DIRECT_MONITOR,1)
        self.assertEqual((c.ge_monitor_saved(),c.ge_monitor_now(),c.ge_monitor_label()),(0,0,b'OFF'))
        self.leave();c.ge_reboot();self.settle()
        self.assertEqual((c.ge_monitor_saved(),c.ge_monitor_now()),(0,0))
        self.open_settings();self.go_row(nav.RESET);c.ge_press();c.ge_turn(1);c.ge_press()
        self.assertEqual((c.ge_monitor_saved(),c.ge_monitor_now()),(1,1))
        self.leave();c.ge_reboot();self.settle();self.assertEqual(c.ge_monitor_now(),1)

    def test_older_record_defaults_on_and_invalid_value_rejected(self):
        c=self.lib
        for encoded in (0,3,255):
            c.ge_monitor_record(encoded);c.ge_monitor_override(0);c.ge_reboot();self.settle()
            self.assertEqual((c.ge_monitor_saved(),c.ge_monitor_now()),(1,1))

    def test_host_override_is_visible_but_does_not_save_itself(self):
        c=self.lib;c.ge_monitor_override(0)
        self.assertEqual((c.ge_monitor_label(),c.ge_monitor_saved()),(b'OFF',1))
        self.open_settings();self.leave();c.ge_reboot();self.settle()
        self.assertEqual(c.ge_monitor_now(),1)
        c.ge_monitor_override(0);self.open_settings();self.set_value(nav.DIRECT_MONITOR,-1)
        self.assertEqual((c.ge_monitor_saved(),c.ge_monitor_now()),(1,1))

    def test_factory_reset_restores_on(self):
        c=self.lib;self.open_settings();self.set_value(nav.DIRECT_MONITOR,-1);self.leave()
        self.open_settings();self.go_row(nav.FACTORY)
        c.ge_press();c.ge_turn(1);c.ge_press();c.ge_turn(1);c.ge_press()
        self.assertEqual((c.ge_monitor_saved(),c.ge_monitor_now()),(1,1))

    def test_bluetooth_menu_applies_persists_and_reset_restores_on(self):
        c=self.lib
        self.assertEqual((c.ge_bluetooth_saved(),c.ge_bluetooth_now()),(1,1))
        self.open_settings();self.set_value(nav.BLUETOOTH,1)
        self.assertEqual((c.ge_bluetooth_saved(),c.ge_bluetooth_now()),(0,0))
        self.leave();c.ge_reboot();self.settle()
        self.assertEqual((c.ge_bluetooth_saved(),c.ge_bluetooth_now()),(0,0))
        self.open_settings();self.set_value(nav.BLUETOOTH,-1)
        self.assertEqual(c.ge_bluetooth_now(),1)
        self.set_value(nav.BLUETOOTH,1)
        self.go_row(nav.RESET);c.ge_press();c.ge_turn(1);c.ge_press()
        self.assertEqual((c.ge_bluetooth_saved(),c.ge_bluetooth_now()),(1,1))

    def test_bluetooth_older_record_on_and_factory_reset(self):
        c=self.lib
        for encoded in (0,3,255):
            c.ge_bluetooth_record(encoded);c.ge_reboot();self.settle()
            self.assertEqual((c.ge_bluetooth_saved(),c.ge_bluetooth_now()),(1,1))
        self.open_settings();self.set_value(nav.BLUETOOTH,1)
        self.go_row(nav.FACTORY);c.ge_press();c.ge_turn(1);c.ge_press();c.ge_turn(1);c.ge_press()
        self.assertEqual((c.ge_bluetooth_saved(),c.ge_bluetooth_now()),(1,1))

if __name__=='__main__':unittest.main()
