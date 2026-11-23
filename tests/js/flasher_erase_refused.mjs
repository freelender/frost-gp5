// The installer's message when the first erase fails: a refusal (non-zero
// status, e.g. a reportedly locked newer unit) versus no answer. Offline, with
// the simulated pedal; prints one JSON line.
import * as F from '../../web/flasher/gp5flash.js';
import { FakePedal } from './flasher/fakepedal.mjs';
Object.assign(F.Timing, { reply: 10, ack: 10, begin: 10, final: 10, beginSettle: 0, settle: 0, retryDelay: 0, reenum: 60, poll: 1 });
const sections = [[0x63, new Uint8Array(64)]];
const out = {};
for (const [name, o] of [['refused', { nakBegin: true }], ['silent', { stallBegin: true }]]) {
  const p = new FakePedal(F.MODE_LOADER, o);
  try { await F.flash(p, sections, () => {}); out[name] = { error: null }; }
  catch (e) { out[name] = { error: e.message, status: e.status ?? null, written: Object.keys(p.written).length }; }
}
console.log(JSON.stringify(out));
