// M-VAVE Chocolate Plus footswitch controller (USB name "FootCtrlPlus"): read and write what each footswitch sends,
// over USB MIDI SysEx, so the page can set it up for Frost. The protocol was measured from the vendor's editor
// (CubeSuite 2.8.10) talking to a Chocolate Plus that answers "FootCtrlPlus_012":
//   message  F0 00 32 <body> F7. The body carries the bits 1, 0 and then the packet bytes, least significant bit
//            first, cut into 7-bit groups (least significant bit first, the last group padded with zeros).
//   packet   command, payload length (3 bytes, little-endian), payload, check = 0xFF - (sum of payload & 0xFF).
//   0x11     who are you; the answer's payload starts with the name ("FootCtrlPlus_012").
//   0x23     read  [04, address (4 bytes LE), length (3 bytes LE)]; answer 0x23 with the same 8 bytes + the data.
//   0x22     write [04, address, length, data...]; answer 0x00 [00] when stored (a single byte can be written too).
// Memory: 23 646 bytes. From 0x5D, 32 footswitch records of 417 bytes (group 1 = A, B, C, D, then group 2...).
// A record: +0 behaviour (SINGLE ... SHORT_LONG below), +1 bank A, +0x51 bank B; a bank holds up to 16 messages of
// 5 bytes: 1 (in use), MIDI channel - 1, type (0 PC, 1 CC, 2 note on, 3 note off), data 1, data 2; the first unused
// entry ends the list (measured with three messages in a bank, 27/11). Other bytes are kept as read.

export const CHOC = { BASE: 0x5d, RECORD: 417, SWITCHES: 4, GROUPS: 8, SIZE: 23646, BANK_B: 0x50, MAX_MESSAGES: 16, ENTRY: 5, CHUNK: 1009 };
export const SWITCH_NAMES = ['A', 'B', 'C', 'D'];
// Footswitch behaviours (record byte +0), as the maker's editor stores them: 0 bank A at each press, 1 A and B in
// turn, 2 A on press and B on release, 3 A repeated while held, 4 A on a short press and B on a long press.
export const SINGLE = 0, ALTERNATE = 1, HOLD = 2, REPEAT = 3, SHORT_LONG = 4;
export const TYPES = ['PC', 'CC', 'Note on', 'Note off'];

// ---- packets ----
export function packet(cmd, payload = []) {
  const n = payload.length, sum = payload.reduce((a, x) => a + x, 0);
  return [cmd, n & 255, (n >> 8) & 255, (n >> 16) & 255, ...payload, (0xff - (sum & 0xff)) & 0xff];
}
export function encode(bytes) {
  const bits = [1, 0];
  for (const x of bytes) for (let i = 0; i < 8; i++) bits.push((x >> i) & 1);
  while (bits.length % 7) bits.push(0);
  const out = [0xf0, 0x00, 0x32];
  for (let i = 0; i < bits.length; i += 7) { let v = 0; for (let j = 0; j < 7; j++) v |= bits[i + j] << j; out.push(v); }
  out.push(0xf7);
  return new Uint8Array(out);
}
/** A received message -> {cmd, payload}, or null when it is not a well-formed Chocolate packet. */
export function decode(msg) {
  if (!msg || msg.length < 6 || msg[0] !== 0xf0 || msg[1] !== 0x00 || msg[2] !== 0x32 || msg[msg.length - 1] !== 0xf7) return null;
  const bits = []; for (let i = 3; i < msg.length - 1; i++) for (let j = 0; j < 7; j++) bits.push((msg[i] >> j) & 1);
  if (bits[0] !== 1 || bits[1] !== 0) return null;
  const b = []; for (let i = 2; i + 8 <= bits.length; i += 8) { let v = 0; for (let j = 0; j < 8; j++) v |= bits[i + j] << j; b.push(v); }
  if (b.length < 5) return null;
  const n = b[1] | (b[2] << 8) | (b[3] << 16);
  if (b.length < 5 + n) return null;
  const payload = b.slice(4, 4 + n), sum = payload.reduce((a, x) => a + x, 0);
  return ((0xff - (sum & 0xff)) & 0xff) === b[4 + n] ? { cmd: b[0], payload } : null;
}
const le = (x, n) => Array.from({ length: n }, (_, i) => (x >>> (8 * i)) & 255);
const num = (b, o, n) => { let v = 0; for (let i = n - 1; i >= 0; i--) v = v * 256 + b[o + i]; return v; };
export const whoRequest = () => encode(packet(0x11));
export const readRequest = (addr, len) => encode(packet(0x23, [4, ...le(addr, 4), ...le(len, 3)]));
export const writeRequest = (addr, data) => encode(packet(0x22, [4, ...le(addr, 4), ...le(data.length, 3), ...data]));
export const recordAddress = (sw, group = 0) => CHOC.BASE + (group * CHOC.SWITCHES + sw) * CHOC.RECORD;

