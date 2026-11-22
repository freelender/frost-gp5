// Signal-processing primitives for the browser FCM converter (plain JS, no
// dependencies; runs in browsers and Node). Float64 throughout. Each routine
// mirrors the scipy/numpy call used by build/nam-distill (named per function).

// ---------- FFT (iterative radix-2, in place on separate re/im arrays) ----------
const twiddleCache = new Map();
function twiddles(n) {
  let t = twiddleCache.get(n);
  if (!t) {
    t = { cos: new Float64Array(n / 2), sin: new Float64Array(n / 2) };
    for (let i = 0; i < n / 2; i++) { t.cos[i] = Math.cos(2 * Math.PI * i / n); t.sin[i] = Math.sin(2 * Math.PI * i / n); }
    twiddleCache.set(n, t);
  }
  return t;
}
export function nextPow2(n) { let p = 1; while (p < n) p <<= 1; return p; }
/** In-place FFT. inverse=true computes the unnormalized inverse (sum with e^{+i}). */
export function fft(re, im, inverse = false) {
  const n = re.length; if (n & (n - 1)) throw new Error('fft size must be a power of two');
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
    if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  const tw = twiddles(n), sg = inverse ? 1 : -1;
  for (let len = 2; len <= n; len <<= 1) {
    const half = len >> 1, step = n / len;
    for (let i = 0; i < n; i += len) {
      for (let k = 0; k < half; k++) {
        const wr = tw.cos[k * step], wi = sg * tw.sin[k * step];
        const a = i + k, b = a + half;
        const xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
      }
    }
  }
}
/** numpy.fft.rfft(x, n) magnitude helpers: returns {re, im} of length n/2+1. */
export function rfft(x, n) {
  const re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < Math.min(n, x.length); i++) re[i] = x[i];
  fft(re, im);
  return { re: re.subarray(0, n / 2 + 1), im: im.subarray(0, n / 2 + 1) };
}

// ---------- convolution ----------
/** Causal FIR (scipy.signal.lfilter(h, 1, x)), output length = x.length. FFT overlap-add for long inputs. */
export function firFilter(h, x) {
  const n = x.length, m = h.length, y = new Float64Array(n);
  if (m <= 64 || n < 4096) {
    for (let t = 0; t < n; t++) { let s = 0; const kmax = Math.min(m - 1, t); for (let k = 0; k <= kmax; k++) s += h[k] * x[t - k]; y[t] = s; }
    return y;
  }
  const L = nextPow2(Math.max(4 * m, 8192)), B = L - m + 1;
  const hr = new Float64Array(L), hi = new Float64Array(L); hr.set(h); fft(hr, hi);
  const r = new Float64Array(L), i_ = new Float64Array(L);
  for (let s0 = 0; s0 < n; s0 += B) {
    r.fill(0); i_.fill(0); const cnt = Math.min(B, n - s0);
    for (let k = 0; k < cnt; k++) r[k] = x[s0 + k];
    fft(r, i_);
    for (let k = 0; k < L; k++) { const a = r[k] * hr[k] - i_[k] * hi[k], b = r[k] * hi[k] + i_[k] * hr[k]; r[k] = a; i_[k] = b; }
    fft(r, i_, true);
    const lim = Math.min(L, n - s0);
    for (let k = 0; k < lim; k++) y[s0 + k] += r[k] / L;
  }
  return y;
}
/** Correlation used by backprop of a causal FIR y=h*x: returns g_x[t]=sum_k h[k] g_y[t+k]. */
export function firAdjointInput(h, gy) {
  const n = gy.length, m = h.length, gx = new Float64Array(n);
  for (let t = 0; t < n; t++) { let s = 0; const kmax = Math.min(m - 1, n - 1 - t); for (let k = 0; k <= kmax; k++) s += h[k] * gy[t + k]; gx[t] = s; }
  return gx;
}
/** Gradient of a causal FIR's taps: g_h[k] = sum_t g_y[t] x[t-k], accumulated into gh. */
export function firAdjointTaps(x, gy, gh) {
  const n = gy.length, m = gh.length;
  for (let k = 0; k < m; k++) { let s = 0; for (let t = k; t < n; t++) s += gy[t] * x[t - k]; gh[k] += s; }
}
/** scipy.signal.lfilter(b, a, x) for a short IIR (a[0] normalized). */
export function lfilter(b, a, x) {
  const n = x.length, y = new Float64Array(n), a0 = a[0], nb = b.length, na = a.length;
  for (let t = 0; t < n; t++) {
    let s = 0; for (let k = 0; k < nb && k <= t; k++) s += b[k] * x[t - k];
    for (let k = 1; k < na && k <= t; k++) s -= a[k] * y[t - k];
    y[t] = s / a0;
  }
  return y;
}
/** scipy.signal.sosfilt for rows [b0,b1,b2,1,a1,a2] (direct form II transposed). */
export function sosfilt(sos, x) {
  let y = Float64Array.from(x);
  for (const [b0, b1, b2, , a1, a2] of sos) {
    let z1 = 0, z2 = 0; const o = new Float64Array(y.length);
    for (let t = 0; t < y.length; t++) { const v = y[t], w = b0 * v + z1; z1 = b1 * v - a1 * w + z2; z2 = b2 * v - a2 * w; o[t] = w; }
    y = o;
  }
  return y;
}

