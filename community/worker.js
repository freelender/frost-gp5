// Frost community server: a Cloudflare Worker that keeps the loops, presets, amp models and cabinets that
// GP-5 players share, each with the nickname of who made it, without accounts.
//
// Identity: each browser makes its own signing key (ECDSA P-256) and keeps it; every upload is signed with it. The
// author id is a hash of the public key, and a nickname belongs to the first key that uses it, so nobody can publish
// under someone else's name without their key.
// Moderation: the first upload of each author waits for the owner's approval; after that the author publishes freely.
// A report hides an item at once until the owner decides. The owner is whoever holds a key listed in env.ADMIN_KEYS.
// Storage: the list in D1 (env.DB); the files in R2 (env.FILES) or, on an account without R2, in Workers KV (env.KV).
// The tables are made on first use.
// Every item also has a public page, /i/<id>, that plays in any browser and links back to the Frost site.

const DAY = 864e5, MAX_PENDING = 3, MAX_BODY = 21e6;
export const KINDS = {
  loop: { max: 20e6, exts: ['wav'] },
  preset: { max: 256e3, exts: ['frostpreset'] },
  model: { max: 4e6, exts: ['nam', 'fcm'] },
  cab: { max: 2e6, exts: ['wav'] },
};
export const LIMITS = { author: 30, ip: 60, report: 20 };   // per day
const TYPE = { loop: 'audio/wav', cab: 'audio/wav', preset: 'application/json', model: 'application/octet-stream' };
const SCHEMA = [
  `CREATE TABLE IF NOT EXISTS authors (id TEXT PRIMARY KEY, pub TEXT NOT NULL, nick TEXT NOT NULL, nick_lc TEXT NOT NULL UNIQUE,
     trusted INTEGER NOT NULL DEFAULT 0, banned INTEGER NOT NULL DEFAULT 0, created INTEGER NOT NULL)`,
  `CREATE TABLE IF NOT EXISTS items (id TEXT PRIMARY KEY, kind TEXT NOT NULL, author TEXT NOT NULL, title TEXT NOT NULL,
     note TEXT NOT NULL DEFAULT '', style TEXT NOT NULL DEFAULT '', file_name TEXT NOT NULL, size INTEGER NOT NULL,
     bpm REAL, beats INTEGER, bars INTEGER, music_key TEXT, reply_to TEXT, status TEXT NOT NULL,
     reports INTEGER NOT NULL DEFAULT 0, kept INTEGER NOT NULL DEFAULT 0, created INTEGER NOT NULL)`,
  'CREATE INDEX IF NOT EXISTS items_feed ON items (status, kind, created)',
  'CREATE INDEX IF NOT EXISTS items_reply ON items (reply_to, status, created)',
  'CREATE INDEX IF NOT EXISTS items_author ON items (author, created)',
  'CREATE TABLE IF NOT EXISTS seen (sig TEXT PRIMARY KEY, at INTEGER NOT NULL)',
  'CREATE TABLE IF NOT EXISTS hits (k TEXT PRIMARY KEY, n INTEGER NOT NULL, day INTEGER NOT NULL)',
  'CREATE TABLE IF NOT EXISTS reports (item TEXT NOT NULL, who TEXT NOT NULL, at INTEGER NOT NULL, reason TEXT NOT NULL DEFAULT \'\')',
];
const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, X-Key, X-Time, X-Sig, X-Meta',
  'Access-Control-Max-Age': '86400',
};

