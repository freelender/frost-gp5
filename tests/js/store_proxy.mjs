// Drives web/store.js against a pedal emulated by the caller ({"q": hex} out on
// stdout, {"r": hex|null} back on stdin). argv[2]: JSON list of [op, ...args].
import readline from 'node:readline';
import fs from 'node:fs';
import { crc32, PedalFiles } from '../../web/pedal.js';
import { Store, parsePresetFile, presetAssets } from '../../web/store.js';
import { SetlistStore } from '../../web/setlist.js';
const rl = readline.createInterface({ input: process.stdin }), lines = rl[Symbol.asyncIterator]();
const ask = async q => { process.stdout.write(JSON.stringify({ q: Buffer.from(q).toString('hex') }) + '\n'); const { value } = await lines.next(); const r = JSON.parse(value).r; return r == null ? null : new Uint8Array(Buffer.from(r, 'hex')); };
const s = new Store(new PedalFiles(ask)), out = [];
for (const [op, ...a] of JSON.parse(process.argv[2])) {
  if (op === 'setGet') out.push(await new SetlistStore(s).read());
  else if (op === 'setDamage') { const d = await s.readGroup('glb', 0); d[64+97]=255; new DataView(d.buffer).setUint32(4028,crc32(d.subarray(64,4028)),true); await s.writeGroup('glb',0,d); out.push(true); }
  else if (op === 'setPut') { const sets = new SetlistStore(s); await sets.read(); await sets.write(a[0]); out.push(true); }
  else if (op === 'irPut') out.push(await s.irPut(a[0], new Uint8Array(fs.readFileSync(a[1]))));
  else if (op === 'irRemove') out.push(await s.irRemove(a[0]));
  else if (op === 'irs') out.push(await s.irs());
  else if (op === 'irGet') { const r = await s.irGet(a[0]); out.push(r ? Buffer.from(r).toString('hex') : null); }
  else if (op === 'migrate') out.push(await s.migrate());
  else if (op === 'ampPut') out.push(await s.ampPut(a[0], new Uint8Array(fs.readFileSync(a[1]))));
  else if (op === 'ampGet') { const r = await s.ampGet(a[0]); out.push(r ? Buffer.from(r).toString('hex') : null); }
  else if (op === 'ampRemove') out.push(await s.ampRemove(a[0]));
  else if (op === 'amps') out.push((await s.amps()).models);
  else if (op === 'presets') out.push(await s.presets());
  else if (op === 'rename') out.push(await s.presetRename(a[0], a[1]));
  else if (op === 'swap') out.push(await s.presetSwap(a[0], a[1]));
  else if (op === 'delete') out.push(await s.presetDelete(a[0]));
  else if (op === 'backup') { const t = await s.backup(); fs.writeFileSync(a[0], t); out.push(t.length); }
  else if (op === 'restore') out.push(await s.restore(fs.readFileSync(a[0], 'utf8')));
  else if (op === 'presetFile') { const p = await s.presetFile(a[0]); fs.writeFileSync(a[1], JSON.stringify(p)); out.push({ models: p.models.map(x => [x.number, x.name]), cabs: p.cabs.map(x => [x.number, x.name]) }); }
  else if (op === 'parse') { const f = parsePresetFile(fs.readFileSync(a[0], 'utf8')); out.push({ name: f.name, used: f.used, models: f.models.map(x => x.number), cabs: f.cabs.map(x => x.number) }); }
  else if (op === 'presetImport') out.push(await s.presetImport(a[0], parsePresetFile(fs.readFileSync(a[1], 'utf8')), a[2], a[3]));
  else if (op === 'assets') out.push(presetAssets(await s.presetGet(a[0])));
  else if (op === 'get') { const r = await s.presetGet(a[0]); out.push(r ? Buffer.from(r).toString('hex') : null); }
  else throw new Error('unknown op ' + op);
}
process.stdout.write(JSON.stringify({ done: out }) + '\n', () => process.exit(0));
