// NAM A2 / Lite WaveNet inference in plain JS (Float64): one layer array,
// LeakyReLU, layer1x1 residual, activation-sum head (kernel head_k), head_scale.
// Weight order as NAM Core loads it (port of build/nam-distill/namtorch.py,
// which matches NAM Core to ~1e-13 ESR). A SlimmableContainer picks the FULL
// submodel by default (slim 1.0): the converter's teacher.

export class NamError extends Error {}

/** Parse a .nam JSON object. Returns {net, sampleRate, info}. */
export function loadNam(json, slim = 1.0) {
  let d = json, info = {};
  if (d.architecture === 'SlimmableContainer') {
    const subs = d.config.submodels;
    const pick = subs.find(s => slim < s.max_value) || subs[subs.length - 1];
    info.container = { submodels: subs.length, picked_max_value: pick.max_value };
    d = pick.model;
  }
  if (d.architecture && d.architecture !== 'WaveNet') throw new NamError(`unsupported architecture ${d.architecture} (A2 WaveNet captures only)`);
  const layers = d.config && d.config.layers;
  if (!layers || layers.length !== 1) throw new NamError('unsupported WaveNet layout (one layer array expected: NAM A2)');
  const L = layers[0];
  const acts = L.activation;
  const actList = Array.isArray(acts) ? acts : [acts];
  for (const a of actList) if ((typeof a === 'string' ? a : a.type) !== 'LeakyReLU') throw new NamError('unsupported activation (A2 LeakyReLU expected; A1 captures cannot be converted)');
  const ch = L.channels;
  if ((L.bottleneck ?? ch) !== ch) throw new NamError('unsupported bottleneck');
  const ks = L.kernel_sizes, dil = L.dilations;
  if (!Array.isArray(ks) || ks.length !== dil.length) throw new NamError('unsupported kernel/dilation layout');
  const hk = typeof L.head === 'object' && L.head ? L.head.kernel_size : 1;
  const slope = (typeof actList[0] === 'object' && actList[0].negative_slope != null) ? actList[0].negative_slope : 0.01;
  const w = d.weights; let p = 0; const take = () => { if (p >= w.length) throw new NamError('too few weights'); return w[p++]; };
  const net = { ch, ks, dil, hk, slope, rechannel: new Float64Array(ch), layers: [], head: new Float64Array(ch * hk), headBias: 0, scale: 1 };
  for (let i = 0; i < ch; i++) net.rechannel[i] = take();
  for (let l = 0; l < ks.length; l++) {
    const k = ks[l], conv = new Float64Array(ch * ch * k), bias = new Float64Array(ch), mix = new Float64Array(ch), l1 = new Float64Array(ch * ch), l1b = new Float64Array(ch);
    for (let i = 0; i < ch; i++) for (let j = 0; j < ch; j++) for (let t = 0; t < k; t++) conv[(i * ch + j) * k + t] = take();
    for (let i = 0; i < ch; i++) bias[i] = take();
    for (let i = 0; i < ch; i++) mix[i] = take();
    for (let i = 0; i < ch; i++) for (let j = 0; j < ch; j++) l1[i * ch + j] = take();
    for (let i = 0; i < ch; i++) l1b[i] = take();
    net.layers.push({ k, d: dil[l], conv, bias, mix, l1, l1b });
  }
  for (let j = 0; j < ch; j++) for (let t = 0; t < hk; t++) net.head[j * hk + t] = take();
  net.headBias = take(); net.scale = take();
  if (p !== w.length) throw new NamError(`trailing weights (${w.length - p})`);
  info.channels = ch; info.layers = ks.length; info.weights = w.length;
  info.receptiveField = 1 + ks.reduce((s, k, i) => s + (k - 1) * dil[i], 0) + hk - 1;
  return { net, sampleRate: Number(d.sample_rate || json.sample_rate || 48000), info };
}

/** Render x (Float64Array at the model's rate) through the WaveNet. Like NAM Core,
 * the model is first pre-warmed with receptive-field zeros (NAM/wavenet/a2_fast.cpp). */
