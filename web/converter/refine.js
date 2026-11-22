// Gradient refinement of the spectral baseline: port of build/nam-distill/sbplus.py
// variant cSB+3 (SBTAG=SB128 OSV=2 NAAV=20): two asymmetric exp saturation stages
// at 2x with firwin(20, 0.45) anti-alias filters, a 32-tap mid FIR, an envelope
// bias, trained with Adam on a multi-resolution log-STFT L1 + level loss.
// Gradients are written by hand (no autodiff library). The envelope is the exact
// recursive EMA the DSP runs (sbplus used a 2048-tap truncation; difference < 1e-5).
import { fft, firFilter, firAdjointInput, firAdjointTaps, firwin, lfilter } from './dsp.js';
import { HP } from './sb.js';
import { rng } from './signals.js';

export const OS = 2, NAA = 20;
export const AA = firwin(NAA, 0.9 / OS);
const NPRE = 128, NMID = 32;

// ---- parameters (one flat vector) ----
export function makeParams(sb) {
  const npost = sb.post.length;
  const L = { pre: [0, NPRE], g: [NPRE, 1], ea: [NPRE + 1, 1], ek: [NPRE + 2, 2], lA0: [NPRE + 4, 2], lK0: [NPRE + 6, 2], lA1: [NPRE + 8, 2], lK1: [NPRE + 10, 2], mid: [NPRE + 12, NMID] };
  L.post = [NPRE + 12 + NMID, npost];
  const n = L.post[0] + npost, v = new Float64Array(n), view = k => v.subarray(L[k][0], L[k][0] + L[k][1]);
  view('pre').set(sb.pre); view('g')[0] = sb.g; view('ea')[0] = -5;
  view('lA0').set([Math.log(sb.A[0]), Math.log(sb.A[1])]); view('lK0').set([Math.log(sb.K[0]), Math.log(sb.K[1])]);
  view('lA1').set([Math.log(8), Math.log(8)]); view('lK1').set([Math.log(1 / 8), Math.log(1 / 8)]);
  view('mid')[0] = 1;
  const imp = new Float64Array(npost); imp[0] = 1;
  view('post').set(firFilter(sb.post, lfilter(HP.b, HP.a, imp)));                  // fold the 40 Hz HP into post
  return { v, L, view, rf: NPRE + npost + NMID + 2 * NAA / OS };
}
const sig = z => 1 / (1 + Math.exp(-z));

