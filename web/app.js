// Frost web: connection, navigation, live editor, AMP models, cabinets, presets, firmware.
// Device protocols live in pedal.js / store.js / live.js / firmware.js; this file is the page glue.
import { MidiPedal, PedalFiles, checkRecord, recordLabel } from './pedal.js?v=1.9.1';
import { Store, PRESETS, parsePresetFile, recordCrc, assetName } from './store.js?v=1.9.1';
import { convertIr, readWav, irWav } from './ir.js?v=1.9.1';
import { LiveView, loadCatalog } from './live.js?v=1.9.1';
import { initFirmware, isBusy } from './firmware.js?v=1.9.1';
import { SetlistView } from './setlist_view.js?v=1.9.1';
import { simulatedPedal } from './sim.js?v=1.9.1';
import { libraryRoute, fileKind, matchingSlots, useLibraryFile } from './model_library.js?v=1.9.1';
import { startI18n, keepName } from './i18n.js?v=1.9.1';
import { startSkins } from './skin.js?v=1.9.1';
import { detectFrost } from './onboarding.js?v=1.9.1';

export const VERSION = '1.9.1';
const $ = id => document.getElementById(id);
const pad2 = i => String(i).padStart(2, '0');
const state = {
  mode: 'off',        // off | connecting | frost | demo
  checking: false, detectedFrost: false,
  op: null,           // a running device operation ('firmware', 'Writing MODEL 03', ...): blocks conflicting ones
  pedal: null, files: null, store: null, models: {}, cabs: {}, presets: {},
  record: null, nam: null, worker: null, cab: null, dirty: false, train: null,
};
$('version').textContent = VERSION;
startI18n();
startSkins();

// ---------- feedback: status, toasts, banner, dialog ----------
function status(kind, text) { $('status').dataset.kind = kind; $('status-text').textContent = text; }
function showStatus() {
  if (state.op) return status('busy', state.op === 'firmware' ? 'Installing firmware…' : state.op + '…');
  if (state.mode === 'connecting') return status('busy', 'Connecting…');
  if (state.mode === 'frost') return status('ok', 'Connected · Frost');
  if (state.mode === 'demo') return status('demo', 'Demo · simulated pedal');
  if (state.detectedFrost) return status('ok', 'Frost detected · ready to connect');
  status($('status').dataset.kind === 'error' ? 'error' : 'off', $('status').dataset.kind === 'error' ? $('status-text').textContent : 'Not connected');
}
function toast(text, kind = '') {
  const t = document.createElement('div'); t.className = 'toast ' + kind; t.textContent = text; t.setAttribute('role', kind === 'bad' ? 'alert' : 'status');
  $('toasts').appendChild(t); setTimeout(() => t.remove(), kind === 'bad' ? 7000 : 4200);
}
function notice(text, { firmware = false, title = 'Something went wrong' } = {}) {
  $('notice-text').textContent = text || ''; $('notice').hidden = !text;
  $('notice-title').textContent = title;
  $('notice').className = 'banner ' + (firmware ? 'warn' : 'bad');
  $('notice-firmware').hidden = !text || !firmware;
}
$('notice-close').onclick = () => notice('');
/** Promise<bool> (or the typed text when `input` is given, or the chosen numbers
 * when `selects` [{label, options: [[value, text]], value}] is given; null on cancel). */
function ask(title, text, ok = 'OK', { danger = false, input = null, selects = null } = {}) {
  const d = $('confirm');
  $('confirm-title').textContent = title; $('confirm-text').textContent = text;
  $('confirm-ok').textContent = ok; $('confirm-ok').className = 'btn ' + (danger ? 'danger' : 'primary');
  $('confirm-input-label').hidden = !input;
  $('confirm-input').setCustomValidity('');
  if (input) { $('confirm-input-name').textContent = input.label; $('confirm-input').value = input.value; $('confirm-input').maxLength = input.maxLength ?? 16; }
  const box = $('confirm-selects'); box.textContent = ''; box.hidden = !selects;
  const picks = (selects || []).map(s => {
    const label = document.createElement('label'), name = document.createElement('span'), sel = document.createElement('select');
    name.className = 'inputlabel'; name.textContent = s.label;
    for (const [v, t] of s.options) sel.add(new Option(t, v));
    sel.value = String(s.value); label.append(name, sel); box.append(label); return sel;
  });
  d.returnValue = '';
  // Resolved from the buttons themselves (the dialog's close event alone proved unreliable),
  // with Escape (cancel) and a close from elsewhere as fallbacks; settles exactly once.
  return new Promise(resolve => {
    let settled = false;
    const finish = yes => {
      if (settled) return;
      if (yes && input?.validate) {
        const error = input.validate($('confirm-input').value);
        $('confirm-input').setCustomValidity(error || '');
        if (error) { $('confirm-input').reportValidity(); return; }
      }
      settled = true;
      d.onclose = d.oncancel = $('confirm-ok').onclick = $('confirm-cancel').onclick = null;
      const value = input ? (yes ? $('confirm-input').value : null) : selects ? (yes ? picks.map(p => Number(p.value)) : null) : yes;
      if (d.open) d.close(yes ? 'ok' : 'cancel');
      resolve(value);
    };
    $('confirm-ok').onclick = e => { e.preventDefault(); finish(true); };
    $('confirm-cancel').onclick = e => { e.preventDefault(); finish(false); };
    d.oncancel = () => finish(false);
    $('confirm-input').onkeydown = e => { if (e.key === 'Enter') { e.preventDefault(); finish(true); } };   // Enter renames
    $('confirm-input').oninput = () => $('confirm-input').setCustomValidity('');
    d.onclose = () => finish(d.returnValue === 'ok');
    if (!d.open) d.showModal();
    (input ? $('confirm-input') : picks[0] || $('confirm-ok')).focus();
  });
}