export function receptiveField(net) { return 1 + net.layers.reduce((s, L) => s + (L.k - 1) * L.d, 0) + net.hk - 1; }
export function renderNam(net, x, onProgress, prewarm = true) {
  const { ch, hk, slope, layers } = net, n = x.length;
  // Per layer: a ring history of its input h (ch x (k-1)*d+1); head: ring of the summed activations (ch x hk).
  const hist = layers.map(L => { const len = (L.k - 1) * L.d + 1; return { len, buf: new Float64Array(len * ch), pos: 0 }; });
  const headLen = hk, headBuf = new Float64Array(headLen * ch); let headPos = 0;
  const y = new Float64Array(n), h = new Float64Array(ch), z = new Float64Array(ch), a = new Float64Array(ch), s = new Float64Array(ch);
  const warm = prewarm ? receptiveField(net) : 0;
  for (let t = -warm; t < n; t++) {
    const xt = t < 0 ? 0 : x[t];
    for (let i = 0; i < ch; i++) { h[i] = net.rechannel[i] * xt; s[i] = 0; }
    for (let l = 0; l < layers.length; l++) {
      const L = layers[l], H = hist[l], k = L.k, d = L.d, len = H.len, buf = H.buf;
      // store the current h at pos
      const base = H.pos * ch; for (let i = 0; i < ch; i++) buf[base + i] = h[i];
      for (let i = 0; i < ch; i++) z[i] = L.bias[i] + L.mix[i] * xt;
      for (let tap = 0; tap < k; tap++) {
        // tap index 'tap' multiplies h[t - (k-1-tap)*d]
        let q = H.pos - (k - 1 - tap) * d; if (q < 0) q += len;
        const hb = q * ch;
        for (let i = 0; i < ch; i++) { let acc = 0; const row = (i * ch) * k + tap; for (let j = 0; j < ch; j++) acc += L.conv[row + j * k] * buf[hb + j]; z[i] += acc; }
      }
      H.pos = H.pos + 1 === len ? 0 : H.pos + 1;
      for (let i = 0; i < ch; i++) { const v = z[i]; a[i] = v > 0 ? v : v * slope; s[i] += a[i]; }
      for (let i = 0; i < ch; i++) { let acc = L.l1b[i]; const row = i * ch; for (let j = 0; j < ch; j++) acc += L.l1[row + j] * a[j]; h[i] += acc; }
    }
    const hb = headPos * ch; for (let i = 0; i < ch; i++) headBuf[hb + i] = s[i];
    let out = net.headBias;
    for (let tap = 0; tap < hk; tap++) {
      let q = headPos - (hk - 1 - tap); if (q < 0) q += headLen;
      const qb = q * ch; for (let j = 0; j < ch; j++) out += net.head[j * hk + tap] * headBuf[qb + j];
    }
    headPos = headPos + 1 === headLen ? 0 : headPos + 1;
    if (t >= 0) y[t] = net.scale * out;
    if (onProgress && t >= 0 && (t & 65535) === 0) onProgress(t / n);
  }
  return y;
}

/** Same result as renderNam, computed layer by layer over blocks of samples
 * (contiguous inner loops; several times faster in JS engines). */
export function renderNamBlocks(net, x, onProgress, prewarm = true, C = 2048) {
  const { ch, hk, slope, layers } = net, warm = prewarm ? receptiveField(net) : 0, total = warm + x.length;
  const lb = layers.map(L => (L.k - 1) * L.d);
  // per layer: per channel input buffers with lb history ahead of the block
  const hin = layers.map((L, l) => Array.from({ length: ch }, () => new Float64Array(lb[l] + C)));
  const W = layers.map(L => Array.from({ length: L.k }, (_, tap) => { const m = new Float64Array(ch * ch); for (let i = 0; i < ch; i++) for (let j = 0; j < ch; j++) m[i * ch + j] = L.conv[(i * ch + j) * L.k + tap]; return m; }));
  const sh = Array.from({ length: ch }, () => new Float64Array(hk - 1 + C)), z = Array.from({ length: ch }, () => new Float64Array(C)), a = Array.from({ length: ch }, () => new Float64Array(C));
  const xb = new Float64Array(C), y = new Float64Array(x.length);
  for (let s0 = 0; s0 < total; s0 += C) {
    const n = Math.min(C, total - s0);
    for (let t = 0; t < n; t++) { const g = s0 + t - warm; xb[t] = g >= 0 ? x[g] : 0; }
    for (let i = 0; i < ch; i++) { const r = net.rechannel[i], b = hin[0][i], o = lb[0]; for (let t = 0; t < n; t++) b[o + t] = r * xb[t]; sh[i].fill(0, hk - 1, hk - 1 + n); }
    for (let l = 0; l < layers.length; l++) {
      const L = layers[l], k = L.k, d = L.d, o = lb[l], H = hin[l];
      for (let i = 0; i < ch; i++) {
        const zi = z[i], bi = L.bias[i], mi = L.mix[i];
        for (let t = 0; t < n; t++) zi[t] = bi + mi * xb[t];
        for (let tap = 0; tap < k; tap++) {
          const wm = W[l][tap], off = o - (k - 1 - tap) * d;
          for (let j = 0; j < ch; j++) { const wv = wm[i * ch + j], hj = H[j]; if (wv === 0) continue; for (let t = 0; t < n; t++) zi[t] += wv * hj[off + t]; }
        }
      }
      for (let i = 0; i < ch; i++) { const zi = z[i], ai = a[i], si = sh[i], so = hk - 1; for (let t = 0; t < n; t++) { const v = zi[t]; const q = v > 0 ? v : v * slope; ai[t] = q; si[so + t] += q; } }
      const next = l + 1 < layers.length ? hin[l + 1] : null, no = next ? lb[l + 1] : 0;
      for (let i = 0; i < ch; i++) {
        const out = next ? next[i] : null, hi = H[i], b = L.l1b[i];
        if (!out) continue;
        for (let t = 0; t < n; t++) out[no + t] = hi[o + t] + b;
        for (let j = 0; j < ch; j++) { const wv = L.l1[i * ch + j], aj = a[j]; for (let t = 0; t < n; t++) out[no + t] += wv * aj[t]; }
      }
      // keep this layer's input history for the next block
      if (o) for (let i = 0; i < ch; i++) H[i].copyWithin(0, n, n + o);
    }
    for (let t = 0; t < n; t++) {
      const g = s0 + t - warm; if (g < 0) continue;
      let out = net.headBias;
      for (let j = 0; j < ch; j++) { const sj = sh[j]; for (let tap = 0; tap < hk; tap++) out += net.head[j * hk + tap] * sj[t + tap]; }
      y[g] = net.scale * out;
    }
    if (hk > 1) for (let i = 0; i < ch; i++) sh[i].copyWithin(0, n, n + hk - 1);
    if (onProgress) onProgress(Math.max(0, s0 + n - warm) / x.length);
  }
  return y;
}
