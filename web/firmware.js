// Firmware install (Frost, or the stock file the user downloaded from Valeton),
// on OpenGP5's web flasher (flasher/gp5flash.js, MIT). The Frost image and its
// SHA-256 come from firmware/manifest.json next to this page.
import * as F from './flasher/gp5flash.js';
import { webMidiPorts } from './flasher/webmidi.js';
const $ = id => document.getElementById(id);
const hex = b => [...new Uint8Array(b)].map(x => x.toString(16).padStart(2, '0')).join('');
let frost = null, chosen = null, busy = false, hooks = {};

export function isBusy() { return busy; }
/** hooks.blocked() -> reason or null; hooks.confirm(title, text, ok) -> Promise<bool>;
 *  hooks.before(): release the page's own pedal connection; hooks.after(mode): reconnect;
 *  hooks.done(ok, text): always called when an install ends. */
export async function initFirmware(h) {
  hooks = h;
  try {
    const list = await (await fetch('firmware/manifest.json', { cache: 'no-cache' })).json();
    const m = list.find(x => x.id === 'frost');
    if (!m || !m.file) throw new Error('no Frost entry');
    const r = await fetch('firmware/' + m.file, { cache: 'no-cache' });
    if (!r.ok) throw new Error('not bundled');
    const bytes = new Uint8Array(await r.arrayBuffer());
    if (hex(await crypto.subtle.digest('SHA-256', bytes)) !== m.sha256) throw new Error('checksum mismatch');
    F.inspect(bytes);
    frost = { name: `Frost ${m.version}`, bytes };
    $('fw-frost-info').textContent = `${m.title} ${m.version}. ${m.description || ''}`;
    $('fw-frost').disabled = false; $('fw-frost-why').textContent = '';
  } catch (e) {
    $('fw-frost-info').textContent = e.message === 'not bundled' || e.message === 'no Frost entry'
      ? 'This copy of the site has no Frost firmware file yet (firmware/manifest.json).'
      : `The bundled Frost firmware could not be used: ${e.message}.`;
    $('fw-frost-why').textContent = 'Install Frost is unavailable on this copy of the site. Restoring the stock firmware still works.';
  }
  $('fw-frost').onclick = () => frost && install(frost.name, frost.bytes, false);
  const take = async f => {
    chosen = null; $('fw-file-install').hidden = true; $('fw-file-name').textContent = f.name;
    try {
      const bytes = new Uint8Array(await f.arrayBuffer()), { id, sha } = await identifyVerified(bytes);
      chosen = { name: f.name, bytes, id };
      const what = id.kind === 'frost' ? `Frost firmware${id.version ? ' ' + id.version : ''}` : id.kind === 'stock' ? 'Valeton stock firmware (V1.0.6)' : 'Custom firmware (not Frost, not stock)';
      $('fw-file-info').textContent = `${what}. Sections: ${id.lines.map(l => l.name).join(', ')}. SHA-256 ${sha.slice(0, 16)}…` +
        (id.kind === 'stock' ? ' Your presets section is not overwritten.' : '');
      const b = $('fw-file-install');
      b.textContent = id.kind === 'frost' ? `Install ${what}` : id.kind === 'stock' ? 'Restore stock firmware' : 'Install this file';
      b.hidden = false; b.className = 'btn primary';
    } catch (err) { $('fw-file-info').textContent = `${f.name}: ${err.message}`; }
    $('fw-file-info').hidden = false;
  };
  $('fw-file').onchange = e => { const f = e.target.files[0]; e.target.value = ''; if (f) take(f); };
  const zone = $('fw-drop');
  for (const ev of ['dragenter', 'dragover']) zone.addEventListener(ev, e => { e.preventDefault(); zone.classList.add('over'); });
  for (const ev of ['dragleave', 'drop']) zone.addEventListener(ev, e => { e.preventDefault(); zone.classList.remove('over'); });
  zone.addEventListener('drop', e => { const f = e.dataTransfer.files[0]; if (f && !busy) take(f); });
  $('fw-file-install').onclick = () => {
    if (!chosen) return;
    const id = chosen.id;
    install(id.kind === 'frost' ? `Frost${id.version ? ' ' + id.version : ''} (${chosen.name})` : id.kind === 'stock' ? `Valeton stock firmware (${chosen.name})` : `"${chosen.name}"`, chosen.bytes, true,
      id.kind === 'custom' ? 'This file is neither Frost nor Valeton\'s firmware. Only install it if you trust where it came from.' : '');
  };
}