// ---------- navigation ----------
const TITLES = { live: 'EDITOR', models: 'NAMS & IRS', presets: 'PRESETS', setlists: 'SETLISTS', firmware: 'FIRMWARE', about: 'ABOUT & CREDITS' };
let libraryKind = 'amps';
function show(view, focus = false) {
  const route = libraryRoute(view); view = route.view;
  if (route.kind) libraryKind = route.kind;
  if (!TITLES[view]) view = 'live';
  for (const s of document.querySelectorAll('.view')) s.hidden = s.id !== 'view-' + view;
  for (const a of document.querySelectorAll('[data-view]')) { const on = a.dataset.view === view; a.classList.toggle('active', on); if (on) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current'); }
  for (const kind of ['amps', 'cabs']) {
    $('models-' + kind).hidden = libraryKind !== kind;
    $('models-tab-' + kind).setAttribute('aria-pressed', libraryKind === kind);
  }
  $('breadcrumb').textContent = 'STUDIO / ' + TITLES[view];
  if (focus) $('content').focus({ preventScroll: true });
}
const go = view => { if (location.hash !== '#' + view) location.hash = view; else show(view); };
addEventListener('hashchange', () => { show(location.hash.slice(1), true); scrollTo({ top: 0 }); });
show(location.hash.slice(1));

// ---------- connection ----------
const live = new LiveView($('chain-strip'), $('inspector'), {
  models: () => state.models,
  cabs: () => state.cabs,
  addFile: (kind, slot) => openImport(kind, slot),
  state: t => { $('live-state').textContent = t; },
  edited: () => { if (!state.dirty) { state.dirty = true; renderDirty(); } },
  refused: t => toast(t, 'bad'),
  limit: kind => limitPopup(kind),
  changed: () => { if (!state.op) refreshCurrent(); },   // the chain changed (maybe a preset switched on the pedal)
  confirm: (title, text) => ask(title, text, 'Remove', { danger: true }),
  lost: () => lost(),
});
function renderDirty() {
  const p = $('edit-state'), s = $('save-state'), live = state.mode === 'frost' || state.mode === 'demo';
  p.hidden = !live;
  p.className = 'pill ' + (state.dirty ? 'warn' : 'ok');
  p.textContent = state.dirty ? '● UNSAVED LIVE CHANGES' : state.mode === 'demo' ? 'DEMO' : '● LIVE';
  s.className = 'savestate' + (state.dirty ? ' dirty' : '');
  s.textContent = state.dirty ? 'You changed the chain from this page. To keep it, save it as a preset:' : 'No changes from this page yet. To save the current sound:';
}
function setLivePanel() {
  const on = live.attached;
  $('live-empty').hidden = on; $('live-panel').hidden = !on;
  renderDirty();
}
const setlists = new SetlistView($('setlist-editor'), {
  confirm: (title, text, ok, opts) => ask(title, text, ok, opts), exclusive, error: message => notice(message), saved: () => toast('Songs and setlist saved. On the pedal: GLOBAL SETTINGS → DISPLAY → HOME SCREEN → SETLIST.', 'ok'),
  removed: () => toast('Setlist removed from the pedal.', 'ok'),
  download: (text, name) => save(new TextEncoder().encode(text), name),
});
async function connect() {
  if (state.checking || state.mode === 'connecting') return;
  if (state.op || isBusy()) { toast('Wait until the running operation finishes.', 'bad'); return; }
  disconnect(); notice(''); state.mode = 'connecting'; showStatus(); updateButtons();
  try {
    const pedal = await MidiPedal.connect(), files = new PedalFiles(p => pedal.ask(p));
    const who = await files.who();
    if (!who || who.firmware !== 'frost') {
      pedal.close(); state.mode = 'off';
      status('error', who ? 'Frost not installed' : 'Frost not detected');
      notice(who
        ? 'This pedal is not running Frost. Open Firmware to install it, then return here to connect the editor. Nothing is installed until you confirm.'
        : 'The pedal did not answer the Frost editor. Still using Valeton stock firmware? Open Firmware to install Frost first. If Frost is already installed, check power and USB, close other pedal editors, then reconnect.',
        { firmware: true, title: who ? 'Install Frost to use the editor' : 'Need help connecting?' });
      if (who) go('firmware');
      return;
    }
    pedal.onlost = () => lost();
    Object.assign(state, { pedal, files, store: who.storage >= 2 ? new Store(files) : null, mode: 'frost', dirty: false });
    showStatus(); toast('Connected to the GP-5.', 'ok');
    if (state.store) await state.store.migrate();
    if (!(await live.attach(p => pedal.ask(p)))) notice('This Frost version has no live editing yet (needs 31y18 or later). Install the latest Frost from the Firmware page.');
    setLivePanel();
    await refreshAll();
  } catch (e) {
    disconnect(); status('error', 'Connection failed'); notice(e.message || String(e));
  } finally { if (state.mode === 'connecting') state.mode = 'off'; showStatus(); updateButtons(); }
}
function disconnect() {
  live.detach(); if (state.pedal) state.pedal.close();
  for (const kind of ['amps', 'cabs']) {
    delete importTargets[kind]; delete uploadedTargets[kind]; $(libraryPrefix(kind) + '-result').hidden = true;
  }
  Object.assign(state, { pedal: null, files: null, store: null, mode: 'off', detectedFrost: false, models: {}, cabs: {}, presets: {}, dirty: false });
  setlists.attach(null, {}); setLivePanel(); renderLists(); updateButtons();
}
function lost() {
  if (state.mode !== 'frost' || state.op === 'firmware') return;
  const wasBusy = state.op; disconnect(); status('error', 'Pedal disconnected'); showStatus();
  notice(wasBusy ? `The pedal stopped answering during "${wasBusy}". The transfer did not finish: reconnect and try again.` : 'The pedal stopped answering (unplugged or switched off). Connect it again to continue.');
}
async function demo(open = true) {
  if (state.op || isBusy()) return;
  disconnect(); notice('');
  const sim = simulatedPedal(await loadCatalog());
  state.mode = 'demo'; await live.attach(q => sim.ask(q));
  setLivePanel(); renderLists(); showStatus(); updateButtons(); if (open) go('live');
  toast('Demo: a simulated pedal. Nothing is sent to a real GP-5.');
}
$('connect').onclick = connect;
document.querySelector('[data-connect]').onclick = connect;
$('demo').onclick = () => demo();

/** Run a device operation exclusively: other writes, connect and firmware installs wait for it. */
/** refresh: what to read back afterwards (default everything; preset work reads only the presets). */
async function exclusive(what, fn, refresh = refreshAll) {
  if (state.op || isBusy()) { toast('Another operation is running. Wait until it finishes.', 'bad'); return false; }
  state.op = what; live.pause(true); showStatus(); updateButtons();
  try { await fn(); return true; }
  catch (e) { notice(`${what} failed: ${e.message || e}`); toast(`${what} failed.`, 'bad'); return false; }
  finally { state.op = null; live.pause(false); showStatus(); if (state.store) await refresh(); updateButtons(); }
}

initFirmware({
  blocked: () => state.op ? `Wait until "${state.op}" finishes before installing firmware.` : null,
  confirm: (title, text, ok) => ask(title, text, ok),
  before: async () => { disconnect(); state.op = 'firmware'; showStatus(); updateButtons(); },
  after: async mode => { if (mode) setTimeout(connect, 2500); },
  done: (ok, text) => { state.op = null; showStatus(); updateButtons(); toast(text, ok ? 'ok' : 'bad'); },
});

async function refreshAll() {
  if (!state.store) { renderLists(); return; }
  try {
    const { models } = await state.store.amps(); state.models = models;
    state.cabs = await state.store.irs();
    state.presets = await state.store.presets();
    try { state.current = (await live.live?.presetStatus())?.current; } catch { state.current = null; }
    await setlists.attach(state.store, state.presets);
  } catch (e) { notice('Could not read the pedal: ' + (e.message || e)); }
  renderLists();
}
function renderLists() { renderAmps(); renderCabs(); renderPresets(); fillSlots(); fillPresetSlots(); renderPresetNav(); }
// beta.12: preset work reads back only the presets (the AMP and cabinet groups are
// most of the storage and took seconds to read after every move).
async function refreshPresets() {
  if (!state.store) { renderLists(); return; }
  try {
    state.presets = await state.store.presets();
    await readCurrent();
    await setlists.attach(state.store, state.presets);
  } catch (e) { notice('Could not read the pedal: ' + (e.message || e)); }
  renderPresets(); fillPresetSlots(); renderPresetNav();
}
async function readCurrent() { try { state.current = (await live.live?.presetStatus())?.current; } catch { state.current = null; } }
async function refreshCurrent() {
  const was = state.current; await readCurrent();
  if (state.current !== was) { renderPresets(); fillPresetSlots(); renderPresetNav(); }
}
/** The pedal's own refusal popup, mirrored (slot_editor.h popup: CHAIN TOO BIG / DSP LIMIT). */
let limitTimer = null;
function limitPopup(kind) {
  const p = $('limit-popup');
  $('limit-title').textContent = kind === 1 ? 'CHAIN TOO BIG' : 'DSP LIMIT';
  $('limit-text').textContent = kind === 1 ? "These effects do not fit together in the pedal's effect memory. Remove an effect or choose a smaller one."
    : 'Together these effects would overload the audio processor. Remove an effect or choose a lighter one. The block stays as it was.';
  p.hidden = false; clearTimeout(limitTimer); limitTimer = setTimeout(() => { p.hidden = true; }, 4000);
}
$('limit-close').onclick = () => { clearTimeout(limitTimer); $('limit-popup').hidden = true; };

// ---------- shared list rendering ----------
const offlineText = () => state.mode === 'demo' ? 'The demo has no storage. Connect a pedal running Frost to see it.' : 'Connect a pedal running Frost to see what is on it.';
function list(ul, rows, empty) {
  ul.textContent = '';
  if (!rows.length) { const li = document.createElement('li'); li.className = 'empty'; li.textContent = empty; ul.appendChild(li); return; }
  for (const { tag, name, sub, buttons, current, own } of rows) {   // own: the name is one the user chose
    const li = document.createElement('li'); if (current) { li.className = 'current'; li.setAttribute('aria-current', 'true'); }
    const icon = document.createElement('div'); icon.className = 'libicon'; icon.textContent = tag;
    const info = document.createElement('div'); info.className = 'info';
    const b = keepName(document.createElement('b'), own); b.textContent = name; const s = document.createElement('small'); s.textContent = sub;
    info.append(b, s);
    const btns = document.createElement('div'); btns.className = 'btns';
    for (const [label, cls, fn, aria, off] of buttons) {
      const btn = document.createElement('button'); btn.className = 'btn small ' + (cls || 'ghost'); btn.textContent = label;
      if (off) btn.dataset.off = '1';   // permanently unavailable here (e.g. ↑ on the first row): keeps the column layout
      btn.disabled = !!state.op || !!off;
      if (aria) btn.setAttribute('aria-label', aria);
      btn.onclick = fn; btns.appendChild(btn);
    }
    li.append(icon, info, btns); ul.appendChild(li);
  }
}

// ---------- AMP models ----------
const libraryPrefix = kind => kind === 'amps' ? 'amp' : 'cab';
const importTargets = {}, uploadedTargets = {};
for (const kind of ['amps', 'cabs']) {
  const prefix = libraryPrefix(kind);
  $('models-tab-' + kind).onclick = () => go('models/' + kind);
  $(prefix + '-add').onclick = () => openImport(kind);
  $(prefix + '-close').onclick = () => {
    $(prefix + '-import').hidden = true; $(prefix + '-add').setAttribute('aria-expanded', 'false'); $(prefix + '-add').focus();
  };
  $(prefix + '-search').oninput = () => kind === 'amps' ? renderAmps() : renderCabs();
}
for (const button of document.querySelectorAll('[data-editor]')) button.onclick = () => go('live');
function captureImportTarget(kind, slot = live.selected) {
  return live.attached && live.chainState?.types[slot] === (kind === 'amps' ? 33 : 3) ? { session: live.live, slot } : null;
}
function openImport(kind, slot) {
  importTargets[kind] = captureImportTarget(kind, slot);
  go('models/' + kind);
  const prefix = libraryPrefix(kind);
  $(prefix + '-import').hidden = false; $(prefix + '-add').setAttribute('aria-expanded', 'true');
  $(prefix + '-file').focus();
}
function uploaded(kind, number, label) {
  const prefix = libraryPrefix(kind), target = importTargets[kind] || captureImportTarget(kind);
  uploadedTargets[kind] = { target, number };
  $(prefix + '-result').hidden = false;
  $(prefix + '-result-name').textContent = `${label} · ${kind === 'amps' ? 'MODEL' : 'CAB'} ${pad2(number)} is ready`;
  $(prefix + '-use').hidden = !target;
  $(prefix + '-use').onclick = () => useUploaded(kind);
  $(prefix + '-import').hidden = true; $(prefix + '-add').setAttribute('aria-expanded', 'false');
  $(prefix + '-result').scrollIntoView({ block: 'nearest' });
}
async function useUploaded(kind) {
  const { target, number } = uploadedTargets[kind] || {};
  if (state.op || isBusy()) { toast('Wait for the current operation to finish.', 'bad'); return; }
  try { await useLibraryFile(live, target, kind, number); go('live'); }
  catch (e) { toast(e.message || 'Could not select that file. Try again in the Editor.', 'bad'); }
}
async function takeLibraryFile(file) {
  $('view-models').classList.remove('over');
  if (state.op || isBusy()) { toast('Wait for the current operation to finish.', 'bad'); return; }
  try {
    const kind = fileKind(file.name);
    if ($('view-models').hidden || libraryKind !== kind || $(libraryPrefix(kind) + '-import').hidden) openImport(kind);
    await (kind === 'amps' ? takeAmpFile(file) : takeCabFile(file));
  } catch (e) { toast(e.message || 'Could not read that file.', 'bad'); }
}
dropZone($('view-models'), $('model-file'), takeLibraryFile);
$('model-import').onclick = () => $('model-file').click();
/** A file-name-safe lower-case slug, e.g. "Plexi Crunch!" -> "plexi-crunch". */
const slug = (name, none) => (name || '').replace(/[^A-Za-z0-9]+/g, '-').replace(/^-|-$/g, '').toLowerCase() || none;
/** Read one stored AMP model or cabinet from the pedal and save it as a file. */
function downloadStored(what, read, bytes, file) {
  return exclusive(`Downloading ${what}`, async () => {
    const r = await read(); if (!r) throw new Error(`${what} could not be read`);
    save(bytes(r), file(recordLabel(r))); toast(`${what} downloaded.`, 'ok');
  }, async () => {});
}
async function renameStored(kind, index) {
  const store = state.store, entry = (kind === 'amp' ? state.models : state.cabs)[index];
  if (!store || !entry || state.op || isBusy()) return;
  const what = `${kind === 'amp' ? 'MODEL' : 'CAB'} ${pad2(index + (kind === 'ir' ? 1 : 0))}`;
  const name = await ask(`Rename ${what}`, 'Change the name stored on the pedal. The slot number and sound stay the same.', 'Save', {
    input: { label: 'Name (up to 15 characters)', value: entry.label || '', maxLength: 15,
      validate: value => { try { assetName(value); return ''; } catch (e) { return e.message; } } },
  });
  if (name == null || assetName(name) === entry.label) return;
  if (state.store !== store) { toast('The pedal connection changed. Reopen Rename to try again.', 'bad'); return; }
  if (await exclusive(`Renaming ${what}`, () => kind === 'amp' ? store.ampRename(index, name, entry.crc) : store.irRename(index, name, entry.crc)))
    toast(`${what} renamed to "${assetName(name)}".`, 'ok');
}
function renderAmps() {
  const all = Object.keys(state.models), idx = matchingSlots(state.models, $('amp-search').value);
  list($('amp-list'), idx.map(i => ({ tag: pad2(i), name: state.models[i].label || '(no name)', own: !!state.models[i].label, sub: `MODEL ${pad2(i)}`, buttons: [
    ['Rename', 'ghost', () => renameStored('amp', i), `Rename MODEL ${pad2(i)}`],
    ['Download', 'ghost', () => downloadStored(`MODEL ${pad2(i)}`, () => state.store.ampGet(i), r => r, l => `model-${pad2(i)}-${slug(l, 'amp')}.fcm`), `Download MODEL ${pad2(i)} as an .fcm file`],
    ['Remove', 'danger', async () => {
    if (await ask(`Remove MODEL ${pad2(i)}?`, `"${state.models[i].label}" is deleted from the pedal. An AMP block set to it plays the default model.`, 'Remove', { danger: true }))
      if (await exclusive(`Removing MODEL ${pad2(i)}`, () => state.store.ampRemove(i))) toast(`MODEL ${pad2(i)} removed.`, 'ok');
  }, `Remove MODEL ${pad2(i)}`]] })), state.store ? (all.length ? 'No matching models.' : 'No amp models yet. Choose Add model to import one.') : offlineText());
  $('amps-summary').textContent = state.store ? `${all.length} of 63 used · MODEL 00 is the built-in amp` : 'Not connected';
}
function fillSlots() {
  const fill = (sel, n, first, used, fmt) => {
    const keep = sel.value; sel.textContent = '';
    for (let i = first; i < n; i++) sel.add(new Option(`${fmt(i)}${used[i] ? ' — replaces ' + used[i].label : ' — free'}`, i));
    const free = [...Array(n).keys()].find(i => i >= first && !used[i]);
    sel.value = keep && sel.querySelector(`option[value="${keep}"]`) ? keep : (free ?? first);
  };
  fill($('amp-slot'), 64, 1, state.models, pad2);
  fill($('cab-slot'), 63, 0, state.cabs, i => pad2(i + 1));
}
function dropZone(zone, input, take) {
  for (const ev of ['dragenter', 'dragover']) zone.addEventListener(ev, e => { e.preventDefault(); zone.classList.add('over'); });
  for (const ev of ['dragleave', 'drop']) zone.addEventListener(ev, e => { e.preventDefault(); zone.classList.remove('over'); });
  zone.addEventListener('drop', e => { e.stopPropagation(); const f = e.dataTransfer.files[0]; if (f) take(f); });
  input.onchange = e => { const f = e.target.files[0]; e.target.value = ''; if (f) take(f); };
}
async function takeAmpFile(f) {
  if (state.worker) { toast('A conversion is running. Cancel it first.', 'bad'); return; }
  state.record = null; state.nam = null; state.converted = false; $('amp-quality').hidden = true; $('amp-progress').hidden = true;
  $('amp-dropname').textContent = f.name;
  const info = $('amp-info'); info.hidden = false;
  try {
    if (/\.fcm$/i.test(f.name)) {
      const r = new Uint8Array(await f.arrayBuffer()); checkRecord(r);
      state.record = r; $('amp-name').value = recordLabel(r); info.textContent = `${f.name}: ready-made AMP model, ready to write.`;
    } else if (/\.nam$/i.test(f.name)) {
      const json = JSON.parse(await f.text()), { loadNam } = await import('./converter/nam.js'), { info: m, sampleRate } = loadNam(json);
      state.nam = json; $('amp-name').value = f.name.replace(/\.nam$/i, '').slice(0, 15).toUpperCase();
      info.textContent = `${f.name}: NAM A2 capture, ${m.layers} layers, ${sampleRate / 1000} kHz. Press Convert.`;
    } else throw new Error(`${f.name} is not a .nam or .fcm file.`);
  } catch (e) {
    info.textContent = e instanceof SyntaxError ? `${f.name} is not a readable NAM file.` : e.message || 'Not a readable NAM file.';
    toast('That file cannot be used.', 'bad');
  }
  updateButtons();
}
dropZone($('amp-drop'), $('amp-file'), takeLibraryFile);
$('amp-name').oninput = () => { $('amp-name').value = $('amp-name').value.replace(/[^\x20-\x7e]/g, ''); };
$('amp-convert').onclick = () => {
  if (!state.nam || state.worker) return;
  const name = ($('amp-name').value || 'AMP').slice(0, 15), workers = Math.max(1, Math.min(4, (navigator.hardwareConcurrency || 4) - 1));
  const w = new Worker(new URL('./convert_worker.js?v=1.9.1', import.meta.url), { type: 'module' }); state.worker = w;
  const t0 = performance.now(), prog = (s, v) => { $('amp-progress').hidden = false; $('amp-bar').style.width = `${Math.round(100 * v)}%`; $('amp-stage').textContent = `${s} · ${Math.round((performance.now() - t0) / 1000)} s`; };
  const stop = () => { state.worker = null; w.terminate(); updateButtons(); };
  prog('Starting', 0);
  w.onmessage = e => {
    const m = e.data;
    if (m.type === 'info' && m.info?.input) {
      const c = m.info.input;
      $('amp-info').textContent += c.applied ? ` Input level matched to the GP-5 (capture calibrated at +${c.captureDbu} dBu: ${c.db >= 0 ? '+' : ''}${c.db.toFixed(1)} dB).` : ' The capture has no input calibration, so it is used at its own level.';
    } else if (m.type === 'progress') prog(m.stage === 'teacher' ? 'Rendering the capture' : 'Fitting the model', m.stage === 'teacher' ? 0.35 * m.value : m.stage === 'baseline' ? 0.37 : 0.4 + 0.52 * m.value);
    else if (m.type === 'done') {
      state.record = new Uint8Array(m.record); state.converted = true; prog('Done', 1); stop();
      $('amp-quality').hidden = false; $('amp-quality').textContent = `Difference from the capture: ${m.quality.mel_db.toFixed(2)} dB (lower is closer; 1.5–3 dB sounds very similar).`;
      toast('Conversion finished. Write it to the pedal when ready.', 'ok');
    } else if (m.type === 'error') { notice('Conversion failed: ' + m.text); $('amp-progress').hidden = true; stop(); }
  };
  w.onerror = e => { notice('Conversion failed: ' + (e.message || 'worker error')); $('amp-progress').hidden = true; stop(); };
  const t = state.train, trainAudio = t ? { x: Float32Array.from(t.x), rate: t.rate } : null;
  w.postMessage({ nam: state.nam, name, workers, trainAudio }, trainAudio ? [trainAudio.x.buffer] : []); updateButtons();
};
// Training audio: the user's own copy of NAM's official input.wav (not redistributable, so not bundled).
function renderTrain() {
  const t = state.train, s = $('train-state');
  s.textContent = t ? `${t.name} · ${Math.round(t.x.length / t.rate)} s at ${t.rate / 1000} kHz` : 'Not chosen · built-in test audio is used';
  s.className = 'trainstate' + (t ? ' ok' : ''); $('train-clear').hidden = !t;
  $('train-pick').firstChild.textContent = t ? 'Change' : 'Choose input.wav';
}
$('train-file').onchange = async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  try {
    const { rate, x } = readWav(new Uint8Array(await f.arrayBuffer()), 600, 'Training audio');
    if (x.length < rate * 20) throw new Error('Training audio should be at least 20 seconds long.');
    state.train = { name: f.name, rate, x: Float32Array.from(x) };
    if (!/input.*\.wav$/i.test(f.name)) toast('Using your own training audio. The official NAM input.wav is recommended.');
    else toast('Training audio set: conversions now use input.wav.', 'ok');
  } catch (err) { toast(`${f.name}: ${err.message}`, 'bad'); }
  renderTrain();
};
$('train-clear').onclick = () => { state.train = null; renderTrain(); };
$('amp-cancel').onclick = () => { if (state.worker) { state.worker.terminate(); state.worker = null; $('amp-progress').hidden = true; toast('Conversion cancelled.'); updateButtons(); } };
$('amp-download').onclick = () => {
  if (!state.record) return; const label = recordLabel(state.record);
  save(state.record, `${slug(label, 'amp')}.fcm`); toast(`"${label}" saved as an .fcm file.`, 'ok');
};
$('amp-write').onclick = async () => {
  const i = Number($('amp-slot').value), old = state.models[i], label = recordLabel(state.record);
  if (!(await ask(`Write MODEL ${pad2(i)}?`, `"${label}" is written to the pedal as MODEL ${pad2(i)}${old ? `, replacing "${old.label}"` : ''}.`, 'Write'))) return;
  const ok = await exclusive(`Writing MODEL ${pad2(i)}`, () => state.store.ampPut(i, state.record));
  if (ok) { state.record = null; state.nam = null; state.converted = false; $('amp-info').textContent = `Written as MODEL ${pad2(i)}.`; $('amp-quality').hidden = true; $('amp-progress').hidden = true; $('amp-dropname').textContent = 'Drop a .nam or .fcm file'; toast(`"${label}" is now MODEL ${pad2(i)}.`, 'ok'); uploaded('amps', i, label); }
  updateButtons();
};