// ---- forward with a tape for backprop ----
export function forward(P, x, keep = true) {
  const { view } = P, N = x.length, pre = view('pre'), g = view('g')[0], a = sig(view('ea')[0]), ek = view('ek');
  const u0 = firFilter(pre, x), u = new Float64Array(N);
  for (let t = 0; t < N; t++) u[t] = g * u0[t];
  const e = new Float64Array(N); let s = 0;
  for (let t = 0; t < N; t++) { s = a * Math.abs(u[t]) + (1 - a) * s; e[t] = s; }
  const tape = { x, u0, u, e, a, stages: [] };
  let cur = u;
  for (let i = 0; i < 2; i++) {
    const A = [Math.exp(view('lA' + i)[0]), Math.exp(view('lA' + i)[1])], K = [Math.exp(view('lK' + i)[0]), Math.exp(view('lK' + i)[1])];
    const up = new Float64Array(2 * N);
    for (let t = 0; t < N; t++) up[2 * t] = OS * (cur[t] - ek[i] * e[t]);
    const c1 = firFilter(AA, up), w = new Float64Array(2 * N);
    for (let t = 0; t < 2 * N; t++) { const v = c1[t]; w[t] = v > 0 ? A[0] * (1 - Math.exp(-K[0] * v)) : A[1] * (Math.exp(K[1] * v) - 1); }
    const c2 = firFilter(AA, w), dn = new Float64Array(N);
    for (let t = 0; t < N; t++) dn[t] = c2[2 * t];
    const st = { A, K, c1, dn }; tape.stages.push(st);
    cur = i === 0 ? firFilter(view('mid'), dn) : dn;
  }
  tape.last = cur;
  const y = firFilter(view('post'), cur);
  return keep ? { y, tape } : { y };
}
export function backward(P, tape, gy) {
  const { view, L } = P, G = new Float64Array(P.v.length), gv = k => G.subarray(L[k][0], L[k][0] + L[k][1]);
  const N = gy.length, ek = view('ek');
  firAdjointTaps(tape.last, gy, gv('post'));
  let gcur = firAdjointInput(view('post'), gy);
  const ge = new Float64Array(N);
  for (let i = 1; i >= 0; i--) {
    const st = tape.stages[i];
    let gdn = gcur;
    if (i === 0) { firAdjointTaps(st.dn, gcur, gv('mid')); gdn = firAdjointInput(view('mid'), gcur); }
    const gc2 = new Float64Array(2 * N); for (let t = 0; t < N; t++) gc2[2 * t] = gdn[t];
    const gw = firAdjointInput(AA, gc2), gc1 = new Float64Array(2 * N), { A, K, c1 } = st;
    let dlA0 = 0, dlA1 = 0, dlK0 = 0, dlK1 = 0;
    for (let t = 0; t < 2 * N; t++) {
      const v = c1[t], g = gw[t]; if (g === 0) continue;
      if (v > 0) { const ex = Math.exp(-K[0] * v); gc1[t] = g * A[0] * K[0] * ex; dlA0 += g * A[0] * (1 - ex); dlK0 += g * K[0] * A[0] * v * ex; }
      else { const ex = Math.exp(K[1] * v); gc1[t] = g * A[1] * K[1] * ex; dlA1 += g * A[1] * (ex - 1); dlK1 += g * K[1] * A[1] * v * ex; }
    }
    gv('lA' + i)[0] += dlA0; gv('lA' + i)[1] += dlA1; gv('lK' + i)[0] += dlK0; gv('lK' + i)[1] += dlK1;
    const gup = firAdjointInput(AA, gc1), gin = new Float64Array(N);
    let dek = 0;
    for (let t = 0; t < N; t++) { const q = OS * gup[2 * t]; gin[t] = q; dek -= q * tape.e[t]; ge[t] -= ek[i] * q; }
    gv('ek')[i] += dek;
    gcur = gin;                                     // gradient w.r.t. the stage input (u for stage 0)
  }
  // envelope: e[t] = a|u[t]| + (1-a) e[t-1]
  const { u, u0, e, a, x } = tape, gu = gcur;
  let lam = 0, da = 0;
  for (let t = N - 1; t >= 0; t--) {
    lam = ge[t] + (1 - a) * lam;
    const au = Math.abs(u[t]); da += lam * (au - (t ? e[t - 1] : 0));
    gu[t] += (u[t] > 0 ? 1 : u[t] < 0 ? -1 : 0) * a * lam;
  }
  gv('ea')[0] += da * a * (1 - a);
  let dg = 0; const gu0 = new Float64Array(N), g = view('g')[0];
  for (let t = 0; t < N; t++) { dg += gu[t] * u0[t]; gu0[t] = g * gu[t]; }
  gv('g')[0] += dg;
  firAdjointTaps(x, gu0, gv('pre'));
  return G;
}

// ---- spectral loss (sbplus.spec_loss for one item; caller averages the batch) ----
const winCache = new Map();
function hann(n) { let w = winCache.get(n); if (!w) { w = Float64Array.from({ length: n }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / n)); winCache.set(n, w); } return w; }
function reflectPad(x, p) {
  const M = x.length, o = new Float64Array(M + 2 * p);
  for (let j = 0; j < p; j++) o[j] = x[p - j];
  o.set(x, p);
  for (let j = 0; j < p; j++) o[p + M + j] = x[M - 2 - j];
  return o;
}
function reflectFold(g, p, M) {
  const o = g.slice(p, p + M);
  for (let j = 0; j < p; j++) { o[p - j] += g[j]; o[M - 2 - j] += g[p + M + j]; }
  return o;
}
function stftMag(x, n) {
  const hop = n / 4, pad = reflectPad(x, n / 2), F = 1 + Math.floor(x.length / hop), K = n / 2 + 1, w = hann(n);
  const mag = new Float64Array(F * K), Re = new Float64Array(F * K), Im = new Float64Array(F * K), re = new Float64Array(n), im = new Float64Array(n);
  for (let f = 0; f < F; f++) {
    for (let i = 0; i < n; i++) { re[i] = pad[f * hop + i] * w[i]; im[i] = 0; }
    fft(re, im);
    for (let k = 0; k < K; k++) { const o = f * K + k; Re[o] = re[k]; Im[o] = im[k]; mag[o] = Math.hypot(re[k], im[k]); }
  }
  return { mag, Re, Im, F, K, padLen: pad.length };
}
/** Returns {loss, grad} for prediction p against target t (same length). */
export function specLoss(p, t, withGrad = true) {
  const M = p.length; let loss = 0; const grad = withGrad ? new Float64Array(M) : null;
  for (const n of [512, 2048]) {
    const P = stftMag(p, n), T = stftMag(t, n), cnt = P.F * P.K;
    let l = 0; const gpad = withGrad ? new Float64Array(P.padLen) : null;
    const re = new Float64Array(n), im = new Float64Array(n), w = hann(n), hop = n / 4;
    for (let f = 0; f < P.F; f++) {
      if (withGrad) { re.fill(0); im.fill(0); }
      for (let k = 0; k < P.K; k++) {
        const o = f * P.K + k, d = Math.log(P.mag[o] + 1e-4) - Math.log(T.mag[o] + 1e-4); l += Math.abs(d);
        if (withGrad && P.mag[o] > 0) {
          const gm = (d > 0 ? 1 : d < 0 ? -1 : 0) / (P.mag[o] + 1e-4) / cnt;
          re[k] = gm * P.Re[o] / P.mag[o]; im[k] = gm * P.Im[o] / P.mag[o];
        }
      }
      if (withGrad) { fft(re, im, true); for (let i = 0; i < n; i++) gpad[f * hop + i] += re[i] * w[i]; }
    }
    loss += l / cnt;
    if (withGrad) { const gf = reflectFold(gpad, n / 2, M); for (let i = 0; i < M; i++) grad[i] += gf[i]; }
  }
  let pp = 0, tt = 0; for (let i = 0; i < M; i++) { pp += p[i] * p[i]; tt += t[i] * t[i]; } pp /= M; tt /= M;
  const dl = Math.log(pp + 1e-9) - Math.log(tt + 1e-9); loss += Math.abs(dl);
  if (withGrad) { const c = (dl > 0 ? 1 : dl < 0 ? -1 : 0) / (pp + 1e-9) * 2 / M; for (let i = 0; i < M; i++) grad[i] += c * p[i]; }
  return { loss, grad };
}

