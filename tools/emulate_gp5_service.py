"""Scripted DMA/UART main-loop experiment on actual retained ARM binaries.

This is a logic experiment, NOT a hardware/timing model. DMA completions are
injected at chosen poll intervals, UART drains immediately, and DWT reads use
a scripted clock. post_audio_delay is an imposed condition, not a measurement.
It can expose service-gate starvation but cannot identify a pedal's root cause.
"""
import argparse
from collections import deque
import hashlib
import os
import json
import math
from pathlib import Path
import struct

from emulate_gp5_startup import Target, frame

UART = 0x40005000
DMA = 0x40030000
DWT = 0xe0001004
ABUF = 0x1001f400
STATUS = 0x20007f00


def transactions(edits,ir_default=1.0):
    """Controller-like default-route reads, parameter writes and exact retries."""
    rows = []
    token, revision = 2000, 1
    defaults = [(0, [1.]), (1, [.001]), (2, [1., 1., 1.]), (3, [ir_default]), (255, []), (255, [])]

    def slot_request(slot, op=0, value=0., replay=False):
        nonlocal token, revision
        token += 1
        kind, values = defaults[slot]
        payload = struct.pack('<HH8I', 0x7f40, 36, 1, token, op, revision if op else 0,
                              slot, kind if op else 255, 0 if op else 0xffffffff,
                              struct.unpack('<I', struct.pack('<f', value))[0] if op else 0)
        if op:
            revision += 1
            values[0] = value
        padded = (values + [0., 0., 0.])[:3]
        expected = struct.pack('<HH8I3f', 0x7f40, 48, 1, token, 0, revision, slot,
                               kind, int(kind != 255), len(values), *padded)
        rows.append(dict(request=payload, expected=frame(expected)))
        if replay:
            duplicate = bytearray(expected)
            struct.pack_into('<I', duplicate, 12, 1)
            rows.append(dict(request=payload, expected=frame(duplicate)))

    for slot in range(6):
        slot_request(slot)
    for i in range(edits):
        slot_request(0, 1, 1. + (i % 5) * .125, replay=i % 4 == 0)
    return rows


