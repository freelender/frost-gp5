// Community server (community/worker.js) with the browser's own signing code (web/community.js): signatures, nicknames
// that belong to one key, the owner's approval of a first upload, reports that hide at once, bans, limits, threads of
// loops and the public page. Runs on Node's SQLite and an in-memory file store, as community/dev.mjs does.
import assert from 'node:assert/strict';
import worker, { LIMITS } from '../../community/worker.js';
import { LocalD1, LocalR2, LocalKV } from '../../community/dev.mjs';
import { newKey, signHeaders, metaHeader } from '../../web/community.js';

const env = { DB: new LocalD1(), FILES: new LocalR2(), SITE_URL: 'https://example.test/site/', ADMIN_KEYS: '', SALT: 'test' };
const enc = s => new TextEncoder().encode(s);
async function call(path, { method = 'GET', key = null, meta = null, body = null, headers = {}, ip = '10.0.0.1', time = null } = {}) {
  const bytes = body == null ? new Uint8Array(0) : typeof body === 'string' ? enc(body) : body;
  const m = meta ? metaHeader(meta) : '';
  const signed = key ? await signHeaders(key, method, path, m, bytes) : m ? { 'X-Meta': m } : {};
  if (time != null && key) { signed['X-Time'] = String(time); }
  const res = await worker.fetch(new Request('http://c.test' + path, { method, headers: { 'CF-Connecting-IP': ip, ...headers, ...signed }, body: method === 'GET' ? undefined : bytes }), env);
  const text = await res.text();
  let json = null; try { json = JSON.parse(text); } catch { /* a page or a file */ }
  return { status: res.status, json, text, headers: res.headers };
}
const preset = (name = 'x') => enc(JSON.stringify({ format: 'frost-preset', version: 1, name, record: 'AAAA', models: [], cabs: [] }));
const wav = (n = 100) => { const b = new Uint8Array(44 + n * 4); b.set(enc('RIFF'), 0); b.set(enc('WAVEfmt '), 8); b.set(enc('data'), 36); return b; };
const up = (key, nick, extra = {}, body = preset(), ip) => call('/api/publish', { method: 'POST', key, body, ip, meta: { kind: 'preset', file: 'p.frostpreset', title: 'Crunch', nick, ...extra } });

const owner = await newKey(), ana = await newKey(), bia = await newKey(), cris = await newKey();
env.ADMIN_KEYS = `${owner.id}`;

// Unsigned, badly signed, too old and replayed requests are refused.
assert.equal((await call('/api/publish', { method: 'POST', body: preset(), meta: { kind: 'preset', file: 'p.frostpreset', title: 'x', nick: 'ana' } })).status, 401);
assert.equal((await call('/api/me', { key: ana, time: Date.now() - 10 * 60e3 })).json.error, 'clock', 'an old request');
assert.equal((await call('/api/me', { key: ana, time: Date.now() - 60e3 })).json.error, 'unsigned', 'the time is part of the signature');
const h = await signHeaders(ana, 'GET', '/api/me');
const once = () => worker.fetch(new Request('http://c.test/api/me', { headers: h }), env);
assert.equal((await once()).status, 200); assert.equal((await once()).status, 401, 'a signature works only once');
const forged = await signHeaders(ana, 'GET', '/api/me'); forged['X-Key'] = bia.pub;
assert.equal((await worker.fetch(new Request('http://c.test/api/me', { headers: forged }), env)).status, 401, 'a signature from another key');

// A first upload waits for the owner; its author still sees it and can fetch the file.
let r = await up(ana, 'Ana Blues');
assert.equal(r.status, 200); assert.equal(r.json.status, 'pending');
const first = r.json.id;
assert.deepEqual((await call('/api/feed?kind=preset')).json.items, [], 'not public before the approval');
assert.equal((await call(`/api/file/${first}`)).status, 401, 'the file of a waiting item needs a signature');
assert.equal((await call(`/api/file/${first}`, { key: bia })).status, 404, 'and only its author or the owner');
assert.equal((await call(`/api/file/${first}`, { key: ana })).status, 200);
r = await call('/api/me', { key: ana });
assert.equal(r.json.nick, 'Ana Blues'); assert.equal(r.json.trusted, false); assert.equal(r.json.items[0].status, 'pending');

// A nickname belongs to the first key that used it (any capitals).
r = await up(bia, 'ana blues'); assert.equal(r.status, 409); assert.equal(r.json.error, 'nick-taken');
assert.equal((await up(bia, 'x')).json.error, 'bad-nick');
assert.equal((await up(bia, 'Bia', { title: '  ' })).json.error, 'no-title');

