// web/firmware.js identify() on firmware files given as arguments; prints one JSON line.
import { readFileSync } from 'fs';
import { identify } from '../../web/firmware.js';
const out = process.argv.slice(2).map(p => { try { const r = identify(new Uint8Array(readFileSync(p))); return { kind: r.kind, version: r.version || '' }; } catch (e) { return { error: e.message }; } });
console.log(JSON.stringify(out));