// ---- footswitch records ----
function bank(r, at) {
  const list = [];
  for (let i = 0; i < CHOC.MAX_MESSAGES; i++) { const o = at + CHOC.ENTRY * i; if (!r[o]) break; list.push({ channel: r[o + 1] + 1, type: r[o + 2], d1: r[o + 3], d2: r[o + 4] }); }
  return list;
}
export function parseRecord(r) { return { mode: r[0], a: bank(r, 1), b: bank(r, 1 + CHOC.BANK_B) }; }
/** A new record: `old` with the behaviour and both banks replaced; every other byte stays as it was. */
export function buildRecord(old, { mode, a, b }) {
  const r = Uint8Array.from(old);
  if (r.length !== CHOC.RECORD) throw new Error('a Chocolate footswitch record is 417 bytes');
  r[0] = mode;
  for (const [at, list] of [[1, a], [1 + CHOC.BANK_B, b]]) {
    if (list.length > CHOC.MAX_MESSAGES) throw new Error('too many messages for one footswitch');
    r.fill(0, at, at + CHOC.ENTRY * CHOC.MAX_MESSAGES);
    list.forEach((m, i) => r.set([1, m.channel - 1, m.type, m.d1 & 127, m.d2 & 127], at + CHOC.ENTRY * i));
  }
  return r;
}

// ---- Frost commands (docs/Midi Command.md, channel 1) ----
// A footswitch setup is {mode, a, b} (the behaviours above say when list a and list b are played). A command is one
// Frost MIDI message, described by what it does:
//   prev | next | tap {on}        CC 88 / 87 / 85 (127 acts, 0 is ignored by Frost)
//   tuner | bypass {on}           CC 86 / 90 = 127 on, 0 off
//   block {block 1-6, on}         CC 19 + block: an effect on/off, by its position in the chain
//   scene {blocks}                CC 89 = which positions stay on (the others turn off)
//   param {block, knob 1-8, value 0-127}   CC 32 + 8 (block - 1) + (knob - 1): one control of one effect
//   preset {preset 1-60}          PC preset - 1
//   midi {channel, type, d1, d2}  anything else, kept exactly as it is
const SHOTS = { prev: 88, next: 87, tap: 85 }, SWITCHED = { tuner: 86, bypass: 90 };
export const KINDS = ['prev', 'next', 'tap', 'tuner', 'bypass', 'block', 'scene', 'param', 'preset', 'midi'];
const cc = (n, v) => ({ channel: 1, type: 1, d1: n & 127, d2: v & 127 });
export function toMessage(c) {
  if (c.kind in SHOTS) return cc(SHOTS[c.kind], c.on === false ? 0 : 127);
  if (c.kind in SWITCHED) return cc(SWITCHED[c.kind], c.on ? 127 : 0);
  if (c.kind === 'block') return cc(19 + c.block, c.on ? 127 : 0);
  if (c.kind === 'scene') return cc(89, c.blocks.reduce((m, b) => m | (1 << (b - 1)), 0));
  if (c.kind === 'param') return cc(32 + 8 * (c.block - 1) + (c.knob - 1), c.value);
  if (c.kind === 'preset') return { channel: 1, type: 0, d1: (c.preset - 1) & 127, d2: 0 };
  return { channel: c.channel, type: c.type, d1: c.d1 & 127, d2: c.d2 & 127 };
}
/** A stored message -> the command it is; only exact Frost values are named, so toMessage gives it back unchanged. */
export function fromMessage(m) {
  const raw = { kind: 'midi', channel: m.channel, type: m.type, d1: m.d1, d2: m.d2 };
  if (m.channel !== 1) return raw;
  if (m.type === 0) return m.d2 === 0 && m.d1 < 60 ? { kind: 'preset', preset: m.d1 + 1 } : raw;
  if (m.type !== 1) return raw;
  const n = m.d1, v = m.d2, onoff = v === 127 || v === 0;
  for (const [k, x] of Object.entries({ ...SHOTS, ...SWITCHED })) if (n === x) return onoff ? { kind: k, on: v === 127 } : raw;
  if (n >= 20 && n <= 25) return onoff ? { kind: 'block', block: n - 19, on: v === 127 } : raw;
  if (n === 89) return v < 64 ? { kind: 'scene', blocks: [1, 2, 3, 4, 5, 6].filter(b => v & (1 << (b - 1))) } : raw;
  if (n >= 32 && n <= 79) return { kind: 'param', block: ((n - 32) >> 3) + 1, knob: ((n - 32) & 7) + 1, value: v };
  return raw;
}
export const setupOfRecord = r => ({ mode: r.mode, a: r.a.map(fromMessage), b: r.b.map(fromMessage) });
export const recordOfSetup = s => ({ mode: s.mode, a: s.a.map(toMessage), b: s.b.map(toMessage) });

