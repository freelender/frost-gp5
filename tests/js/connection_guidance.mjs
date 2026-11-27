// Exercise the page's connection handler with read-only simulated identity replies.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../../web/app.js', import.meta.url), 'utf8');
const notice = source.slice(source.indexOf('function notice('), source.indexOf("$('notice-close').onclick"));
const connect = source.slice(source.indexOf('async function connect()'), source.indexOf('function disconnect()'));

async function attempt(identity, error = null) {
  const nodes = new Map();
  const $ = id => { if (!nodes.has(id)) nodes.set(id, {}); return nodes.get(id); };
  let closed = false, route = null, attached = false;
  const state = { mode: 'off', op: null };
  const context = {
    $, state, isBusy: () => false, toast() {}, showStatus() {}, updateButtons() {},
    status: (kind, text) => { $('status').kind = kind; $('status').text = text; },
    disconnect: () => { state.mode = 'off'; }, go: view => { route = view; },
    MidiPedal: { connect: async () => {
      if (error) throw error;
      return { close: () => { closed = true; }, ask: () => assert.fail('unexpected device request') };
    } },
    PedalFiles: class { async who() { return identity; } },
    live: { attach: async () => { attached = true; return true; } },
    lost() {}, setLivePanel() {}, refreshAll: async () => {},
  };
  vm.runInNewContext(notice + connect + '\nthis.run = connect; this.clear = () => notice(\'\');', context);
  await context.run();
  return { $, state, closed, route, attached, clear: context.clear };
}

// Stock firmware can ignore the Frost identity request. Offer the installer
// without claiming to have detected stock or automatically installing anything.
const silent = await attempt(null);
assert.equal(silent.closed, true);
assert.equal(silent.state.mode, 'off');
assert.equal(silent.route, null);
assert.equal(silent.attached, false);
assert.equal(silent.$('notice-firmware').hidden, false);
assert.match(silent.$('notice-text').textContent, /Valeton stock firmware/);
assert.match(silent.$('notice-text').textContent, /If Frost is already installed/);
assert.equal(silent.$('status').text, 'Frost not detected');
silent.clear();
assert.equal(silent.$('notice-firmware').hidden, true);

const other = await attempt({ firmware: 'stock' });
assert.equal(other.closed, true);
assert.equal(other.route, 'firmware');
assert.equal(other.$('notice-firmware').hidden, false);
assert.match(other.$('notice-text').textContent, /Nothing is installed until you confirm/);

const frost = await attempt({ firmware: 'frost', storage: 1 });
assert.equal(frost.state.mode, 'frost');
assert.equal(frost.attached, true);
assert.equal(frost.closed, false);
assert.equal(frost.route, null);
assert.equal(frost.$('notice-firmware').hidden, true);

const denied = await attempt(null, new Error('MIDI permission denied'));
assert.equal(denied.$('notice-text').textContent, 'MIDI permission denied');
assert.equal(denied.$('notice-firmware').hidden, true);
assert.equal(denied.$('notice').className, 'banner bad');
console.log('Connection guidance: silent/stock/Frost/permission-denied checks passed (host mocks).');