/** What a firmware file is: 'stock' (every section matches Valeton V1.0.6), 'frost' (its controller app carries
 *  the FROST menu text; version read from the FIRMWARE row text such as "31Y18"), or 'custom'. Throws when the
 *  flasher rejects the file. */
export function identify(bytes) {
  const { lines, sections } = F.inspect(bytes);
  if (lines.every(l => l.stock || l.name === 'presets')) return { kind: 'stock', lines };
  const app = sections.find(([t]) => t === F.SECTIONS.app)?.[1];
  const text = app ? new TextDecoder('latin1').decode(app) : '';
  const semantic = /\bFROST_FW:(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)\0/.exec(text);
  if (semantic) return { kind: 'frost', version: semantic[1], lines };
  if (!text.includes('FROST\0')) return { kind: 'custom', lines };
  const v = /\0(\d\d[A-Z]\d{1,3}[A-Z]?\d?)\0/.exec(text);
  return { kind: 'frost', version: v ? v[1] : '', lines };
}

// Accepted releases may retain older menu strings or lack the legacy FROST\0
// marker. Match the complete image, after structural validation, for its label.
// This is release identification, not firmware signing or access control.
export async function identifyVerified(bytes) {
  const id = identify(bytes), sha = hex(await crypto.subtle.digest('SHA-256', bytes));
  if (sha === '6f44f2596333d30044322f8e63c99f464cc48521dfae84a32f74b66c1ed5f10a')
    Object.assign(id, { kind: 'frost', version: '31zc' });
  if (sha === '614c554ac9807af395a8c821e61919d5f4d472e66b7d1fcd38f502bc656a6c11')
    Object.assign(id, { kind: 'frost', version: '0.1.0-beta.1' });
  return { id, sha };
}

function log(s) { const l = $('fw-log'); l.textContent += s + '\n'; l.scrollTop = l.scrollHeight; }
async function install(name, bytes, skipPresets, warning = '') {
  if (busy) return;
  const blocked = hooks.blocked?.();
  if (blocked) { hooks.done?.(false, blocked); return; }
  const ok = await hooks.confirm(`Install ${name}?`,
    (warning ? warning + '\n\n' : '') + "This replaces the pedal's firmware and takes about 90 seconds. Keep the pedal connected and powered until it restarts.", 'Install');
  if (!ok || busy) return;
  busy = true; $('fw-run').hidden = false; $('fw-log').textContent = ''; $('fw-bar').style.width = '0';
  for (const id of ['fw-frost', 'fw-file-install']) $(id).disabled = true;
  let wake = null, result = [false, 'Install stopped.']; try { wake = await navigator.wakeLock?.request('screen'); } catch { /* fine */ }
  try {
    await hooks.before?.();
    const sections = F.inspect(bytes).sections.filter(([t]) => !(skipPresets && t === F.SECTIONS.presets));
    const total = sections.reduce((s, [, d]) => s + Math.ceil((d.length + 2) / F.CHUNK), 0), before = {};
    let acc = 0; for (const [t, d] of sections) { before[F.NAMES[t]] = acc; acc += Math.ceil((d.length + 2) / F.CHUNK); }
    const ports = await webMidiPorts();
    $('fw-stage').textContent = 'Putting the pedal into update mode…';
    const mode = await F.flash(ports, sections, s => { log(s); $('fw-stage').textContent = s.split('\n')[0]; },
      (n, i, k) => { $('fw-bar').style.width = `${Math.round(100 * (before[n] + i) / total)}%`; $('fw-stage').textContent = `Writing ${n}: ${Math.round(100 * i / k)}%`; });
    $('fw-bar').style.width = '100%';
    $('fw-stage').textContent = mode ? `Done. The pedal is back, running the ${mode === F.MODE_OPEN ? 'open (Frost)' : 'Valeton'} firmware.` : 'Done, but the pedal did not come back on USB yet. Power it off and on.';
    result = [true, mode ? 'Firmware installed.' : 'Firmware written; power the pedal off and on.'];
    await hooks.after?.(mode);
  } catch (e) {
    $('fw-stage').textContent = 'Install failed. See the recovery steps below.'; log(e.message || String(e)); log(F.RECOVERY);
    result = [false, 'Install failed: ' + (e.message || e)];
  } finally {
    busy = false; $('fw-frost').disabled = !frost; $('fw-file-install').disabled = false;
    try { await wake?.release(); } catch { /* fine */ }
    hooks.done?.(...result);
  }
}
