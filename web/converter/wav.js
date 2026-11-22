// Minimal WAV codec (PCM16 / float32 mono), for Node validation and downloads.
export function readWav(bytes) {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength); let i = 12, fmt = 0, bits = 0, rate = 0, chans = 1;
  while (i + 8 <= bytes.length) {
    const id = String.fromCharCode(...bytes.subarray(i, i + 4)), n = v.getUint32(i + 4, true);
    if (id === 'fmt ') { fmt = v.getUint16(i + 8, true); chans = v.getUint16(i + 10, true); rate = v.getUint32(i + 12, true); bits = v.getUint16(i + 22, true); }
    if (id === 'data') {
      const cnt = n / (bits / 8) / chans, out = new Float64Array(cnt);
      for (let k = 0; k < cnt; k++) {
        const o = i + 8 + k * chans * (bits / 8);
        out[k] = fmt === 3 && bits === 32 ? v.getFloat32(o, true) : fmt === 1 && bits === 16 ? v.getInt16(o, true) / 32768 : fmt === 1 && bits === 24 ? (((v.getUint8(o) | v.getUint8(o + 1) << 8 | v.getInt8(o + 2) << 16)) / 8388608) : NaN;
      }
      if (Number.isNaN(out[0])) throw new Error(`unsupported WAV format ${fmt}/${bits}`);
      return { samples: out, rate };
    }
    i += 8 + n + (n & 1);
  }
  throw new Error('no WAV data chunk');
}
export function writeWavFloat(x, rate) {
  const n = x.length, b = new Uint8Array(44 + 4 * n), v = new DataView(b.buffer);
  const s = (o, t) => { for (let k = 0; k < 4; k++) b[o + k] = t.charCodeAt(k); };
  s(0, 'RIFF'); v.setUint32(4, 36 + 4 * n, true); s(8, 'WAVE'); s(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 3, true); v.setUint16(22, 1, true);
  v.setUint32(24, rate, true); v.setUint32(28, rate * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 32, true); s(36, 'data'); v.setUint32(40, 4 * n, true);
  for (let k = 0; k < n; k++) v.setFloat32(44 + 4 * k, x[k], true);
  return b;
}
