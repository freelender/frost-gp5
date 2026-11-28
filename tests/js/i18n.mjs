// Interface languages: the language menu and lang/ folder, then the Portuguese table: it translates interface
// text, keeps values in place, and never touches the pedal's own words or the names a user stores.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const root = new URL('../../web/', import.meta.url);
const app = await fs.readFile(new URL('app.js', root), 'utf8');
const version = app.match(/export const VERSION = '([^']+)'/)[1];
const { makeTranslator, translate, keepName, languages } = await import(new URL(`i18n.js?v=${version}`, root));
const { LANGUAGES } = await import(new URL(`lang/index.js?v=${version}`, root));

// The language menu: English first and needing no file; every other language is one file in lang/ that loads.
assert.equal(LANGUAGES[0].code, 'en');assert.equal(LANGUAGES[0].load, undefined);
assert.deepEqual(languages().map(l => l.code), LANGUAGES.map(l => l.code));
assert.equal(new Set(LANGUAGES.map(l => l.code)).size, LANGUAGES.length);
const TABLES = {};
for (const { code, name, load } of LANGUAGES.slice(1)) {
  assert.ok(name && typeof load === 'function', code);
  const table = (await load()).default;
  assert.ok(table && table.text && table.patterns, `lang/${code}.js exports { text, patterns } as default`);
  TABLES[code] = table;
}
// Any file in lang/ besides index.js is offered in the menu.
for (const file of await fs.readdir(new URL('lang/', root))) if (/\.js$/.test(file) && file !== 'index.js') assert.ok(TABLES[file.replace(/\.js$/, '')], `${file} is not listed in lang/index.js`);
const PT = TABLES['pt-BR'];
const pt = makeTranslator(PT);

// A name the user chose is marked so it is never translated; interface text is unmarked again.
const shown = { dataset: {} };
assert.equal(keepName(shown), shown);assert.equal(shown.dataset.i18nSkip, '');
keepName(shown, false);assert.equal('i18nSkip' in shown.dataset, false);

// Before a language is chosen, nothing is translated.
assert.equal(translate('Connect GP-5'), null);

// Every pattern's translation uses each value it captures, and no value it does not capture (in every language).
for (const [code, table] of Object.entries(TABLES)) for (const [en, out] of Object.entries(table.patterns)) {
  const holes = en.split('{}').length - 1;
  const used = [...out.matchAll(/\{(\d+)\}/g)].map(m => +m[1]);
  assert.ok(holes > 0, `pattern without {}: ${en}`);
  assert.deepEqual([...new Set(used)].sort(), [...Array(holes).keys()], `${code}: ${en} -> ${out}`);
}
for (const [code, table] of Object.entries(TABLES)) for (const [en, out] of Object.entries(table.text)) assert.ok(out.trim(), `${code}: empty translation for ${en}`);

// Exact text, with the spaces around it kept and the spaces inside normalized as the browser shows them.
assert.equal(pt('Connect GP-5'), 'Conectar GP-5');
assert.equal(pt('  Not connected\n'), '  Desconectado\n');
assert.equal(pt('Select a block\n   to edit it.'), 'Selecione um bloco para editar.');

// Values stay in place; a value that is itself interface text is translated too.
assert.equal(pt('Preset 05 loaded.'), 'Preset 05 carregado.');
assert.equal(pt('20 of 60 used'), '20 de 60 usados');
assert.equal(pt('Waiting for "Saving a preset" to finish.'), 'Esperando "Salvando um preset" terminar.');
assert.equal(pt('Preset 07 downloaded with 2 AMP models and 1 cabinet.'), 'Preset 07 baixado com 2 modelos de AMP e 1 gabinete.');
assert.equal(pt('STUDIO / ABOUT & CREDITS'), 'ESTÚDIO / SOBRE E CRÉDITOS');
assert.equal(pt('Slot 3 · Empty · Pedal busy'), 'Bloco 3 · Vazio · Pedal ocupado');
assert.equal(pt('"MY CLEAN" is now preset 04.'), '"MY CLEAN" agora é o preset 04.');
// The most specific pattern wins, whatever the order of the table ("Block {}" also matches this sentence).
assert.equal(pt('Block 1 becomes empty. Like every live edit, this is not saved until you save a preset on the pedal.'),
  'O bloco 1 fica vazio. Como toda edição ao vivo, isso não é salvo até você salvar um preset no pedal.');
assert.equal(pt('Remove MODEL 03?'), 'Remover o MODEL 03?');
assert.equal(pt('Song library · 96 / 3964 song bytes. Connect the pedal to read or save.'), 'Biblioteca de músicas · 96 / 3964 bytes de músicas. Conecte o pedal para ler ou salvar.');

// Text without letters, unknown text and the pedal's own words stay as they are.
for (const keep of ['42', '→', 'Something brand new', 'PRESETS', 'SAVE', 'PRESETS → SAVE', 'DSP LIMIT', 'CHAIN TOO BIG', 'REMOVE AN EFFECT', 'GLOBAL SETTINGS']) assert.equal(pt(keep), null, keep);

// Effect, knob and category names, and the factory preset names, are never translated.
const catalog = JSON.parse(await fs.readFile(new URL('effects.json', root), 'utf8'));
const names = new Set(catalog.categories);
for (const e of Object.values(catalog.effects)) { names.add(e.name); names.add(e.short); for (const p of e.params || []) names.add(p.name); }
const factory = JSON.parse(await fs.readFile(new URL('../../presets/factory.json', import.meta.url), 'utf8'));
JSON.stringify(factory, (k, v) => { if (k === 'name' && typeof v === 'string') names.add(v); return v; });
for (const name of names) if (typeof name === 'string') assert.equal(pt(name), null, `"${name}" belongs to the pedal or the user`);

console.log(`Version ${version}: ${Object.keys(PT.text).length} texts and ${Object.keys(PT.patterns).length} patterns checked; ${names.size} pedal and preset names left untouched`);
