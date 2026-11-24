// Stage 2 storage (docs/STORAGE_V1.md): one-sector GROUP files in the pedal's
// controller flash area A, rewritten copy-on-write exactly like the firmware
// (firmware/controller/preset_store.h): new copy into a free sector, data,
// read back, header last, then EVERY older copy erased.
//   AMP models: firmware FFCM, amp-0..amp-6, 10 x 400-byte records (MODEL 0..63)
//   presets:    firmware FPST, pst-0..pst-3, 15 x 264-byte records (1..60)
// Legacy single amp-NN files (Frost 31s/31t3) are migrated into the groups.
import { FILES, crc32, fileHeader, parseHeader, recordLabel, checkRecord } from './pedal.js?v=1.7.8';

export const GROUPS = {
  amp: { firm: 0x4d434646, prefix: 'amp-', files: 7, slots: 10, size: 400 },
  ir: { firm: 0x42524946, prefix: 'ir-', files: 21, slots: 3, size: 1048, digits: 2 },
  pst: { firm: 0x54535046, prefix: 'pst-', files: 4, slots: 15, size: 264 },
  glb: { firm: 0x424c4746, prefix: 'glb-', files: 1, slots: 1, size: 4032 },
};
export const PRESETS = 60;
function groupDefinition(kind) {
  const group = GROUPS[kind];
  if (!group) throw new Error(`Unknown storage group "${kind}". Reload the website with Ctrl+Shift+R to update its files.`);
  return group;
}
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const slotOk = (b, size) => b.length === size && crc32(b.subarray(0, size - 4)) === u32(b, size - 4);
const isPreset = r => r[0] === 0x46 && r[1] === 0x50 && r[2] === 1 && slotOk(r, 264);
export function presetName(rec) { let e = 4; while (e < 20 && rec[e]) e++; return new TextDecoder().decode(rec.subarray(4, e)); }
export function checkPreset(rec) { if (!(rec instanceof Uint8Array) || rec.length !== 264 || !isPreset(rec)) throw new Error('not a Frost preset (v1)'); }
export function renamePreset(rec, name) {
  checkPreset(rec);
  const r = Uint8Array.from(rec), n = new TextEncoder().encode(name.replace(/[^\x20-\x7e]/g, '?')).subarray(0, 16);
  r.fill(0, 4, 20); r.set(n, 4); new DataView(r.buffer).setUint32(260, crc32(r.subarray(0, 260)), true);
  return r;
}
const b64enc = b => { let s = ''; for (const x of b) s += String.fromCharCode(x); return btoa(s); };
const b64dec = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));

