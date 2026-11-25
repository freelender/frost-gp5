// A stored cabinet record -> downloadable .wav (web/ir.js irWav) -> converted
// again as an upload would be. argv[2]: record file, argv[3]: output .wav.
import fs from 'node:fs';
import { convertIr, irWav } from '../../web/ir.js';
const rec = new Uint8Array(fs.readFileSync(process.argv[2])), wav = irWav(rec);
fs.writeFileSync(process.argv[3], wav);
const again = convertIr(wav, 'AGAIN'), d = new DataView(rec.buffer, rec.byteOffset), scale = d.getFloat32(16, true);
const h = Array.from({ length: 512 }, (_, i) => d.getInt16(20 + 2 * i, true) * scale);
console.log(JSON.stringify({ h, again: Array.from(again.h), info: again.info }));
