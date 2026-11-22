// Spectral baseline (SB): port of build/nam-distill/sbfit.py as run for the
// shipped models (NPOST=128, OSV=2). Wiener-Hammerstein model at 44.1 kHz:
// pre FIR(128) -> gain -> asym exp saturation (2x oversampled) -> 40 Hz HP -> post FIR(128),
// magnitude-only minimum-phase FIRs by alternating spectral correction.
import { fft, firFilter, lfilter, resamplePoly, butterHighpass2, transfer, rfft } from './dsp.js';

export const FS = 44100, NF = 4096;
const OSV = 2;
const fgrid = Float64Array.from({ length: NF / 2 + 1 }, (_, k) => k * FS / NF);
export const HP = butterHighpass2(40, FS);

export function satFn(u, A, K) {
  return u > 0 ? A[0] * (1 - Math.exp(-K[0] * u)) : A[1] * (Math.exp(K[1] * u) - 1);
}
export class SBModel {
  constructor(npre = 128, npost = 128) {
    this.pre = new Float64Array(npre); this.pre[0] = 1; this.post = new Float64Array(npost); this.post[0] = 1;
    this.g = 1; this.A = [0.7, 0.7]; this.K = [10, 10];
  }
  mid(x) {
    const u = firFilter(this.pre, x); for (let i = 0; i < u.length; i++) u[i] *= this.g;
    const up = resamplePoly(u, OSV, 1); for (let i = 0; i < up.length; i++) up[i] = satFn(up[i], this.A, this.K);
    return resamplePoly(up, 1, OSV).subarray(0, x.length);
  }
  run(x) { return firFilter(this.post, lfilter(HP.b, HP.a, this.mid(x))); }
}
/** Fractional-octave (1/6) smoothing in the log domain on the 4096-point grid. */
export function smoothLog(h, frac = 1 / 6) {
  const n = h.length, l = new Float64Array(n), cs = new Float64Array(n + 1), o = new Float64Array(n);
  for (let i = 0; i < n; i++) { l[i] = Math.log(h[i] + 1e-12); cs[i + 1] = cs[i] + l[i]; }
  const df = FS / NF;
  for (let i = 0; i < n; i++) {
    const f = fgrid[i]; if (f < 20) { o[i] = l[i]; continue; }
    const lo = f * 2 ** (-frac / 2), hi = f * 2 ** (frac / 2);
    const a = Math.ceil(lo / df - 1e-12), b = Math.min(n - 1, Math.floor(hi / df + 1e-12));
    o[i] = (cs[b + 1] - cs[a]) / (b - a + 1);
  }
  for (let i = 0; i < n; i++) o[i] = Math.exp(o[i]);
  return o;
}
/** Minimum-phase FIR of length n from a magnitude on the grid (real cepstrum), tapered end. */
export function minphase(mag, n) {
  const re = new Float64Array(NF), im = new Float64Array(NF);
  for (let k = 0; k <= NF / 2; k++) re[k] = Math.log(Math.max(mag[k], 1e-6));
  for (let k = NF / 2 + 1; k < NF; k++) re[k] = re[NF - k];
  fft(re, im, true); for (let i = 0; i < NF; i++) { re[i] /= NF; im[i] = 0; }      // cepstrum (real part)
  for (let i = 1; i < NF / 2; i++) re[i] *= 2; for (let i = NF / 2 + 1; i < NF; i++) re[i] = 0;
  fft(re, im);
  for (let k = 0; k < NF; k++) { const m = Math.exp(re[k]); re[k] = m * Math.cos(im[k]); im[k] = m * Math.sin(im[k]); }
  fft(re, im, true);
  const h = new Float64Array(n), kk = Math.max(8, Math.floor(n / 8));
  for (let i = 0; i < n; i++) h[i] = re[i] / NF;
  for (let i = 0; i < kk; i++) h[n - kk + i] *= 0.5 * (1 + Math.cos(Math.PI * i / kk));
  return h;
}
export function magOf(h) { const { re, im } = rfft(h, NF); const m = new Float64Array(NF / 2 + 1); for (let k = 0; k < m.length; k++) m[k] = Math.hypot(re[k], im[k]); return m; }
/** sbfit.mel_err: mean |dB| of the smoothed transfer y->t over 80 Hz..10 kHz. */
export function melErr(y, t) {
  const s = smoothLog(transfer(y, t)); let e = 0, c = 0;
  for (let k = 0; k < s.length; k++) if (fgrid[k] >= 80 && fgrid[k] <= 10000) { e += Math.abs(20 * Math.log10(s[k])); c++; }
  return e / c;
}
function fitSaturation(m, xr, yr) {
  const b = Math.floor(0.1 * FS), P = [], N = [], U = [];
  for (let i = 0; i < xr.length - b; i += b) {
    let mx = -Infinity, mn = Infinity, u = 0;
    for (let k = i; k < i + b; k++) { if (yr[k] > mx) mx = yr[k]; if (yr[k] < mn) mn = yr[k]; if (Math.abs(xr[k]) > u) u = Math.abs(xr[k]); }
    P.push(Math.max(mx, 0)); N.push(Math.min(mn, 0)); U.push(u);
  }
  const Ap = Math.max(Math.max(...P), 1e-4), An = Math.max(-Math.min(...N), 1e-4), Umax = Math.max(...U);
  let su = 0, sp = 0, sn = 0;
  for (let i = 0; i < U.length; i++) if (U[i] < Umax * 10 ** (-60 / 20)) { su += U[i] * U[i]; sp += U[i] * P[i]; sn += U[i] * N[i]; }
  let Kp = (sp / su) / Ap, Kn = (-sn / su) / An;
  const S = (u, kp, kn) => u > 0 ? Ap * (1 - Math.exp(-kp * u)) : An * (Math.exp(kn * u) - 1);
  for (const which of [0, 1]) {
    let best = null;
    for (let sc = 0.5; sc <= 2.001; sc += 0.05) {
      const kp = which === 0 ? Kp * sc : Kp, kn = which === 0 ? Kn : Kn * sc; let e = 0;
      for (let i = 0; i < U.length; i++) e += (P[i] - S(U[i], kp, kn)) ** 2 + (N[i] - S(-U[i], kp, kn)) ** 2;
      if (!best || e < best[0]) best = [e, kp, kn];
    }
    Kp = best[1]; Kn = best[2];
  }
  m.A = [Ap, An]; m.K = [Kp, Kn];
}
/** sbfit.fit. Signals are at 44.1 kHz: ramp (xr,yr), noise (xn,yn), sweep (xs,ys), audio (xa,ya). */
export function fitSB(sig, log = () => {}) {
  const { xr, yr, xn, yn, xs, ys, xa, ya } = sig, m = new SBModel();
  fitSaturation(m, xr, yr);
  m.post = minphase(smoothLog(transfer(lfilter(HP.b, HP.a, m.mid(xn)), yn)), m.post.length);
  const a20x = xa.subarray(0, FS * 20), a20y = ya.subarray(0, FS * 20), a30x = xa.subarray(0, FS * 30), a30y = ya.subarray(0, FS * 30);
  const segs = [['sweep', xs, ys, 'post', 3], ['noise', xn, yn, 'pre', 2], ['audio', a30x, a30y, 'post', 5], ['noise', xn, yn, 'pre', 2]];
  let best = [1e9, null];
  for (const [name, x, y, which, iters] of segs) {
    let step = 1.0;
    for (let it = 0; it < iters; it++) {
      const C = smoothLog(transfer(m.run(x), y)); for (let k = 0; k < C.length; k++) C[k] = Math.min(5, Math.max(0.2, C[k])) ** step;
      const tgt = which === 'post' ? m.post : m.pre, cur = smoothLog(magOf(tgt)); for (let k = 0; k < cur.length; k++) cur[k] *= C[k];
      if (which === 'post') m.post = minphase(cur, m.post.length); else m.pre = minphase(cur, m.pre.length);
      const e = melErr(m.run(a20x), a20y); log(`SB ${name} ${it} ${e.toFixed(3)}`);
      if (e < best[0]) best = [e, [Float64Array.from(m.pre), Float64Array.from(m.post)]];
      else if (e > 1.2 * best[0]) { m.pre = Float64Array.from(best[1][0]); m.post = Float64Array.from(best[1][1]); step *= 0.5; }
      step *= 0.9;
    }
  }
  [m.pre, m.post] = best[1];
  const y = m.run(a30x); let p = 0, q = 0; for (let i = 0; i < y.length; i++) { p += a30y[i] ** 2; q += y[i] ** 2; }
  const sc = Math.sqrt((p / y.length) / (q / y.length)); for (let i = 0; i < m.post.length; i++) m.post[i] *= sc;
  return m;
}