// Files must be what they say.
assert.equal((await up(bia, 'Bia', {}, enc('{"a":1}'))).json.error, 'bad-file');
assert.equal((await up(bia, 'Bia', { file: 'p.exe' })).json.error, 'bad-file');
assert.equal((await up(bia, 'Bia', { kind: 'cab', file: 'c.wav' }, enc('not a wav at all, sorry................................'))).json.error, 'bad-file');
assert.equal((await up(bia, 'Bia', { kind: 'cab', file: 'c.wav' }, new Uint8Array(2.1e6).fill(1))).json.error, 'too-big');

// Only the owner sees the queue and acts on it; approving trusts the author.
assert.equal((await call('/api/admin/queue', { key: ana })).status, 403);
r = await call('/api/admin/queue', { key: owner });
assert.equal(r.json.items.length, 1); assert.equal(r.json.items[0].id, first);
assert.equal((await call('/api/admin/act', { method: 'POST', key: owner, body: JSON.stringify({ act: 'approve', id: first }) })).json.ok, true);
r = await call('/api/feed?kind=preset'); assert.equal(r.json.items.length, 1); assert.equal(r.json.items[0].nick, 'Ana Blues');
r = await up(ana, 'Ana Blues', { title: 'Lead' }); assert.equal(r.json.status, 'ok', 'after the first approval, uploads are public at once');
const file = await call(`/api/file/${r.json.id}`);
assert.equal(file.headers.get('Content-Type'), 'application/json'); assert.match(file.headers.get('Content-Disposition'), /attachment/);
assert.equal(file.headers.get('X-Content-Type-Options'), 'nosniff');
assert.equal((await call('/api/feed?kind=preset&q=lea')).json.items.length, 1, 'search by title');
assert.equal((await call('/api/feed?kind=preset&q=ana%20b')).json.items.length, 2, 'search by nickname');

// The owner's own uploads never wait.
r = await call('/api/publish', { method: 'POST', key: owner, body: wav(4410), meta: { kind: 'loop', file: 'base.wav', title: 'Blues em A', nick: 'Felipe', bpm: 92, beats: 4, bars: 4, key: 'A' } });
assert.equal(r.json.status, 'ok'); const base = r.json.id;

// A loop thread: a reply waits (Bia is new), then shows inside its loop, not on the loop feed.
r = await call('/api/publish', { method: 'POST', key: bia, body: wav(4410), meta: { kind: 'loop', file: 'take.wav', title: 'Re: Blues em A', nick: 'Bia', bpm: 92, beats: 4, bars: 4, replyTo: base } });
assert.equal(r.json.status, 'pending'); const reply = r.json.id;
assert.equal((await call('/api/publish', { method: 'POST', key: bia, body: wav(10), meta: { kind: 'loop', file: 't.wav', title: 'x', nick: 'Bia', replyTo: 'AAAAAAAAAAAA' } })).json.error, 'not-found');
await call('/api/admin/act', { method: 'POST', key: owner, body: JSON.stringify({ act: 'approve', id: reply }) });
r = await call('/api/feed?kind=loop'); assert.deepEqual(r.json.items.map(i => i.id), [base]); assert.equal(r.json.items[0].replies, 1);
assert.equal(r.json.items[0].bpm, 92); assert.equal(r.json.items[0].key, 'A');
r = await call(`/api/item/${base}`); assert.equal(r.json.replies[0].id, reply); assert.equal(r.json.replies[0].nick, 'Bia');
r = await call(`/api/item/${reply}`); assert.equal(r.json.parent.id, base);
const audio = await call(`/api/file/${base}`);
assert.equal(audio.status, 200); assert.equal(audio.headers.get('Content-Type'), 'audio/wav'); assert.equal(audio.headers.get('Content-Disposition'), null);

// A report hides at once; the owner restores it, and after that reports only count.
assert.equal((await call(`/api/report/${base}`, { method: 'POST', body: JSON.stringify({ reason: 'spam' }), headers: { 'Content-Type': 'application/json' } })).json.ok, true);
assert.equal((await call('/api/feed?kind=loop')).json.items.length, 0);
assert.equal((await call(`/api/item/${base}`)).status, 404, 'a hidden item is not public');
r = await call('/api/admin/queue', { key: owner }); assert.deepEqual(r.json.items.find(i => i.id === base).reasons, ['spam']);
await call('/api/admin/act', { method: 'POST', key: owner, body: JSON.stringify({ act: 'restore', id: base }) });
await call(`/api/report/${base}`, { method: 'POST', body: '{}', headers: { 'Content-Type': 'application/json' }, ip: '10.0.0.2' });
r = await call('/api/feed?kind=loop'); assert.equal(r.json.items.length, 1, 'kept by the owner'); assert.equal(r.json.items[0].reports, 2);