// Ready-made functions: each fills both lists; recognize() finds them again in what a controller stores.
// 'shortlong' is two commands on one footswitch: one on a short press, one on a long press.
export const QUICK = ['prev', 'next', 'tap', 'tuner', 'bypass', 'block', 'param', 'scene', 'preset', 'shortlong', 'custom'];
export const QUICK_DEFAULTS = { hold: false, block: 1, knob: 1, value: 127, value2: 0, blocks: [1, 2, 3, 4, 5, 6], preset: 1, short: { kind: 'next', on: true }, long: { kind: 'tuner', on: true } };
export function quickSetup(q) {
  const each = c => ({ mode: ALTERNATE, a: [c], b: [{ ...c }] });              // the same command at every press
  const pair = (on, off) => ({ mode: q.hold ? HOLD : ALTERNATE, a: [on], b: [off] });
  switch (q.id) {
    case 'prev': case 'next': case 'tap': return each({ kind: q.id, on: true });
    case 'tuner': case 'bypass': return pair({ kind: q.id, on: true }, { kind: q.id, on: false });
    case 'block': return pair({ kind: 'block', block: q.block, on: true }, { kind: 'block', block: q.block, on: false });
    case 'param': return pair({ kind: 'param', block: q.block, knob: q.knob, value: q.value }, { kind: 'param', block: q.block, knob: q.knob, value: q.value2 });
    case 'scene': return each({ kind: 'scene', blocks: [...q.blocks] });
    case 'preset': return each({ kind: 'preset', preset: q.preset });
    case 'shortlong': return { mode: SHORT_LONG, a: [{ ...q.short }], b: [{ ...q.long }] };
  }
  throw new Error('unknown footswitch function ' + q.id);
}
export function recognize({ mode, a, b }) {
  const D = QUICK_DEFAULTS, custom = { ...D, id: 'custom' };
  if (mode === SHORT_LONG && a.length === 1 && b.length === 1) return { ...D, id: 'shortlong', short: a[0], long: b[0] };
  if (a.length !== 1 || b.length !== 1 || (mode !== HOLD && mode !== ALTERNATE)) return custom;
  const x = a[0], y = b[0], same = JSON.stringify(x) === JSON.stringify(y), hold = mode === HOLD;
  if (x.kind in SHOTS && x.on && ((!hold && same) || (hold && y.kind === x.kind && !y.on))) return { ...D, id: x.kind };   // the maker's editor stores a one-shot as press 127 / release 0
  if (!hold && same && x.kind === 'scene') return { ...D, id: 'scene', blocks: x.blocks };
  if (!hold && same && x.kind === 'preset') return { ...D, id: 'preset', preset: x.preset };
  if (x.kind in SWITCHED && y.kind === x.kind && x.on && !y.on) return { ...D, id: x.kind, hold };
  if (x.kind === 'block' && y.kind === 'block' && x.block === y.block && x.on && !y.on) return { ...D, id: 'block', block: x.block, hold };
  if (x.kind === 'param' && y.kind === 'param' && x.block === y.block && x.knob === y.knob) return { ...D, id: 'param', block: x.block, knob: x.knob, value: x.value, value2: y.value, hold };
  return custom;
}
/** The value a Frost control takes for MIDI value m (firmware/controller/midi_control.h, midi_parameter). */
export function pedalValue(p, m) {
  let v = p.min + Math.floor(((p.max - p.min) * m + 63) / 127);
  if (m !== 127 && p.step > 1) v = p.min + Math.floor((v - p.min + Math.floor(p.step / 2)) / p.step) * p.step;
  return Math.min(v, p.max);
}
/** The MIDI value for the k-th of n choices (docs/Midi Command.md: 0, 32, 64, 95, 127 for five). */
export const choiceValue = (k, n) => n > 1 ? Math.round(k * 127 / (n - 1)) : 0;
export const describe = m => m.type === 0 ? `PC ${m.d1}` : m.type === 1 ? `CC ${m.d1} = ${m.d2}` : `${TYPES[m.type] || 'type ' + m.type} ${m.d1} ${m.d2}`;
/** A short MIDI message from the controller (status, d1, d2) -> the footswitch index that sends it, or -1. */
export function switchFor(records, s, d1) {
  const type = (s & 0xf0) === 0xc0 ? 0 : (s & 0xf0) === 0xb0 ? 1 : -1, channel = (s & 15) + 1;
  return records.findIndex(r => r && [...r.a, ...r.b].some(m => m.type === type && m.channel === channel && m.d1 === d1));
}

