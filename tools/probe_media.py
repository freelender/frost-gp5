"""USB audio/BLE status for a Frost --media-platform build (7E 21).

Read-only by default. --direct on/off changes volatile monitoring only.
No flash, preset writes, reset, or audio streaming is performed.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--direct',choices=('on','off'))
    a=p.parse_args()
    sys.path.insert(0,str((a.stage/'tools').resolve()))
    import gp5tool
    ports=gp5tool.MidiPorts()
    names=[n for n in ports.names() if gp5tool.OPEN_PORT in n]
    if len(names)!=1:raise SystemExit('Expected exactly one GP-5 running Frost')
    link=ports.open(names[0])
    try:
        time.sleep(.3)
        q=b'\x7e\x21'+(bytes([int(a.direct=='on')]) if a.direct else b'')
        r=gp5tool.ask(link,q,b'\x7e\x21',2.0)
        if r is None or len(r)!=32 or r[2]!=1:raise SystemExit('No Frost media status; this build may not enable USB audio/BLE')
        under,over,rec_over,ppm,midi,dropped=struct.unpack_from('<IIIiII',r,8)
        print(json.dumps(dict(direct=bool(r[3]),playing=bool(r[4]),recording=bool(r[5]),
            bluetooth=['off','advertising','connected'][min(r[6],2)],playback_underruns=under,
            playback_overruns=over,recording_overruns=rec_over,clock_correction_ppm=ppm,
            bluetooth_midi_received=midi,bluetooth_queue_drops=dropped),indent=2))
    finally:link.close()

if __name__=='__main__':main()