// The public page: Portuguese or English, everything escaped, a link back to the site and a report form.
r = await call('/api/publish', { method: 'POST', key: ana, body: wav(100), meta: { kind: 'loop', file: 'x.wav', title: '<script>alert(1)</script>', nick: 'Ana Blues', bpm: 120, beats: 6 } });
r = await call(`/i/${r.json.id}`, { headers: { 'Accept-Language': 'pt-BR,pt;q=0.9' } });
assert.equal(r.status, 200); assert.ok(!r.text.includes('<script>alert'), 'the title is escaped'); assert.ok(r.text.includes('&lt;script&gt;'));
assert.ok(r.text.includes('Gravar por cima no Frost')); assert.ok(r.text.includes('6/8')); assert.ok(r.text.includes('https://example.test/site/?item='));
assert.match(r.headers.get('Content-Security-Policy'), /default-src 'none'/);
assert.ok((await call(`/i/${base}`)).text.includes('Record over it in Frost'));
assert.ok((await call(`/i/${base}`)).text.includes('Re: Blues em A'), 'the replies are listed');
assert.ok((await call('/i/AAAAAAAAAAAA')).text.includes('This item is not available'));
r = await call(`/i/${reply}/report`, { method: 'POST', ip: '10.0.0.3' }); assert.ok(r.text.includes('hidden until'));
assert.equal((await call(`/api/item/${reply}`)).status, 404);

// An author still waiting for approval cannot pile uploads in the queue.
for (let k = 0; k < 3; k++) assert.equal((await up(cris, 'Cris', { title: 'n' + k })).json.status, 'pending');
assert.equal((await up(cris, 'Cris')).json.error, 'queue-full');

// The author removes their own item; nobody else can.
r = await up(ana, 'Ana Blues', { title: 'Old' }); const old = r.json.id;
assert.equal((await call(`/api/remove/${old}`, { method: 'POST', key: bia, body: '{}' })).status, 403);
assert.equal((await call(`/api/remove/${old}`, { method: 'POST', key: ana, body: '{}' })).json.ok, true);
assert.equal((await call(`/api/file/${old}`)).status, 404);

// A ban hides the author's items and stops new ones.
await call('/api/admin/act', { method: 'POST', key: owner, body: JSON.stringify({ act: 'ban', author: ana.id }) });
assert.equal((await call('/api/feed?kind=preset')).json.items.length, 0);
assert.equal((await up(ana, 'Ana Blues')).json.error, 'banned');
assert.equal((await call('/api/admin/act', { method: 'POST', key: ana, body: JSON.stringify({ act: 'unban', author: ana.id }) })).status, 403);

// Daily limit per author.
const dora = await newKey(); env.ADMIN_KEYS += `,${dora.id}`;   // trusted, so nothing waits
let last; for (let k = 0; k <= LIMITS.author; k++) last = await up(dora, 'Dora', { title: 'p' + k }, preset(), '10.9.0.' + k);
assert.equal(last.json.error, 'limit');

// Without R2 (an account with no card), the files live in Workers KV: the same calls work.
const kvEnv = { DB: new LocalD1(), KV: new LocalKV(), ADMIN_KEYS: owner.id, SALT: 'test' };
const kvCall = async (path, init) => worker.fetch(new Request('http://c.test' + path, init), kvEnv);
const loopBody = wav(50), loopMeta = metaHeader({ kind: 'loop', file: 'k.wav', title: 'KV', nick: 'Felipe' });
let kr = await (await kvCall('/api/publish', { method: 'POST', body: loopBody, headers: await signHeaders(owner, 'POST', '/api/publish', loopMeta, loopBody) })).json();
assert.equal(kr.status, 'ok');
const kvFile = await kvCall('/api/file/' + kr.id);
assert.equal(kvFile.status, 200); assert.deepEqual(new Uint8Array(await kvFile.arrayBuffer()), loopBody, 'the file comes back from KV');
const rm = new TextEncoder().encode('{}');
await kvCall('/api/remove/' + kr.id, { method: 'POST', body: rm, headers: await signHeaders(owner, 'POST', '/api/remove/' + kr.id, '', rm) });
assert.equal((await kvCall('/api/file/' + kr.id)).status, 404, 'and leaves KV when removed');

// Browsers on another site may call it (CORS), including the signed headers.
const pre = await worker.fetch(new Request('http://c.test/api/publish', { method: 'OPTIONS' }), env);
assert.equal(pre.status, 204); assert.equal(pre.headers.get('Access-Control-Allow-Origin'), '*'); assert.match(pre.headers.get('Access-Control-Allow-Headers'), /X-Sig/);
console.log('Community: signatures, nicknames, approval, reports, bans, limits, loop threads, the public page and both file stores (R2, KV) checked');
