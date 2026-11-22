// FIR -> biquad cascade (port of build/nam-distill/iirfit.fit_cascade):
// weighted equation-error least squares on the complex response, Steiglitz-
// McBride reweighting, unstable poles reflected inside 0.999, then second-
// order sections (poles closest to the unit circle last, like scipy zpk2sos).
const FS = 44100, NW = 4096;

class C { constructor(re, im = 0) { this.re = re; this.im = im; } }
const cadd = (a, b) => new C(a.re + b.re, a.im + b.im), csub = (a, b) => new C(a.re - b.re, a.im - b.im);
const cmul = (a, b) => new C(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
const cdiv = (a, b) => { const d = b.re * b.re + b.im * b.im; return new C((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d); };
const cabs = a => Math.hypot(a.re, a.im);

/** Roots of p[0] x^n + ... + p[n] (real coefficients), Aberth-Ehrlich iteration plus Newton polish. */
export function roots(p) {
  let q = Array.from(p); while (q.length > 1 && q[0] === 0) q.shift();
  const n = q.length - 1; if (n < 1) return [];
  const c = q.map(v => v / q[0]);
  const ev = z => { let v = new C(1), d = new C(0); for (let k = 1; k <= n; k++) { d = cadd(cmul(d, z), v); v = cadd(cmul(v, z), new C(c[k])); } return [v, d]; };
  let R = 0; for (let k = 1; k <= n; k++) R = Math.max(R, Math.abs(c[k]) ** (1 / k)); R = 2 * R + 1e-3;
  let z = Array.from({ length: n }, (_, k) => new C(R * Math.cos(2 * Math.PI * k / n + 0.4), R * Math.sin(2 * Math.PI * k / n + 0.4)));
  for (let it = 0; it < 500; it++) {
    let moved = 0;
    const nz = z.map((zi, i) => {
      const [v, d] = ev(zi); if (cabs(v) === 0) return zi;
      const ratio = cdiv(v, d); let s = new C(0);
      for (let j = 0; j < n; j++) if (j !== i) s = cadd(s, cdiv(new C(1), csub(zi, z[j])));
      const w = cdiv(ratio, csub(new C(1), cmul(ratio, s)));
      moved = Math.max(moved, cabs(w) / Math.max(1, cabs(zi))); return csub(zi, w);
    });
    z = nz; if (moved < 1e-15) break;
  }
  return z.map(zi => { for (let k = 0; k < 3; k++) { const [v, d] = ev(zi); if (cabs(d) > 0) zi = csub(zi, cdiv(v, d)); } return zi; });
}
function poly(rs) { let c = [new C(1)]; for (const r of rs) { const o = Array(c.length + 1).fill(0).map(() => new C(0)); for (let k = 0; k < c.length; k++) { o[k] = cadd(o[k], c[k]); o[k + 1] = csub(o[k + 1], cmul(c[k], r)); } c = o; } return c.map(v => v.re); }
/** Least squares min |Mx-b| by Householder QR (rows >> cols). */
function lstsq(M, b, rows, cols) {
  const A = M.map(r => Float64Array.from(r)), y = Float64Array.from(b);
  for (let k = 0; k < cols; k++) {
    let nrm = 0; for (let i = k; i < rows; i++) nrm += A[i][k] ** 2; nrm = Math.sqrt(nrm); if (nrm === 0) continue;
    const alpha = A[k][k] > 0 ? -nrm : nrm, v = new Float64Array(rows); for (let i = k; i < rows; i++) v[i] = A[i][k]; v[k] -= alpha;
    let vv = 0; for (let i = k; i < rows; i++) vv += v[i] * v[i]; if (vv === 0) continue;
    for (let j = k; j < cols; j++) { let s = 0; for (let i = k; i < rows; i++) s += v[i] * A[i][j]; s = 2 * s / vv; for (let i = k; i < rows; i++) A[i][j] -= s * v[i]; }
    let s = 0; for (let i = k; i < rows; i++) s += v[i] * y[i]; s = 2 * s / vv; for (let i = k; i < rows; i++) y[i] -= s * v[i];
  }
  const x = new Float64Array(cols);
  for (let k = cols - 1; k >= 0; k--) { let s = y[k]; for (let j = k + 1; j < cols; j++) s -= A[k][j] * x[j]; x[k] = A[k][k] !== 0 ? s / A[k][k] : 0; }
  return x;
}
const grid = (() => { const W = new Float64Array(NW), wt = new Float64Array(NW); let s = 0;
  for (let i = 0; i < NW; i++) { W[i] = Math.PI * (i + 1) / NW; const f = W[i] * FS / (2 * Math.PI); wt[i] = f > 30 && f < 18000 ? 1 / f : 0; s += wt[i]; }
  for (let i = 0; i < NW; i++) wt[i] /= s; return { W, wt }; })();
function freqz(b, a, W) {
  return Array.from(W, w => { let nr = 0, ni = 0, dr = 0, di = 0;
    for (let k = 0; k < b.length; k++) { nr += b[k] * Math.cos(w * k); ni -= b[k] * Math.sin(w * k); }
    for (let k = 0; k < a.length; k++) { dr += a[k] * Math.cos(w * k); di -= a[k] * Math.sin(w * k); }
    return cdiv(new C(nr, ni), new C(dr, di)); });
}
/** Order-2K IIR fitted to FIR h; returns {sos: [[b0,b1,b2,1,a1,a2], ...], err}. */
export function fitCascade(h, K, iters = 12) {
  const n = 2 * K, { W, wt } = grid, H = freqz(h, [1], W);
  const w = H.map((hv, i) => Math.sqrt(wt[i]) / (cabs(hv) + 1e-9));
  const Zr = Array.from(W, x => Float64Array.from({ length: n + 1 }, (_, k) => Math.cos(x * k)));
  const Zi = Array.from(W, x => Float64Array.from({ length: n + 1 }, (_, k) => -Math.sin(x * k)));
  let a = new Float64Array(n + 1); a[0] = 1; let b = new Float64Array(n + 1);
  for (let it = 0; it < iters; it++) {
    const rows = [], rhs = [];
    for (let i = 0; i < NW; i++) {
      let ar = 0, ai = 0; for (let k = 0; k <= n; k++) { ar += Zr[i][k] * a[k]; ai += Zi[i][k] * a[k]; }
      const sw = w[i] / Math.max(Math.hypot(ar, ai), 1e-6), hr = H[i].re, hi = H[i].im;
      const re = new Float64Array(2 * n + 1), im = new Float64Array(2 * n + 1);
      for (let k = 0; k <= n; k++) { re[k] = Zr[i][k] * sw; im[k] = Zi[i][k] * sw; }
      for (let k = 1; k <= n; k++) { re[n + k] = -(hr * Zr[i][k] - hi * Zi[i][k]) * sw; im[n + k] = -(hr * Zi[i][k] + hi * Zr[i][k]) * sw; }
      rows.push(re); rhs.push(hr * sw); rows.push(im); rhs.push(hi * sw);
    }
    const sol = lstsq(rows, rhs, rows.length, 2 * n + 1);
    b = sol.subarray(0, n + 1); a = new Float64Array(n + 1); a[0] = 1; a.set(sol.subarray(n + 1), 1);
    let r = roots(a); if (r.some(z => cabs(z) > 0.999)) { r = r.map(z => cabs(z) > 0.999 ? cdiv(new C(0.999), new C(z.re, -z.im)) : z); a = Float64Array.from(poly(r)); }
  }
  const Hf = freqz(b, a, W); let err = 0; for (let i = 0; i < NW; i++) { const d = csub(Hf[i], H[i]); err += wt[i] * (d.re ** 2 + d.im ** 2) / (cabs(H[i]) ** 2 + 1e-12); }
  return { sos: toSos(b, a), err };
}
// pair conjugate roots into real quadratics; reals paired by magnitude
function quadratics(rs) {
  const cpx = rs.filter(z => Math.abs(z.im) > 1e-9 * Math.max(1, cabs(z)) && z.im > 0), real = rs.filter(z => Math.abs(z.im) <= 1e-9 * Math.max(1, cabs(z))).map(z => z.re);
  const q = cpx.map(z => ({ c1: -2 * z.re, c2: z.re * z.re + z.im * z.im, r: cabs(z) }));
  real.sort((x, y) => Math.abs(y) - Math.abs(x));
  for (let i = 0; i < real.length; i += 2) {
    if (i + 1 < real.length) q.push({ c1: -(real[i] + real[i + 1]), c2: real[i] * real[i + 1], r: Math.max(Math.abs(real[i]), Math.abs(real[i + 1])) });
    else q.push({ c1: -real[i], c2: 0, r: Math.abs(real[i]) });
  }
  return q;
}
function toSos(b, a) {
  let lead = 0; while (lead < b.length - 1 && Math.abs(b[lead]) < 1e-300) lead++;
  const k = b[lead] / a[0], zs = quadratics(roots(b.slice(lead))), ps = quadratics(roots(a));
  ps.sort((x, y) => x.r - y.r);                         // poles closest to |z|=1 last
  const used = new Set(), sec = [];
  for (let s = ps.length - 1; s >= 0; s--) {            // give each pole pair the nearest remaining zero pair
    let best = -1, bd = Infinity;
    zs.forEach((z, i) => { if (used.has(i)) return; const d = Math.abs(z.c1 - ps[s].c1) + Math.abs(z.c2 - ps[s].c2); if (d < bd) { bd = d; best = i; } });
    const z = best >= 0 ? zs[best] : { c1: 0, c2: 0 }; if (best >= 0) used.add(best);
    sec[s] = [1, z.c1, z.c2, 1, ps[s].c1, ps[s].c2];
  }
  // leftover zero pairs (more zeros than pole pairs: not expected for equal orders)
  sec[0][0] *= k; sec[0][1] *= k; sec[0][2] *= k;
  return sec;
}
