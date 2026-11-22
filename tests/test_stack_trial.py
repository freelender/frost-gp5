import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from stack_trial import collect

def reply(p):
    cmd=struct.unpack_from('<H',p)[0]
    if cmd==0x7f30:return struct.pack('<HH10I',cmd,44,0x46584332,180000,360000,1,12876,0,0,6,1,1)
    token=struct.unpack_from('<I',p,8)[0]
    body={0x7f38:[1024,3072,0x20006b00,0x20005c00,0x20006c00,3,1,2],
          0x7f36:[1024,0,0,0,0,0,1,2],
          0x7f35:[1024,0,0,0,0,0,1,2],
          0x7f37:[16,40,0,0,0,0,40,2]}[cmd]
    return struct.pack('<HH11I',cmd,48,1,token,0,*body)

class StackTrialTests(unittest.TestCase):
    def test_only_reads_and_preserves_startup_overrun_without_reset(self):
        requests=[];events=[];sleeps=[]
        def exchange(p):requests.append(p);return reply(p)
        collect(exchange,events.append,rounds=3,sleep=sleeps.append)
        self.assertEqual(len(requests),15)
        self.assertEqual({struct.unpack_from('<H',p)[0] for p in requests},{0x7f30,0x7f35,0x7f36,0x7f37,0x7f38})
        self.assertEqual(sleeps,[2,2]);self.assertEqual(events[-1]['event'],'completed')
        self.assertEqual(events[-1]['initial_late_blocks'],1)
        self.assertEqual(sum(e['event']=='reply' for e in events),15)
    def test_failed_request_is_journaled_once_and_not_retried(self):
        events=[];calls=[]
        def exchange(p):calls.append(p);raise TimeoutError('missing reply')
        with self.assertRaises(TimeoutError):collect(exchange,events.append,sleep=lambda _:None)
        self.assertEqual(len(calls),1)
        self.assertEqual([e['event'] for e in events],['start','request','stopped'])
    def test_guard_mute_source_and_headroom_failures_stop_before_next_read(self):
        # Field offsets include two u16 frame fields and eleven u32 words.
        for cmd,word,value in [(0x7f38,8,2),(0x7f38,9,0),(0x7f38,10,1),
                               (0x7f35,5,1),(0x7f36,7,1)]:
            events=[];calls=[]
            def exchange(p):
                calls.append(p);data=bytearray(reply(p))
                if struct.unpack_from('<H',p)[0]==cmd:struct.pack_into('<I',data,4+4*word,value)
                return bytes(data)
            with self.assertRaises(ValueError):collect(exchange,events.append,sleep=lambda _:None)
            self.assertEqual(events[-1]['event'],'stopped')
            self.assertEqual(struct.unpack_from('<H',calls[-1])[0],cmd)
            self.assertTrue(any(e['event']=='reply' and e['name']=={0x7f38:'stack',0x7f35:'output',0x7f36:'input'}[cmd] for e in events))
    def test_new_late_block_stops_without_reset_or_followup(self):
        events=[];n=0
        def exchange(p):
            nonlocal n
            data=bytearray(reply(p))
            if p[:2]==b'\x30\x7f':
                n+=1
                if n==2:struct.pack_into('<I',data,16,2)
            return bytes(data)
        with self.assertRaisesRegex(ValueError,'late counter changed'):
            collect(exchange,events.append,sleep=lambda _:None)
        self.assertEqual(n,2);self.assertEqual(events[-2]['name'],'metrics')
    def test_failed_journal_prevents_device_request(self):
        def emit(_):raise OSError('disk full')
        def exchange(_):self.fail('must not send an unjournaled request')
        with self.assertRaises(OSError):collect(exchange,emit)
    def test_unbounded_runs_rejected_before_request(self):
        for rounds,interval in [(0,2),(32,2),(True,2),(1,-1),(1,3)]:
            with self.assertRaises(ValueError):collect(lambda _:self.fail(),lambda _:None,rounds,interval)

if __name__=='__main__':unittest.main()
