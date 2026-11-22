"""Offline ARM startup/command checks for retained GP-5 ELF artifacts.

Optional dependency: unicorn==2.1.4 (use a private build venv). This is NOT a
pedal model: MMIO is zero-initialized backing RAM. No DMA, resident firmware,
flash/cache latency, UART timing, interrupts or clock behavior is simulated.
Completion cannot qualify hardware startup, audio, timing or stack limits.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from audit_gp5_layout import Elf

COMMAND_ABI = 'fixed-rx-r0-command-r1-length'
NIBBLE_TABLE = bytes.fromhex('00070e091c1b1215383f363124232a2d')


def frame(payload):
    crc = 0
    for byte in payload:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ (7 if crc & 128 else 0)) & 255
    return bytes([crc]) + struct.pack('<HB', len(payload) + 4, 1) + payload


class Target:
    def __init__(self, path):
        import unicorn
        from unicorn import arm_const as arm
        self.unicorn, self.arm = unicorn, arm
        self.elf = Elf(path)
        self.symbols = {s['name']: s for s in self.elf.symbols}
        self.uc = u = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
        u.ctl_set_cpu_model(arm.UC_CPU_ARM_CORTEX_M4)
        # Deliberately coarse maps: peripheral reads/writes have no device semantics.
        for addr, size in [(0, 0x80000), (0x10000000, 0x20000), (0x20000000, 0x10000),
                           (0x40000000, 0x50000), (0x50000000, 0x20000),
                           (0xe0000000, 0x100000), (0x01000000, 0x1000)]:
            u.mem_map(addr, size)
        # FCM: the DSP-readable models flash region (section 0x66 at 0x114000,
        # proven readable on the pedal) up to 0x160000, modeled as erased/foreign
        # (0xFF): no valid FCB1 bank, so the AMP block uses its default model.
        u.mem_map(0x80000, 0xE0000);u.mem_write(0x80000, b'\xff' * 0xE0000)
        for s in self.elf.sections:
            if s[1] != 8 and s[2] & 2 and s[5]:
                u.mem_write(s[3], self.elf.section_data(s))
        # Flash also holds .data at its load address; the modeled reset copies it
        # from there into VARS (as on the pedal). Needed once .data is non-empty
        # (25c RAM-resident control code); empty .data is unaffected.
        load = self.symbols.get('__data_load')
        for s in self.elf.sections:
            if load and s[3] == self.symbols.get('__data_start', {}).get('address') and s[1] != 8 and s[5]:
                u.mem_write(load['address'], self.elf.section_data(s))
        # 31c: the same for LARGE-resident code (.lcode), copied by c_start.
        load = self.symbols.get('__lcode_load')
        for s in self.elf.sections:
            if load and s[3] == self.symbols.get('__lcode_start', {}).get('address') and s[1] != 8 and s[5]:
                u.mem_write(load['address'], self.elf.section_data(s))
        u.mem_write(0x01000000, b'\x00\xbe')
        u.reg_write(arm.UC_ARM_REG_SP, 0x20006c00)
        u.reg_write(arm.UC_ARM_REG_C1_C0_2, 0xf << 20)
        u.mem_write(0xe000ed88, struct.pack('<I', 0xf << 20))
        self.invalid = []

        def invalid(uc, access, addr, size, value, context):
            self.invalid.append(dict(access=access, address=hex(addr), size=size))
            return False

        u.hook_add(unicorn.UC_HOOK_MEM_INVALID, invalid)
        self.mmio_writes = []

        def mmio_write(uc, access, address, size, value, context):
            self.mmio_writes.append([hex(address), size, value])

        for begin, end in [(0x40000000, 0x4004ffff), (0x50000000, 0x5001ffff), (0xe0000000, 0xe00fffff)]:
            u.hook_add(unicorn.UC_HOOK_MEM_WRITE, mmio_write, begin=begin, end=end)

    def call(self, name, *args):
        u, arm = self.uc, self.arm
        for reg, value in zip([arm.UC_ARM_REG_R0, arm.UC_ARM_REG_R1,
                               arm.UC_ARM_REG_R2, arm.UC_ARM_REG_R3], args):
            u.reg_write(reg, value)
        u.reg_write(arm.UC_ARM_REG_LR, 0x01000001)
        try:
            u.emu_start(self.symbols[name]['address'] | 1, 0x01000000, count=5_000_000)
            error = None
        except self.unicorn.UcError as exc:
            error = str(exc)
        return dict(function=name, error=error, pc=hex(u.reg_read(arm.UC_ARM_REG_PC)),
                    sp=hex(u.reg_read(arm.UC_ARM_REG_SP)),
                    returned=u.reg_read(arm.UC_ARM_REG_PC) == 0x01000000,
                    r0=u.reg_read(arm.UC_ARM_REG_R0), invalid=self.invalid.copy())

    def boot(self):
        u, arm = self.uc, self.arm
        stop = self.symbols['audio_poll.isra.0']['address']
        reached = []

        def entered(uc, address, size, context):
            reached.append(address)
            uc.emu_stop()

        hook = u.hook_add(self.unicorn.UC_HOOK_CODE, entered, begin=stop, end=stop)
        try:
            u.emu_start(self.symbols['reset_handler']['address'] | 1, 0, count=5_000_000)
            error = None
        except self.unicorn.UcError as exc:
            error = str(exc)
        finally:
            u.hook_del(hook)
        return dict(phase='reset through first audio_poll', reached_audio_poll=bool(reached),
                    error=error, pc=hex(u.reg_read(arm.UC_ARM_REG_PC)),
                    sp=hex(u.reg_read(arm.UC_ARM_REG_SP)),
                    status=list(struct.unpack('<32I', u.mem_read(0x20007f00, 128))),
                    mmio_writes=self.mmio_writes,
                    bss_sha256=self.object_hash('__bss_start', '__bss_end'),
                    large_sha256=self.object_hash('__large_start', '__large_end'),
                    invalid=self.invalid.copy())

    def object_hash(self, start, end):
        begin, limit = self.symbols[start]['address'], self.symbols[end]['address']
        return hashlib.sha256(self.uc.mem_read(begin, limit - begin)).hexdigest()

    def queued_bytes(self):
        addr = self.symbols['frost_link_tx']['address']
        head, have = struct.unpack('<II', self.uc.mem_read(addr + 1024, 8))
        if head >= 1024 or have > 1024:
            raise ValueError('invalid emulated TX queue')
        data = self.uc.mem_read(addr, 1024)
        return bytes(data[(head + i) & 1023] for i in range(have))

    def query_defaults(self):
        # Explicit ABI opt-in is required by the CLI. The retained 23g/m/p/t/v
        # resp_main call sites specialize p=0x20006c04, leaving cmd/n in r0/r1.
        route = struct.pack('<HHII', 0x7f41, 12, 1, 101)
        slot = struct.pack('<HH8I', 0x7f40, 36, 1, 102, 0, 0, 0, 255, 0xffffffff, 0)
        expected_route = struct.pack('<HH7I', 0x7f41, 44, 1, 101, 0, 1, 0, 0, 3)
        expected_route += bytes([0, 1, 2, 3, 255, 255, 0, 1, 2, 3, 4, 5])
        expected_slot = struct.pack('<HH8I3f', 0x7f40, 48, 1, 102, 0, 1, 0, 0, 1, 1, 1., 0., 0.)
        expected = b''
        rows = []
        for cmd, payload, reply in [(0x7f41, route, expected_route), (0x7f40, slot, expected_slot)]:
            self.uc.mem_write(0x20006c04, payload)
            row = self.call('fw_command.constprop.0', cmd, len(payload))
            expected += frame(reply)
            actual = self.queued_bytes()
            row.update(command=hex(cmd), queue_hex=actual.hex(), expected_queue_hex=expected.hex(),
                       wire_matches=actual == expected)
            rows.append(row)
            if not row['returned']:
                break
        return rows


def examine(path, command_abi=None, negative_controls=False):
    target = Target(path)
    if command_abi and target.symbols['frost_link_tx']['size'] != 1056:
        raise ValueError('Direct-query helper requires immediate replies; use emulate_gp5_service.py for queued slot replies')
    boot = Target(path).boot()
    calls = []
    for name in ('fw_init', 'fw_prepare'):
        row = target.call(name)
        calls.append(row)
        if not row['returned']:
            break
    queries = target.query_defaults() if command_abi and all(r['returned'] for r in calls) else []
    result = dict(elf=str(path), elf_sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(),
                  boot=boot, calls=calls, command_abi=command_abi, queries=queries)
    if negative_controls:
        broken = Target(path)
        broken.uc.mem_write(broken.symbols['fw_init']['address'], b'\x00\xde')  # Thumb UDF
        result['invalid_instruction_detected'] = not broken.call('fw_init')['returned']
        tables = [s for s in target.elf.symbols if s['size'] == 16 and s['kind'] != 2
                  and s['section'] < len(target.elf.sections) and target.elf.sections[s['section']][1] != 8
                  and target.elf.memory(s['address'], 16) == NIBBLE_TABLE]
        if tables and command_abi:
            bad_crc = Target(path)
            bad_crc.uc.mem_write(tables[0]['address'], bytes(16))
            bad_crc.call('fw_init')
            result['corrupted_crc_table_detected'] = not all(r['wire_matches'] for r in bad_crc.query_defaults())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', nargs='+', type=Path)
    parser.add_argument('--command-abi', choices=[COMMAND_ABI], help='opt in only after checking the call-site ABI')
    parser.add_argument('--negative-controls', action='store_true')
    parser.add_argument('--report', type=Path, required=True, help='fresh ignored build JSON report')
    args = parser.parse_args()
    if args.report.exists():
        parser.error('report exists; choose a fresh evidence path')
    result = dict(kind='ARM instruction emulation ONLY',
                  limits=__doc__, artifacts=[examine(p, args.command_abi, args.negative_controls) for p in args.elf])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open('x', encoding='utf-8') as f:
        json.dump(result, f, indent=2)
        f.write('\n')
    ok = True
    for item in result['artifacts']:
        complete = item['boot']['reached_audio_poll'] and all(r['returned'] for r in item['calls'])
        wires = all(r['returned'] and r['wire_matches'] for r in item['queries'])
        controls = item.get('invalid_instruction_detected', True) and item.get('corrupted_crc_table_detected', True)
        ok &= complete and wires and controls
        wire_status = str(wires) if item['queries'] else 'not run'
        print(f"{item['elf']}: modeled paths complete={complete}; query wires match={wire_status}; negative controls={controls}")
    print('These checks do not qualify pedal startup or timing.')
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
