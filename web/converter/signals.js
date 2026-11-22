// Probe and training signals for the converter, generated at the model's rate
// (48 kHz for tone3000 A2). Same recipes as build/nam-distill (sbfit probes,
// distill.guitarish, distill2.heldout); a seeded PRNG of our own (not numpy's),
// so sample values differ but the material is statistically the same.

export function rng(seed) {
  // xoshiro128** seeded by splitmix32
  let s0, s1, s2, s3, z = seed >>> 0;
  const sm = () => { z = (z + 0x9e3779b9) >>> 0; let t = z; t = Math.imul(t ^ (t >>> 16), 0x21f0aaad); t = Math.imul(t ^ (t >>> 15), 0x735a2d97); return (t ^ (t >>> 15)) >>> 0; };
  s0 = sm(); s1 = sm(); s2 = sm(); s3 = sm();
  const next = () => {
    const r = Math.imul(((Math.imul(s1, 5) << 7) | (Math.imul(s1, 5) >>> 25)), 9) >>> 0, t = (s1 << 9) >>> 0;
    s2 ^= s0; s3 ^= s1; s1 ^= s2; s0 ^= s3; s2 ^= t; s3 = ((s3 << 11) | (s3 >>> 21)) >>> 0;
    return r;
  };
  let spare = null;
  const random = () => (next() * 2 ** -32 + next()) * 2 ** -32;            // [0,1), 53-ish bits
  return {
    random,
    uniform: (lo, hi) => lo + (hi - lo) * random(),
    integers: (lo, hi) => lo + Math.floor(random() * (hi - lo)),
    normal(mu = 0, sd = 1) {
      if (spare !== null) { const v = spare; spare = null; return mu + sd * v; }
      let u, v, s; do { u = 2 * random() - 1; v = 2 * random() - 1; s = u * u + v * v; } while (s >= 1 || s === 0);
      const m = Math.sqrt(-2 * Math.log(s) / s); spare = v * m; return mu + sd * u * m;
    },
  };
}
const clip = (x, c = 0.99) => { for (let i = 0; i < x.length; i++) x[i] = Math.max(-c, Math.min(c, x[i])); return x; };
function pluck(rate, f, dur, amp, damp, r) {
  const L = Math.max(2, Math.floor(rate / f)), buf = new Float64Array(L), y = new Float64Array(dur);
  for (let i = 0; i < L; i++) buf[i] = r.uniform(-1, 1) * amp;
  for (let i = 0, k = 0; i < dur; i++) { y[i] = buf[k]; const nk = (k + 1) % L; buf[k] = damp * 0.5 * (buf[k] + buf[nk]); k = nk; }
  return y;
}
/** sbfit probes: 300 Hz ramp -90..0 dB (5 s), -70 dB noise (15 s), log sweep 20 Hz-10 kHz at -5 dB (5 s). */
export function probes(rate) {
  const n5 = Math.round(rate * 5), ramp = new Float64Array(n5), sweep = new Float64Array(n5), r = rng(3);
  for (let i = 0; i < n5; i++) {
    const t = i / rate;
    ramp[i] = Math.sin(2 * Math.PI * 300 * t) * 10 ** ((-90 + 90 * t / 5) / 20);
    sweep[i] = 0.5623 * Math.sin(2 * Math.PI * 20 * 5 * ((10000 / 20) ** (t / 5) - 1) / Math.log(10000 / 20));
  }
  const noise = new Float64Array(Math.round(rate * 15)), sd = 10 ** (-70 / 20);
  for (let i = 0; i < noise.length; i++) noise[i] = r.normal(0, sd);
  return { ramp, noise, sweep };
}
/** distill.guitarish: Karplus-Strong plucks/chords, sweeps and noise bursts at -30..0 dB. */
export function guitarish(seconds, rate, seed = 1) {
  const r = rng(seed), n = Math.round(seconds * rate), x = new Float64Array(n);
  let t = 0;
  while (t < n) {
    const kind = r.random(), dur = Math.floor(rate * r.uniform(0.25, 1.6)); let seg = new Float64Array(dur);
    if (kind < 0.6) {
      const notes = r.integers(1, 4);
      for (let q = 0; q < notes; q++) {
        const f = 82.4 * 2 ** (r.integers(0, 30) / 12), amp = r.uniform(0.3, 1.0);
        const damp = r.random() > 0.25 ? r.uniform(0.990, 0.9995) : r.uniform(0.95, 0.985);
        const y = pluck(rate, f, dur, amp, damp, r); for (let i = 0; i < dur; i++) seg[i] += y[i] / notes;
      }
    } else if (kind < 0.8) {
      const f0 = r.uniform(40, 200), f1 = r.uniform(800, 9000), T = dur / rate;
      for (let i = 0; i < dur; i++) { const tt = i / rate, ph = 2 * Math.PI * f0 * T * ((f1 / f0) ** (tt / T) - 1) / Math.log(f1 / f0); seg[i] = Math.sin(ph) * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / (dur - 1))); }
    } else {
      const tau = rate * r.uniform(0.05, 0.5); for (let i = 0; i < dur; i++) seg[i] = r.normal(0, 0.3) * Math.exp(-i / tau);
    }
    const level = 10 ** (r.uniform(-30, 0) / 20) * 0.7, m = Math.min(dur, n - t);
    for (let i = 0; i < m; i++) x[t + i] += seg[i] * level;
    t += Math.floor(dur * r.uniform(0.6, 1.1));
  }
  return clip(x);
}
/** distill2.heldout clips used for the quality report (attacks, recovery, sustained). */
export function heldout(rate, seed = 9) {
  const r = rng(seed), cat = {}, cat_ = (parts) => { const n = parts.reduce((s, p) => s + p.length, 0), o = new Float64Array(n); let k = 0; for (const p of parts) { o.set(p, k); k += p.length; } return o; };
  const sumN = (arrs, div) => { const o = new Float64Array(arrs[0].length); for (const a of arrs) for (let i = 0; i < o.length; i++) o[i] += a[i] / div; return o; };
  let x = [];
  for (const lv of [-30, -20, -12, -6, -2]) for (let q = 0; q < 3; q++) { x.push(pluck(rate, 82.4 * 2 ** (r.integers(0, 24) / 12), Math.floor(rate * 0.6), 0.7 * 10 ** (lv / 20), 0.996, r)); x.push(new Float64Array(Math.floor(rate * 0.25))); }
  cat.attacks = cat_(x); x = [];
  for (let q = 0; q < 3; q++) {
    const burst = sumN([0, 7, 12, 16].map(nn => pluck(rate, 82.4 * 2 ** (nn / 12), Math.floor(rate * 0.8), 0.9, 0.999, r)), 2);
    const quiet = cat_([0, 1, 2, 3].map(() => pluck(rate, 196 * 2 ** (r.integers(0, 12) / 12), Math.floor(rate * 0.3), 0.03, 0.995, r)));
    x.push(burst, quiet);
  }
  cat.recovery = cat_(x); x = [];
  for (const lv of [-18, -6]) for (const root of [0, 5, 7]) x.push(sumN([0, 7, 12, 16, 19].map(nn => pluck(rate, 82.4 * 2 ** ((root + nn) / 12), Math.floor(rate * 2.0), 0.7 * 10 ** (lv / 20), 0.9997, r)), 3));
  cat.sustained = cat_(x);
  for (const k in cat) clip(cat[k]);
  return cat;
}
