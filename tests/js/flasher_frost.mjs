// Flashes a real Frost image through the vendored OpenGP5 web flasher into the
// simulated pedal (tests/js/flasher/fakepedal.mjs) and checks every section
// arrives intact and the pedal comes back running the open firmware.
import { readFileSync } from 'node:fs';
import * as F from '../../web/flasher/gp5flash.js';
import { FakePedal } from './flasher/fakepedal.mjs';
Object.assign(F.Timing, { reply: 10, ack: 10, begin: 10, final: 10, beginSettle: 0, settle: 0, retryDelay: 0, reenum: 60, poll: 1 });
const bytes = new Uint8Array(readFileSync(process.argv[2]));
const { sections } = F.inspect(bytes);
const p = new FakePedal(F.MODE_OPEN);
const mode = await F.flash(p, sections, () => {});
const out = { mode, sections: sections.map(([t, d]) => ({ type: t, bytes: d.length, same: !!p.written[t] && Buffer.compare(Buffer.from(p.written[t]), Buffer.from(d)) === 0 })) };
console.log(JSON.stringify(out));
