import assert from 'node:assert/strict';
import { FILES, PedalFiles, crc32 } from '../../web/pedal.js';
import { Store } from '../../web/store.js';

// NOR-like flash with faults at the protocol boundary: a refused pause does
// not write, whereas a lost acknowledgement can follow a completed write.
function pedal(writeDelay = async () => {}) {
  const flash = new Uint8Array(FILES.SECTORS * FILES.SECTOR).fill(255);
  const calls = [], faults = new Map();
  const ask = async q => {
    const command = q[1], addr = (q[2] | q[3] << 8 | q[4] << 16 | q[5] << 24) >>> 0;
    const offset = addr - FILES.BASE, key = `${command}:${addr}`;
    calls.push([...q]);
    const fault = faults.get(key)?.shift();
    if (fault === 'refuse') return Uint8Array.of(0x7e, command, 0);
    if (command === 5) return Uint8Array.of(0x7e, command, 1, ...flash.subarray(offset, offset + q[6]));
    if (command === 7) flash.fill(255, offset, offset + FILES.SECTOR);
    if (command === 8) for (let i = 6; i < q.length; i++) flash[offset + i - 6] &= q[i];
    if (fault === 'lost') return null;
    if (fault === 'corrupt') flash[offset] ^= 1;
    return Uint8Array.of(0x7e, command, 1);
  };
  return { store: new Store(new PedalFiles(ask, 3, { writeDelay })), calls, faults };
}
function record(size, label) {
  const r = new Uint8Array(size);
  r.set(new TextEncoder().encode(label));
  new DataView(r.buffer).setUint32(size - 4, crc32(r.subarray(0, size - 4)), true);
  return r;
}

for (const [kind, size] of [['amp', 400], ['ir', 1048]]) {
  const p = pedal(), rec = record(size, 'TEST');
  // Match the reported xx20b0 chunk offset, with two negative acknowledgements.
  const chunk = FILES.BASE + 0xb0;
  p.faults.set(`7:${FILES.BASE}`, ['refuse']);
  p.faults.set(`8:${chunk}`, ['refuse', 'refuse']);
  // This chunk was programmed, but its acknowledgement never reached the host.
  p.faults.set(`8:${chunk + 56}`, ['lost']);
  if (kind === 'amp') await p.store.ampPut(1, rec); else await p.store.irPut(0, rec);
  assert.deepEqual(await p.store.slot(kind, kind === 'amp' ? 1 : 0), rec);
  const retries = p.calls.filter(q => q[1] === 8 && (q[2] | q[3] << 8 | q[4] << 16 | q[5] << 24) === chunk);
  assert.equal(retries.length, 3);
  assert.deepEqual(retries[1], retries[0]);
  assert.deepEqual(retries[2], retries[0]);

  // A persistent failure must exhaust the limit and leave the previous copy.
  const spare = FILES.BASE + FILES.SECTOR;
  p.faults.set(`8:${spare + 0xb0}`, Array(8).fill('refuse'));
  await assert.rejects(p.store.putSlot(kind, kind === 'amp' ? 1 : 0, record(size, 'REPLACE')), /program failed/);
  assert.deepEqual(await p.store.slot(kind, kind === 'amp' ? 1 : 0), rec);
  assert.equal(p.faults.get(`8:${spare + 0xb0}`).length, 0);
  assert.equal(p.calls.filter(q => q[1] === 7 && (q[2] | q[3] << 8 | q[4] << 16 | q[5] << 24) === FILES.BASE).length, 2,
    'the committed old sector must never be erased during the failed replacement');

  // Successful acknowledgements cannot bypass the full data read-back check.
  p.faults.set(`8:${spare + 64}`, ['corrupt']);
  await assert.rejects(p.store.putSlot(kind, kind === 'amp' ? 1 : 0, record(size, 'CORRUPT')), /read-back failed/);
  assert.deepEqual(await p.store.slot(kind, kind === 'amp' ? 1 : 0), rec);
}

// Erase refusals are bounded too, and ordinary reads retain their behavior.
let attempts = 0;
const refused = new PedalFiles(async q => { attempts++; return Uint8Array.of(q[0], q[1], 0); }, 3, { writeDelay: async () => {} });
await assert.rejects(refused.erase(FILES.BASE), /erase failed/);
assert.equal(attempts, 8);
attempts = 0;
await assert.rejects(refused.read(FILES.BASE, 4), /flash read failed/);
assert.equal(attempts, 1);
// 32s: a long run of refusals (the pedal busy re-syncing with its DSP) is ridden
// out: seven refusals of one chunk, then success, with growing pauses.
{ const waits = [], p = pedal(async ms => { waits.push(ms); }), rec = record(400, 'LONG');
  const chunk = FILES.BASE + 0xb0; p.faults.set(`8:${chunk}`, Array(7).fill('refuse'));
  await p.store.ampPut(2, rec); assert.deepEqual(await p.store.slot('amp', 2), rec);
  assert.deepEqual(waits, [100, 200, 300, 400, 500, 600, 700]); }
console.log('AMP/CAB transient refusal, lost reply, bounded failure and copy preservation checks passed');
