// Local test server for the community Worker: the same worker.js, with Node's SQLite in place of D1 and a folder in
// place of R2, so the whole community runs on this computer before anything goes online. Nothing is downloaded.
//   node community/dev.mjs [port]        (default 8096; data in community/.local, ignored by git)
// The owner's author ids go in community/.local/admin.txt (one per line); the file is read on every request.
// The server stops by itself after 4 hours without requests.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { DatabaseSync } from 'node:sqlite';
import worker from './worker.js';

class Statement {
  constructor(st, args = []) { this.st = st; this.args = args; }
  bind(...args) { return new Statement(this.st, args.map(a => (a === undefined ? null : a))); }
  async first(col) { const r = this.st.get(...this.args); return r === undefined ? null : col ? r[col] : { ...r }; }
  async all() { return { success: true, results: this.st.all(...this.args).map(r => ({ ...r })) }; }
  async run() { const r = this.st.run(...this.args); return { success: true, meta: { changes: Number(r.changes), last_row_id: Number(r.lastInsertRowid) } }; }
}
/** The D1 calls the worker uses (prepare, bind, first, all, run), on a SQLite file or in memory. */
export class LocalD1 {
  constructor(file = ':memory:') { this.db = new DatabaseSync(file); }
  prepare(sql) { return new Statement(this.db.prepare(sql)); }
}
/** The R2 calls the worker uses (put, get, delete), in a folder or, without one, in memory. */
export class LocalR2 {
  constructor(dir = null) { this.dir = dir; this.mem = new Map(); if (dir) fs.mkdirSync(dir, { recursive: true }); }
  async put(key, body, opts = {}) {
    const bytes = new Uint8Array(body instanceof ArrayBuffer ? body : body.buffer ? body : await new Response(body).arrayBuffer());
    const meta = { type: opts.httpMetadata?.contentType || 'application/octet-stream' };
    if (this.dir) { fs.writeFileSync(path.join(this.dir, key), bytes); fs.writeFileSync(path.join(this.dir, key + '.json'), JSON.stringify(meta)); }
    else this.mem.set(key, { bytes: bytes.slice(), meta });
  }
  async get(key) {
    let hit = this.mem.get(key);
    if (this.dir && fs.existsSync(path.join(this.dir, key))) hit = { bytes: new Uint8Array(fs.readFileSync(path.join(this.dir, key))), meta: JSON.parse(fs.readFileSync(path.join(this.dir, key + '.json'), 'utf8')) };
    return hit ? { body: hit.bytes, size: hit.bytes.length, httpMetadata: { contentType: hit.meta.type } } : null;
  }
  async delete(key) { this.mem.delete(key); if (this.dir) for (const f of [key, key + '.json']) fs.rmSync(path.join(this.dir, f), { force: true }); }
}

/** The Workers KV calls the worker uses (put, get as arrayBuffer, delete), in memory. */
export class LocalKV {
  constructor() { this.mem = new Map(); }
  async put(key, value) { this.mem.set(key, new Uint8Array(value instanceof ArrayBuffer ? value : value.buffer ? value : await new Response(value).arrayBuffer()).slice()); }
  async get(key, type) { const v = this.mem.get(key); if (!v) return null; return type === 'arrayBuffer' ? v.slice().buffer : new TextDecoder().decode(v); }
  async delete(key) { this.mem.delete(key); }
}

const here = path.dirname(fileURLToPath(import.meta.url));
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const port = Number(process.argv[2]) || 8096, dir = path.join(here, '.local');
  fs.mkdirSync(dir, { recursive: true });
  const env = { DB: new LocalD1(path.join(dir, 'community.db')), FILES: new LocalR2(path.join(dir, 'files')), SITE_URL: 'http://localhost:8095/', SALT: 'local' };
  Object.defineProperty(env, 'ADMIN_KEYS', { get: () => { try { return fs.readFileSync(path.join(dir, 'admin.txt'), 'utf8'); } catch { return ''; } } });
  let idle;
  const keepAlive = () => { clearTimeout(idle); idle = setTimeout(() => { console.log('4 hours without requests: stopping.'); process.exit(0); }, 4 * 3600e3); };
  http.createServer(async (req, res) => {
    keepAlive();
    const chunks = []; for await (const c of req) chunks.push(c);
    const headers = new Headers(); for (const [k, v] of Object.entries(req.headers)) if (typeof v === 'string' && !['host', 'connection', 'keep-alive', 'transfer-encoding'].includes(k)) headers.set(k, v);
    headers.set('CF-Connecting-IP', req.socket.remoteAddress || '0');
    const body = chunks.length && !['GET', 'HEAD'].includes(req.method) ? Buffer.concat(chunks) : undefined;
    try {
      const out = await worker.fetch(new Request(`http://localhost:${port}${req.url}`, { method: req.method, headers, body }), env);
      res.writeHead(out.status, Object.fromEntries(out.headers));
      res.end(Buffer.from(await out.arrayBuffer()));
    } catch (e) { console.error(e); res.writeHead(500); res.end(); }
  }).listen(port, '127.0.0.1', () => { keepAlive(); console.log(`Frost community (local test) on http://localhost:${port}`); });
}
