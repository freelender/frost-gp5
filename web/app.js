// Frost web: connection, navigation, live editor, AMP models, cabinets, presets, firmware.
// Device protocols live in pedal.js / store.js / live.js / firmware.js; this file is the page glue.
import { MidiPedal, PedalFiles, checkRecord, recordLabel } from './pedal.js?v=1.3.1';
import { Store, PRESETS } from './store.js?v=1.3.1';
import { convertIr, readWav } from './ir.js?v=1.3.1';
import { LiveView, loadCatalog } from './live.js?v=1.3.1';
import { initFirmware, isBusy } from './firmware.js?v=1.3.1';
import { SetlistView } from './setlist_view.js?v=1.3.1';
import { simulatedPedal } from './sim.js?v=1.3.1';

export const VERSION = '1.3.1';
const $ = id => document.getElementById(id);
const pad2 = i => String(i).padStart(2, '0');
const state = {
  mode: 'off',        // off | connecting | frost | demo
  op: null,           // a running device operation ('firmware', 'Writing MODEL 03', ...): blocks conflicting ones
  pedal: null, files: null, store: null, models: {}, cabs: {}, presets: {},
  record: null, nam: null, worker: null, cab: null, dirty: false, train: null,
};
$('version').textContent = VERSION;

// ---------- feedback: status, toasts, banner, dialog ----------
function status(kind, text) { $('status').dataset.kind = kind; $('status-text').textContent = text; }
function showStatus() {
  if (state.op) return status('busy', state.op === 'firmware' ? 'Installing firmware…' : state.op + '…');
  if (state.mode === 'connecting') return status('busy', 'Connecting…');
  if (state.mode === 'frost') return status('ok', 'Connected · Frost');
  if (state.mode === 'demo') return status('demo', 'Demo · simulated pedal');
  status($('status').dataset.kind === 'error' ? 'error' : 'off', $('status').dataset.kind === 'error' ? $('status-text').textContent : 'Not connected');
}
function toast(text, kind = '') {
  const t = document.createElement('div'); t.className = 'toast ' + kind; t.textContent = text; t.setAttribute('role', kind === 'bad' ? 'alert' : 'status');
  $('toasts').appendChild(t); setTimeout(() => t.remove(), kind === 'bad' ? 7000 : 4200);
}
function notice(text) { $('notice-text').textContent = text || ''; $('notice').hidden = !text; }
$('notice-close').onclick = () => notice('');
/** Promise<bool> (or the typed text when `input` is given; null on cancel). */
function ask(title, text, ok = 'OK', { danger = false, input = null } = {}) {
  const d = $('confirm');
  $('confirm-title').textContent = title; $('confirm-text').textContent = text;
  $('confirm-ok').textContent = ok; $('confirm-ok').className = 'btn ' + (danger ? 'danger' : 'primary');
  $('confirm-input-label').hidden = !input;
  if (input) { $('confirm-input-name').textContent = input.label; $('confirm-input').value = input.value; }
  d.returnValue = '';
  // Resolved from the buttons themselves (the dialog's close event alone proved unreliable),
  // with Escape (cancel) and a close from elsewhere as fallbacks; settles exactly once.
  return new Promise(resolve => {
    let settled = false;
    const finish = yes => {
      if (settled) return; settled = true;
      d.onclose = d.oncancel = $('confirm-ok').onclick = $('confirm-cancel').onclick = null;
      const value = input ? (yes ? $('confirm-input').value : null) : yes;
      if (d.open) d.close(yes ? 'ok' : 'cancel');
      resolve(value);
    };
    $('confirm-ok').onclick = e => { e.preventDefault(); finish(true); };
    $('confirm-cancel').onclick = e => { e.preventDefault(); finish(false); };
    d.oncancel = () => finish(false);
    $('confirm-input').onkeydown = e => { if (e.key === 'Enter') { e.preventDefault(); finish(true); } };   // Enter renames
    d.onclose = () => finish(d.returnValue === 'ok');
    if (!d.open) d.showModal();
    (input ? $('confirm-input') : $('confirm-ok')).focus();
  });
}

