"""Read-only DSP input meter (0x7F36): peak, minimum/maximum and rail hits of the
GP-5 input since the DSP booted. Used to measure the input's full-scale level in dBu
(GP5_INPUT_DBU in web/converter/convert.js). Nothing is written.

Measuring: power the pedal on with the cable already plugged in (plugging later can
spike the peak), play a 1 kHz sine of known level into the GUITAR input, then run:
  python tools/probe_input.py --stage build/gp5-line31y18-live-adc --sine-dbu -10
GP-5 full scale (dBu) = sine dBu + 3.01 - peak dBFS (a sine's peak is 3.01 dB over its RMS level).
"""
import argparse,json,math,struct,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--stage',type=Path,required=True)
p.add_argument('--sine-dbu',type=float,help='RMS level of the test sine at the GP-5 input, dBu');a=p.parse_args()
sys.path.insert(0,str((a.stage/'tools').resolve()));import gp5tool
ports=gp5tool.MidiPorts();names=[n for n in ports.names() if gp5tool.OPEN_PORT in n]
if not names:raise SystemExit('No GP-5 running open firmware found on USB.')
link=ports.open(names[0]);time.sleep(.3)
try:
    payload=bytes([0x36,0x7f,12,0])+struct.pack('<II',1,36)
    gp5tool.ask(link,b'\x7e\x03\x00\x00'+payload,b'\x7e\x03',1.0)
    r=gp5tool.ask(link,bytes([0x7e,4,len(payload),0,48]),b'\x7e\x04',2.0)
    d=bytes(r[4:4+r[3]]);v=struct.unpack_from('<11I',d,4)
    out=dict(zip(['version','request','status','frames','minimum','maximum','peak','rails','nonzero','muted','source'],v))
    for k in ('minimum','maximum'):out[k]=struct.unpack('<i',struct.pack('<I',out[k]))[0]
    out['peak_dbfs']=round(20*math.log10(out['peak']/2147483648),2) if out['peak'] else None
    if a.sine_dbu is not None and out['peak']:out['full_scale_dbu']=round(a.sine_dbu+3.0103-out['peak_dbfs'],2)
    print(json.dumps(out))
finally:link.close()