class Fail extends Error { constructor(status, code) { super(code); this.status = status; this.code = code; } }
const enc = s => new TextEncoder().encode(s);
export const b64u = u8 => { let s = ''; for (const b of u8) s += String.fromCharCode(b); return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''); };
const b64d = s => { const t = atob(String(s).replace(/-/g, '+').replace(/_/g, '/')); return Uint8Array.from(t, c => c.charCodeAt(0)); };
const sha = async data => new Uint8Array(await crypto.subtle.digest('SHA-256', data));
export const authorId = async raw => b64u(await sha(raw)).slice(0, 16);
const newId = () => b64u(crypto.getRandomValues(new Uint8Array(9)));          // 12 characters
const ID_RE = /^[\w-]{12}$/;
const NICK_RE = /^[\p{L}\p{N}][\p{L}\p{N} ._-]{1,23}$/u;
const clean = (v, max) => String(v ?? '').replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, max);
const num = (v, lo, hi) => { const n = Number(v); return v != null && v !== '' && Number.isFinite(n) && n >= lo && n <= hi ? n : null; };
const json = (data, status = 200) => new Response(JSON.stringify(data), { status, headers: { ...CORS, 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' } });
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);

/** The file store: R2 when bound as FILES, else Workers KV bound as KV (no card needed; values up to 25 MB). */
function files(env) {
  if (env.FILES) return { put: (k, body, type) => env.FILES.put(k, body, { httpMetadata: { contentType: type } }), get: async k => (await env.FILES.get(k))?.body ?? null, del: k => env.FILES.delete(k) };
  if (env.KV) return { put: (k, body) => env.KV.put('f:' + k, body), get: k => env.KV.get('f:' + k, 'arrayBuffer'), del: k => env.KV.delete('f:' + k) };
  throw new Error('No file store bound: add an R2 bucket as FILES or a KV namespace as KV');
}
const made = new WeakSet();
async function ready(env) {
  if (made.has(env.DB)) return;
  for (const s of SCHEMA) await env.DB.prepare(s).run();
  made.add(env.DB);
}
const SELECT = `SELECT i.*, a.nick AS nick, (SELECT COUNT(*) FROM items r WHERE r.reply_to = i.id AND r.status = 'ok') AS replies
  FROM items i JOIN authors a ON a.id = i.author`;
const one = (env, id) => env.DB.prepare(`${SELECT} WHERE i.id = ?`).bind(id).first();
const pub = r => ({ id: r.id, kind: r.kind, title: r.title, note: r.note, style: r.style, nick: r.nick, author: r.author, file: r.file_name,
  size: r.size, bpm: r.bpm, beats: r.beats, bars: r.bars, key: r.music_key, replyTo: r.reply_to, replies: r.replies || 0, status: r.status,
  reports: r.reports, created: r.created });
const admins = env => String(env.ADMIN_KEYS || '').split(/[\s,]+/).filter(Boolean);
/** A hash of the caller's address, for the daily limits and the reports (the address itself is never stored). */
const who_ip = async (req, env) => b64u(await sha(enc('ip:' + (req.headers.get('CF-Connecting-IP') || '0') + ':' + (env.SALT || 'frost')))).slice(0, 16);

/** Check the browser signature: "TS1", method, path, time, SHA-256 of X-Meta and of the body, signed with the key in X-Key.
 *  A signature is good for 5 minutes and only once. Returns { id, key, admin }. */
export async function verify(req, env, meta, body) {
  const key = req.headers.get('X-Key'), sig = req.headers.get('X-Sig'), time = Number(req.headers.get('X-Time'));
  if (!key || !sig || !Number.isFinite(time)) throw new Fail(401, 'unsigned');
  if (Math.abs(Date.now() - time) > 5 * 60e3) throw new Fail(401, 'clock');
  let raw, sigBytes;
  try { raw = b64d(key); sigBytes = b64d(sig); } catch { throw new Fail(401, 'unsigned'); }
  if (raw.length !== 65 || raw[0] !== 4 || sigBytes.length !== 64) throw new Fail(401, 'unsigned');
  const url = new URL(req.url);
  const text = ['TS1', req.method, url.pathname + url.search, String(time), b64u(await sha(enc(meta || ''))), b64u(await sha(body))].join('\n');
  let ok = false;
  try {
    const k = await crypto.subtle.importKey('raw', raw, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
    ok = await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, k, sigBytes, enc(text));
  } catch { ok = false; }
  if (!ok) throw new Fail(401, 'unsigned');
  const now = Date.now(), once = b64u(await sha(sigBytes));
  await env.DB.prepare('DELETE FROM seen WHERE at < ?').bind(now - 11 * 60e3).run();
  const r = await env.DB.prepare('INSERT OR IGNORE INTO seen (sig, at) VALUES (?, ?)').bind(once, now).run();
  if (!r.meta?.changes) throw new Fail(401, 'unsigned');
  const id = await authorId(raw);
  return { id, key, admin: admins(env).includes(id) };
}
async function bump(env, k, limit) {
  const day = Math.floor(Date.now() / DAY), row = await env.DB.prepare('SELECT n, day FROM hits WHERE k = ?').bind(k).first();
  const n = row && row.day === day ? row.n + 1 : 1;
  if (n > limit) throw new Fail(429, 'limit');
  await env.DB.prepare('INSERT INTO hits (k, n, day) VALUES (?, ?, ?) ON CONFLICT (k) DO UPDATE SET n = excluded.n, day = excluded.day').bind(k, n, day).run();
}
/** The author row for this key with this nickname: made on first use; a nickname held by another key is refused. */
async function claim(env, who, nick) {
  const lc = nick.toLowerCase();
  const holder = await env.DB.prepare('SELECT id FROM authors WHERE nick_lc = ?').bind(lc).first();
  if (holder && holder.id !== who.id) throw new Fail(409, 'nick-taken');
  const a = await env.DB.prepare('SELECT * FROM authors WHERE id = ?').bind(who.id).first();
  if (a?.banned) throw new Fail(403, 'banned');
  try {
    if (!a) await env.DB.prepare('INSERT INTO authors (id, pub, nick, nick_lc, trusted, banned, created) VALUES (?, ?, ?, ?, ?, 0, ?)')
      .bind(who.id, who.key, nick, lc, who.admin ? 1 : 0, Date.now()).run();
    else if (a.nick !== nick) await env.DB.prepare('UPDATE authors SET nick = ?, nick_lc = ? WHERE id = ?').bind(nick, lc, who.id).run();
  } catch (e) { if (/UNIQUE/i.test(String(e?.message))) throw new Fail(409, 'nick-taken'); throw e; }
  return { trusted: !!(a?.trusted || who.admin) };
}
/** The file must be what its kind says: a WAV for loops and cabinets, a Frost preset file, a NAM capture or an .fcm model. */
function checkFile(kind, ext, body) {
  const ascii = (o, n) => String.fromCharCode(...body.subarray(o, o + n));
  if (ext === 'wav') { if (body.length < 44 || ascii(0, 4) !== 'RIFF' || ascii(8, 4) !== 'WAVE') throw new Fail(400, 'bad-file'); return; }
  if (ext === 'fcm') { if (body.length > 64e3) throw new Fail(413, 'too-big'); return; }
  let j; try { j = JSON.parse(new TextDecoder().decode(body)); } catch { throw new Fail(400, 'bad-file'); }
  if (ext === 'frostpreset' && !(j?.format === 'frost-preset' && j.version === 1 && typeof j.record === 'string')) throw new Fail(400, 'bad-file');
  if (ext === 'nam' && !(j && typeof j === 'object' && 'architecture' in j)) throw new Fail(400, 'bad-file');
}

async function feed(url, env) {
  const kind = url.searchParams.get('kind') || 'loop';
  if (!KINDS[kind]) throw new Fail(400, 'bad-request');
  const before = num(url.searchParams.get('before'), 0, 9e15) ?? 9e15, q = clean(url.searchParams.get('q'), 40).toLowerCase();
  const author = clean(url.searchParams.get('author'), 16);
  let sql = `${SELECT} WHERE i.status = 'ok' AND i.kind = ? AND i.created < ?`; const args = [kind, before];
  if (kind === 'loop' && !q && !author) sql += ' AND i.reply_to IS NULL';                 // replies show inside their loop
  if (q) {
    const like = '%' + q.replace(/[\\%_]/g, m => '\\' + m) + '%';
    sql += " AND (lower(i.title) LIKE ? ESCAPE '\\' OR lower(a.nick) LIKE ? ESCAPE '\\' OR lower(i.style) LIKE ? ESCAPE '\\')"; args.push(like, like, like);
  }
  if (author) { sql += ' AND i.author = ?'; args.push(author); }
  const { results } = await env.DB.prepare(sql + ' ORDER BY i.created DESC LIMIT 25').bind(...args).all();
  return json({ items: results.map(pub), next: results.length === 25 ? results[24].created : null });
}
async function thread(id, env) {
  const r = await one(env, id);
  if (!r || r.status !== 'ok') throw new Fail(404, 'not-found');
  const { results } = await env.DB.prepare(`${SELECT} WHERE i.reply_to = ? AND i.status = 'ok' ORDER BY i.created`).bind(id).all();
  const parent = r.reply_to ? await one(env, r.reply_to) : null;
  return { item: pub(r), replies: results.map(pub), parent: parent?.status === 'ok' ? pub(parent) : null };
}
const safeName = s => String(s).replace(/[^\w .()-]/g, '_').slice(0, 80) || 'file';
async function file(id, req, env) {
  const r = await one(env, id);
  if (!r) throw new Fail(404, 'not-found');
  if (r.status !== 'ok') {                     // waiting or hidden: only its author and the owner, with a signed request
    const who = await verify(req, env, '', new Uint8Array(0));
    if (!who.admin && who.id !== r.author) throw new Fail(404, 'not-found');
  }
  const body = await files(env).get(id);
  if (body == null) throw new Fail(404, 'not-found');
  const headers = { ...CORS, 'Content-Type': TYPE[r.kind], 'X-Content-Type-Options': 'nosniff', 'Cache-Control': r.status === 'ok' ? 'public, max-age=600' : 'no-store' };
  if (r.kind !== 'loop') headers['Content-Disposition'] = `attachment; filename="${safeName(r.file_name)}"`;
  return new Response(body, { headers });
}
async function publish(req, env) {
  if (Number(req.headers.get('Content-Length') || 0) > MAX_BODY) throw new Fail(413, 'too-big');
  const meta = req.headers.get('X-Meta') || '', body = new Uint8Array(await req.arrayBuffer());
  if (body.length > MAX_BODY) throw new Fail(413, 'too-big');
  const who = await verify(req, env, meta, body);
  let m; try { m = JSON.parse(new TextDecoder().decode(b64d(meta))); } catch { throw new Fail(400, 'bad-request'); }
  const kind = m?.kind, spec = KINDS[kind];
  if (!spec) throw new Fail(400, 'bad-request');
  const fileName = safeName(clean(m.file, 80)), ext = (fileName.split('.').pop() || '').toLowerCase();
  if (!spec.exts.includes(ext) || !body.length) throw new Fail(400, 'bad-file');
  if (body.length > spec.max) throw new Fail(413, 'too-big');
  checkFile(kind, ext, body);
  const title = clean(m.title, 60), nick = clean(m.nick, 24);
  if (!title) throw new Fail(400, 'no-title');
  if (!NICK_RE.test(nick)) throw new Fail(400, 'bad-nick');
  let replyTo = null;
  if (m.replyTo) {
    const p = kind === 'loop' && ID_RE.test(m.replyTo) ? await one(env, m.replyTo) : null;
    if (!p || p.kind !== 'loop' || p.status !== 'ok') throw new Fail(404, 'not-found');
    replyTo = p.id;
  }
  const author = await claim(env, who, nick);
  if (!author.trusted) {
    const waiting = await env.DB.prepare("SELECT COUNT(*) AS n FROM items WHERE author = ? AND status = 'pending'").bind(who.id).first();
    if (waiting.n >= MAX_PENDING) throw new Fail(429, 'queue-full');
  }
  await bump(env, 'a:' + who.id, LIMITS.author);
  await bump(env, 'ip:' + await who_ip(req, env), LIMITS.ip);
  const id = newId(), status = author.trusted ? 'ok' : 'pending', loop = kind === 'loop';
  await files(env).put(id, body, TYPE[kind]);
  await env.DB.prepare(`INSERT INTO items (id, kind, author, title, note, style, file_name, size, bpm, beats, bars, music_key, reply_to, status, created)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`).bind(id, kind, who.id, title, clean(m.note, 300), clean(m.style, 30), fileName, body.length,
    loop ? num(m.bpm, 40, 240) : null, loop ? num(m.beats, 1, 12) : null, loop ? num(m.bars, 1, 512) : null, loop ? clean(m.key, 8) || null : null,
    replyTo, status, Date.now()).run();
  return json({ id, status });
}
async function report(id, req, env, reason) {
  const r = ID_RE.test(id) ? await one(env, id) : null;
  if (!r || r.status === 'pending') throw new Fail(404, 'not-found');
  const ip = await who_ip(req, env);
  await bump(env, 'r:' + ip, LIMITS.report);
  await env.DB.prepare('INSERT INTO reports (item, who, at, reason) VALUES (?, ?, ?, ?)').bind(id, ip, Date.now(), clean(reason, 200)).run();
  // One report hides it until the owner decides; an item the owner already kept only counts the report.
  await env.DB.prepare("UPDATE items SET reports = reports + 1, status = CASE WHEN status = 'ok' AND kept = 0 THEN 'hidden' ELSE status END WHERE id = ?").bind(id).run();
}
async function remove(id, env) { await files(env).del(id); await env.DB.prepare('DELETE FROM items WHERE id = ?').bind(id).run(); }
async function signedJson(req, env) {
  const body = new Uint8Array(await req.arrayBuffer());
  if (body.length > 4096) throw new Fail(413, 'too-big');
  const who = await verify(req, env, '', body);
  let m = {}; if (body.length) try { m = JSON.parse(new TextDecoder().decode(body)); } catch { throw new Fail(400, 'bad-request'); }
  return { who, m };
}
async function me(req, env) {
  const who = await verify(req, env, '', new Uint8Array(0));
  const a = await env.DB.prepare('SELECT * FROM authors WHERE id = ?').bind(who.id).first();
  const { results } = await env.DB.prepare(`${SELECT} WHERE i.author = ? ORDER BY i.created DESC LIMIT 100`).bind(who.id).all();
  return json({ id: who.id, nick: a?.nick || null, trusted: !!(a?.trusted || who.admin), banned: !!a?.banned, admin: who.admin, items: results.map(pub) });
}
async function queue(req, env) {
  const who = await verify(req, env, '', new Uint8Array(0));
  if (!who.admin) throw new Fail(403, 'admin');
  const { results } = await env.DB.prepare(`${SELECT} WHERE i.status IN ('pending', 'hidden') ORDER BY i.created LIMIT 100`).all();
  const { results: why } = await env.DB.prepare("SELECT item, reason, at FROM reports WHERE item IN (SELECT id FROM items WHERE status = 'hidden') ORDER BY at DESC LIMIT 500").all();
  return json({ items: results.map(r => ({ ...pub(r), reasons: why.filter(w => w.item === r.id).slice(0, 5).map(w => w.reason).filter(Boolean) })) });
}
async function act(req, env) {
  const { who, m } = await signedJson(req, env);
  if (!who.admin) throw new Fail(403, 'admin');
  const r = ID_RE.test(m.id || '') ? await one(env, m.id) : null;
  const author = clean(m.author, 16) || r?.author;
  const set = (status, extra = '') => env.DB.prepare(`UPDATE items SET status = ?${extra} WHERE id = ?`).bind(status, r.id).run();
  if (['approve', 'reject', 'hide', 'restore', 'delete'].includes(m.act) && !r) throw new Fail(404, 'not-found');
  switch (m.act) {
    case 'approve': await set('ok'); await env.DB.prepare('UPDATE authors SET trusted = 1 WHERE id = ?').bind(r.author).run(); break;
    case 'restore': await set('ok', ', kept = 1'); break;
    case 'hide': await set('hidden'); break;
    case 'reject': case 'delete': await remove(r.id, env); break;
    case 'ban':
      if (!author) throw new Fail(400, 'bad-request');
      await env.DB.prepare('UPDATE authors SET banned = 1, trusted = 0 WHERE id = ?').bind(author).run();
      await env.DB.prepare("UPDATE items SET status = 'hidden' WHERE author = ? AND status = 'ok'").bind(author).run(); break;
    case 'unban':
      if (!author) throw new Fail(400, 'bad-request');
      await env.DB.prepare('UPDATE authors SET banned = 0 WHERE id = ?').bind(author).run(); break;
    default: throw new Fail(400, 'bad-request');
  }
  return json({ ok: true });
}

// ---- the public page of an item: plays in any browser, in Portuguese or English
const WORDS = {
  pt: { by: 'por', over: 'Gravar por cima no Frost', open: 'Abrir no Frost', download: 'Baixar o arquivo', replyTo: 'Resposta a',
    replies: 'Respostas', none: 'Ninguém gravou por cima ainda.', report: 'Denunciar', reported: 'Obrigado. O item ficou escondido até a moderação olhar.',
    missing: 'Este item não está disponível.', need: 'Para gravar por cima: GP-5 com o Frost, no Chrome ou Edge de um computador.',
    kinds: { loop: 'Loop', preset: 'Preset', model: 'Modelo de amp', cab: 'Caixa (IR)' }, bars: 'compassos', back: 'Voltar' },
  en: { by: 'by', over: 'Record over it in Frost', open: 'Open in Frost', download: 'Download the file', replyTo: 'A reply to',
    replies: 'Replies', none: 'Nobody has recorded over it yet.', report: 'Report', reported: 'Thank you. The item is hidden until a moderator reviews it.',
    missing: 'This item is not available.', need: 'To record over it: a GP-5 with Frost, in Chrome or Edge on a computer.',
    kinds: { loop: 'Loop', preset: 'Preset', model: 'Amp model', cab: 'Cabinet (IR)' }, bars: 'bars', back: 'Back' },
};
const SIGNATURES = { 4: '4/4', 3: '3/4', 6: '6/8', 5: '5/4', 7: '7/8' };
function shell(lang, title, body, og = '') {
  const html = `<!doctype html><html lang="${lang === 'pt' ? 'pt-BR' : 'en'}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(title)}</title>${og}<style>
body{margin:0;background:#0d0b0c;color:#eee;font:16px/1.5 system-ui,sans-serif}main{max-width:620px;margin:0 auto;padding:28px 18px}
.brand{color:#e0313b;font-weight:800;letter-spacing:.12em;font-size:13px}h1{margin:.2em 0 .1em;font-size:28px;line-height:1.2}.by{color:#aaa}
.kind{display:inline-block;border:1px solid #e0313b;color:#e0313b;border-radius:99px;padding:0 10px;font-size:12px;font-weight:700;margin-right:6px}
.info{color:#bbb;font-size:14px;margin:10px 0}.note{white-space:pre-wrap;background:#171315;border-radius:10px;padding:10px 14px}
audio{width:100%;margin:16px 0}a.btn,button{display:inline-block;background:#e0313b;color:#fff;border:0;border-radius:10px;padding:10px 16px;font-weight:700;text-decoration:none;margin:4px 6px 4px 0;font-size:15px;cursor:pointer}
a.ghost,button.ghost{background:transparent;border:1px solid #555;color:#ddd}.small{font-size:13px;color:#999}ul{padding-left:18px}li{margin:6px 0}a{color:#ff6b73}
form{display:inline}</style></head><body><main><div class="brand">FROST · GP-5</div>${body}</main></body></html>`;
  return new Response(html, { headers: { 'Content-Type': 'text/html; charset=utf-8', 'X-Content-Type-Options': 'nosniff', 'Cache-Control': 'no-store',
    'Content-Security-Policy': "default-src 'none'; media-src 'self'; style-src 'unsafe-inline'; img-src 'self' https: data:; form-action 'self'; base-uri 'none'; frame-ancestors 'none'" } });
}
const pageLang = req => (/^pt/i.test(req.headers.get('Accept-Language') || '') ? 'pt' : 'en');
async function page(id, req, env, url) {
  const lang = pageLang(req), w = WORDS[lang], site = env.SITE_URL || 'https://freelender.github.io/frost-gp5/';
  let t; try { t = await thread(id, env); } catch { return shell(lang, w.missing, `<h1>${esc(w.missing)}</h1><p><a class="btn ghost" href="${esc(site)}">Frost</a></p>`); }
  const it = t.item, loop = it.kind === 'loop', base = `${url.origin}`;
  const facts = loop ? [it.bpm && `${it.bpm} BPM`, SIGNATURES[it.beats] || (it.beats && `${it.beats}/4`), it.bars && `${it.bars} ${w.bars}`, it.key, it.style].filter(Boolean)
    : [w.kinds[it.kind], it.style, it.file].filter(Boolean);
  const desc = `${w.kinds[it.kind]} ${w.by} ${it.nick}${facts.length ? ' · ' + facts.join(' · ') : ''}`;
  const og = `<meta property="og:title" content="${esc(it.title)}"><meta property="og:description" content="${esc(desc)}"><meta property="og:type" content="${loop ? 'music.song' : 'website'}">`
    + `<meta property="og:url" content="${esc(base + '/i/' + it.id)}"><meta property="og:image" content="${esc(site + 'frost_logo.png')}"><meta name="description" content="${esc(desc)}">`
    + (loop ? `<meta property="og:audio" content="${esc(base + '/api/file/' + it.id)}">` : '');
  const link = r => `<li><a href="/i/${esc(r.id)}">${esc(r.title)}</a> <span class="small">${esc(w.by)} ${esc(r.nick)}</span></li>`;
  const body = `<p><span class="kind">${esc(w.kinds[it.kind])}</span></p><h1>${esc(it.title)}</h1><div class="by">${esc(w.by)} <b>${esc(it.nick)}</b></div>`
    + (t.parent ? `<p class="small">${esc(w.replyTo)} <a href="/i/${esc(t.parent.id)}">${esc(t.parent.title)}</a> (${esc(t.parent.nick)})</p>` : '')
    + `<div class="info">${facts.map(esc).join(' · ')}</div>` + (it.note ? `<div class="note">${esc(it.note)}</div>` : '')
    + (loop ? `<audio controls loop preload="metadata" src="/api/file/${esc(it.id)}"></audio>` : '<p></p>')
    + `<a class="btn" href="${esc(site)}?item=${esc(it.id)}#community">${esc(loop ? w.over : w.open)}</a>`
    + `<a class="btn ghost" href="/api/file/${esc(it.id)}" download="${esc(safeName(it.file))}">${esc(w.download)}</a>`
    + (loop ? `<p class="small">${esc(w.need)}</p><h2>${esc(w.replies)} (${t.replies.length})</h2>` + (t.replies.length ? `<ul>${t.replies.map(link).join('')}</ul>` : `<p class="small">${esc(w.none)}</p>`) : '')
    + `<p><form method="post" action="/i/${esc(it.id)}/report"><button class="ghost" type="submit">${esc(w.report)}</button></form></p>`;
  return shell(lang, `${it.title} · ${it.nick} · Frost`, body, og);
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url), p = url.pathname, get = req.method === 'GET' || req.method === 'HEAD', post = req.method === 'POST';
    if (req.method === 'OPTIONS') return new Response(null, { status: 204, headers: CORS });
    let m;
    try {
      await ready(env);
      if (get && p === '/api/feed') return await feed(url, env);
      if (get && (m = p.match(/^\/api\/item\/([\w-]{12})$/))) return json(await thread(m[1], env));
      if (get && (m = p.match(/^\/api\/file\/([\w-]{12})$/))) return await file(m[1], req, env);
      if (get && p === '/api/me') return await me(req, env);
      if (post && p === '/api/publish') return await publish(req, env);
      if (post && (m = p.match(/^\/api\/report\/([\w-]{12})$/))) {
        let reason = ''; try { reason = (await req.json())?.reason || ''; } catch { /* no reason given */ }
        await report(m[1], req, env, reason); return json({ ok: true });
      }
      if (post && (m = p.match(/^\/api\/remove\/([\w-]{12})$/))) {
        const { who } = await signedJson(req, env), r = await one(env, m[1]);
        if (!r) throw new Fail(404, 'not-found');
        if (r.author !== who.id && !who.admin) throw new Fail(403, 'admin');
        await remove(r.id, env); return json({ ok: true });
      }
      if (get && p === '/api/admin/queue') return await queue(req, env);
      if (post && p === '/api/admin/act') return await act(req, env);
      if (get && (m = p.match(/^\/i\/([\w-]{12})$/))) return await page(m[1], req, env, url);
      if (post && (m = p.match(/^\/i\/([\w-]{12})\/report$/))) {
        const lang = pageLang(req), w = WORDS[lang];
        try { await report(m[1], req, env, 'from the public page'); } catch (e) { if (!(e instanceof Fail)) throw e; }
        return shell(lang, w.reported, `<h1>${esc(w.reported)}</h1>`);
      }
      if (get && p === '/') return json({ name: 'Frost community', ok: true });
      throw new Fail(404, 'not-found');
    } catch (e) {
      if (e instanceof Fail) return json({ error: e.code }, e.status);
      console.error(e);
      return json({ error: 'server' }, 500);
    }
  },
};
