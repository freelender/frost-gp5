"""One read-only startup diagnosis, with USB and DSP failures kept separate.

Only app link counters/version and Frost line/slot GETs are sent. The bridge
stages these fixed read requests in RAM; no setting, reset, counter clear or
flash command exists here. No retries or continuous polling. Raw observations
belong in a fresh ignored build directory, not public source.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

import line_meter


def decode_counters(reply):
    if len(reply) < 16 or bytes(reply[:2]) != b'\x7e\x01':
        raise ValueError('malformed app link counters')
    v = struct.unpack_from('<HHHBBBBBBH', reply, 2)
    result = dict(zip(('frames', 'unsolicited', 'last_command', 'uart_errors', 'bad_frames',
                     'ring_lost', 'acks', 'naks', 'irq_off', 'bytes_lo16'), v))
    tail=bytes(reply[16:])
    if len(tail)>=8 and tail[:2]==b'\x01\x40' and tail[4:8]==b'FRX1':
        if len(tail)!=24 or tail[2:4]!=b'\x18\x00':
            result['rx_progress_error']='malformed FRX1 report'
        else:
            seen,dispatched,packed,status=struct.unpack_from('<4I',tail,8)
            result['rx_progress']=dict(version=1,bytes_seen=seen,dispatched=dispatched,
                buffered=packed&1023,crc_progress=(packed>>10)&1023,ready=(packed>>20)&1023,
                last_action=(status>>7)&7,flags=(status>>10)&255,epoch_low14=status>>18)
    return result


def diagnose(exchange):
    """exchange(app_payload, phase) -> decoded app payload or None; no IO here."""
    result = dict(outcome='incomplete', queries=[])

    def ask(payload, phase):
        reply = exchange(bytes(payload), phase)
        result['queries'].append(dict(phase=phase, request_hex=bytes(payload).hex(),
                                      reply_hex=None if reply is None else bytes(reply).hex()))
        return reply

    def raw(payload, phase):
        # These requests fit one bridge staging packet. No arbitrary DSP payload.
        reply = ask(b'\x7e\x03\x00\x00' + payload, phase + '-stage')
        if reply is None:
            result['outcome'] = phase + '-usb-stage-unanswered'
            return None
        if bytes(reply[:3]) != b'\x7e\x03\x01':
            result['outcome'] = phase + '-usb-stage-rejected'
            return None
        reply = ask(bytes([0x7e, 4, len(payload), 0, 50]), phase + '-dispatch')
        if reply is None:
            result['outcome'] = phase + '-usb-dispatch-unanswered'
            return None
        if len(reply) < 4 or bytes(reply[:2]) != b'\x7e\x04':
            raise ValueError('malformed USB dispatch reply')
        result[phase + '_bridge_status'] = reply[2]
        if reply[2] != 1:
            result['outcome'] = phase + ('-dsp-unanswered' if reply[2] == 0 else '-bridge-status-error')
            return None
        if reply[3] != len(reply) - 4:
            raise ValueError('USB dispatch length mismatch')
        return bytes(reply[4:])

    try:
        reply = ask(b'\x7e\x01', 'app-link-counters')
        if reply is None:
            result['outcome'] = 'usb-app-unanswered'
            return result
        result['app_counters'] = decode_counters(reply)
        reply = ask(b'\x7e\x00', 'dsp-version')
        if reply is None:
            result['outcome'] = 'usb-version-query-unanswered'
            return result
        if len(reply) < 3 or bytes(reply[:2]) != b'\x7e\x00' or reply[2] not in (0, 1):
            raise ValueError('malformed DSP version reply')
        if not reply[2]:
            result['outcome'] = 'app-responsive-dsp-version-unanswered'
            return result
        if len(reply) < 7:
            raise ValueError('truncated DSP version')
        result['dsp_version'] = struct.unpack_from('<I', reply, 3)[0]
        token = 980001
        reply = raw(line_meter.request(token), 'line')
        if reply is None:
            return result
        result['line'] = line_meter.decode(reply, token)
        token += 1
        payload = struct.pack('<HH8I', 0x7f40, 36, 1, token, 0, 0, 0, 255, 0xffffffff, 0)
        reply = raw(payload, 'slot-get')
        if reply is None:
            return result
        if len(reply) != 48:
            raise ValueError('invalid slot reply length')
        w = struct.unpack('<HH11I', reply)
        if w[:5] != (0x7f40, 48, 1, token, 0) or w[6] != 0:
            raise ValueError('slot reply identity/status mismatch')
        if w[8] > 1 or w[9] > 8:
            raise ValueError('invalid slot reply state')
        result['slot'] = dict(revision=w[5], slot=w[6], type=w[7], enabled=w[8],
                              parameter_count=w[9], values=list(struct.unpack_from('<3f', reply, 36)))
        result['outcome'] = 'responding-with-flags' if result['line']['flags'] or result['line']['chain_faults'] else 'responding'
    except Exception as exc:
        result['outcome'] = 'invalid-observation'
        result['error'] = repr(exc)
    finally:
        # A second app-local snapshot after an explicit DSP timeout can show
        # whether unsolicited frames arrived during the query. This is not a
        # retry: no further request is sent to the DSP. USB timeouts stop outright.
        if result['outcome'] in ('app-responsive-dsp-version-unanswered', 'line-dsp-unanswered',
                                  'slot-get-dsp-unanswered'):
            try:
                reply = ask(b'\x7e\x01', 'app-counters-after-dsp-timeout')
                if reply is not None:
                    result['app_counters_after'] = decode_counters(reply)
                    result['unsolicited_counter_changed'] = result['app_counters_after']['unsolicited'] != result['app_counters']['unsolicited']
                else:
                    result['counter_followup'] = 'usb-unanswered'
            except Exception as exc:
                result['counter_followup_error'] = repr(exc)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='fresh ignored build directory')
    parser.add_argument('--report', type=Path, required=True, help='fresh external hardware-trial JSON')
    args = parser.parse_args()
    if args.report.exists() or args.output.exists():
        parser.error('choose fresh output/report paths; evidence is never overwritten')
    image = args.stage / 'build/GP5_Frost_line_test.bin'
    expected = hashlib.sha256(image.read_bytes()).hexdigest()
    sys.path.insert(0, str((args.stage / 'tools').resolve()))
    import gp5tool
    args.output.mkdir(parents=True, exist_ok=False)
    link = None
    observations = []
    start = time.monotonic()
    try:
        ports = gp5tool.MidiPorts()
        names = [n for n in ports.names() if gp5tool.OPEN_PORT in n]
        if len(names) != 1:
            raise RuntimeError('expected exactly one GP-5 open-firmware output/input pair')
        link = ports.open(names[0])
        time.sleep(.3)

        def exchange(payload, phase):
            before = time.monotonic()
            reply = gp5tool.ask(link, payload, payload[:2], 2.0 if phase.endswith('-dispatch') else 1.0)
            observations.append(dict(phase=phase, elapsed_s=round(time.monotonic()-start, 4),
                                     request_hex=payload.hex(), reply_hex=None if reply is None else reply.hex(),
                                     duration_s=round(time.monotonic()-before, 4)))
            return reply

        result = diagnose(exchange)
    except Exception as exc:
        result = dict(outcome='host-setup-error', error=repr(exc))
    finally:
        if link is not None:
            link.close()
    journal = args.output / 'startup-probe.json'
    journal.write_bytes((json.dumps(observations, indent=2)+'\n').encode('utf-8'))
    report = dict(date=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  kind='physical read-only startup bridge diagnosis', expected_package_sha256=expected,
                  identity_note='Expected artifact only; installation must be independently confirmed.',
                  result=result, journal=str(journal.resolve()),
                  journal_sha256=hashlib.sha256(journal.read_bytes()).hexdigest(),
                  scope='One bounded pass; no retries/reset/setting/counter-clear/flash. No timing, audio or startup qualification.')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open('x', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
        f.write('\n')
    print(json.dumps(result, indent=2))
    return 0 if result['outcome'] in ('responding', 'responding-with-flags') else 1


if __name__ == '__main__':
    raise SystemExit(main())
