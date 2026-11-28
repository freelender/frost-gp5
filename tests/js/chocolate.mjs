// M-VAVE Chocolate Plus setup: the packets match what the vendor's editor exchanges with a real controller, the
// footswitch records read and write without touching their other bytes, and a full read/change/write/read round trip
// works against the simulated controller.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const root = new URL('../../web/', import.meta.url);
const app = await fs.readFile(new URL('app.js', root), 'utf8');
const version = app.match(/export const VERSION = '([^']+)'/)[1];
const C = await import(new URL(`chocolate.js?v=${version}`, root));
const bytes = s => Uint8Array.from(s.trim().split(/\s+/), h => parseInt(h, 16));
const unhex = s => Uint8Array.from(s.match(/../g), h => parseInt(h, 16));

// Captured from CubeSuite 2.8.10 and a Chocolate Plus ("FootCtrlPlus_012"), 2026-11-27.
assert.deepEqual(C.whoRequest(), bytes('F0 00 32 45 00 00 00 40 7F F7'));
assert.deepEqual(C.readRequest(0, 1009), bytes('F0 00 32 0D 41 00 00 00 02 00 00 00 00 10 7E 00 00 07 00 F7'));
assert.deepEqual(C.writeRequest(4, [0x20]), bytes('F0 00 32 09 49 00 00 00 02 04 00 00 00 10 00 00 00 20 2C 03 F7'));
assert.deepEqual(C.decode(bytes('F0 00 32 01 08 00 00 00 00 7F 01 F7')), { cmd: 0, payload: [0] });
const who = C.decode(bytes('F0 00 32 45 58 01 00 00 23 6F 5E 51 1B 44 4E 1C 36 50 58 55 1B 77 0B 4C 18 32 00 00 00 00 00 00 00 00 00 00 00 00 00 0F F7'));
assert.equal(who.cmd, 0x11);
assert.equal(String.fromCharCode(...who.payload.slice(0, 16)), 'FootCtrlPlus_012');
// A damaged message is refused (check byte), and so is anything that is not this maker's SysEx.
const bad = bytes('F0 00 32 01 08 00 00 00 00 7F 01 F7'); bad[9] = 0x7e;
assert.equal(C.decode(bad), null);
assert.equal(C.decode(bytes('F0 7E 00 06 01 F7')), null);
// Every packet survives the 7-bit packing, whatever its length.
for (let n = 0; n < 40; n++) { const p = C.packet(0x22, Array.from({ length: n }, (_, i) => (i * 37 + n) & 255)); assert.deepEqual(C.decode(C.encode(p)), { cmd: 0x22, payload: p.slice(4, 4 + n) }); }
// The behaviour byte the editor writes for each mode of footswitch C (record at 0x39F), captured the same day.
assert.deepEqual([C.SINGLE, C.ALTERNATE, C.HOLD, C.REPEAT, C.SHORT_LONG], [0, 1, 2, 3, 4]);
assert.equal(C.recordAddress(2), 0x39f);
assert.equal(C.recordAddress(3), 0x540);                       // footswitch D, where the editor changed the behaviour byte

// Footswitch A as the real controller stored it: hold mode, CC 88 127 on press, CC 88 0 on release, and a default
// SysEx text further on (+0xA0) that must survive our writes.
const recA = new Uint8Array(C.CHOC.RECORD);
recA.set(unhex('02010001587f000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010001580000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000016f0090d000100000006010104030304000000000000f700000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000016f0080b000100000006010104030305000000000000f7'));
const parsed = C.parseRecord(recA);
assert.deepEqual(parsed, { mode: 2, a: [{ channel: 1, type: 1, d1: 88, d2: 127 }], b: [{ channel: 1, type: 1, d1: 88, d2: 0 }] });
const setupA = C.setupOfRecord(parsed);
assert.deepEqual(setupA, { mode: 2, a: [{ kind: 'prev', on: true }], b: [{ kind: 'prev', on: false }] });
assert.equal(C.recognize(setupA).id, 'prev');                 // the maker's way of storing a one-shot is still recognised

