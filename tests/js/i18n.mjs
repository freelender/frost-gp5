// Interface language: the Portuguese table translates interface text, keeps values in place, and never
// touches the pedal's own words or the names a user stores.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const root = new URL('../../web/', import.meta.url);
const app = await fs.readFile(new URL('app.js', root), 'utf8');
const version = app.match(/export const VERSION = '([^']+)'/)[1];
const { makeTranslator, translate } = await import(new URL(`i18n.js?v=${version}`, root));
const { PT } = await import(new URL(`i18n_pt.js?v=${version}`, root));
const pt = makeTranslator(PT);

// Before a language is chosen, nothing is translated.
assert.equal(translate('Connect GP-5'), null);

// Every pattern's translation uses each value it captures, and no value it does not capture.
for (const [en, out] of Object.entries(PT.patterns)) {
  const holes = en.split('{}').length - 1;
  const used = [...out.matchAll(/\{(\d+)\}/g)].map(m => +m[1]);
  assert.ok(holes > 0, `pattern without {}: ${en}`);
  assert.deepEqual([...new Set(used)].sort(), [...Array(holes).keys()], `${en} -> ${out}`);
}
for (const [en, out] of Object.entries(PT.text)) assert.ok(out.trim(), `empty translation for ${en}`);

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
