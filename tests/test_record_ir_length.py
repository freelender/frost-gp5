"""Read-only precondition for a meaningful 512-tap hardware timing trial."""
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from record_slot_load import verify_ir_taps

class RecordIRLengthTests(unittest.TestCase):
    def exchange(self,taps=512,revision=7):
        calls=[]
        def send(request):
            calls.append(request)
            if len(calls)==1:
                self.assertEqual(struct.unpack('<HH8I',request),(0x7f40,36,1,100,0,0,0,255,0xffffffff,0))
                return struct.pack('<HH11I',0x7f40,48,1,100,0,7,0,14,1,3,0,0,0)
            self.assertEqual(struct.unpack('<HH5I',request),(0x7f44,24,1,101,1,7,42))
            return struct.pack('<HH11I',0x7f44,48,1,101,0,revision,42,5,0,0,0,taps,0)
        return send,calls
    def test_reads_only_metadata_without_describe_or_coefficients(self):
        send,calls=self.exchange();self.assertEqual(verify_ir_taps(send,512,100),dict(revision=7,ir_taps=[512,0]));self.assertEqual(len(calls),2)
    def test_wrong_length_stops_instead_of_changing_ir(self):
        send,calls=self.exchange(taps=256)
        with self.assertRaisesRegex(ValueError,'expected first IR'):verify_ir_taps(send,512,100)
        self.assertEqual(len(calls),2)
    def test_revision_change_is_rejected_without_retry(self):
        send,calls=self.exchange(revision=8)
        with self.assertRaisesRegex(ValueError,'revision mismatch'):verify_ir_taps(send,512,100)
        self.assertEqual(len(calls),2)

if __name__=='__main__':unittest.main()