class ServiceModel:
    def __init__(self, elf, blocks=64, audio_every=8, post_audio_delay=0, edits=24, report_every=0, pre_rx_delay=0,ir_default=1.0):
        from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
        self.target = t = Target(elf)
        self.boot = t.boot()
        if not self.boot['reached_audio_poll']:
            raise RuntimeError('modeled boot did not reach the main loop')
        self.blocks, self.audio_every, self.delay = blocks, audio_every, post_audio_delay
        self.polls = self.injected = self.completed = 0
        self.clock = 0
        self.rows = transactions(edits,ir_default)
        self.rx, self.tx = deque(), bytearray()
        self.replies, self.errors = [], []
        self.parse_epochs, self.dispatch_epochs = [], []
        self.job_progress = []
        self.unsolicited_reports = 0
        self.rx_progress_reports = []
        self.request_epochs = []
        self.report_pending = False
        self.report_every = report_every
        self.pre_rx_delay = pre_rx_delay
        self.job_queue = t.symbols['frost_link_tx']['address'] if t.symbols['frost_link_tx']['size'] == 1116 else None
        self.pending_audio = None
        self.output_hash = hashlib.sha256()
        self.output_peak = 0
        self.stop_reason = None
        self.return_pc = t.uc.reg_read(t.arm.UC_ARM_REG_LR) & ~1
        self.next_request = 0

        def read(uc, access, address, size, value, context):
            if address == UART + 12:
                uc.mem_write(address, struct.pack('<I', min(16, len(self.rx)) << 4))
            elif address == UART:
                uc.mem_write(address, struct.pack('<I', self.rx.popleft() if self.rx else 0))
            elif address == DWT:
                uc.mem_write(address, struct.pack('<I', self.clock & 0xffffffff))
            elif address == 0xe000e010:
                uc.mem_write(address, struct.pack('<I', 5 | (0x10000 if self.report_pending else 0)))
                self.report_pending = False
                # Imposed work between the original report and RX admission
                # checks. This models a scheduling condition, not SysTick cost.
                self.clock += self.pre_rx_delay

        def write(uc, access, address, size, value, context):
            if address == DMA + 8:
                done = struct.unpack('<I', uc.mem_read(DMA + 4, 4))[0] & ~value
                uc.mem_write(DMA + 4, struct.pack('<I', done))
                uc.mem_write(DMA, struct.pack('<I', int(bool(done))))
            elif address == UART:
                self.tx.append(value & 255)
                if len(self.tx) >= 4:
                    length = struct.unpack_from('<H', self.tx, 1)[0]
                    if length < 6 or length > 512:
                        self.errors.append('invalid TX frame length')
                        self.stop_reason = 'invalid-TX'
                        uc.emu_stop()
                    elif len(self.tx) == length:
                        actual = bytes(self.tx)
                        if actual[4:6] == b'\x01\x40':
                            self.unsolicited_reports += 1
                            if frame(actual[4:]) != actual:self.errors.append('invalid unsolicited report CRC')
                            if actual[8:12] == b'FRX1':
                                from probe_gp5_startup import decode_counters
                                decoded=decode_counters(b'\x7e\x01'+bytes(14)+actual[4:])
                                if 'rx_progress_error' in decoded:self.errors.append(decoded['rx_progress_error'])
                                else:self.rx_progress_reports.append(decoded['rx_progress'])
                            self.tx.clear()
                            return
                        index = len(self.replies)
                        expected = self.rows[index]['expected'] if index < len(self.rows) else None
                        self.replies.append(dict(index=index, matches=actual == expected,
                                                 wire_hex=actual.hex(), crc_valid=frame(actual[4:]) == actual,
                                                 reply_epoch=self.completed,
                                                 elapsed_audio_blocks=self.completed-self.request_epochs[index] if index<len(self.request_epochs) else None))
                        self.tx.clear()

        def verified(uc, access, address, size, value, context):
            if value:
                self.parse_epochs.append(struct.unpack('<I', uc.mem_read(STATUS + 13 * 4, 4))[0])

        def dispatch(uc, address, size, context):
            if self.job_queue and struct.unpack('<I', uc.mem_read(self.job_queue + 1104, 4))[0]:
                self.errors.append('dispatch while slot reply pending')
            self.dispatch_epochs.append(struct.unpack('<I', uc.mem_read(STATUS + 13 * 4, 4))[0])

        def job_progress(uc, access, address, size, value, context):
            if value:
                epoch=struct.unpack('<I', uc.mem_read(STATUS + 13 * 4, 4))[0]
                self.job_progress.append([len(self.replies), epoch, value])

        def sender(uc, address, size, context):
            if not self.job_queue:return
            payload=uc.reg_read(t.arm.UC_ARM_REG_R0)
            if bytes(uc.mem_read(payload,2)) == b'\x01\x40' and struct.unpack('<I',uc.mem_read(self.job_queue+1104,4))[0]:
                self.errors.append('new report queued while slot reply pending')

        def after_audio(uc, address, size, context):
            if self.pending_audio is not None:
                half = self.pending_audio
                raw = bytes(uc.mem_read(ABUF + 0x400 + half * 0x100, 256))
                right = bytes(uc.mem_read(ABUF + 0x600 + half * 0x100, 256))
                if right != raw:
                    self.errors.append('left/right output mismatch')
                if int(os.environ.get('FROST_HASH_FROM','0'))<=self.completed<int(os.environ.get('FROST_HASH_TO','1000000000')):self.output_hash.update(raw)   # gate window (31y5/31y14)
                self.output_peak = max(self.output_peak, *(abs(v) for v in struct.unpack('<64i', raw)))
                self.completed += 1
                self.pending_audio = None
                self.clock += self.delay

        def poll(uc, address, size, context):
            self.polls += 1
            self.clock += 4096  # scripted tick advance; NOT cycles per instruction
            if self.report_every and self.polls % self.report_every == 0:self.report_pending=True
            if len(self.replies) == self.next_request and self.next_request < len(self.rows) and not self.rx:
                self.rx.extend(frame(self.rows[self.next_request]['request']))
                self.request_epochs.append(self.completed)
                self.next_request += 1
            if (self.audio_every and self.completed >= self.blocks) or (not self.audio_every and len(self.replies) >= len(self.rows)):
                self.stop_reason = 'scenario-complete' if len(self.replies) >= len(self.rows) else 'audio-budget-exhausted'
                uc.emu_stop()
                return
            if self.polls > max(4096,self.blocks*max(self.audio_every,1)+512):
                self.stop_reason = 'poll-limit'
                uc.emu_stop()
                return
            inject = self.injected < self.blocks and self.audio_every and (self.polls - 1) % self.audio_every == 0
            if inject:
                half = struct.unpack('<I', uc.mem_read(STATUS + 16 * 4, 4))[0]
                values = [int(.2 * (2**31 - 1) * math.sin((self.injected * 64 + i) * .031)) for i in range(64)]
                uc.mem_write(ABUF + half * 0x100, struct.pack('<64i', *values))
                uc.mem_write(ABUF + 0x200 + half * 0x100, struct.pack('<64i', *[-v for v in values]))
                for addr in (DMA, DMA + 4, DMA + 0x1c):
                    uc.mem_write(addr, struct.pack('<I', 15))
                self.injected += 1
                self.pending_audio = half
            else:
                for addr in (DMA, DMA + 4, DMA + 0x1c):
                    uc.mem_write(addr, bytes(4))

        t.uc.hook_add(UC_HOOK_MEM_READ, read, begin=UART, end=UART + 15)
        t.uc.hook_add(UC_HOOK_MEM_READ, read, begin=DWT, end=DWT + 3)
        t.uc.hook_add(UC_HOOK_MEM_READ, read, begin=0xe000e010, end=0xe000e013)
        t.uc.hook_add(UC_HOOK_MEM_WRITE, write, begin=UART, end=UART + 3)
        t.uc.hook_add(UC_HOOK_MEM_WRITE, write, begin=DMA + 8, end=DMA + 11)
        ready = t.symbols['frost_link_rx']['address'] + 12
        t.uc.hook_add(UC_HOOK_MEM_WRITE, verified, begin=ready, end=ready + 3)
        handler = t.symbols['fw_command.constprop.0']['address']
        t.uc.hook_add(UC_HOOK_CODE, dispatch, begin=handler, end=handler)
        if self.job_queue:
            t.uc.hook_add(UC_HOOK_MEM_WRITE, job_progress, begin=self.job_queue+1108, end=self.job_queue+1111)
            send=t.symbols['send_frame']['address']
            t.uc.hook_add(UC_HOOK_CODE,sender,begin=send,end=send)
        t.uc.hook_add(UC_HOOK_CODE, after_audio, begin=self.return_pc, end=self.return_pc)
        start = t.symbols['audio_poll.isra.0']['address']
        t.uc.hook_add(UC_HOOK_CODE, poll, begin=start, end=start)
        t.uc.mem_write(STATUS + 24 * 4, struct.pack('<I', 65536))  # simulated volume, never hardware

    def run(self):
        t = self.target
        try:
            t.uc.emu_start(t.uc.reg_read(t.arm.UC_ARM_REG_PC) | 1, 0, count=100_000_000)
            error = None
        except t.unicorn.UcError as exc:
            error = str(exc)
        addr = t.symbols['line_output']['address']
        line = struct.unpack('<4I', t.uc.mem_read(addr, 16))
        return dict(stop=self.stop_reason, error=error, polls=self.polls, injected_blocks=self.injected,
                    completed_blocks=self.completed, expected_replies=len(self.rows), received_replies=len(self.replies),
                    all_replies_match=all(r['matches'] and r['crc_valid'] for r in self.replies),
                    all_expected_replies_received=len(self.replies) == len(self.rows),
                    parse_epochs=self.parse_epochs, dispatch_epochs=self.dispatch_epochs,
                    parse_dispatch_epoch_pairs=list(zip(self.parse_epochs, self.dispatch_epochs)),
                    job_progress=self.job_progress, unsolicited_reports=self.unsolicited_reports,
                    rx_progress_reports=self.rx_progress_reports,
                    output_sha256=self.output_hash.hexdigest(), output_peak_q31=self.output_peak,
                    output_within_cap=self.output_peak <= 536870912, line_gain_q16=line[0], line_flags=line[1],
                    line_requested_q16=line[2], tx=self.replies, errors=self.errors, invalid=t.invalid,
                    imposed_post_audio_delay=self.delay, audio_every_n_polls=self.audio_every,
                    imposed_pre_rx_delay=self.pre_rx_delay,
                    limit='Scripted peripheral logic only. DWT timestamps are imposed, not measured; no deadline or hardware startup qualification.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', nargs='+', type=Path)
    p.add_argument('--blocks', type=int, default=64)
    p.add_argument('--audio-every', type=int, default=8)
    p.add_argument('--post-audio-delay', type=int, default=0)
    p.add_argument('--report-every',type=int,default=0,help='scripted COUNTFLAG period in polls, not physical time')
    p.add_argument('--pre-rx-delay',type=int,default=0,help='imposed delay after report eligibility, before old RX check; not measured')
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--ir-default',type=float,choices=(1.0,1.6),default=1.0,help='expected IR reset level: historical1.0 or 31za1.6; never inferred from replies')
    a = p.parse_args()
    if not 1 <= a.blocks <= 2048 or not 0 <= a.audio_every <= 64 or not 0 <= a.post_audio_delay <= 1_000_000 or not 0<=a.report_every<=65536 or not 0<=a.pre_rx_delay<=1_000_000:
        p.error('scenario bounds exceeded')
    if a.report.exists():
        p.error('choose a fresh report path')
    results = [dict(elf=str(path), result=ServiceModel(path, a.blocks, a.audio_every, a.post_audio_delay,report_every=a.report_every,pre_rx_delay=a.pre_rx_delay,ir_default=a.ir_default).run()) for path in a.elf]
    a.report.parent.mkdir(parents=True, exist_ok=True)
    with a.report.open('x', encoding='utf-8') as f:
        json.dump(dict(kind='scripted main-loop experiment, not physical measurement', artifacts=results), f, indent=2)
        f.write('\n')
    for row in results:
        r = row['result']
        print(f"{row['elf']}: {r['stop']}; audio={r['completed_blocks']}; replies={r['received_replies']}/{r['expected_replies']}; match={r['all_replies_match']}; error={r['error']}")
    return 0 if all(r['result']['error'] is None and not r['result']['errors'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
