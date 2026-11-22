// Drives web/pedal.js PedalFiles against a pedal emulated by the caller: each USB
// request goes out on stdout as {"q": hex}, the reply comes back on stdin as {"r": hex|null}.
import readline from 'node:readline';
import fs from 'node:fs';
import { PedalFiles } from '../../web/pedal.js';
const rl = readline.createInterface({ input: process.stdin }), lines = rl[Symbol.asyncIterator]();
const ask = async q => { process.stdout.write(JSON.stringify({ q: Buffer.from(q).toString('hex') }) + '\n'); const { value } = await lines.next(); const r = JSON.parse(value).r; return r == null ? null : new Uint8Array(Buffer.from(r, 'hex')); };
const f = new PedalFiles(ask), out = {};
out.who = await f.who();
const [, , ...ops] = process.argv;
for (const op of ops) {
  const [cmd, idx, ...rest] = op.split(':'), file = rest.join(':');
  if (cmd === 'add') out['add' + idx] = await f.add(Number(idx), new Uint8Array(fs.readFileSync(file)));
  if (cmd === 'remove') out['remove' + idx] = await f.remove(Number(idx));
}
const { models, used } = await f.models(); out.models = Object.fromEntries(Object.entries(models).map(([k, v]) => [k, { label: v.label, sector: v.sector, seq: v.seq }])); out.used = used;
if (models[1]) out.download1 = Buffer.from(await f.download(1)).toString('hex');
process.stdout.write(JSON.stringify({ done: out }) + '\n');