export class Store {
  /** files: a PedalFiles (pedal.js). */
  constructor(files) { this.f = files; }
  scan(onProgress) { return this.f.files(onProgress); }
  newest(files, firm, name) { let best = null; for (const h of files) if (h.firm === firm && h.name === name && (!best || h.seq > best.seq)) best = h; return best; }
  async readGroup(kind, g, files) {
    const G = groupDefinition(kind), h = this.newest(files || await this.scan(), G.firm, G.prefix + String(g).padStart(G.digits || 1, '0')), bytes = G.slots * G.size;
    if (!h || (h.length !== bytes && !(kind === 'glb' && h.length === 64))) return null;
    const d = await this.f.read(FILES.BASE + h.sector * FILES.SECTOR + FILES.HEADER, h.length);
    if (crc32(d) !== h.crc) return null;
    if (d.length === bytes) return d;
    const padded = new Uint8Array(bytes); padded.set(d); return padded;
  }
  /** Write data as the new copy of kind/g; null or all-zero data removes the group file. */
  async writeGroup(kind, g, data, say = () => {}, expected) {
    const G = groupDefinition(kind);
    if (kind === 'glb') {
      const r = await this.f.ask([0x7e, 0x1b]);
      if (!r || r.length !== 8 || r[2] !== 1 || r[3] !== 1) throw new Error('Setlists need Frost 31z6 or later. Update the firmware first.');
    }
    // Establish the controller SAVE guard before choosing its spare sector.
    await this.f.changed();
    const name = G.prefix + String(g).padStart(G.digits || 1, '0'), files = await this.scan();
    const others = files.filter(h => h.firm === G.firm && h.name === name);
    const current = this.newest(files, G.firm, name);
    if (expected !== undefined && (current?.seq ?? null) !== expected) throw new Error('This group changed on the pedal. Refresh and retry.');
    if (data && data.some(b => b)) {
      const covered = new Set(); for (const h of files) for (let s = h.sector; s < h.sector + h.sectors; s++) covered.add(s);
      const free = [...Array(FILES.SECTORS).keys()].find(s => !covered.has(s));
      if (free === undefined) throw new Error('Storage full: all 33 places are used.');
      const seq = Math.max(0, ...files.map(h => h.seq)) + 1, addr = FILES.BASE + free * FILES.SECTOR;
      say(`writing ${name}`);
      await this.f.erase(addr); await this.f.program(addr + FILES.HEADER, data);
      const back = await this.f.read(addr + FILES.HEADER, data.length);
      if (back.some((b, i) => b !== data[i])) throw new Error('read-back failed; the old copy is untouched');
      await this.f.program(addr, fileHeader(seq, G.firm, name, data).subarray(0, 60));
      if (!parseHeader(await this.f.read(addr, 64), free)) throw new Error('header read-back failed');
    }
    for (const h of others) await this.f.erase(FILES.BASE + h.sector * FILES.SECTOR);
    await this.f.changed();
  }
  async slot(kind, index, files) {
    const G = groupDefinition(kind), d = await this.readGroup(kind, Math.floor(index / G.slots), files);
    if (!d) return null;
    const r = d.subarray((index % G.slots) * G.size, (index % G.slots + 1) * G.size);
    return slotOk(r, G.size) ? Uint8Array.from(r) : null;
  }
  async putSlot(kind, index, rec, say) {
    const G = groupDefinition(kind), g = Math.floor(index / G.slots), o = (index % G.slots) * G.size;
    const files = await this.scan(), h = this.newest(files, G.firm, G.prefix + String(g).padStart(G.digits || 1, '0'));
    const d = Uint8Array.from((await this.readGroup(kind, g, files)) || new Uint8Array(G.slots * G.size));
    if (rec) d.set(rec, o); else d.fill(0, o, o + G.size);
    await this.writeGroup(kind, g, d, say, h?.seq ?? null);
  }
  // ---- AMP models ----
  /** Move legacy amp-NN single files into the groups (MODEL numbers kept). Returns how many moved. */
  async migrate(say = () => {}) {
    const files = await this.scan(), legacy = files.filter(h => h.firm === GROUPS.amp.firm && /^amp-\d\d$/.test(h.name));
    if (!legacy.length) return 0;
    const best = {}; for (const h of legacy) if (!best[h.name] || h.seq > best[h.name].seq) best[h.name] = h;
    const groups = {};
    for (const h of Object.values(best)) {
      const i = Number(h.name.slice(4)), g = Math.floor(i / 10), rec = await this.f.read(FILES.BASE + h.sector * FILES.SECTOR + FILES.HEADER, 400);
      if (h.length !== 400 || !slotOk(rec, 400)) continue;
      if (!groups[g]) groups[g] = Uint8Array.from((await this.readGroup('amp', g, files)) || new Uint8Array(4000));
      if (!slotOk(groups[g].subarray((i % 10) * 400, (i % 10 + 1) * 400), 400)) groups[g].set(rec, (i % 10) * 400);
    }
    for (const [g, d] of Object.entries(groups)) { say(`moving AMP models into amp-${g}`); await this.writeGroup('amp', Number(g), d); }
    for (const h of legacy) await this.f.erase(FILES.BASE + h.sector * FILES.SECTOR);
    await this.f.changed();
    return Object.keys(best).length;
  }
  async amps(onProgress) {
    const files = await this.scan(onProgress), out = {};
    for (let g = 0; g < GROUPS.amp.files; g++) {
      const d = await this.readGroup('amp', g, files); if (!d) continue;
      for (let s = 0; s < 10; s++) { const r = d.subarray(s * 400, s * 400 + 400); if (slotOk(r, 400)) out[g * 10 + s] = { label: recordLabel(r) }; }
    }
    for (const h of files) if (h.firm === GROUPS.amp.firm && /^amp-\d\d$/.test(h.name)) {
      const i = Number(h.name.slice(4));
      if (!out[i]) out[i] = { label: recordLabel(await this.f.read(FILES.BASE + h.sector * FILES.SECTOR + FILES.HEADER, 16)), legacy: true };
    }
    return { models: out, used: files.reduce((s, h) => s + h.sectors, 0) };
  }
  async ampGet(i) { await this.migrate(); return this.slot('amp', i); }
  async ampPut(i, rec, say) { checkRecord(rec); if (!(i >= 0 && i < 64)) throw new Error('MODEL must be 0..63'); await this.migrate(say); await this.putSlot('amp', i, rec, say); }
  async ampRemove(i) { await this.migrate(); await this.putSlot('amp', i, null); }
  // ---- cabinet IRs (CAB 1..63; storage indices 0..62) ----
  async irs() {
    const files = await this.scan(), out = {};
    for (let g = 0; g < 21; g++) {
      const d = await this.readGroup('ir', g, files); if (!d) continue;
      for (let k = 0; k < 3; k++) { const r = d.subarray(k * 1048, (k + 1) * 1048); if (slotOk(r, 1048)) out[g * 3 + k] = { label: recordLabel(r) }; }
    }
    return out;
  }
  irGet(i) { if (!Number.isInteger(i) || i < 0 || i >= 63) throw new Error('CAB must be 1..63'); return this.slot('ir', i); }
  async irPut(i, rec, say) {
    if (!Number.isInteger(i) || i < 0 || i >= 63) throw new Error('CAB must be 1..63');
    if (rec && !slotOk(rec, 1048)) throw new Error('invalid IR record');
    await this.putSlot('ir', i, rec, say);
  }
  irRemove(i) { return this.irPut(i, null); }
  // ---- presets ----
  async presets(files) {
    files = files || await this.scan(); const out = {};
    for (let g = 0; g < GROUPS.pst.files; g++) {
      const d = await this.readGroup('pst', g, files); if (!d) continue;
      for (let s = 0; s < 15; s++) { const r = d.subarray(s * 264, s * 264 + 264); if (isPreset(r)) out[g * 15 + s] = { name: presetName(r) }; }
    }
    return out;
  }
  presetGet(i) { return this.slot('pst', i); }
  async presetPut(i, rec) { if (rec) checkPreset(rec); if (!(i >= 0 && i < PRESETS)) throw new Error('preset must be 1..60'); await this.putSlot('pst', i, rec); }
  async presetRename(i, name) { const r = await this.presetGet(i); if (!r) throw new Error('empty preset'); await this.presetPut(i, renamePreset(r, name)); }
  presetDelete(i) { return this.presetPut(i, null); }
  /** Swap presets a and b (either may be empty). beta.12: one scan and one read
   * per group file, then one write per group (was three scans and reads first). */
  async presetSwap(a, b) {
    if (a === b) return;
    if (!(a >= 0 && a < PRESETS && b >= 0 && b < PRESETS)) throw new Error('preset must be 1..60');
    const files = await this.scan(), ga = Math.floor(a / 15), gb = Math.floor(b / 15), name = g => 'pst-' + g;
    const seq = g => this.newest(files, GROUPS.pst.firm, name(g))?.seq ?? null;
    const da = Uint8Array.from((await this.readGroup('pst', ga, files)) || new Uint8Array(3960));
    const db = ga === gb ? da : Uint8Array.from((await this.readGroup('pst', gb, files)) || new Uint8Array(3960));
    const at = (d, i) => d.subarray((i % 15) * 264, (i % 15 + 1) * 264), keep = r => isPreset(r) ? Uint8Array.from(r) : null;
    const ra = keep(at(da, a)), rb = keep(at(db, b));
    at(da, a).fill(0); at(db, b).fill(0);
    if (rb) at(da, a).set(rb); if (ra) at(db, b).set(ra);
    await this.writeGroup('pst', ga, da, undefined, seq(ga));
    if (gb !== ga) await this.writeGroup('pst', gb, db, undefined, seq(gb));
  }
  // ---- backup / restore (all group files) ----
  async backup() {
    await this.migrate(); const files = await this.scan(), out = [];
    for (const kind of Object.keys(GROUPS)) for (let g = 0; g < GROUPS[kind].files; g++) { const d = await this.readGroup(kind, g, files); if (d) out.push({ kind, g, data: b64enc(d) }); }
    return JSON.stringify({ format: 'frost-backup', version: 1, created: new Date().toISOString(), files: out }, null, 1);
  }
  async restore(text, say = () => {}) {
    const j = JSON.parse(text); if (j.format !== 'frost-backup' || j.version !== 1) throw new Error('not a Frost backup file');
    const want = {};
    for (const f of j.files) {
      const G = GROUPS[f.kind]; if (!G || !(f.g >= 0 && f.g < G.files)) throw new Error('bad backup entry');
      const d = b64dec(f.data); if (d.length !== G.slots * G.size) throw new Error('bad backup entry size');
      want[f.kind + f.g] = d;
    }
    if (want.glb0) { const r = await this.f.ask([0x7e, 0x1b]); if (!r || r.length !== 8 || r[2] !== 1) throw new Error('This backup needs Frost 31z6 or later.'); }
    await this.migrate();
    // Older backups had no global group: preserve settings/setlists in that case.
    for (const kind of Object.keys(GROUPS)) for (let g = 0; g < GROUPS[kind].files; g++) { if (kind === 'glb' && !want.glb0) continue; say(`restoring ${GROUPS[kind].prefix}${g}`); await this.writeGroup(kind, g, want[kind + g] || null); }
  }
}
