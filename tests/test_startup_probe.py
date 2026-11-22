"""Read-only probe distinguishes host, USB bridge and DSP response failures."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from probe_gp5_startup import diagnose


COUNTERS = b'\x7e\x01' + struct.pack('<HHHBBBBBBH', 17, 2, 0x4001, 0, 0, 0, 9, 0, 0, 100)
VERSION = b'\x7e\x00\x01' + struct.pack('<I', 0x10000)
LINE = struct.pack('<HH11I', 0x7f3b, 48, 1, 980001, 0, 1, 16384, 16384, 0, 0, 65536, 2, 0)
SLOT = struct.pack('<HH8I3f', 0x7f40, 48, 1, 980002, 0, 1, 0, 0, 1, 1, 1., 0., 0.)


class StartupProbeTests(unittest.TestCase):
    def probe(self, responses):
        sent = []
        responses = iter(responses)

        def exchange(payload, phase):
            sent.append((payload, phase))
            return next(responses)

        return diagnose(exchange), sent

    def test_no_usb_response_stops_without_sending_dsp_request(self):
        result, sent = self.probe([None])
        self.assertEqual(result['outcome'], 'usb-app-unanswered')
        self.assertEqual([p for p, _ in sent], [b'\x7e\x01'])

    def test_app_says_dsp_version_unanswered_is_not_usb_failure(self):
        result, sent = self.probe([COUNTERS, b'\x7e\x00\x00', COUNTERS])
        self.assertEqual(result['outcome'], 'app-responsive-dsp-version-unanswered')
        self.assertEqual(len(sent), 3)
        self.assertFalse(result['unsolicited_counter_changed'])
        self.assertEqual(sent[-1][1], 'app-counters-after-dsp-timeout')
        self.assertEqual(result['app_counters']['frames'], 17)

    def test_stage_timeout_is_not_reported_as_dsp_silence(self):
        result, sent = self.probe([COUNTERS, VERSION, None])
        self.assertEqual(result['outcome'], 'line-usb-stage-unanswered')
        self.assertEqual(len(sent), 3)
        self.assertNotIn('line_bridge_status', result)

    def test_dispatch_usb_timeout_is_distinct_from_dsp_timeout(self):
        result, sent = self.probe([COUNTERS, VERSION, b'\x7e\x03\x01', None])
        self.assertEqual(result['outcome'], 'line-usb-dispatch-unanswered')
        self.assertEqual(len(sent), 4)
        after = bytearray(COUNTERS)
        struct.pack_into('<H', after, 4, 3)
        result, sent = self.probe([COUNTERS, VERSION, b'\x7e\x03\x01', b'\x7e\x04\x00\x00', bytes(after)])
        self.assertEqual(result['outcome'], 'line-dsp-unanswered')
        self.assertEqual(result['line_bridge_status'], 0)
        self.assertEqual(len(sent), 5)
        self.assertTrue(result['unsolicited_counter_changed'])

    def test_nak_is_not_mislabeled_as_missing_dsp_response(self):
        result, _ = self.probe([COUNTERS, VERSION, b'\x7e\x03\x01', b'\x7e\x04\x13\x00'])
        self.assertEqual(result['outcome'], 'line-bridge-status-error')

    def test_malformed_or_mismatched_reply_never_reaches_next_request(self):
        result, sent = self.probe([COUNTERS, VERSION, b'\x7e\x03\x01', b'\x7e\x04\x01\x30' + LINE[:-1]])
        self.assertEqual(result['outcome'], 'invalid-observation')
        self.assertEqual(len(sent), 4)
        wrong = bytearray(LINE)
        struct.pack_into('<I', wrong, 8, 123)
        result, sent = self.probe([COUNTERS, VERSION, b'\x7e\x03\x01', b'\x7e\x04\x01\x30' + wrong])
        self.assertEqual(result['outcome'], 'invalid-observation')
        self.assertEqual(len(sent), 4)

    def test_success_only_sends_fixed_reads_and_validates_slot_path(self):
        result, sent = self.probe([COUNTERS, VERSION, b'\x7e\x03\x01', b'\x7e\x04\x01\x30' + LINE,
                                   b'\x7e\x03\x01', b'\x7e\x04\x01\x30' + SLOT])
        self.assertEqual(result['outcome'], 'responding')
        self.assertEqual(result['slot']['values'], [1., 0., 0.])
        self.assertEqual(len(sent), 6)
        staged = [p[4:] for p, _ in sent if p[:2] == b'\x7e\x03']
        self.assertEqual(len(staged), 2)
        self.assertEqual(struct.unpack_from('<HH', staged[0]), (0x7f3b, 12))
        self.assertEqual(struct.unpack_from('<HH', staged[1]), (0x7f40, 36))
        self.assertEqual(struct.unpack_from('<I', staged[1], 12)[0], 0)  # GET, never a setter
        self.assertEqual([p for p, _ in sent if p[:2] == b'\x7e\x04'], [b'\x7e\x04\x0c\x00\x32', b'\x7e\x04\x24\x00\x32'])


if __name__ == '__main__':
    unittest.main()
