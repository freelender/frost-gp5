import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const root = new URL('../../web/', import.meta.url);
const app = await fs.readFile(new URL('app.js', root), 'utf8');
const version = app.match(/export const VERSION = '([^']+)'/)[1];
const html = await fs.readFile(new URL('index.html', root), 'utf8');
assert.ok(html.includes(`src="app.js?v=${version}"`));
assert.ok(html.includes(`href="style.css?v=${version}"`));
// The entry point alone is insufficient: nested imports must bypass old URLs too.
const seen = new Set();
async function visit(name) {
  if (seen.has(name)) return;
  seen.add(name);
  const source = await fs.readFile(new URL(name, root), 'utf8');
  for (const match of source.matchAll(/['"]\.\/([^/'"?]+\.js)(\?[^'"]*)?['"]/g)) {
    assert.equal(match[2], `?v=${version}`, `${name} imports an unversioned/mixed module ${match[1]}`);
    await visit(match[1]);
  }
}
await visit('app.js');
for (const required of ['store.js', 'setlist.js', 'setlist_view.js', 'pedal.js', 'live.js', 'ir.js']) assert.ok(seen.has(required));
const { Store, GROUPS } = await import(new URL(`store.js?v=${version}`, root));
const { SetlistStore, emptySetlist } = await import(new URL(`setlist.js?v=${version}`, root));
assert.equal(GROUPS.glb.firm, 0x424c4746);
let io = 0;
const files = { files: async () => { io++; return []; }, changed: async () => { io++; }, ask: async () => { io++; } };
assert.deepEqual(await new SetlistStore(new Store(files)).read(), emptySetlist());
io = 0;
const store = new Store(files);
for (const run of [() => store.readGroup('missing', 0), () => store.writeGroup('missing', 0, new Uint8Array(4)), () => store.slot('missing', 0), () => store.putSlot('missing', 0, null)]) await assert.rejects(run, /Unknown storage group.*Reload/);
assert.equal(io, 0, 'invalid storage requests must fail before device access');
console.log(`Version ${version}: complete module graph and setlist storage checks passed`);