// ---------- cabinets ----------
function renderCabs() {
  const all = Object.keys(state.cabs), idx = matchingSlots(state.cabs, $('cab-search').value, 1);
  list($('cab-list'), idx.map(i => ({ tag: pad2(i + 1), name: state.cabs[i].label, own: true, sub: `CAB ${pad2(i + 1)}`, buttons: [
    ['Rename', 'ghost', () => renameStored('ir', i), `Rename CAB ${pad2(i + 1)}`],
    ['Download', 'ghost', () => downloadStored(`CAB ${pad2(i + 1)}`, () => state.store.irGet(i), irWav, l => `cab-${pad2(i + 1)}-${slug(l, 'cab')}.wav`), `Download CAB ${pad2(i + 1)} as a .wav file`],
    ['Remove', 'danger', async () => {
    if (await ask(`Remove CAB ${pad2(i + 1)}?`, `"${state.cabs[i].label}" is deleted from the pedal. IR blocks set to it play the built-in cabinet until it is filled again.`, 'Remove', { danger: true }))
      if (await exclusive(`Removing CAB ${pad2(i + 1)}`, () => state.store.irRemove(i))) toast(`CAB ${pad2(i + 1)} removed.`, 'ok');
  }, `Remove CAB ${pad2(i + 1)}`]] })), state.store ? (all.length ? 'No matching cabinets.' : 'No cabinets yet. Choose Add cabinet to import one.') : offlineText());
  $('cabs-summary').textContent = state.store ? `${all.length} of 63 used · CAB 00 is the built-in cabinet` : 'Not connected';
}
let cabSource = null;
function processCab() {
  try { state.cab = convertIr(cabSource, $('cab-name').value || 'CAB'); const i = state.cab.info; $('cab-info').textContent = `${i.sourceRate} Hz → 44.1 kHz, 512 taps. Ready to write.`; }
  catch (e) { state.cab = null; $('cab-info').textContent = e.message; }
  $('cab-info').hidden = false; updateButtons();
}
async function takeCabFile(f) {
  $('cab-dropname').textContent = f.name;
  cabSource = new Uint8Array(await f.arrayBuffer()); $('cab-name').value = f.name.replace(/\.wav$/i, '').slice(0, 15).toUpperCase(); processCab();
  if (!state.cab) toast('That file is not a usable impulse response.', 'bad');
}
dropZone($('cab-drop'), $('cab-file'), takeLibraryFile);
$('cab-name').onchange = () => { if (cabSource) processCab(); };
$('cab-write').onclick = async () => {
  if(!await live.live?.hasCabControl()){notice('Update the pedal to the IR-control firmware (31z5 or later) before writing level-calibrated cabinets.');return;}
  const i = Number($('cab-slot').value), old = state.cabs[i], label = recordLabel(state.cab.record);
  if (!(await ask(`Write CAB ${pad2(i + 1)}?`, `"${label}" is written to the pedal as CAB ${pad2(i + 1)}${old ? `, replacing "${old.label}"` : ''}.`, 'Write'))) return;
  const ok = await exclusive(`Writing CAB ${pad2(i + 1)}`, () => state.store.irPut(i, state.cab.record));
  if (ok) { state.cab = null; cabSource = null; $('cab-info').textContent = `Written as CAB ${pad2(i + 1)}.`; $('cab-dropname').textContent = 'Drop a .wav impulse response'; toast(`"${label}" is now CAB ${pad2(i + 1)}.`, 'ok'); uploaded('cabs', i + 1, label); }
  updateButtons();
};