// ---- three gestures per footswitch, recognised by Frost (firmware/controller/midi_control.h) ----
// The controller becomes a box of switches: on each press it sends, on channel 16, the footswitch's twelve
// definition bytes (CC 16..27: tap, double, hold, each kind/a/b/c) and CC 28 = n; on release CC 29 = n.
// Frost then runs the tap, the double press (second press within 400 ms) or the 2-second hold. A gesture action:
//   none | prev | next | tap | tuner {mode} | bypass {mode} | block {block, mode} | ctl (the preset's CTL effects) | scene {blocks}
//   | param {block, knob, value, value2 (null: always value)} | preset {preset} | midi {type 0 PC / 1 CC, d1, d2}
// mode: 'toggle' (the pedal's own state decides), 'on' or 'off'.
export const GESTURES = ['tap', 'double', 'hold'];
export const GESTURE_KINDS = ['none', 'prev', 'next', 'tap', 'tuner', 'bypass', 'block', 'ctl', 'scene', 'param', 'preset', 'midi'];
const FSW_CHANNEL = 16, FSW_DEF = 16, FSW_PRESS = 28, FSW_RELEASE = 29;
const NONE = { kind: 'none' };
export function actionBytes(x) {
  const sw = (cc, mode) => mode === 'toggle' ? [3, cc, 0, 0] : [1, cc, mode === 'on' ? 127 : 0, 0];
  switch (x?.kind) {
    case 'prev': return [1, 88, 127, 0];
    case 'next': return [1, 87, 127, 0];
    case 'tap': return [1, 85, 127, 0];
    case 'tuner': return sw(86, x.mode);
    case 'bypass': return sw(90, x.mode);
    case 'block': return sw(19 + x.block, x.mode);
    case 'ctl': return [5, 0, 0, 0];
    case 'scene': return [1, 89, x.blocks.reduce((m, b) => m | (1 << (b - 1)), 0), 0];
    case 'param': { const cc = 32 + 8 * (x.block - 1) + (x.knob - 1); return x.value2 == null ? [1, cc, x.value & 127, 0] : [4, cc, x.value & 127, x.value2 & 127]; }
    case 'preset': return [2, (x.preset - 1) & 127, 0, 0];
    case 'midi': return x.type === 0 ? [2, x.d1 & 127, 0, 0] : [1, x.d1 & 127, x.d2 & 127, 0];
  }
  return [0, 0, 0, 0];
}
export function actionOf([kind, a, b, c]) {
  if (kind === 5) return { kind: 'ctl' };
  if (kind === 2) return a < 60 ? { kind: 'preset', preset: a + 1 } : { kind: 'midi', type: 0, d1: a, d2: 0 };
  if (kind === 3) {
    if (a === 86) return { kind: 'tuner', mode: 'toggle' };
    if (a === 90) return { kind: 'bypass', mode: 'toggle' };
    if (a >= 20 && a <= 25) return { kind: 'block', block: a - 19, mode: 'toggle' };
  }
  if (kind === 4 && a >= 32 && a <= 79) return { kind: 'param', block: ((a - 32) >> 3) + 1, knob: ((a - 32) & 7) + 1, value: b, value2: c };
  if (kind !== 1) return { ...NONE };
  const onoff = b === 127 ? 'on' : b === 0 ? 'off' : null;
  if (b === 127 && a === 88) return { kind: 'prev' };
  if (b === 127 && a === 87) return { kind: 'next' };
  if (b === 127 && a === 85) return { kind: 'tap' };
  if (onoff && a === 86) return { kind: 'tuner', mode: onoff };
  if (onoff && a === 90) return { kind: 'bypass', mode: onoff };
  if (onoff && a >= 20 && a <= 25) return { kind: 'block', block: a - 19, mode: onoff };
  if (a === 89 && b < 64) return { kind: 'scene', blocks: [1, 2, 3, 4, 5, 6].filter(x => b & (1 << (x - 1))) };
  if (a >= 32 && a <= 79) return { kind: 'param', block: ((a - 32) >> 3) + 1, knob: ((a - 32) & 7) + 1, value: b, value2: null };
  return { kind: 'midi', type: 1, d1: a, d2: b };
}
const fswCC = (cc, v) => ({ channel: FSW_CHANNEL, type: 1, d1: cc, d2: v & 127 });
/** Footswitch n with actions {tap, double, hold} -> the record fields that make the controller report it to Frost. */
export function gestureRecord(n, g) {
  const def = GESTURES.flatMap(k => actionBytes(g[k]));
  return { mode: HOLD, a: [...def.map((v, i) => fswCC(FSW_DEF + i, v)), fswCC(FSW_PRESS, n)], b: [fswCC(FSW_RELEASE, n)] };
}
/** The record fields -> {n, tap, double, hold}, or null when the footswitch is not set up for gestures. */
export function gesturesOf({ mode, a, b }) {
  const cc16 = m => m.channel === FSW_CHANNEL && m.type === 1;
  if (mode !== HOLD || a.length !== 13 || b.length !== 1 || !a.every(cc16) || !cc16(b[0])) return null;
  if (a.some((m, i) => m.d1 !== (i < 12 ? FSW_DEF + i : FSW_PRESS)) || b[0].d1 !== FSW_RELEASE || b[0].d2 !== a[12].d2) return null;
  const bytes = a.slice(0, 12).map(m => m.d2);
  return { n: a[12].d2, tap: actionOf(bytes.slice(0, 4)), double: actionOf(bytes.slice(4, 8)), hold: actionOf(bytes.slice(8, 12)) };
}
/** A ready-made function (recognize) as a tap action, so a footswitch keeps doing what it did when it gets gestures. */
export function actionOfQuick(q) {
  switch (q.id) {
    case 'prev': case 'next': case 'tap': return { kind: q.id };
    case 'tuner': case 'bypass': return { kind: q.id, mode: 'toggle' };
    case 'block': return { kind: 'block', block: q.block, mode: 'toggle' };
    case 'param': return { kind: 'param', block: q.block, knob: q.knob, value: q.value, value2: q.value2 };
    case 'scene': return { kind: 'scene', blocks: [...q.blocks] };
    case 'preset': return { kind: 'preset', preset: q.preset };
  }
  return { ...NONE };
}
/** The MIDI messages a gesture makes the pedal play, for showing ("CC 86 toggled"). */
export function describeAction(x) {
  const [kind, a, b, c] = actionBytes(x);
  return kind === 0 ? 'nothing' : kind === 5 ? 'CTL' : kind === 2 ? `PC ${a}` : kind === 3 ? `CC ${a} toggled` : kind === 4 ? `CC ${a} = ${b} / ${c}` : `CC ${a} = ${b}`;
}