/** Mean loss and gradient over a batch of [start] indices into (xa, ya). */
export function batchGrad(P, xa, ya, starts, seg) {
  const w = P.rf, G = new Float64Array(P.v.length); let loss = 0;
  for (const s of starts) {
    const x = xa.subarray(s, s + seg + w), t = ya.subarray(s + w, s + seg + w);
    const { y, tape } = forward(P, x), r = specLoss(y.subarray(w), t);
    const gy = new Float64Array(seg + w); gy.set(r.grad, w);
    const g = backward(P, tape, gy); for (let i = 0; i < G.length; i++) G[i] += g[i] / starts.length;
    loss += r.loss / starts.length;
  }
  return { loss, grad: G };
}
/** sbplus.train: Adam(1e-3), cosine schedule, grad-norm clip 1.0, batch 8 x 16384. gradFn may run in workers. */
export async function train(P, xa, ya, { steps = 1500, seg = 16384, batch = 8, lr = 1e-3, seed = 0, gradFn = null, onStep = null } = {}) {
  const r = rng(1000 + seed), n = P.v.length, m = new Float64Array(n), v = new Float64Array(n), b1 = 0.9, b2 = 0.999, eps = 1e-8;
  const hi = xa.length - seg - P.rf - 1; let t = 0;
  for (let s = 0; s < steps; s++) {
    const starts = Array.from({ length: batch }, () => r.integers(0, hi));
    const { loss, grad } = gradFn ? await gradFn(P.v, starts, seg) : batchGrad(P, xa, ya, starts, seg);
    if (!Number.isFinite(loss) || grad.some(x => !Number.isFinite(x))) { if (onStep) onStep(s, NaN); continue; }
    let norm = 0; for (const x of grad) norm += x * x; norm = Math.sqrt(norm);
    const clip = Math.min(1, 1 / (norm + 1e-6)), lrS = 0.5 * lr * (1 + Math.cos(Math.PI * s / steps)); t++;
    for (let i = 0; i < n; i++) {
      const g = grad[i] * clip; m[i] = b1 * m[i] + (1 - b1) * g; v[i] = b2 * v[i] + (1 - b2) * g * g;
      P.v[i] -= lrS * (m[i] / (1 - b1 ** t)) / (Math.sqrt(v[i] / (1 - b2 ** t)) + eps);
    }
    if (onStep) onStep(s, loss);
  }
  return P;
}
/** The trained model in iirfit's terms: FIRs and the nonlinear parameters. */
export function exportModel(P) {
  const { view } = P, ex = k => [Math.exp(view(k)[0]), Math.exp(view(k)[1])];
  return { pre: Float64Array.from(view('pre')), mid: Float64Array.from(view('mid')), post: Float64Array.from(view('post')),
    g: view('g')[0], A: [ex('lA0'), ex('lA1')], K: [ex('lK0'), ex('lK1')], ea: sig(view('ea')[0]), ek: [view('ek')[0], view('ek')[1]] };
}