// Every Frost command turns into one message and back, unchanged; values Frost does not name stay raw MIDI.
const commands = [{ kind: 'next', on: true }, { kind: 'tap', on: false }, { kind: 'tuner', on: true }, { kind: 'bypass', on: false },
  { kind: 'block', block: 6, on: true }, { kind: 'scene', blocks: [1, 3, 6] }, { kind: 'param', block: 5, knob: 3, value: 32 },
  { kind: 'preset', preset: 60 }, { kind: 'midi', channel: 2, type: 1, d1: 7, d2: 100 }, { kind: 'midi', channel: 1, type: 1, d1: 86, d2: 100 },
  { kind: 'midi', channel: 1, type: 2, d1: 60, d2: 90 }];
for (const c of commands) assert.deepEqual(C.fromMessage(C.toMessage(c)), c, JSON.stringify(c));
// docs/Midi Command.md examples: DELAY MIX 25 in block 5 is CC 66 = 32; a scene keeping blocks 1, 3 and 6 on is CC 89 = 37.
assert.deepEqual(C.toMessage({ kind: 'param', block: 5, knob: 3, value: 32 }), { channel: 1, type: 1, d1: 66, d2: 32 });
assert.deepEqual(C.toMessage({ kind: 'scene', blocks: [1, 3, 6] }), { channel: 1, type: 1, d1: 89, d2: 37 });
assert.deepEqual(C.toMessage({ kind: 'preset', preset: 1 }), { channel: 1, type: 0, d1: 0, d2: 0 });   // PC 0 = preset 01
// The value a control takes, as the firmware computes it; and the MIDI values of the choices.
assert.equal(C.pedalValue({ min: 0, max: 100, step: 1 }, 32), 25);
assert.equal(C.pedalValue({ min: 1, max: 500, step: 5 }, 127), 500);
assert.deepEqual([0, 1, 2, 3, 4].map(k => C.choiceValue(k, 5)), [0, 32, 64, 95, 127]);

// Ready-made functions: what they store, that they are found again, and that the rest of the record survives.
const D = C.QUICK_DEFAULTS;
for (const q of [{ id: 'next' }, { id: 'tap' }, { id: 'tuner' }, { id: 'tuner', hold: true }, { id: 'bypass' }, { id: 'block', block: 3 },
  { id: 'param', block: 5, knob: 3, value: 90, value2: 10 }, { id: 'param', block: 2, knob: 1, value: 0, value2: 127, hold: true },
  { id: 'scene', blocks: [2, 4] }, { id: 'preset', preset: 12 },
  { id: 'shortlong', short: { kind: 'next', on: true }, long: { kind: 'tuner', on: true } },
  { id: 'shortlong', short: { kind: 'block', block: 2, on: true }, long: { kind: 'preset', preset: 7 } }]) {
  const s = C.quickSetup({ ...D, ...q });
  const r = C.buildRecord(recA, C.recordOfSetup(s));
  assert.deepEqual(r.subarray(0xa0), recA.subarray(0xa0), 'bytes after the two banks are kept');
  const back = C.recognize(C.setupOfRecord(C.parseRecord(r)));
  for (const k of Object.keys(q)) assert.deepEqual(back[k], q[k], JSON.stringify(q) + ' ' + k);
}
assert.equal(C.quickSetup({ ...D, id: 'tuner' }).mode, C.ALTERNATE);
assert.equal(C.quickSetup({ ...D, id: 'tuner', hold: true }).mode, C.HOLD);
assert.equal(C.quickSetup({ ...D, id: 'shortlong' }).mode, C.SHORT_LONG);
assert.equal(C.quickSetup({ ...D, id: 'next' }).b[0].on, true, 'a one-shot sends the same command at every press');
// Lists of several commands, or another behaviour, are custom setups.
assert.equal(C.recognize({ mode: 1, a: [{ kind: 'block', block: 1, on: true }, { kind: 'block', block: 2, on: true }], b: [] }).id, 'custom');
assert.equal(C.recognize({ mode: 3, a: [{ kind: 'tap', on: true }], b: [{ kind: 'tap', on: true }] }).id, 'custom');
assert.equal(C.recognize({ mode: 4, a: [{ kind: 'tap', on: true }, { kind: 'next', on: true }], b: [{ kind: 'tuner', on: true }] }).id, 'custom');
assert.throws(() => C.buildRecord(new Uint8Array(10), C.recordOfSetup(C.quickSetup({ ...D, id: 'tap' }))), /417/);
const full = { mode: 1, a: Array(16).fill({ kind: 'tap', on: true }), b: [] };
assert.equal(C.parseRecord(C.buildRecord(recA, C.recordOfSetup(full))).a.length, 16);
assert.throws(() => C.buildRecord(recA, C.recordOfSetup({ ...full, a: Array(17).fill({ kind: 'tap', on: true }) })), /too many/);
// Several messages in one bank, as the maker's editor stored them (footswitch C, 27/11): 5 bytes each, used flag first.
const recC = new Uint8Array(C.CHOC.RECORD); recC.set(unhex('010100015' + '57f010f011463010000000000'));
assert.deepEqual(C.parseRecord(recC).a, [{ channel: 1, type: 1, d1: 85, d2: 127 }, { channel: 16, type: 1, d1: 20, d2: 99 }, { channel: 1, type: 0, d1: 0, d2: 0 }]);
assert.deepEqual(C.buildRecord(recC, C.parseRecord(recC)).subarray(0, 16), recC.subarray(0, 16));