// ---------- presets ----------
/** Load preset i (0..59) on the pedal; asks first when live edits would be lost. */
async function loadPreset(i) {
  if (state.dirty && !await ask(`Load preset ${pad2(i + 1)}?`, 'Unsaved live edits will be replaced.', 'Load')) return false;
  const ok = await exclusive('Loading preset', async () => {
    try { await live.live.loadPreset(i); } finally { await live.ackRefusals(); }   // its own error says why; no second popup
    state.dirty = false; renderDirty();
    const c = await live.live.chain(); if (c) await live.render(c);
  }, refreshCurrent);
  if (ok) toast(`Preset ${pad2(i + 1)} loaded.`, 'ok');
  return ok;
}
/** Previous/next stored preset from the current one, wrapping like the pedal's PRESET home screen. */
function presetStep(dir) {
  const used = Object.keys(state.presets).map(Number).sort((a, b) => a - b);
  if (!used.length) return null;
  const from = state.current != null && state.current < PRESETS ? state.current : dir > 0 ? -1 : PRESETS;
  return dir > 0 ? (used.find(i => i > from) ?? used[0]) : ([...used].reverse().find(i => i < from) ?? used[used.length - 1]);
}
function renderPresetNav() {
  const nav = $('preset-nav'), on = !!state.store && live.attached, cur = state.current, p = cur != null ? state.presets[cur] : null;
  nav.hidden = !on; if (!on) return;
  keepName($('preset-nav-name'), !!(cur != null && cur < PRESETS && p?.name));
  $('preset-nav-name').textContent = cur != null && cur < PRESETS ? `${pad2(cur + 1)} · ${p ? p.name || '(no name)' : 'empty'}` : 'No preset';
  const none = !Object.keys(state.presets).length || !!state.op;
  $('preset-prev').disabled = $('preset-next').disabled = none;
  $('preset-nav-open').disabled = !!state.op;
  if (!pick.el.hidden) { if (state.op) pickOpen(false); else pickRender(); }
}
// Preset picker: the live editor's preset name opens a searchable list; one click loads.
const pick = { el: $('preset-pick'), list: $('preset-pick-list'), search: $('preset-pick-search'), active: -1, rows: [] };
function pickRender() {
  const q = pick.search.value.trim().toLowerCase(), all = Object.keys(state.presets).map(Number).sort((a, b) => a - b);
  pick.rows = all.filter(i => !q || (state.presets[i].name || '').toLowerCase().includes(q) || pad2(i + 1).includes(q) || String(i + 1) === q);
  if (!pick.rows.includes(pick.rows[pick.active])) pick.active = Math.max(0, pick.rows.indexOf(state.current));
  pick.list.textContent = '';
  pick.rows.forEach((i, k) => {
    const li = document.createElement('li'); li.role = 'option'; li.id = `pick-${i}`; li.dataset.i = i;
    li.className = (i === state.current ? 'current' : '') + (k === pick.active ? ' active' : '');
    li.setAttribute('aria-selected', i === state.current);
    const n = document.createElement('span'); n.className = 'picknum'; n.textContent = pad2(i + 1);
    const t = document.createElement('span'); t.className = 'pickname'; t.textContent = state.presets[i].name || '(no name)';
    li.append(n, t);
    if (i === state.current) { const m = document.createElement('span'); m.className = 'pickmark'; m.textContent = 'playing'; li.append(m); }
    li.onclick = () => pickChoose(i);
    pick.list.appendChild(li);
  });
  if (!pick.rows.length) { const li = document.createElement('li'); li.className = 'pickempty'; li.textContent = all.length ? 'No preset matches.' : 'No presets saved yet.'; pick.list.appendChild(li); }
  $('preset-pick-count').textContent = `${pick.rows.length} of ${all.length} presets${q ? ' match' : ''} · click to load`;
  const a = pick.rows[pick.active]; pick.search.setAttribute('aria-activedescendant', a != null ? `pick-${a}` : '');
  pick.list.querySelector('.active')?.scrollIntoView({ block: 'nearest' });
}
function pickOpen(on) {
  if (on && (state.op || !state.store)) return;
  pick.el.hidden = !on; $('preset-nav-open').setAttribute('aria-expanded', on);
  if (on) { pick.search.value = ''; pick.active = -1; pickRender(); pick.search.focus(); }
}
function pickChoose(i) { pickOpen(false); $('preset-nav-open').focus(); if (i !== state.current) loadPreset(i); }
$('preset-nav-open').onclick = () => pickOpen(pick.el.hidden);
pick.search.oninput = () => { pick.active = 0; pickRender(); };
pick.search.onkeydown = e => {
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); if (pick.rows.length) { pick.active = (pick.active + (e.key === 'ArrowDown' ? 1 : -1) + pick.rows.length) % pick.rows.length; pickRender(); } }
  else if (e.key === 'Enter') { e.preventDefault(); const i = pick.rows[pick.active]; if (i != null) pickChoose(i); }
  else if (e.key === 'Escape') { e.preventDefault(); pickOpen(false); $('preset-nav-open').focus(); }
};
document.addEventListener('pointerdown', e => { if (!pick.el.hidden && !$('preset-nav').contains(e.target)) pickOpen(false); });
$('preset-prev').onclick = () => { const i = presetStep(-1); if (i != null) loadPreset(i); };
$('preset-next').onclick = () => { const i = presetStep(1); if (i != null) loadPreset(i); };
$('preset-search').oninput = () => renderPresets();
/** Move preset i by d (-1 up, +1 down): the list changes at once, the pedal follows. */
function movePreset(i, d) {
  const j = i + d, s = state.store, a = state.presets[i], b = state.presets[j];
  return exclusive('Moving a preset', async () => {
    if (b) state.presets[i] = b; else delete state.presets[i];
    if (a) state.presets[j] = a; else delete state.presets[j];
    renderPresets();
    await s.presetSwap(i, j);
  }, refreshPresets);
}
function renderPresets() {
  const q = ($('preset-search').value || '').trim().toLowerCase(), s = state.store;
  const all = Object.keys(state.presets).map(Number).sort((a, b) => a - b);
  const idx = all.filter(i => !q || (state.presets[i].name || '').toLowerCase().includes(q) || pad2(i + 1).includes(q) || String(i + 1) === q);
  list($('preset-list'), idx.map(i => ({ tag: pad2(i + 1), name: state.presets[i].name || '(no name)', own: !!state.presets[i].name, sub: `Preset ${pad2(i + 1)} · ${i === state.current ? 'playing now' : 'saved on the pedal'}`, current: i === state.current, buttons: [
    ['Load', 'primary', () => loadPreset(i), `Load preset ${pad2(i + 1)}`],
    ['↑', 'ghost', () => movePreset(i, -1), `Move preset ${pad2(i + 1)} up`, i === 0],
    ['↓', 'ghost', () => movePreset(i, 1), `Move preset ${pad2(i + 1)} down`, i === PRESETS - 1],
    ['Download', 'ghost', () => exclusive('Downloading a preset', async () => {
      const p = await s.presetFile(i), name = p.name;
      const extra = [p.models.length && `${p.models.length} AMP model${p.models.length > 1 ? 's' : ''}`, p.cabs.length && `${p.cabs.length} cabinet${p.cabs.length > 1 ? 's' : ''}`].filter(Boolean);
      save(new TextEncoder().encode(JSON.stringify(p, null, 1)), `frost-preset-${pad2(i + 1)}-${slug(name, 'preset')}.frostpreset`);
      toast(`Preset ${pad2(i + 1)} downloaded${extra.length ? ' with ' + extra.join(' and ') : ''}.`, 'ok');
    }, async () => {}), `Download preset ${pad2(i + 1)}`],
    ['Rename', 'ghost', async () => {
      const n = await ask(`Rename preset ${pad2(i + 1)}`, '', 'Rename', { input: { label: 'Name (up to 16 characters)', value: state.presets[i].name } });
      if (n != null && n.trim()) if (await exclusive('Renaming a preset', () => s.presetRename(i, n.slice(0, 16)), refreshPresets)) toast('Preset renamed.', 'ok');
    }, `Rename preset ${pad2(i + 1)}`],
    ['Delete', 'danger', async () => {
      if (await ask(`Delete preset ${pad2(i + 1)}?`, `"${state.presets[i].name}" is deleted from the pedal. This cannot be undone unless you have a backup.`, 'Delete', { danger: true }))
        if (await exclusive('Deleting a preset', () => s.presetDelete(i), refreshPresets)) toast('Preset deleted.', 'ok');
    }, `Delete preset ${pad2(i + 1)}`],
  ] })), !state.store ? offlineText() : all.length ? `No preset matches "${$('preset-search').value.trim()}".` : 'No presets yet. Save one from the Live editor or on the pedal: PRESETS → SAVE.');
  $('presets-summary').textContent = state.store ? `${all.length} of ${PRESETS} used` + (q ? ` · ${idx.length} shown` : '') : 'Not connected';
  $('preset-search').disabled = !state.store;
}
function fillPresetSlots() {
  for (const id of ['save-slot', 'import-slot']) {
    const sel = $(id), keep = sel.value; sel.textContent = '';
    for (let i = 0; i < PRESETS; i++) sel.add(new Option(`${pad2(i + 1)} · ${state.presets[i] ? state.presets[i].name + ' (replace)' : 'empty'}`, i));
    const current = state.current ?? null, free = [...Array(PRESETS).keys()].find(i => !state.presets[i]);
    sel.value = keep !== '' && sel.querySelector(`option[value="${keep}"]`) ? keep : String(id === 'save-slot' && current != null && current < PRESETS ? current : (free ?? 0));
  }
  const i = Number($('save-slot').value);
  if (!$('save-name').dataset.edited) $('save-name').value = state.presets[i]?.name || `PRESET ${pad2(i + 1)}`;
}
$('save-slot').onchange = () => { delete $('save-name').dataset.edited; fillPresetSlots(); };
$('save-name').oninput = () => { $('save-name').value = $('save-name').value.replace(/[^\x20-\x7e]/g, ''); $('save-name').dataset.edited = '1'; };
$('save-preset').onclick = async () => {
  const i = Number($('save-slot').value), name = ($('save-name').value.trim() || `PRESET ${pad2(i + 1)}`).slice(0, 16), old = state.presets[i];
  if (!(await ask(`Save as preset ${pad2(i + 1)}?`, `The current sound is saved on the pedal as "${name}"${old ? `, replacing "${old.name}"` : ''}.`, 'Save'))) return;
  if (await exclusive('Saving a preset', async () => { await live.live.savePreset(i, name); state.dirty = false; renderDirty(); delete $('save-name').dataset.edited; }, refreshPresets)) toast(`Saved as preset ${pad2(i + 1)}.`, 'ok');
};
/* AMP models and cabinets included in a preset file: where each goes by default
 * (the number already holding the identical file, else its own number when
 * empty, else the first empty number, else its own number, replacing) and the
 * choices offered. `stored` is state.models (MODEL n at n) or state.cabs (CAB n
 * at n-1). */