// ---- the link: one question at a time, each answered or timed out ----
export class ChocolateLink {
  /** out: {send(bytes)}; the caller hands every received message to receive(). */
  constructor(out) { this.out = out; this.queue = Promise.resolve(); this.waiting = null; this.onshort = null; }
  receive(data) {
    if (data[0] === 0xf0) { const p = decode(data); if (p && this.waiting?.match(p)) { const w = this.waiting; this.waiting = null; w.resolve(p); } return; }
    if (data[0] >= 0x80 && data[0] < 0xf0) this.onshort?.(data);
  }
  ask(msg, match, timeout = 1500) {
    const run = () => new Promise(resolve => {
      const timer = setTimeout(() => { if (this.waiting === w) this.waiting = null; resolve(null); }, timeout);
      const w = { match, resolve: p => { clearTimeout(timer); resolve(p); } };
      this.waiting = w;
      try { this.out.send(msg); } catch { /* port gone: the timeout answers */ }
    });
    const turn = this.queue.then(run); this.queue = turn.catch(() => {}); return turn;
  }
  async who() { const p = await this.ask(whoRequest(), p => p.cmd === 0x11); if (!p) return null; let e = 0; while (e < p.payload.length && p.payload[e]) e++; return String.fromCharCode(...p.payload.slice(0, e)); }
  async read(addr, len) {
    const out = new Uint8Array(len);
    for (let done = 0; done < len;) {
      const n = Math.min(CHOC.CHUNK, len - done), at = addr + done;
      let p = null;
      for (let tries = 0; !p && tries < 3; tries++) p = await this.ask(readRequest(at, n), p => p.cmd === 0x23 && num(p.payload, 1, 4) === at);
      if (!p || num(p.payload, 5, 3) !== n) throw new Error('The controller did not answer a read. Check its USB cable and that it is on U.');
      out.set(p.payload.slice(8, 8 + n), done); done += n;
    }
    return out;
  }
  async write(addr, data) {
    for (let done = 0; done < data.length;) {
      const n = Math.min(1024, data.length - done);
      const p = await this.ask(writeRequest(addr + done, Array.from(data.subarray(done, done + n))), p => p.cmd === 0x00, 3000);
      if (!p || p.payload[0] !== 0) throw new Error('The controller did not confirm the write.');
      done += n;
    }
  }
  /** The four footswitch records of group 1, as bytes. */
  async readSwitches() { return this.read(CHOC.BASE, CHOC.SWITCHES * CHOC.RECORD); }
}