// ---------- filter design ----------
function sinc(x) { if (x === 0) return 1; const p = Math.PI * x; return Math.sin(p) / p; }
function besselI0(x) { let s = 1, t = 1; for (let k = 1; k < 60; k++) { t *= (x / (2 * k)) ** 2; s += t; if (t < 1e-17 * s) break; } return s; }
/** scipy.signal.firwin(numtaps, cutoff) lowpass, cutoff relative to Nyquist, window 'hamming' or {kaiser: beta}. */
export function firwin(numtaps, cutoff, window = 'hamming') {
  const h = new Float64Array(numtaps), alpha = (numtaps - 1) / 2;
  for (let i = 0; i < numtaps; i++) {
    const m = i - alpha; let w;
    if (window === 'hamming') w = 0.54 - 0.46 * Math.cos(2 * Math.PI * i / (numtaps - 1));
    else { const beta = window.kaiser, r = 2 * i / (numtaps - 1) - 1; w = besselI0(beta * Math.sqrt(Math.max(0, 1 - r * r))) / besselI0(beta); }
    h[i] = cutoff * sinc(cutoff * m) * w;
  }
  let s = 0; for (const v of h) s += v; for (let i = 0; i < numtaps; i++) h[i] /= s;
  return h;
}
/** scipy.signal.butter(2, fc/(fs/2), 'highpass') as {b, a}. */
export function butterHighpass2(fc, fs) {
  const K = Math.tan(Math.PI * fc / fs), r2 = Math.SQRT2, n = 1 / (1 + r2 * K + K * K);
  return { b: [n, -2 * n, n], a: [1, 2 * (K * K - 1) * n, (1 - r2 * K + K * K) * n] };
}
function gcd(a, b) { while (b) [a, b] = [b, a % b]; return a; }
const resampleCache = new Map();
/** scipy.signal.resample_poly(x, up, down) with its default Kaiser(5) window. */
export function resamplePoly(x, up, down) {
  const g = gcd(up, down); up /= g; down /= g;
  if (up === 1 && down === 1) return Float64Array.from(x);
  const key = up + '/' + down; let h = resampleCache.get(key);
  const maxRate = Math.max(up, down), halfLen = 10 * maxRate;
  if (!h) { h = firwin(2 * halfLen + 1, 1 / maxRate, { kaiser: 5.0 }); for (let i = 0; i < h.length; i++) h[i] *= up; resampleCache.set(key, h); }
  const nPrePad = (down - halfLen % down) % down, nPreRemove = Math.floor((halfLen + nPrePad) / down);
  const hp = new Float64Array(nPrePad + h.length); hp.set(h, nPrePad);
  const nOut = Math.ceil(x.length * up / down), y = new Float64Array(nOut), L = hp.length;
  for (let m = 0; m < nOut; m++) {
    const p = (m + nPreRemove) * down;                 // index in the upsampled, filtered stream
    let nHi = Math.floor(p / up); if (nHi > x.length - 1) nHi = x.length - 1;
    let s = 0;
    for (let n = nHi; n >= 0; n--) { const k = p - n * up; if (k >= L) break; s += hp[k] * x[n]; }
    y[m] = s;
  }
  return y;
}

// ---------- spectra ----------
/** |Pab|/(Paa+eps) like sbfit.transfer: scipy csd/welch, Hann (periodic), nperseg nf, 75% overlap, constant detrend. */
export function transfer(a, b, nf = 4096) {
  const hop = nf / 4, win = new Float64Array(nf);
  for (let i = 0; i < nf; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / nf);
  const nb = nf / 2 + 1, Pr = new Float64Array(nb), Pi = new Float64Array(nb), Paa = new Float64Array(nb);
  const ar = new Float64Array(nf), ai = new Float64Array(nf), br = new Float64Array(nf), bi = new Float64Array(nf);
  let segs = 0;
  for (let s = 0; s + nf <= a.length; s += hop) {
    let ma = 0, mb = 0; for (let i = 0; i < nf; i++) { ma += a[s + i]; mb += b[s + i]; } ma /= nf; mb /= nf;
    for (let i = 0; i < nf; i++) { ar[i] = (a[s + i] - ma) * win[i]; br[i] = (b[s + i] - mb) * win[i]; ai[i] = 0; bi[i] = 0; }
    fft(ar, ai); fft(br, bi);
    for (let k = 0; k < nb; k++) { Pr[k] += ar[k] * br[k] + ai[k] * bi[k]; Pi[k] += ar[k] * bi[k] - ai[k] * br[k]; Paa[k] += ar[k] * ar[k] + ai[k] * ai[k]; }
    segs++;
  }
  let mx = 0; for (const v of Paa) if (v > mx) mx = v;
  const out = new Float64Array(nb);
  for (let k = 0; k < nb; k++) out[k] = Math.hypot(Pr[k], Pi[k]) / (Paa[k] + 1e-20 * mx + 1e-30 * segs);
  return out;
}