// Three gestures per footswitch (firmware/controller/midi_control.h): every action as four bytes and back.
const actions = [{ kind: 'none' }, { kind: 'prev' }, { kind: 'next' }, { kind: 'tap' }, { kind: 'tuner', mode: 'toggle' }, { kind: 'tuner', mode: 'on' },
  { kind: 'bypass', mode: 'off' }, { kind: 'block', block: 4, mode: 'toggle' }, { kind: 'block', block: 6, mode: 'on' }, { kind: 'scene', blocks: [1, 3, 6] },
  { kind: 'param', block: 5, knob: 3, value: 90, value2: 10 }, { kind: 'param', block: 2, knob: 8, value: 64, value2: null }, { kind: 'preset', preset: 12 },
  { kind: 'midi', type: 1, d1: 7, d2: 100 }, { kind: 'midi', type: 0, d1: 99, d2: 0 }, { kind: 'ctl' }];
assert.deepEqual(C.actionBytes({ kind: 'ctl' }), [5, 0, 0, 0]);                    // the preset's CTL effects (kind 5)
for (const x of actions) assert.deepEqual(C.actionOf(C.actionBytes(x)), x, JSON.stringify(x));
assert.deepEqual(C.actionBytes({ kind: 'tap' }), [1, 85, 127, 0]);                 // a tap tempo tap: CC 85, timed on the press
assert.deepEqual(C.actionBytes({ kind: 'block', block: 1, mode: 'toggle' }), [3, 20, 0, 0]);
assert.deepEqual(C.actionBytes({ kind: 'param', block: 5, knob: 3, value: 90, value2: 10 }), [4, 66, 90, 10]);
// A footswitch with gestures: 12 definition bytes and the press on channel 16 when pressed, the release when let go.
const G = { tap: { kind: 'next' }, double: { kind: 'prev' }, hold: { kind: 'tuner', mode: 'toggle' } };
const gr = C.gestureRecord(2, G);
assert.equal(gr.mode, C.HOLD);
assert.equal(gr.a.length, 13);
assert.ok(gr.a.length <= C.CHOC.MAX_MESSAGES && gr.a.concat(gr.b).every(m => m.channel === 16 && m.type === 1));
assert.deepEqual(gr.a.map(m => m.d1), [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28]);
assert.deepEqual(gr.a.slice(0, 12).map(m => m.d2), [1, 87, 127, 0, 1, 88, 127, 0, 3, 86, 0, 0]);
assert.deepEqual([gr.a[12].d2, gr.b[0].d1, gr.b[0].d2], [2, 29, 2]);
const stored = C.parseRecord(C.buildRecord(recA, gr));
assert.deepEqual(C.gesturesOf(stored), { n: 2, ...G, pair: { kind: 'none' }, joins: false });
// Two footswitches together: the pair's action rides on the left one (CC 30 = b, CC 31 = c when not 0, CC 32+kind = a),
// and every footswitch in a pair reports its press as CC 38, so Frost waits for its neighbour.
const pa = C.gestureRecord(0, { ...G, pair: { kind: 'tuner', mode: 'toggle' } });
assert.deepEqual(pa.a.slice(12).map(m => [m.d1, m.d2]), [[35, 86], [38, 0]]);
const PARAM = { kind: 'param', block: 5, knob: 3, value: 90, value2: 10 };
const pp = C.gestureRecord(1, { ...G, pair: PARAM, joins: true });
assert.equal(pp.a.length, C.CHOC.MAX_MESSAGES, 'the longest footswitch is exactly the controller\'s limit');
assert.deepEqual(pp.a.slice(12).map(m => [m.d1, m.d2]), [[30, 90], [31, 10], [36, 66], [38, 1]]);
assert.deepEqual(C.gestureRecord(3, { ...G, joins: true }).a.slice(12).map(m => [m.d1, m.d2]), [[38, 3]]);   // the right one of C+D
for (const r of [pa, pp]) { const back = C.parseRecord(C.buildRecord(recA, r)); assert.deepEqual(C.recordOfSetup(C.setupOfRecord(back)), back); }
assert.deepEqual(C.gesturesOf(C.parseRecord(C.buildRecord(recA, pp))), { n: 1, ...G, pair: PARAM, joins: true });
for (const x of actions) {
  const r = C.gestureRecord(1, { ...G, pair: x, joins: true });
  assert.ok(r.a.length <= C.CHOC.MAX_MESSAGES); assert.deepEqual(C.gesturesOf(r).pair, x, JSON.stringify(x));
}
assert.equal(C.gesturesOf(parsed), null, 'a setup of the controller itself is not a gesture footswitch');
// Through the command lists the page edits, the gesture footswitch stays exactly the same.
assert.deepEqual(C.recordOfSetup(C.setupOfRecord(stored)), stored);
assert.deepEqual(C.actionOfQuick({ ...C.QUICK_DEFAULTS, id: 'tuner' }), { kind: 'tuner', mode: 'toggle' });

