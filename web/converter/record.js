// FCM record (400 bytes) exactly as tools/fcm_bank.py pack_record writes it:
// name[16] | n_pre n_mid n_post (u32) | pre/mid/post 4 x [b0 b1 b2 a1 a2] (f32) |
// gain Ap0 Ap1 An0 An1 Kp0 Kp1 Kn0 Kn1 | env_a env_k0 env_k1 | aa[20] | crc32(zlib) of the first 396 bytes.
export const RECORD_BYTES = 400, SOS = 4, AA_TAPS = 20;
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
export function crc32(bytes) { let c = 0xffffffff; for (const b of bytes) c = CRC[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
/** model: {pre, mid, post: [[b0,b1,b2,1,a1,a2],...], g, A:[[Ap,An],[Ap,An]], K:[[Kp,Kn],[Kp,Kn]], ea, ek:[k0,k1], aa: 20 taps} */
export function packRecord(name, m) {
  const out = new Uint8Array(RECORD_BYTES), v = new DataView(out.buffer);
  const raw = new TextEncoder().encode(name.replace(/[^\x20-\x7e]/g, '?')).subarray(0, 15); out.set(raw, 0);
  let o = 16;
  const u32 = x => { v.setUint32(o, x, true); o += 4; }, f32 = x => { if (!Number.isFinite(x)) throw new Error('non-finite model value'); v.setFloat32(o, x, true); o += 4; };
  for (const k of ['pre', 'mid', 'post']) { if (m[k].length > SOS) throw new Error(`${k}: at most ${SOS} sections`); u32(m[k].length); }
  for (const k of ['pre', 'mid', 'post']) for (let s = 0; s < SOS; s++) { const r = m[k][s]; if (r && Math.abs(r[3] - 1) > 1e-12) throw new Error('sections must have a0 = 1'); for (const x of r ? [r[0], r[1], r[2], r[4], r[5]] : [0, 0, 0, 0, 0]) f32(x); }
  [m.g, m.A[0][0], m.A[1][0], m.A[0][1], m.A[1][1], m.K[0][0], m.K[1][0], m.K[0][1], m.K[1][1], m.ea, m.ek[0], m.ek[1]].forEach(f32);
  if (m.aa.length !== AA_TAPS) throw new Error('aa must have 20 taps'); m.aa.forEach(f32);
  if (o !== RECORD_BYTES - 4) throw new Error('record layout');
  v.setUint32(o, crc32(out.subarray(0, o)), true);
  return out;
}
