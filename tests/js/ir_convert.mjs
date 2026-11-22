import fs from 'node:fs';
import { convertIr } from '../../web/ir.js';
const r=convertIr(new Uint8Array(fs.readFileSync(process.argv[2])),process.argv[3]||'TEST');
console.log(JSON.stringify({record:Buffer.from(r.record).toString('hex'),h:Array.from(r.h),info:r.info}));