// Which footswitch sent a message the controller just played.
const recs = ['prev', 'next', 'tap', 'tuner'].map(id => C.parseRecord(C.buildRecord(recA, C.recordOfSetup(C.quickSetup({ ...D, id })))));
assert.equal(C.switchFor(recs, 0xb0, 87), 1);
assert.equal(C.switchFor(recs, 0xb0, 86), 3);
assert.equal(C.switchFor(recs, 0xb1, 86), -1);                 // another channel
assert.equal(C.switchFor(recs, 0xc0, 0), -1);

// Round trip through the simulated controller, with the real packets.
const sim = new C.SimulatedChocolate();
const link = new C.ChocolateLink(sim); sim.onmessage = d => link.receive(d);
assert.equal(await link.who(), 'FootCtrlPlus_012');
let all = await link.readSwitches();
assert.equal(all.length, 4 * C.CHOC.RECORD);
const rec = i => all.subarray(i * C.CHOC.RECORD, (i + 1) * C.CHOC.RECORD);
assert.deepEqual([0, 1, 2, 3].map(i => C.recognize(C.setupOfRecord(C.parseRecord(rec(i)))).id), ['prev', 'next', 'tap', 'tuner']);
const before = sim.memory.slice();
await link.write(C.recordAddress(2), C.buildRecord(rec(2), C.recordOfSetup(C.quickSetup({ ...D, id: 'preset', preset: 7 }))));
all = await link.readSwitches();
assert.deepEqual(C.recognize(C.setupOfRecord(C.parseRecord(rec(2)))), { ...D, id: 'preset', preset: 7 });
const changed = [...sim.memory].flatMap((x, i) => x !== before[i] ? [i] : []);
assert.ok(changed.every(i => i >= C.recordAddress(2) && i < C.recordAddress(3)), 'a write touches only its own footswitch');
// A long read is asked in pieces no bigger than the editor's own (1009 bytes).
assert.equal((await link.read(0, 3000)).length, 3000);
// Nothing answers: a clear error, not a hang.
const mute = new C.ChocolateLink({ send() {} });
await assert.rejects(mute.read(0, 10), /did not answer/);
console.log('Chocolate Plus protocol, records and round trip checks passed');
