"""Default scheduling remains unchanged; explicit diagnostic tests missed-window starvation."""
import ctypes as ct
from pathlib import Path
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from host import build
from prepare_gp5 import stage
from emulate_gp5_service import transactions


class ServiceWindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.models = {}
        for window in (16384, 32768):
            output = ROOT / ('build/window%d' % window + ('.dll' if os.name == 'nt' else '.so'))
            flags = [] if window == 16384 else ['-DFROST_SERVICE_WINDOW=32768u']
            c = ct.CDLL(str(build(output, ROOT / 'tests/timing_host.c', includes=[ROOT / 'tests/stubs'], flags=flags)))
            c.trace_record.argtypes = [ct.c_uint32] * 5
            c.service_open.argtypes = [ct.c_uint32]
            cls.models[window] = c

    def setUp(self):
        for c in self.models.values():
            c.protocol_init()

    def test_default_window_is_unchanged_and_diagnostic_is_still_bounded(self):
        for limit, c in self.models.items():
            c.trace_record(10, 20, 30, 100000, 0)
            self.assertEqual(c.service_open(100000 + limit - 1), 1)
            self.assertEqual(c.service_open(100000 + limit), 0)
            c.service_take()
            self.assertEqual(c.service_open(100001), 0)

    def test_imposed_20k_post_block_delay_starves_default_but_not_32k_diagnostic(self):
        counts = {}
        for limit, c in self.models.items():
            admitted = 0
            for i in range(64):
                end = (i + 1) * 278494
                c.trace_record(end - 180000, end - 179990, end - 10, end, 0)
                if c.service_open(end + 20000):
                    admitted += 1
                    c.service_take()
                self.assertEqual(c.service_open(end + 40000), 0)
            counts[limit] = admitted
        self.assertEqual(counts, {16384: 0, 32768: 64})

    def test_stale_fallback_and_epoch_wrap_remain_in_both_profiles(self):
        for c in self.models.values():
            c.trace_record(10, 20, 30, 0xfffff000, 0)
            c.service_take()
            self.assertEqual(c.service_open(0xfffff100), 0)
            self.assertEqual(c.service_open((0xfffff000 + 557000) & 0xffffffff), 1)
            c.service_epoch(0xffffffff)
            c.trace_record(10, 20, 30, 1000, 0)
            self.assertEqual(c.service_open(1100), 1)

    def test_invalid_or_muted_override_fails_before_git_or_filesystem_mutation(self):
        with tempfile.TemporaryDirectory() as folder:
            dest = Path(folder) / 'must-not-exist'
            with patch('prepare_gp5.subprocess.check_output', side_effect=AssertionError('git must not run')):
                for value in (0, -1, True, 32767, 65536, '32768'):
                    with self.assertRaises(ValueError):
                        stage(Path(folder), dest, audio_input='adc', audio_output='line-test', service_window_cycles=value)
                with self.assertRaises(ValueError):
                    stage(Path(folder), dest, service_window_cycles=32768)
            self.assertFalse(dest.exists())

    def test_controller_scenario_includes_exact_retries_and_queue_wrap_workload(self):
        rows = transactions(24)
        self.assertEqual(len(rows), 36)
        self.assertGreater(sum(len(r['expected']) for r in rows), 1024)
        self.assertEqual(rows[6]['request'], rows[7]['request'])
        self.assertNotEqual(rows[6]['expected'], rows[7]['expected'])
        self.assertEqual(rows[6]['expected'][16:20], bytes(4))  # APPLIED in payload
        self.assertEqual(rows[7]['expected'][16:20], b'\x01\0\0\0')  # REPLAYED


if __name__ == '__main__':
    unittest.main()
