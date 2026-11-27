import assert from 'node:assert/strict';
import { detectFrost } from '../../web/onboarding.js';
import { buildSysex, parseSysex } from '../../web/pedal.js';

function fixture({ permission = 'granted', firmware = 'frost', name = 'GP-5 Open FW', connected = true } = {}) {
  const listeners = new Set(), sent = [], calls = [];
  const originalHandler = () => {};
  const inp = { name, state: connected ? 'connected' : 'disconnected', onmidimessage: originalHandler,
    addEventListener: (type, fn) => listeners.add(fn), removeEventListener: (type, fn) => listeners.delete(fn) };
  const out = { name, state: inp.state, send: bytes => {
    sent.push([...parseSysex(bytes)]);
    if (firmware !== null) queueMicrotask(() => {
      const reply = [0x7e, 0x0d, 1, 0, 0, 0, ...new TextEncoder().encode(firmware), 0, 2];
      for (const fn of listeners) fn({ data: buildSysex(reply) });
    });
  } };
  const nav = {
    permissions: { query: async descriptor => { assert.deepEqual(descriptor, { name: 'midi', sysex: true }); return { state: permission }; } },
    requestMIDIAccess: async () => { calls.push('access'); return { inputs: new Map([['in', inp]]), outputs: new Map([['out', out]]) }; },
  };
  return { nav, inp, sent, calls, listeners, originalHandler };
}
for (const permission of ['prompt', 'denied']) {
  const f = fixture({ permission });
  assert.equal(await detectFrost(f.nav), false);
  assert.deepEqual(f.calls, [], 'do not request MIDI access or show a prompt');
  assert.deepEqual(f.sent, []);
}
for (const [options, expected] of [
  [{}, true],
  [{ name: 'GP-5', firmware: 'frost' }, true],
  [{ name: 'GP-5 Open FW', firmware: 'opengp5' }, false],
  [{ firmware: null }, false],
  [{ connected: false }, false],
]) {
  const f = fixture(options);
  assert.equal(await detectFrost(f.nav, () => true, 5), expected);
  for (const payload of f.sent) assert.deepEqual(payload, [0x7e, 0x0d], 'only read identity, never write or reboot');
  assert.equal(f.listeners.size, 0, 'remove temporary input listeners');
  assert.equal(f.inp.onmidimessage, f.originalHandler, 'preserve the editor input handler');
}
const busy = fixture();
assert.equal(await detectFrost(busy.nav, () => false), false);
assert.deepEqual(busy.calls, []);
const changed = fixture();
let idle = true;
changed.nav.permissions.query = async () => { idle = false; return { state: 'granted' }; };
assert.equal(await detectFrost(changed.nav, () => idle), false);
assert.deepEqual(changed.calls, [], 'do not probe if an operation started while checking permission');
assert.equal(await detectFrost({}), false);
assert.equal(await detectFrost({ requestMIDIAccess() {}, permissions: { query: async () => { throw Error('unsupported descriptor'); } } }), false);
console.log('Read-only Frost detection: permission, identity, timeout, disconnect and busy checks passed (host mocks).');
