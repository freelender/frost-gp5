// Community client: this browser's own signing key and the calls to the community server (community/worker.js).
// There is no account. The key is made once and kept in this browser; what it signs carries the nickname chosen
// here, and the server gives each nickname to the first key that uses it. The key can be saved to a file and
// opened on another computer, so the same person keeps the same name.

// The community server (community/worker.js, a Cloudflare Worker): set ONLINE to its address once it is deployed
// (community/README.md). Until then a local test server on this computer (node community/dev.mjs) is used when the
// site itself runs locally. localStorage 'frost.community.url' overrides both.
const ONLINE = '';
const LOCAL = 'http://127.0.0.1:8096';
const KEY_STORE = 'frost.community.key';
const ALG = { name: 'ECDSA', namedCurve: 'P-256' };

export function serverUrl() {
  let own = ''; try { own = localStorage.getItem('frost.community.url') || ''; } catch { /* private mode */ }
  const local = typeof location !== 'undefined' && /^(localhost|127\.0\.0\.1|\[::1\])$/.test(location.hostname);
  return (own || ONLINE || (local ? LOCAL : '')).replace(/\/+$/, '');
}

export const b64u = u8 => { let s = ''; for (const b of u8) s += String.fromCharCode(b); return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''); };
const b64d = s => { const t = atob(String(s).replace(/-/g, '+').replace(/_/g, '/')); return Uint8Array.from(t, c => c.charCodeAt(0)); };
const enc = s => new TextEncoder().encode(s);
const sha = async data => new Uint8Array(await crypto.subtle.digest('SHA-256', data));

/** A key from its private JWK: { priv (CryptoKey), pub (base64url raw point), id (author id, as the server computes it) }. */
export async function keyFromJwk(jwk) {
  const priv = await crypto.subtle.importKey('jwk', { ...jwk, key_ops: ['sign'] }, ALG, true, ['sign']);
  const raw = new Uint8Array([4, ...b64d(jwk.x), ...b64d(jwk.y)]);
  return { priv, jwk, pub: b64u(raw), id: b64u(await sha(raw)).slice(0, 16) };
}
export async function newKey() {
  const pair = await crypto.subtle.generateKey(ALG, true, ['sign', 'verify']);
  return keyFromJwk(await crypto.subtle.exportKey('jwk', pair.privateKey));
}
/** The headers that sign one request: method, path with query, time, and the SHA-256 of X-Meta and of the body. */
export async function signHeaders(key, method, pathq, meta = '', body = new Uint8Array(0)) {
  const time = String(Date.now());
  const text = ['TS1', method, pathq, time, b64u(await sha(enc(meta))), b64u(await sha(body))].join('\n');
  const sig = new Uint8Array(await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, key.priv, enc(text)));
  return { 'X-Key': key.pub, 'X-Time': time, 'X-Sig': b64u(sig), ...(meta ? { 'X-Meta': meta } : {}) };
}
export const metaHeader = obj => b64u(enc(JSON.stringify(obj)));

// ---- the identity kept in this browser: { nick, key }
let cached = null;
export async function identity() {
  if (cached) return cached;
  let saved = null; try { saved = JSON.parse(localStorage.getItem(KEY_STORE) || 'null'); } catch { saved = null; }
  if (!saved?.jwk) return null;
  cached = { nick: saved.nick || '', key: await keyFromJwk(saved.jwk) };
  return cached;
}
function keep(nick, jwk) { localStorage.setItem(KEY_STORE, JSON.stringify({ nick, jwk })); }
export async function createIdentity(nick) { const key = await newKey(); keep(nick, key.jwk); cached = { nick, key }; return cached; }
export async function setNick(nick) { const id = await identity(); if (!id) return createIdentity(nick); keep(nick, id.key.jwk); id.nick = nick; return id; }
/** The key as a file the person keeps: whoever has it publishes under this name, so it is private. */
export async function exportIdentity() {
  const id = await identity(); if (!id) return null;
  return JSON.stringify({ format: 'frost-community-key', version: 1, nick: id.nick, jwk: id.key.jwk }, null, 1);
}
export async function importIdentity(text) {
  let j; try { j = JSON.parse(text); } catch { j = null; }
  if (j?.format !== 'frost-community-key' || !j.jwk?.d || !j.jwk.x || !j.jwk.y) throw new Error('This is not a Frost community key file.');
  await keyFromJwk(j.jwk);                                    // refuses a broken key before anything is kept
  keep(j.nick || '', j.jwk); cached = null;
  return identity();
}

// ---- calls to the server
export class CommunityError extends Error { constructor(code, status) { super(code); this.code = code; this.status = status; } }
async function call(path, { method = 'GET', body, meta = '', signed = false, raw = false } = {}) {
  const base = serverUrl();
  if (!base) throw new CommunityError('offline', 0);
  const bytes = body == null ? new Uint8Array(0) : body instanceof Uint8Array ? body : typeof body === 'string' ? enc(body) : new Uint8Array(await body.arrayBuffer());
  let headers = {};
  if (signed) { const id = await identity(); if (!id) throw new CommunityError('no-key', 0); headers = await signHeaders(id.key, method, path, meta, bytes); }
  else if (meta) headers['X-Meta'] = meta;
  if (typeof body === 'string') headers['Content-Type'] = 'application/json';
  let res;
  try { res = await fetch(base + path, { method, headers, body: method === 'GET' ? undefined : bytes }); } catch { throw new CommunityError('network', 0); }
  if (!res.ok) { let code = 'server'; try { code = (await res.json()).error || code; } catch { /* not JSON */ } throw new CommunityError(code, res.status); }
  return raw ? res : res.json();
}
export const feed = ({ kind = 'loop', q = '', before = null } = {}) => call(`/api/feed?kind=${encodeURIComponent(kind)}${q ? '&q=' + encodeURIComponent(q) : ''}${before ? '&before=' + before : ''}`);
export const item = id => call(`/api/item/${encodeURIComponent(id)}`);
export const fileUrl = id => `${serverUrl()}/api/file/${encodeURIComponent(id)}`;
export const pageUrl = id => `${serverUrl()}/i/${encodeURIComponent(id)}`;
/** The file of an item, signed when it is not public yet (only its author and the owner can fetch it then). */
export async function fileBlob(it) {
  const res = await call(`/api/file/${encodeURIComponent(it.id)}`, { signed: it.status && it.status !== 'ok', raw: true });
  return res.blob();
}
export const me = () => call('/api/me', { signed: true });
export async function publish(meta, blob) {
  const id = await identity(); if (!id) throw new CommunityError('no-key', 0);
  return call('/api/publish', { method: 'POST', body: blob, meta: metaHeader({ ...meta, nick: id.nick }), signed: true });
}
export const report = (id, reason = '') => call(`/api/report/${encodeURIComponent(id)}`, { method: 'POST', body: JSON.stringify({ reason }) });
export const remove = id => call(`/api/remove/${encodeURIComponent(id)}`, { method: 'POST', body: '{}', signed: true });
export const queue = () => call('/api/admin/queue', { signed: true });
export const act = (what, target) => call('/api/admin/act', { method: 'POST', body: JSON.stringify({ act: what, ...target }), signed: true });