// ---- a simulated controller (demo and tests): answers the same packets from a memory image ----
export function defaultMemory() {
  const m = new Uint8Array(CHOC.SIZE);
  m.set([3, 1, 0, 0, 0x20, 0, 0x21, 0, 0x22, 0, 0x23, 0, 7, 1]);
  ['prev', 'next', 'tap', 'tuner'].forEach((id, i) => m.set(buildRecord(new Uint8Array(CHOC.RECORD), recordOfSetup(quickSetup({ ...QUICK_DEFAULTS, id }))), recordAddress(i)));
  return m;
}
export class SimulatedChocolate {
  constructor(memory = defaultMemory()) { this.memory = memory; this.name = 'FootCtrlPlus (demo)'; this.onmessage = null; }
  send(msg) {
    const p = decode(msg); if (!p) return;
    let reply = null;
    if (p.cmd === 0x11) reply = packet(0x11, [...'FootCtrlPlus_012'].map(c => c.charCodeAt(0)).concat(Array(11).fill(0)));
    if (p.cmd === 0x23) { const at = num(p.payload, 1, 4), n = num(p.payload, 5, 3); reply = packet(0x23, [...p.payload.slice(0, 8), ...this.memory.subarray(at, at + n)]); }
    if (p.cmd === 0x22) { const at = num(p.payload, 1, 4), n = num(p.payload, 5, 3); this.memory.set(p.payload.slice(8, 8 + n), at); reply = packet(0x00, [0]); }
    if (reply) { const bytes = encode(reply); setTimeout(() => this.onmessage?.(bytes), 5); }
  }
}