const NUMBERS = [...Array(63).keys()].map(k => k + 1);
const sameFile = (a, s) => !!s && s.crc === a.crc && s.label === a.name;
function importTarget(a) {
  const at = n => a.stored[a.at(n)];
  return NUMBERS.find(n => sameFile(a, at(n))) ?? (!at(a.number) ? a.number : NUMBERS.find(n => !at(n)) ?? a.number);
}
function importOptions(a) {
  return NUMBERS.map(n => {
    const s = a.stored[a.at(n)], what = !s ? 'empty' : sameFile(a, s) ? `${s.label} (already here)` : `${s.label || '(no name)'} (replace)`;
    return [n, `${a.kind} ${pad2(n)} · ${what}`];
  });
}
$('import-file').onchange = async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f || !state.store) return;
  let file;
  try { file = parsePresetFile(await f.text()); } catch (err) { toast(`${f.name}: ${err.message}`, 'bad'); return; }
  const i = Number($('import-slot').value), old = state.presets[i], name = file.name;
  const items = [...file.models.map(a => ({ ...a, kind: 'MODEL', stored: state.models, at: n => n, crc: recordCrc(a.record) })),
                 ...file.cabs.map(a => ({ ...a, kind: 'CAB', stored: state.cabs, at: n => n - 1, crc: recordCrc(a.record) }))];
  const missing = [...file.used.models.filter(n => !file.models.some(a => a.number === n)).map(n => `MODEL ${pad2(n)}`),
                   ...file.used.cabs.filter(n => !file.cabs.some(a => a.number === n)).map(n => `CAB ${pad2(n)}`)];
  let text = old ? `This replaces "${old.name}" on the pedal.` : 'The preset is written to the pedal.';
  if (items.length) text += `\nChoose where to store the ${items.length > 1 ? 'files' : 'file'} that came with it. Replacing a used number changes every preset that uses it.`;
  if (missing.length) text += `\nNot included in the file: ${missing.join(', ')}. The preset plays whatever this pedal has at ${missing.length > 1 ? 'those numbers' : 'that number'}.`;
  const selects = items.map(a => ({ label: `${a.kind === 'MODEL' ? 'AMP model' : 'Cabinet'} "${a.name}" (was ${a.kind} ${pad2(a.number)})`, options: importOptions(a), value: importTarget(a) }));
  let picked;
  for (;;) {
    const replacing = items.some((a, k) => { const s = a.stored[a.at(selects[k].value)]; return s && !sameFile(a, s); });
    picked = await ask(`Import "${name}" as preset ${pad2(i + 1)}?`, text, 'Import', { danger: !!old || replacing, selects: items.length ? selects : null });
    if (!picked) return;
    if (picked === true) { picked = []; break; }
    picked.forEach((v, k) => { selects[k].value = v; });
    const clash = items.find((a, k) => items.some((b, m) => m !== k && b.kind === a.kind && picked[m] === picked[k]));
    if (!clash) break;
    toast(`Two files cannot share ${clash.kind} ${pad2(picked[items.indexOf(clash)])}. Choose different numbers.`, 'bad');
  }
  const models = {}, cabs = {};
  items.forEach((a, k) => { (a.kind === 'MODEL' ? models : cabs)[a.number] = { to: picked[k], write: !sameFile(a, a.stored[a.at(picked[k])]) }; });
  const placed = items.map((a, k) => `${a.kind} ${pad2(picked[k])}`);
  if (await exclusive('Importing a preset', () => state.store.presetImport(i, file, models, cabs), items.length ? refreshAll : refreshPresets))
    toast(`"${name}" is now preset ${pad2(i + 1)}${placed.length ? ' (' + placed.join(', ') + ')' : ''}.`, 'ok');
};
function save(bytes, name) { const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([bytes])); a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 2000); }
$('backup').onclick = () => exclusive('Backing up', async () => {
  $('backup-state').textContent = 'Reading everything from the pedal…';
  try { save(new TextEncoder().encode(await state.store.backup()), `frost-${new Date().toISOString().slice(0, 10)}.frostbackup`); $('backup-state').textContent = 'Backup saved to your downloads.'; toast('Backup saved.', 'ok'); }
  catch (e) { $('backup-state').textContent = 'Backup failed: ' + (e.message || e); throw e; }
});
$('restore-file').onchange = async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f || !state.store) return;
  if (!(await ask(`Restore "${f.name}"?`, 'This replaces all presets, AMP models, cabinets and any included settings/setlist on the pedal with the ones in the backup.', 'Restore', { danger: true }))) return;
  await exclusive('Restoring a backup', async () => {
    try { await state.store.restore(await f.text(), t => { $('backup-state').textContent = t; }); $('backup-state').textContent = 'Restored.'; toast('Backup restored.', 'ok'); }
    catch (err) { $('backup-state').textContent = 'Restore failed: ' + (err.message || err); throw err; }
  });
};

