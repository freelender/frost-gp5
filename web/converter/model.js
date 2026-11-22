// The converted FCM model at 44.1 kHz in JS (float64): the same structure the
// DSP runs (effects/fcm/fcm_iir.c; iirfit.forward), for the quality report and
// A/B previews. Plus the Mel log-spectral metric of build/nam-distill/specmetrics.py.
import { sosfilt, firFilter, fft } from './dsp.js';

export function runModel(m, x) {
  let u = sosfilt(m.pre, x); for (let i = 0; i < u.length; i++) u[i] *= m.g;
  const a = m.ea, e = new Float64Array(u.length); let s = 0;
  for (let i = 0; i < u.length; i++) { s = a * Math.abs(u[i]) + (1 - a) * s; e[i] = s; }
  for (let st = 0; st < 2; st++) {
    const A = m.A[st], K = m.K[st], N = u.length, up = new Float64Array(2 * N);
    for (let i = 0; i < N; i++) up[2 * i] = 2 * (u[i] - m.ek[st] * e[i]);
    const c1 = firFilter(m.aa, up);
    for (let i = 0; i < c1.length; i++) { const v = c1[i]; c1[i] = v > 0 ? A[0] * (1 - Math.exp(-K[0] * v)) : A[1] * (Math.exp(K[1] * v) - 1); }
    const c2 = firFilter(m.aa, c1), d = new Float64Array(N); for (let i = 0; i < N; i++) d[i] = c2[2 * i];
    u = st === 0 ? sosfilt(m.mid, d) : d;
  }
  return sosfilt(m.post, u);
}

// ---- specmetrics.compare ----
const FS = 44100, N = 2048, HOP = 1024;
const FB = (() => { const mel = f => 2595 * Math.log10(1 + f / 700), im = v => 700 * (10 ** (v / 2595) - 1), nb = 64;
  const edges = Array.from({ length: nb + 2 }, (_, i) => im(mel(80) + (mel(10000) - mel(80)) * i / (nb + 1)));
  return Array.from({ length: nb }, (_, b) => { const [lo, c, hi] = edges.slice(b, b + 3); return Float64Array.from({ length: N / 2 + 1 }, (_, k) => { const f = k * FS / N; return Math.max(0, Math.min((f - lo) / (c - lo), (hi - f) / (hi - c))); }); }); })();
const WIN = Float64Array.from({ length: N }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1)));
function melspec(x) {
  const frames = [], levels = [], re = new Float64Array(N), im = new Float64Array(N);
  for (let s = 0; s < x.length - N; s += HOP) {
    let lv = 0; for (let i = 0; i < N; i++) { re[i] = x[s + i] * WIN[i]; im[i] = 0; lv += re[i] * re[i]; }
    fft(re, im); const P = new Float64Array(N / 2 + 1); for (let k = 0; k <= N / 2; k++) P[k] = re[k] * re[k] + im[k] * im[k];
    frames.push(FB.map(fb => { let v = 0; for (let k = 0; k < fb.length; k++) v += P[k] * fb[k]; return 10 * Math.log10(v + 1e-12); }));
    levels.push(10 * Math.log10(lv / N + 1e-12));
  }
  return { frames, levels };
}
function percentile(v, q) { const s = [...v].sort((a, b) => a - b), p = (s.length - 1) * q / 100, lo = Math.floor(p); return s[lo] + (s[Math.min(lo + 1, s.length - 1)] - s[lo]) * (p - lo); }
/** Mel log-magnitude error (dB) of y against the reference t, overall/loud/quiet, long-term and level ratio. */
export function compare(t, y) {
  const n = Math.min(t.length, y.length); t = t.subarray(0, n); y = y.subarray(0, n);
  const T = melspec(t), Y = melspec(y), mx = Math.max(...T.levels), act = T.levels.map(l => l > mx - 50);
  const err = T.frames.map((f, i) => f.reduce((s, v, b) => s + Math.abs(Y.frames[i][b] - v), 0) / f.length);
  const al = T.levels.filter((_, i) => act[i]), p60 = percentile(al, 60), p40 = percentile(al, 40);
  const mean = sel => { let s = 0, c = 0; err.forEach((e, i) => { if (sel(i)) { s += e; c++; } }); return s / c; };
  const lt = FB.map((_, b) => { let ys = 0, ts = 0; T.frames.forEach((f, i) => { if (act[i]) { ys += 10 ** (Y.frames[i][b] / 10); ts += 10 ** (f[b] / 10); } }); return Math.abs(10 * Math.log10(ys / ts)); });
  let py = 0, pt = 0; for (let i = 0; i < n; i++) { py += y[i] * y[i]; pt += t[i] * t[i]; }
  return { mel_db: mean(i => act[i]), mel_db_loud: mean(i => act[i] && T.levels[i] > p60), mel_db_quiet: mean(i => act[i] && T.levels[i] <= p40),
    longterm_db: lt.reduce((s, v) => s + v, 0) / lt.length, level_ratio: Math.sqrt(py / pt) };
}
