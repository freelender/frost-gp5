// Talking to a GP-5 running Frost over USB MIDI (Web MIDI in the browser), and
// the AMP model files on its controller flash. Ports tools/gp5_proto.py framing
// and tools/fcm_files.py (OpenGP5 0.5 file format, run A 0x13F000..0x15FFFF,
// firmware id FFCM, files amp-NN holding one 400-byte record).
// Every command here is safe to repeat; a lost reply is asked again.

export const FILES = { BASE: 0x13f000, SECTOR: 0x1000, SECTORS: 33, HEADER: 64, MAGIC: 0x46355047, FIRM: 0x4d434646, RECORD: 400 };
const CRC32 = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
export function crc32(b) { let c = 0xffffffff; for (const x of b) c = CRC32[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }

// ---- framing (gp5_proto.build_sysex / parse_sysex) ----
function crc8(bytes) { let c = 0; for (const b of bytes) { c ^= b; for (let i = 0; i < 8; i++) c = c & 0x80 ? ((c << 1) ^ 0x07) & 255 : (c << 1) & 255; } return c; }
export function buildSysex(payload, cls = 0x01, index = 0) {
  const pl = Array.from(payload), head = [0, cls, index, pl.length], body = [crc8([...head, ...pl]), cls, index, pl.length, ...pl];
  const out = [0xf0]; for (const b of body) out.push(b >> 4, b & 15); out.push(0xf7); return new Uint8Array(out);
}
export function parseSysex(msg) {
  if (msg[0] !== 0xf0 || msg[msg.length - 1] !== 0xf7 || (msg.length - 2) % 2) return null;
  const d = []; for (let i = 1; i < msg.length - 1; i += 2) d.push((msg[i] << 4) | msg[i + 1]);
  if (d.length < 4) return null;
  const [crc, cls, index, ln] = d, pl = d.slice(4, 4 + ln);
  return crc8([0, cls, index, ln, ...pl]) === crc ? Uint8Array.from(pl) : null;
}

// ---- Web MIDI transport ----
export class MidiPedal {
  static async connect() {
    if (!navigator.requestMIDIAccess) throw new Error('This browser has no Web MIDI. Use Chrome, Edge or Opera on a computer (Firefox asks to install a site permission add-on).');
    const access = await navigator.requestMIDIAccess({ sysex: true });
    const outs = [...access.outputs.values()].filter(p => /GP-5/.test(p.name)), ins = [...access.inputs.values()].filter(p => /GP-5/.test(p.name));
    if (!outs.length || !ins.length) throw new Error('No GP-5 found on USB. Connect the pedal (powered on, running Frost) and try again.');
    const pick = list => list.find(p => /Open FW/.test(p.name)) || list[0];
    const pedal = new MidiPedal(pick(outs), pick(ins));
    // Unplugging the pedal: tell the page (pedal.onlost), so it does not keep editing a pedal that is gone.
    pedal.access = access;
    access.onstatechange = e => { if (e.port.state === 'disconnected' && (e.port.id === pedal.out.id || e.port.id === pedal.inp.id)) pedal.onlost?.(); };
    return pedal;
  }
  constructor(out, inp) {
    this.out = out; this.inp = inp; this.waiters = [];
    inp.onmidimessage = e => { const pl = parseSysex(e.data); if (!pl) return; const w = this.waiters.find(w => w.match(pl)); if (w) { this.waiters.splice(this.waiters.indexOf(w), 1); w.resolve(pl); } };
  }
  get name() { return this.out.name; }
  /** Send `payload` (7E xx ...) and resolve with the reply payload starting with the same two bytes, or null on timeout. */
  ask(payload, timeout = 1500) {
    return new Promise(resolve => {
      const w = { match: pl => pl[0] === payload[0] && pl[1] === payload[1], resolve };
      this.waiters.push(w);
      setTimeout(() => { const i = this.waiters.indexOf(w); if (i >= 0) { this.waiters.splice(i, 1); resolve(null); } }, timeout);
      try { this.out.send(buildSysex(payload)); } catch { /* port gone: the timeout answers null */ }
    });
  }
  close() { this.inp.onmidimessage = null; if (this.access) this.access.onstatechange = null; this.onlost = null; }
}

// ---- files (tools/fcm_files.py Pedal) ----
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const le32 = x => [x & 255, (x >>> 8) & 255, (x >>> 16) & 255, (x >>> 24) & 255];
export const modelName = i => { if (!(i >= 0 && i < 64)) throw new Error('MODEL must be 0..63'); return 'amp-' + String(i).padStart(2, '0'); };
export function fileHeader(seq, firm, name, data) {
  const h = new Uint8Array(64), v = new DataView(h.buffer), sectors = Math.ceil((64 + data.length) / FILES.SECTOR);
  v.setUint32(0, FILES.MAGIC, true); v.setUint32(4, seq, true); v.setUint32(8, firm, true); v.setUint32(12, data.length, true);
  v.setUint32(16, crc32(data), true); v.setUint16(20, sectors, true); v.setUint16(22, 0, true);
  h.set(new TextEncoder().encode(name).subarray(0, 31), 24); v.setUint32(56, crc32(h.subarray(0, 56)), true); v.setUint32(60, 0xffffffff, true);
  return h;
}
export function parseHeader(h, sector) {
  if (h.length < 64 || u32(h, 0) !== FILES.MAGIC || crc32(h.subarray(0, 56)) !== u32(h, 56)) return null;
  const length = u32(h, 12), sectors = h[20] | (h[21] << 8);
  if (!sectors || sector + sectors > FILES.SECTORS || 64 + length > sectors * FILES.SECTOR) return null;
  let end = 24; while (end < 56 && h[end]) end++;
  return { sector, seq: u32(h, 4), firm: u32(h, 8), length, crc: u32(h, 16), sectors, name: new TextDecoder().decode(h.subarray(24, end)) };
}
export const recordLabel = r => { let e = 0; while (e < 16 && r[e]) e++; return new TextDecoder().decode(r.subarray(0, e)); };
export function checkRecord(r) {
  if (!(r instanceof Uint8Array) || r.length !== FILES.RECORD) throw new Error('an AMP model file is exactly 400 bytes');
  if (crc32(r.subarray(0, 396)) !== u32(r, 396)) throw new Error('AMP model checksum mismatch');
}

export class PedalFiles {
  /** ask(payload) -> Promise<reply payload | null> (a MidiPedal's ask, or a test double). */
  /** writeTries/writeDelay (32s): a long upload (a setlist is ~70 chunks) makes
   *  the pedal re-sync AMP/IR data with its DSP between chunks; that traffic can
   *  hold the link longer than a short retry window. A refused erase/program
   *  never touches flash, so it is retried with growing pauses (~2.8 s total). */
  constructor(ask, tries = 3, { writeTries = 8, writeDelay = ms => new Promise(resolve => setTimeout(resolve, ms)) } = {}) {
    this.ask = async p => {
      const writing = p[0] === 0x7e && (p[1] === 0x07 || p[1] === 0x08), limit = writing ? writeTries : tries;
      for (let i = 0; i < limit; i++) {
        // A write can be refused before touching flash when the DSP pause
        // handshake times out. Retry the SAME sector/chunk, never the whole
        // upload. Repeating erase/program is also safe after a lost reply.
        if (writing && i) await writeDelay(100 * i);
        const r = await ask(p);
        const refused = writing && r?.length === 3 && r[0] === p[0] && r[1] === p[1] && r[2] === 0;
        if (r && (!refused || i === limit - 1)) return r;
      }
      return null;
    };
  }
  async who() {
    const r = await this.ask([0x7e, 0x0d]);
    if (!r || r.length < 7 || r[2] !== 1) return null;
    let e = 6; while (e < r.length && r[e]) e++;
    // storage version after the name (Frost stage 2); absent on 31s/31t3 = 1 (single amp-NN files)
    return { platform: [r[3], r[4], r[5]], firmware: new TextDecoder().decode(r.subarray(6, e)), storage: e + 1 < r.length ? r[e + 1] : 1 };
  }
  async read(addr, n) {
    const out = new Uint8Array(n);
    for (let o = 0; o < n; o += 64) {
      const k = Math.min(64, n - o), r = await this.ask([0x7e, 0x05, ...le32(addr + o), k]);
      if (!r || r.length !== 3 + k || r[2] !== 1) throw new Error(`flash read failed at 0x${(addr + o).toString(16)}`);
      out.set(r.subarray(3), o);
    }
    return out;
  }
  async erase(addr) { const r = await this.ask([0x7e, 0x07, ...le32(addr)]); if (!r || r[2] !== 1) throw new Error(`erase failed at 0x${addr.toString(16)}`); }
  async program(addr, data) {
    for (let o = 0; o < data.length; o += 56) {
      const piece = data.subarray(o, o + 56); if (piece.every(b => b === 255)) continue;
      const r = await this.ask([0x7e, 0x08, ...le32(addr + o), ...piece]);
      if (!r || r[2] !== 1) throw new Error(`program failed at 0x${(addr + o).toString(16)}`);
    }
  }
  async changed() { await this.ask([0x7e, 0x0e, ...le32(FILES.FIRM)]); }
  async files(onProgress) {
    const out = [];
    for (let s = 0; s < FILES.SECTORS; s++) { const h = parseHeader(await this.read(FILES.BASE + s * FILES.SECTOR, 64), s); if (h) out.push(h); if (onProgress) onProgress((s + 1) / FILES.SECTORS); }
    return out;
  }
  /** {index: {file header, label}} of the newest copy of each amp-NN. */
  async models(onProgress) {
    const files = await this.files(onProgress), best = {};
    for (const f of files) if (f.firm === FILES.FIRM && /^amp-\d\d$/.test(f.name) && (!best[f.name] || f.seq > best[f.name].seq)) best[f.name] = f;
    const out = {};
    for (const f of Object.values(best)) out[Number(f.name.slice(4))] = { ...f, label: recordLabel(await this.read(FILES.BASE + f.sector * FILES.SECTOR + 64, 16)) };
    return { models: out, used: files.reduce((s, f) => s + f.sectors, 0) };
  }
  async add(index, record, say = () => {}) {
    checkRecord(record);
    const name = modelName(index), files = await this.files(), used = new Set();
    for (const f of files) for (let s = f.sector; s < f.sector + f.sectors; s++) used.add(s);
    const free = [...Array(FILES.SECTORS).keys()].filter(s => !used.has(s));
    if (!free.length) throw new Error('All 33 model places are used: remove a model first.');
    const old = files.filter(f => f.firm === FILES.FIRM && f.name === name), seq = Math.max(0, ...files.map(f => f.seq)) + 1, s = free[0], addr = FILES.BASE + s * FILES.SECTOR;
    say(`writing ${name} (${recordLabel(record)})`);
    await this.erase(addr); await this.program(addr + 64, record);
    const back = await this.read(addr + 64, FILES.RECORD); if (back.some((b, i) => b !== record[i])) throw new Error('read-back of the model failed; the old copy is untouched');
    await this.program(addr, fileHeader(seq, FILES.FIRM, name, record).subarray(0, 60));       // header last
    if (!parseHeader(await this.read(addr, 64), s)) throw new Error('read-back of the header failed');
    for (const f of old) await this.erase(FILES.BASE + f.sector * FILES.SECTOR);                 // then forget older copies
    await this.changed(); return s;
  }
  async remove(index) {
    const name = modelName(index), hit = (await this.files()).filter(f => f.firm === FILES.FIRM && f.name === name);
    for (const f of hit) await this.erase(FILES.BASE + f.sector * FILES.SECTOR);
    if (hit.length) await this.changed();
    return hit.length;
  }
  async download(index) {
    const { models } = await this.models(); const f = models[index]; if (!f) return null;
    return this.read(FILES.BASE + f.sector * FILES.SECTOR + 64, FILES.RECORD);
  }
}
