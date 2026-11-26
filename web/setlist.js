import { crc32 } from './pedal.js?v=1.9.0';
export const SETLIST_BYTES = 3968;
const enc = new TextEncoder(), dec = new TextDecoder();
export const emptySetlist = () => ({ format: 'frost-setlist', version: 1, name: 'My setlist', songs: [], order: [] });
function nameBytes(name) {
  if (typeof name !== 'string' || !name.trim() || name.length > 16 || /[^\x20-\x7e]/.test(name)) throw new Error('Names must have 1–16 printable English characters.');
  const b = new Uint8Array(16); b.set(enc.encode(name)); return b;
}
export function encodeSetlist(data) {
  if (data?.format !== 'frost-setlist' || data.version !== 1 || !Array.isArray(data.songs) || !Array.isArray(data.order) || data.songs.length > 64 || data.order.length > 64) throw new Error('Invalid Frost setlist.');
  const b = new Uint8Array(SETLIST_BYTES), ids = new Set(); b.set([70, 83, 69, 84, 1, data.songs.length, data.order.length]); b.set(nameBytes(data.name), 8);
  let pos = 96;
  for (const song of data.songs) {
    if (!Number.isInteger(song.id) || song.id < 0 || song.id >= 64 || ids.has(song.id) || !Array.isArray(song.parts) || !song.parts.length || song.parts.length > 16) throw new Error('Each song needs a unique ID and 1–16 parts.');
    ids.add(song.id); const n = 18 + 17 * song.parts.length;
    if (pos + n > b.length - 4) throw new Error('Song storage is full. Remove a song or some parts.');
    b[pos] = song.id; b[pos + 1] = song.parts.length; b.set(nameBytes(song.name), pos + 2);
    song.parts.forEach((part, i) => { if (!Number.isInteger(part.preset) || part.preset < 0 || part.preset >= 60) throw new Error('Choose a preset from 01–60.'); b.set(nameBytes(part.name), pos + 18 + 17 * i); b[pos + 34 + 17 * i] = part.preset; });
    pos += n;
  }
  data.order.forEach((id, i) => { if (!Number.isInteger(id) || !ids.has(id)) throw new Error('Setlist contains an unknown song.'); b[32 + i] = id; });
  new DataView(b.buffer).setUint32(b.length - 4, crc32(b.subarray(0, -4)), true);
  return { bytes: b, used: pos, capacity: b.length - 4 };
}
export function decodeSetlist(b) {
  if (!(b instanceof Uint8Array) || b.length !== SETLIST_BYTES) throw new Error('Invalid setlist size.');
  if (b.every(v => v === 0)) return emptySetlist();
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (v.getUint32(0, true) !== 0x54455346 || b[4] !== 1 || b[5] > 64 || b[6] > 64 || v.getUint32(b.length - 4, true) !== crc32(b.subarray(0, -4))) throw new Error('Damaged or unsupported setlist.');
  const name = p => { let end = p; while (end < p + 16 && b[end]) end++; return dec.decode(b.subarray(p, end)); };
  const data = { format: 'frost-setlist', version: 1, name: name(8), songs: [], order: Array.from(b.subarray(32, 32 + b[6])) }; let pos = 96;
  for (let i = 0; i < b[5]; i++) {
    if (pos + 18 > b.length - 4 || b[pos + 1] < 1 || b[pos + 1] > 16 || pos + 18 + b[pos + 1] * 17 > b.length - 4) throw new Error('Invalid song record.');
    const song = { id: b[pos], name: name(pos + 2), parts: [] };
    for (let j = 0; j < b[pos + 1]; j++) song.parts.push({ name: name(pos + 18 + 17 * j), preset: b[pos + 34 + 17 * j] });
    data.songs.push(song); pos += 18 + 17 * song.parts.length;
  }
  encodeSetlist(data); return data;
}
// The setlist shares the glb-0 group with the global settings (its first 64
// bytes). The pedal rewrites that group on its own, e.g. on every preset save
// (last preset) or settings change. A save therefore keeps the pedal's current
// settings and refuses only when the SETLIST part changed since it was read
// (another browser saved a different setlist).
const sameBytes = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
export class SetlistStore {
  constructor(store) { this.store = store; this.seq = undefined; this.base = undefined; }
  async read() {
    const files = await this.store.scan(), h = this.store.newest(files, 0x424c4746, 'glb-0');
    const group = await this.store.readGroup('glb', 0, files);
    if (h && !group) throw new Error('Global settings group is damaged; save was disabled.');
    this.seq = h?.seq ?? null;
    this.base = group ? Uint8Array.from(group.subarray(64)) : new Uint8Array(SETLIST_BYTES);
    return group ? decodeSetlist(group.subarray(64)) : emptySetlist();
  }
  /** Remove the songs and setlist stored on the pedal; global settings are kept. */
  async clear(say) { return this.writeBytes(new Uint8Array(SETLIST_BYTES), say); }
  async write(data, say) { return this.writeBytes(encodeSetlist(data).bytes, say); }
  async writeBytes(bytes, say) {
    if (this.base === undefined) throw new Error('Read the setlist from the pedal first.');
    const files = await this.store.scan();
    const h = this.store.newest(files, 0x424c4746, 'glb-0'), seq = h?.seq ?? null;
    const old = await this.store.readGroup('glb', 0, files);
    if (h && !old) throw new Error('Global settings group is damaged; save was disabled.');
    const current = old ? old.subarray(64) : new Uint8Array(SETLIST_BYTES);
    if (!sameBytes(current, this.base)) throw new Error('The setlist on the pedal was changed elsewhere since it was read. Read from pedal and retry.');
    const group = old ? Uint8Array.from(old) : new Uint8Array(4032);
    if (!old) { group.set([70, 71, 1, 0, 0, 100, 0, 0, 100, 0, 255, 40, 2]); new DataView(group.buffer).setUint32(60, crc32(group.subarray(0, 60)), true); }
    group.set(bytes, 64); await this.store.writeGroup('glb', 0, group, say, seq);
    this.seq = undefined; this.base = Uint8Array.from(bytes);   // what the pedal now holds
  }
}