// ---------- enable/disable with reasons ----------
async function detectInstalledFrost() {
  const idle = () => state.mode === 'off' && !state.op && !isBusy();
  if (state.checking || !idle()) return;
  state.checking = true; updateButtons();
  try {
    const found = await detectFrost(navigator, idle);
    if (idle()) state.detectedFrost = found;
  } finally { state.checking = false; showStatus(); updateButtons(); }
}
function updateButtons() {
  const on = !!state.store, op = state.op || (isBusy() ? 'firmware' : null), conv = !!state.worker;
  const offline = state.mode === 'demo' ? 'The demo cannot write. Connect a pedal running Frost.' : 'Connect a pedal running Frost to write.';
  const why = (id, text) => { $(id).textContent = text || ''; };
  const returning = state.detectedFrost || state.mode === 'frost';
  $('connect').disabled = !!op || state.checking || state.mode === 'connecting';
  $('connect').textContent = state.mode === 'frost' ? 'Reconnect' : 'Connect GP-5';
  $('connect').className = 'btn ' + (returning ? 'primary' : 'ghost');
  $('install-start').hidden = returning;
  $('onboarding-title').textContent = returning ? 'Frost detected on your GP-5' : 'Start by installing Frost';
  $('onboarding-install').hidden = $('onboarding-install-note').hidden = returning;
  $('onboarding-intro').hidden = returning;
  $('onboarding-existing').classList.toggle('onboarding-existing', !returning);
  const welcomeConnect = document.querySelector('[data-connect]');
  welcomeConnect.disabled = $('connect').disabled;
  welcomeConnect.textContent = returning ? 'Connect GP-5' : 'Already installed? Connect GP-5';
  welcomeConnect.className = 'btn ' + (returning ? 'primary' : 'ghost');
  $('demo').disabled = !!op || state.checking;
  $('amp-convert').disabled = !state.nam || conv || !!state.record; $('amp-cancel').hidden = !conv;
  $('amp-download').hidden = !(state.converted && state.record) || conv;
  $('amp-convert').hidden = !state.nam;
  $('amp-training').hidden = !state.nam;
  $('amp-conversion-note').hidden = !state.nam;
  $('model-file').disabled = !!op || conv;
  $('model-import').disabled = !!op || conv;
  for (const prefix of ['amp', 'cab']) {
    $(prefix + '-import').inert = !!op;
    $(prefix + '-use').disabled = !!op;
  }
  $('train-file').disabled = conv; $('train-pick').classList.toggle('disabled', conv); $('train-clear').disabled = conv;
  $('amp-write').disabled = !on || !state.record || conv || !!op;
  why('amp-why', !state.record ? '' : !on ? offline : op ? `Waiting for "${op}" to finish.` : '');
  $('cab-write').disabled = !on || !state.cab || !!op;
  why('cab-why', !state.cab ? '' : !on ? offline : op ? `Waiting for "${op}" to finish.` : '');
  $('backup').disabled = !on || !!op;
  const canSave = on && !!live.attached && !op;
  $('save-preset').disabled = !canSave; $('save-slot').disabled = $('save-name').disabled = !on;
  why('save-why', state.mode === 'demo' ? 'The demo cannot save. Connect a pedal running Frost.' : !on ? '' : op ? `Waiting for "${op}" to finish.` : '');
  $('import-label').classList.toggle('disabled', !on || !!op); $('import-file').disabled = !on || !!op;
  $('restore-label').classList.toggle('disabled', !on || !!op);
  $('restore-file').disabled = !on || !!op;
  $('live-panel').inert = !!op;
  $('setlist-editor').inert = !!op;
  for (const b of document.querySelectorAll('.library .btn')) b.disabled = !!op || b.dataset.off === '1';
  renderPresetNav();
}
renderLists(); setLivePanel(); updateButtons();
if (new URLSearchParams(location.search).has('demo')) demo(false);
else detectInstalledFrost();
addEventListener('focus', () => detectInstalledFrost());
