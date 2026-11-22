// Host checks for web/converter (run by tests/test_fcm_converter.py). Prints one JSON line.
import fs from 'node:fs';
import { packRecord, crc32 } from '../../web/converter/record.js';
import { fitCascade } from '../../web/converter/iir.js';
import { lfilter, sosfilt, resamplePoly, firwin } from '../../web/converter/dsp.js';
import { makeParams, batchGrad } from '../../web/converter/refine.js';
import { loadNam, renderNam, renderNamBlocks } from '../../web/converter/nam.js';
import { fitRecord } from '../../web/converter/convert.js';
import { probes, guitarish } from '../../web/converter/signals.js';
const out = {}, model = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
out.record = Buffer.from(packRecord('TEST AMP', model)).toString('hex');
out.crc = crc32(new TextEncoder().encode('FrostCaptureModel'));
// biquad fit of a known 6th-order IIR's (truncated) impulse response
const sos = [[0.3, 0.1, -0.05, 1, -1.2, 0.5], [1, -0.5, 0.2, 1, -0.4, 0.1], [0.8, 0.3, 0.1, 1, 0.2, 0.3]];
const imp = new Float64Array(512); imp[0] = 1; const h = sosfilt(sos, imp), fit = fitCascade(h, 3);
const y1 = sosfilt(fit.sos, imp); let e = 0, p = 0; for (let i = 0; i < 512; i++) { e += (y1[i] - h[i]) ** 2; p += h[i] ** 2; }
out.iir = { err: fit.err, esr: e / p, sections: fit.sos.length, stable: fit.sos.every(s => Math.abs(s[5]) < 1 && Math.abs(s[4]) < 1 + s[5]) };
// gradient check at a few parameters
const rx = guitarish(8, 44100, 5), ry = rx.map(v => 0.4 * Math.tanh(4 * v));
const P = makeParams({ pre: Object.assign(new Float64Array(128), { 0: 1 }), post: Object.assign(new Float64Array(128), { 0: 1 }), g: 1.5, A: [0.4, 0.35], K: [8, 9] });
P.view('ek')[0] = 0.1; P.view('ek')[1] = 0.05; P.view('ea')[0] = -4; P.view('mid')[2] = 0.1;
const starts = [20000, 150000], seg = 4096, G = batchGrad(P, rx, ry, starts, seg).grad; let worst = 0;
for (const i of [3, P.L.g[0], P.L.ea[0], P.L.ek[0], P.L.lA0[0], P.L.lK0[1], P.L.lA1[1], P.L.lK1[0], P.L.mid[0] + 2, P.L.post[0] + 7]) {
  const hh = 1e-5 * Math.max(1, Math.abs(P.v[i])), o = P.v[i];
  P.v[i] = o + hh; const lp = batchGrad(P, rx, ry, starts, seg).loss; P.v[i] = o - hh; const lm = batchGrad(P, rx, ry, starts, seg).loss; P.v[i] = o;
  const fd = (lp - lm) / (2 * hh); worst = Math.max(worst, Math.abs(fd - G[i]) / Math.max(1e-6, Math.abs(fd) + Math.abs(G[i])));
}
out.grad_worst_rel = worst;
// resampler: a 1 kHz tone keeps its level and frequency 48k -> 44.1k
const tone = Float64Array.from({ length: 48000 }, (_, i) => Math.sin(2 * Math.PI * 1000 * i / 48000)), rs = resamplePoly(tone, 44100, 48000);
let err = 0; for (let i = 2000; i < 42000; i++) err = Math.max(err, Math.abs(rs[i] - Math.sin(2 * Math.PI * 1000 * i / 44100)));
out.resample = { length: rs.length, max_err: err };
out.firwin_sum = firwin(20, 0.45).reduce((s, v) => s + v, 0);
// optional: NAM example + a short end-to-end smoke conversion
if (process.argv[3] && fs.existsSync(process.argv[3])) {
  const { net, info } = loadNam(JSON.parse(fs.readFileSync(process.argv[3], 'utf8')));
  const x = guitarish(1, 48000, 2), a = renderNam(net, x), b = renderNamBlocks(net, x); let d = 0; for (let i = 0; i < x.length; i++) d = Math.max(d, Math.abs(a[i] - b[i]));
  out.nam = { info, block_vs_scalar: d };
  const pr = probes(48000), tr = guitarish(35, 48000, 1), rs44 = v => resamplePoly(v, 44100, 48000), R = v => rs44(renderNamBlocks(net, v));
  const sig = { xr: rs44(pr.ramp), yr: R(pr.ramp), xn: rs44(pr.noise), yn: R(pr.noise), xs: rs44(pr.sweep), ys: R(pr.sweep), xa: rs44(tr), ya: R(tr) };
  const r = await fitRecord(sig, 'SMOKE', { steps: 4 });
  out.smoke = { bytes: r.record.length, crc_ok: crc32(r.record.subarray(0, 396)) === new DataView(r.record.buffer).getUint32(396, true), sections: [r.model.pre.length, r.model.mid.length, r.model.post.length] };
}
console.log(JSON.stringify(out));