// ---------- navigation ----------
const TITLES = { live: 'LIVE EDITOR', amps: 'AMP MODELS', cabs: 'CABINET IRS', presets: 'PRESETS', setlists: 'SONGS & SETLIST', firmware: 'FIRMWARE', about: 'ABOUT & CREDITS' };
function show(view, focus = false) {
  if (!TITLES[view]) view = 'live';
  for (const s of document.querySelectorAll('.view')) s.hidden = s.id !== 'view-' + view;
  for (const a of $('nav').children) { const on = a.dataset.view === view; a.classList.toggle('active', on); if (on) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current'); }
  $('breadcrumb').textContent = 'STUDIO / ' + TITLES[view];
  if (focus) $('content').focus({ preventScroll: true });
}
const go = view => { if (location.hash !== '#' + view) location.hash = view; else show(view); };
addEventListener('hashchange', () => { show(location.hash.slice(1), true); scrollTo({ top: 0 }); });
show(location.hash.slice(1));

// ---------- connection ----------
const live = new LiveView($('chain-strip'), $('inspector'), {
  cabs: () => state.cabs,
  state: t => { $('live-state').textContent = t; },
  edited: () => { if (!state.dirty) { state.dirty = true; renderDirty(); } },
  refused: t => toast(t, 'bad'),
  confirm: (title, text) => ask(title, text, 'Remove', { danger: true }),
  lost: () => lost(),
});
function renderDirty() {
  const p = $('edit-state'), s = $('save-state'), live = state.mode === 'frost' || state.mode === 'demo';
  p.hidden = !live;
  p.className = 'pill ' + (state.dirty ? 'warn' : 'ok');
  p.textContent = state.dirty ? '● UNSAVED LIVE CHANGES' : state.mode === 'demo' ? 'DEMO' : '● LIVE';
  s.className = 'savestate' + (state.dirty ? ' dirty' : '');
  s.textContent = state.dirty ? 'You changed the chain from this page. To keep it:' : 'No changes from this page yet. To save a chain:';
}
function setLivePanel() {
  const on = live.attached;
  $('live-empty').hidden = on; $('live-panel').hidden = !on;
  renderDirty();
}
const setlists = new SetlistView($('setlist-editor'), {
  confirm: ask, exclusive, error: message => notice(message), saved: () => toast('Songs and setlist saved. Select Setlist in Global → Home Screen.', 'ok'),
  download: (text, name) => save(new TextEncoder().encode(text), name),
});
async function connect() {
  if (state.op || isBusy()) { toast('Wait until the running operation finishes.', 'bad'); return; }
  disconnect(); notice(''); state.mode = 'connecting'; showStatus(); updateButtons();
  try {
    const pedal = await MidiPedal.connect(), files = new PedalFiles(p => pedal.ask(p));
    const who = await files.who();
    if (!who || who.firmware !== 'frost') {
      pedal.close(); state.mode = 'off';
      status('error', who ? 'Stock firmware' : 'No answer');
      notice(who ? 'The pedal is connected but not running Frost. Install it from the Firmware page.' : 'The pedal did not answer. Switch it off and on, then connect again.');
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
  Object.assign(state, { pedal: null, files: null, store: null, mode: 'off', models: {}, cabs: {}, presets: {}, dirty: false });
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
async function exclusive(what, fn) {
  if (state.op || isBusy()) { toast('Another operation is running. Wait until it finishes.', 'bad'); return false; }
  state.op = what; live.pause(true); showStatus(); updateButtons();
  try { await fn(); return true; }
  catch (e) { notice(`${what} failed: ${e.message || e}`); toast(`${what} failed.`, 'bad'); return false; }
  finally { state.op = null; live.pause(false); showStatus(); if (state.store) await refreshAll(); updateButtons(); }
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
    await setlists.attach(state.store, state.presets);
  } catch (e) { notice('Could not read the pedal: ' + (e.message || e)); }
  renderLists();
}
function renderLists() { renderAmps(); renderCabs(); renderPresets(); fillSlots(); }

// ---------- shared list rendering ----------
const offlineText = () => state.mode === 'demo' ? 'The demo has no storage. Connect a pedal running Frost to see it.' : 'Connect a pedal running Frost to see what is on it.';
function list(ul, rows, empty) {
  ul.textContent = '';
  if (!rows.length) { const li = document.createElement('li'); li.className = 'empty'; li.textContent = empty; ul.appendChild(li); return; }
  for (const { tag, name, sub, buttons } of rows) {
    const li = document.createElement('li');
    const icon = document.createElement('div'); icon.className = 'libicon'; icon.textContent = tag;
    const info = document.createElement('div'); info.className = 'info';
    const b = document.createElement('b'); b.textContent = name; const s = document.createElement('small'); s.textContent = sub;
    info.append(b, s);
    const btns = document.createElement('div'); btns.className = 'btns';
    for (const [label, cls, fn, aria] of buttons) {
      const btn = document.createElement('button'); btn.className = 'btn small ' + (cls || 'ghost'); btn.textContent = label; btn.disabled = !!state.op;
      if (aria) btn.setAttribute('aria-label', aria);
      btn.onclick = fn; btns.appendChild(btn);
    }
    li.append(icon, info, btns); ul.appendChild(li);
  }
}

// ---------- AMP models ----------
function renderAmps() {
  const idx = Object.keys(state.models).map(Number).sort((a, b) => a - b);
  list($('amp-list'), idx.map(i => ({ tag: pad2(i), name: state.models[i].label || '(no name)', sub: `MODEL ${pad2(i)}`, buttons: [['Remove', 'danger', async () => {
    if (await ask(`Remove MODEL ${pad2(i)}?`, `"${state.models[i].label}" is deleted from the pedal. An AMP block set to it plays the default model.`, 'Remove', { danger: true }))
      if (await exclusive(`Removing MODEL ${pad2(i)}`, () => state.store.ampRemove(i))) toast(`MODEL ${pad2(i)} removed.`, 'ok');
  }, `Remove MODEL ${pad2(i)}`]] })), state.store ? 'No AMP models yet. Add one on the left.' : offlineText());
  $('amps-summary').textContent = state.store ? `${idx.length} of 63 used · MODEL 00 is the built-in amp` : 'Not connected';
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
  zone.addEventListener('drop', e => { const f = e.dataTransfer.files[0]; if (f) take(f); });
  input.onchange = e => { const f = e.target.files[0]; e.target.value = ''; if (f) take(f); };
}
dropZone($('amp-drop'), $('amp-file'), async f => {
  if (state.worker) { toast('A conversion is running. Cancel it first.', 'bad'); return; }
  state.record = null; state.nam = null; $('amp-quality').hidden = true; $('amp-progress').hidden = true;
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
});
$('amp-name').oninput = () => { $('amp-name').value = $('amp-name').value.replace(/[^\x20-\x7e]/g, ''); };
$('amp-convert').onclick = () => {
  if (!state.nam || state.worker) return;
  const name = ($('amp-name').value || 'AMP').slice(0, 15), workers = Math.max(1, Math.min(4, (navigator.hardwareConcurrency || 4) - 1));
  const w = new Worker(new URL('./convert_worker.js?v=1.3.1', import.meta.url), { type: 'module' }); state.worker = w;
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
      state.record = new Uint8Array(m.record); prog('Done', 1); stop();
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
$('amp-write').onclick = async () => {
  const i = Number($('amp-slot').value), old = state.models[i], label = recordLabel(state.record);
  if (!(await ask(`Write MODEL ${pad2(i)}?`, `"${label}" is written to the pedal as MODEL ${pad2(i)}${old ? `, replacing "${old.label}"` : ''}.`, 'Write'))) return;
  const ok = await exclusive(`Writing MODEL ${pad2(i)}`, () => state.store.ampPut(i, state.record));
  if (ok) { state.record = null; state.nam = null; $('amp-info').textContent = `Written as MODEL ${pad2(i)}.`; $('amp-quality').hidden = true; $('amp-progress').hidden = true; $('amp-dropname').textContent = 'Drop a .nam or .fcm file'; toast(`"${label}" is now MODEL ${pad2(i)}.`, 'ok'); }
  updateButtons();
};

// ---------- cabinets ----------
function renderCabs() {
  const idx = Object.keys(state.cabs).map(Number).sort((a, b) => a - b);
  list($('cab-list'), idx.map(i => ({ tag: pad2(i + 1), name: state.cabs[i].label, sub: `CAB ${pad2(i + 1)}`, buttons: [['Remove', 'danger', async () => {
    if (await ask(`Remove CAB ${pad2(i + 1)}?`, `"${state.cabs[i].label}" is deleted from the pedal. IR blocks set to it play the built-in cabinet until it is filled again.`, 'Remove', { danger: true }))
      if (await exclusive(`Removing CAB ${pad2(i + 1)}`, () => state.store.irRemove(i))) toast(`CAB ${pad2(i + 1)} removed.`, 'ok');
  }, `Remove CAB ${pad2(i + 1)}`]] })), state.store ? 'No custom cabinets yet. Add one on the left.' : offlineText());
  $('cabs-summary').textContent = state.store ? `${idx.length} of 63 used · CAB 00 is the built-in cabinet` : 'Not connected';
}
let cabSource = null;
function processCab() {
  try { state.cab = convertIr(cabSource, $('cab-name').value || 'CAB'); const i = state.cab.info; $('cab-info').textContent = `${i.sourceRate} Hz → 44.1 kHz, 512 taps. Ready to write.`; }
  catch (e) { state.cab = null; $('cab-info').textContent = e.message; }
  $('cab-info').hidden = false; updateButtons();
}
dropZone($('cab-drop'), $('cab-file'), async f => {
  $('cab-dropname').textContent = f.name;
  cabSource = new Uint8Array(await f.arrayBuffer()); $('cab-name').value = f.name.replace(/\.wav$/i, '').slice(0, 15).toUpperCase(); processCab();
  if (!state.cab) toast('That file is not a usable impulse response.', 'bad');
});
$('cab-name').onchange = () => { if (cabSource) processCab(); };
$('cab-write').onclick = async () => {
  if(!await live.live?.hasCabControl()){notice('Update the pedal to the IR-control firmware (31z5 or later) before writing level-calibrated cabinets.');return;}
  const i = Number($('cab-slot').value), old = state.cabs[i], label = recordLabel(state.cab.record);
  if (!(await ask(`Write CAB ${pad2(i + 1)}?`, `"${label}" is written to the pedal as CAB ${pad2(i + 1)}${old ? `, replacing "${old.label}"` : ''}.`, 'Write'))) return;
  const ok = await exclusive(`Writing CAB ${pad2(i + 1)}`, () => state.store.irPut(i, state.cab.record));
  if (ok) { state.cab = null; cabSource = null; $('cab-info').textContent = `Written as CAB ${pad2(i + 1)}.`; $('cab-dropname').textContent = 'Drop a .wav impulse response'; toast(`"${label}" is now CAB ${pad2(i + 1)}.`, 'ok'); }
  updateButtons();
};

// ---------- presets ----------
function renderPresets() {
  const idx = Object.keys(state.presets).map(Number).sort((a, b) => a - b), s = state.store;
  list($('preset-list'), idx.map(i => ({ tag: pad2(i + 1), name: state.presets[i].name || '(no name)', sub: `Preset ${pad2(i + 1)} · saved on the pedal`, buttons: [
    ['Load', 'primary', async () => {
      if (state.dirty && !await ask(`Load preset ${pad2(i + 1)}?`, 'Unsaved live edits will be replaced.', 'Load')) return;
      if (await exclusive('Loading preset', async () => {
        await live.live.loadPreset(i);state.dirty=false;renderDirty();
        const c=await live.live.chain();if(c)await live.render(c);
      })) toast(`Preset ${pad2(i + 1)} loaded.`, 'ok');
    }, `Load preset ${pad2(i + 1)}`],
    ...(i > 0 ? [['↑', 'ghost', () => exclusive('Moving a preset', () => s.presetSwap(i, i - 1)), `Move preset ${pad2(i + 1)} up`]] : []),
    ...(i < PRESETS - 1 ? [['↓', 'ghost', () => exclusive('Moving a preset', () => s.presetSwap(i, i + 1)), `Move preset ${pad2(i + 1)} down`]] : []),
    ['Rename', 'ghost', async () => {
      const n = await ask(`Rename preset ${pad2(i + 1)}`, '', 'Rename', { input: { label: 'Name (up to 16 characters)', value: state.presets[i].name } });
      if (n != null && n.trim()) if (await exclusive('Renaming a preset', () => s.presetRename(i, n.slice(0, 16)))) toast('Preset renamed.', 'ok');
    }, `Rename preset ${pad2(i + 1)}`],
    ['Delete', 'danger', async () => {
      if (await ask(`Delete preset ${pad2(i + 1)}?`, `"${state.presets[i].name}" is deleted from the pedal. This cannot be undone unless you have a backup.`, 'Delete', { danger: true }))
        if (await exclusive('Deleting a preset', () => s.presetDelete(i))) toast('Preset deleted.', 'ok');
    }, `Delete preset ${pad2(i + 1)}`],
  ] })), state.store ? 'No presets yet. Save one on the pedal: PRESETS → SAVE.' : offlineText());
  $('presets-summary').textContent = state.store ? `${idx.length} of ${PRESETS} used` : 'Not connected';
}
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
function updateButtons() {
  const on = !!state.store, op = state.op || (isBusy() ? 'firmware' : null), conv = !!state.worker;
  const offline = state.mode === 'demo' ? 'The demo cannot write. Connect a pedal running Frost.' : 'Connect a pedal running Frost to write.';
  const why = (id, text) => { $(id).textContent = text || ''; };
  $('connect').disabled = !!op || state.mode === 'connecting';
  $('connect').textContent = state.mode === 'frost' ? 'Reconnect' : 'Connect GP-5';
  $('demo').disabled = !!op;
  $('amp-convert').disabled = !state.nam || conv || !!state.record; $('amp-cancel').hidden = !conv;
  $('train-file').disabled = conv; $('train-pick').classList.toggle('disabled', conv); $('train-clear').disabled = conv;
  $('amp-write').disabled = !on || !state.record || conv || !!op;
  why('amp-why', !state.record ? '' : !on ? offline : op ? `Waiting for "${op}" to finish.` : '');
  $('cab-write').disabled = !on || !state.cab || !!op;
  why('cab-why', !state.cab ? '' : !on ? offline : op ? `Waiting for "${op}" to finish.` : '');
  $('backup').disabled = !on || !!op;
  $('restore-label').classList.toggle('disabled', !on || !!op);
  $('restore-file').disabled = !on || !!op;
  $('live-panel').inert = !!op;
  $('setlist-editor').inert = !!op;
  for (const b of document.querySelectorAll('.library .btn')) b.disabled = !!op;
}
renderLists(); setLivePanel(); updateButtons();
if (new URLSearchParams(location.search).has('demo')) demo(false);
