// Looper page: a live looper that runs in this browser, with the GP-5 as its audio interface. The pedal's USB input
// brings the finished guitar sound (after Frost's effects) and the pedal mixes what the page plays into its outputs, so
// loops keep your tone and you play over them through the same amp. The footswitch controller switches between two
// modes with B+C pressed together: Effects (the presses go to the GP-5 as set on the Controller page) and Loop (they
// play the looper); holding D in Loop mode enters drum mode, where A, B and C play drums. Loops keep running in Effects
// mode, so you can change presets and come back to overdub. Sections (A, B, C), a drum machine, track effects, WAV
// export and a saved session complete it. The GP-5 shows the looper on its light and screen (CC 42-44 on channel 16).
import { TRACKS, FOOT_MAP, DRUM_MAP, PIECES, DRUM_PAGES } from './looper_engine.js?v=1.9.1';

const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const STATE_LABEL = { empty: 'Empty', count: 'Count-in', arm: 'Waiting for the bar', rec: 'Recording', dub: 'Overdubbing', play: 'Playing', stop: 'Stopped' };
const LED_STATE = { count: 5, arm: 5, rec: 2, dub: 3, play: 4 };
const TEMPO_KEY = 'frost.looper.tempo';
const TEMPO_DEFAULT = { bpm: 100, beats: 4, countBars: 1, quantize: true, metro: false, clickGain: 0.3, qstart: true, decay: 1, fadeStop: false };
const GP5 = /GP-5|Open FW|OpenGP5/i;
const OFFSET_KEY = 'frost.looper.offset';
const RING = 2 * Math.PI * 42;
const SECTION_NAMES = ['A', 'B', 'C'];
// Ready beats, one bar of 16 steps; the rows follow PIECES (kick, snare, hi-hat, open hat, clap, crash, toms, rim).
// A beat with `swing` plays its "and"s late (0.66: the triplet feel of a shuffle or a blues).
const BEATS = {
  'Blues shuffle': { swing: 0.66, rows: ['x.......x.......', '....x.......x...', 'x.x.x.x.x.x.x.x.'] },
  'Slow blues': { swing: 0.66, rows: ['x.....x.x.....x.', '....x.......x...', 'x.x.x.x.x.x.x.x.', '', '', '', '', '', '..............x.'] },
  Rock: ['x.......x.......', '....x.......x...', 'x.x.x.x.x.x.x.x.'],
  Pop: ['x.....x.x.......', '....x.......x...', 'x.x.x.x.x.x.x.x.', '', '....x.......x...'],
  Funk: ['x..x..x...x..x..', '....x..x.x..x...', 'xxxxxxxxxxxxxxxx'],
  Shuffle: { swing: 0.66, rows: ['x.....x.x.....x.', '....x.......x...', 'x.x.x.x.x.x.x.x.'] },
  Disco: ['x...x...x...x...', '....x.......x...', 'x.x.x.x.x.x.x.x.', '..x...x...x...x.'],
  Metal: ['xxxxxxxxxxxxxxxx', '....x.......x...', 'x...x...x...x...', '', '', 'x...............'],
  Reggae: ['........x.......', '', 'x.x.x.x.x.x.x.x.', '', '', '', '', '', '........x.......'],
  Bossa: ['x..x..x.x..x..x.', '', 'xxxxxxxxxxxxxxxx', '', '', '', '', '', 'x..x...x..x..x..'],
};
const DB = 'frost', STORE = 'looper';
function idb() { return new Promise((ok, no) => { const r = indexedDB.open(DB, 1); r.onupgradeneeded = () => r.result.createObjectStore(STORE); r.onsuccess = () => ok(r.result); r.onerror = () => no(r.error); }); }
async function idbPut(key, value) { const db = await idb(); return new Promise((ok, no) => { const tx = db.transaction(STORE, 'readwrite'); tx.objectStore(STORE).put(value, key); tx.oncomplete = () => ok(); tx.onerror = () => no(tx.error); }); }
async function idbGet(key) { const db = await idb(); return new Promise((ok, no) => { const r = db.transaction(STORE).objectStore(STORE).get(key); r.onsuccess = () => ok(r.result); r.onerror = () => no(r.error); }); }
/** 16-bit stereo WAV from channels of float samples. */
function wav(chs, rate) {
  const n = chs[0].length, data = new DataView(new ArrayBuffer(44 + n * 4)), w = (o, s) => [...s].forEach((c, i) => data.setUint8(o + i, c.charCodeAt(0)));
  w(0, 'RIFF'); data.setUint32(4, 36 + n * 4, true); w(8, 'WAVEfmt '); data.setUint32(16, 16, true); data.setUint16(20, 1, true); data.setUint16(22, 2, true);
  data.setUint32(24, rate, true); data.setUint32(28, rate * 4, true); data.setUint16(32, 4, true); data.setUint16(34, 16, true); w(36, 'data'); data.setUint32(40, n * 4, true);
  for (let i = 0; i < n; i++) for (let c = 0; c < 2; c++) data.setInt16(44 + i * 4 + c * 2, Math.max(-1, Math.min(1, chs[Math.min(c, chs.length - 1)][i])) * 32767, true);
  return new Blob([data.buffer], { type: 'audio/wav' });
}
function download(blob, name) { const a = el('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.append(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000); }

export class LooperView {
  /** hooks: midiOut() -> MIDIOutput of the GP-5 or null, toast(text, kind), findController(), controllerReady() */
  constructor(root, hooks) {
    this.root = root; this.hooks = hooks; this.mode = 'effects';
    this.ctx = null; this.node = null; this.stream = null; this.state = null; this.ledSent = -1; this.lat = null; this.screenAt = 0; this.screenSent = '';
    this.offset = Number(localStorage.getItem(OFFSET_KEY) || 0) || 0;
    try { this.tempo = { ...TEMPO_DEFAULT, ...JSON.parse(localStorage.getItem(TEMPO_KEY) || '{}') }; } catch { this.tempo = { ...TEMPO_DEFAULT }; }
    this.taps = []; this.gridPage = 0; this.waiting = {};
    this.build(); this.render();
    setInterval(() => this.keepLight(), 1000);
  }
  q(role) { return this.root.querySelector(`[data-role="${role}"]`); }
  build() {
    this.root.innerHTML = `
      <div class="lpbar">
        <button class="lpmode" type="button" data-role="mode" aria-live="polite"></button>
        <span class="lpstatus" data-role="status" role="status"></span>
        <span class="ctlgap"></span>
        <button class="btn ghost small" type="button" data-role="find">Find controller</button>
        <button class="btn primary small" type="button" data-role="audio">Start audio</button>
      </div>
      <div class="lpstage">
        <div class="lpsections" role="group" aria-label="Sections" data-role="sections"></div>
        <div class="lpmaster"><span data-role="length">—</span><span class="lpcount" data-role="count" aria-live="assertive"></span><div class="lpbeat"><div data-role="pos"></div></div></div>
        <div class="lptracks" data-role="tracks"></div>
        <div class="lpall">
          <button class="btn ghost small" type="button" data-cmd="playAll">Play all</button>
          <button class="btn ghost small" type="button" data-cmd="stopAll">Stop all</button>
          <button class="btn ghost small" type="button" data-cmd="clearAll">Clear the section</button>
        </div>
      </div>
      <div class="lpside">
        <div class="lptabs" role="tablist" aria-label="Looper settings">
          <button type="button" role="tab" data-tab="tempo">Tempo</button><button type="button" role="tab" data-tab="drums">Drums</button><button type="button" role="tab" data-tab="audio">Audio</button><button type="button" role="tab" data-tab="feet">Footswitches</button>
        </div>
        <fieldset class="ctlpanel lptempo" data-panel="tempo"><legend>Tempo and count-in</legend>
          <div class="lptemporow">
            <label><span class="ctllabel">BPM</span><input type="number" min="40" max="240" step="1" data-role="bpm"></label>
            <button class="btn ghost small" type="button" data-role="tap">TAP</button>
            <label><span class="ctllabel">Time signature</span><select data-role="beats"><option value="4">4/4</option><option value="3">3/4</option><option value="6">6/8</option><option value="5">5/4</option><option value="7">7/8</option></select></label>
            <label><span class="ctllabel">Count-in</span><select data-role="countBars"><option value="0">Off</option><option value="1">1 bar</option><option value="2">2 bars</option></select></label>
          </div>
          <label class="lpcheck"><input type="checkbox" data-role="quantize"> Round the first loop to whole bars</label>
          <label class="lpcheck"><input type="checkbox" data-role="qstart"> Start new tracks on the next bar</label>
          <label class="lpcheck"><input type="checkbox" data-role="metro"> Metronome while recording and looping</label>
          <label class="lpcheck"><input type="checkbox" data-role="fadeStop"> Stop tracks with a fade</label>
          <label><span class="ctllabel">Click volume</span><input type="range" min="0" max="1" step="0.05" data-role="clickGain"></label>
          <label><span class="ctllabel">Overdub decay (older layers fade each turn)</span><input type="range" min="0.5" max="1" step="0.05" data-role="decay"></label>
          <p class="ctlnote">Clicks and drums are heard only in the GP-5 outputs: they are never recorded. Before the first loop, D taps the tempo. A new track can be 1, 2, 4 or 8 loops long: close it when you finish.</p>
        </fieldset>
        <fieldset class="ctlpanel lpdrums" data-panel="drums"><legend>Drum machine</legend>
          <div class="lptemporow">
            <button class="btn primary small" type="button" data-role="drumPlay">Play drums</button>
            <button class="btn ghost small" type="button" data-role="drumMode">Drum mode</button>
            <label><span class="ctllabel">Beat</span><select data-role="beat"><option value="">Choose…</option></select></label>
            <label><span class="ctllabel">Bars</span><select data-role="drumBars"><option value="1">1</option><option value="2">2</option><option value="4">4</option></select></label>
            <button class="btn ghost small" type="button" data-role="drumClear">Clear</button>
          </div>
          <div class="lptwo"><label><span class="ctllabel">Drum volume</span><input type="range" min="0" max="1" step="0.05" data-role="drumVol"></label>
          <label><span class="ctllabel">Swing</span><input type="range" min="0.5" max="0.75" step="0.01" value="0.5" data-role="drumSwing" title="Straight on the left; the triplet shuffle of a blues at two thirds"></label></div>
          <div class="lpgrid" data-role="grid"></div>
          <div class="lpgridpages" data-role="gridPages"></div>
          <p class="ctlnote" data-role="drumNote"></p>
        </fieldset>
        <fieldset class="ctlpanel lpaudio" data-panel="audio"><legend>Audio</legend>
          <label><span class="ctllabel">GP-5 input (guitar)</span><select data-role="in"></select></label>
          <label><span class="ctllabel">GP-5 output (loops)</span><select data-role="out"></select></label>
          <label><span class="ctllabel">Timing correction</span><span class="lpoffset"><input type="range" min="-40" max="40" step="1" data-role="offset"><output data-role="offsetv"></output></span></label>
          <p class="ctlnote" data-role="latency"></p>
          <div class="lptemporow">
            <button class="btn ghost small" type="button" data-role="exportMix">Export the mix (WAV)</button>
            <button class="btn ghost small" type="button" data-role="exportTracks">Export the tracks (WAV)</button>
          </div>
          <div class="lptemporow">
            <button class="btn ghost small" type="button" data-role="save">Save the session</button>
            <button class="btn ghost small" type="button" data-role="open">Open the saved session</button>
          </div>
          <p class="ctlnote">Keep DIRECT MONITORING on (GLOBAL SETTINGS → AUDIO) so the guitar is heard while loops play. If overdubs land late, move the correction to the right. The session is saved in this browser.</p>
        </fieldset>
        <fieldset class="ctlpanel lpfeet" data-panel="feet"><legend>Footswitches</legend><div data-role="map"></div>
          <p class="ctlnote">B+C pressed together switch between Effects and Loop. In Effects mode the footswitches do what the Controller page set; loops keep playing. In Loop mode, hold D for drum mode.</p>
        </fieldset>
      </div>`;
    SECTION_NAMES.forEach((name, i) => { const b = el('button', 'lpsection', name); b.type = 'button'; b.dataset.section = i; b.title = `Section ${name}`; b.setAttribute('aria-label', `Section ${name}`); b.onclick = () => this.send({ cmd: 'section', i }); this.q('sections').append(b); });
    const tracks = this.q('tracks');
    for (let i = 0; i < TRACKS; i++) {
      const t = el('div', 'lptrack'); t.dataset.track = i;
      t.innerHTML = `<svg viewBox="0 0 100 100" class="lpring" aria-hidden="true"><circle cx="50" cy="50" r="42" class="lpringbg"/><circle cx="50" cy="50" r="42" class="lpringfg" data-ring/><text x="50" y="47" class="lpnum">${i + 1}</text><text x="50" y="64" class="lplayers" data-layers></text></svg>
        <div class="lptstate" data-tstate></div><div class="lpmeter"><div data-meter></div></div>
        <div class="lpbtns">
          <button class="btn small lprec" type="button" data-cmd="record">Rec / Dub</button>
          <button class="btn ghost small lpicon" type="button" data-cmd="stop" title="Stop" aria-label="Stop">■</button>
          <button class="btn ghost small lpicon" type="button" data-cmd="play" title="Play" aria-label="Play">▶</button>
          <button class="btn ghost small lpicon" type="button" data-cmd="undo" title="Undo" aria-label="Undo">↶</button>
          <button class="btn ghost small lpicon" type="button" data-cmd="redo" title="Redo" aria-label="Redo">↷</button>
          <button class="btn ghost small lpicon" type="button" data-cmd="reverse" title="Reverse" aria-label="Reverse">⇄</button>
          <button class="btn ghost small lpicon" type="button" data-cmd="half" title="Half speed (an octave down)" aria-label="Half speed (an octave down)">½</button>
          <button class="btn ghost small lpicon" type="button" data-cmd="mute" title="Mute" aria-label="Mute">🔇</button>
          <button class="btn ghost small lpicon" type="button" data-cmd="clear" title="Clear" aria-label="Clear">✕</button>
        </div>
        <input class="lpcut" type="range" min="0" max="1" step="0.02" value="1" data-cut title="Filter (left: darker)" aria-label="Filter (left: darker)">`;
      t.onclick = e => {
        if (e.target.closest('[data-cut]')) return;
        const b = e.target.closest('[data-cmd]');
        this.send({ cmd: 'select', track: i }); if (b) this.send({ cmd: b.dataset.cmd, track: i });
      };
      t.querySelector('[data-cut]').oninput = e => this.send({ cmd: 'cut', track: i, v: Number(e.target.value) });
      tracks.append(t);
    }
    this.root.querySelectorAll('.lpall [data-cmd]').forEach(b => { b.onclick = () => this.send({ cmd: b.dataset.cmd }); });
    const table = (title, rows) => {
      const map = el('table', 'lpmap'), cap = el('caption', '', title), head = el('tr');
      map.append(cap); for (const h of ['', 'Press', 'Double press', 'Hold 2 s']) head.append(el('th', '', h)); map.append(head);
      rows.forEach((row, i) => { const tr = el('tr'); tr.append(el('th', '', 'ABCD'[i])); for (const c of row) tr.append(el('td', '', c || '—')); map.append(tr); });
      return map;
    };
    this.q('map').append(table('Loop mode', FOOT_MAP), table('Drum mode', DRUM_MAP));
    const beat = this.q('beat'); for (const name of Object.keys(BEATS)) beat.add(new Option(name, name));
    beat.onchange = () => {
      if (!beat.value) return;
      const b = BEATS[beat.value], rows = Array.isArray(b) ? b : b.rows, swing = Array.isArray(b) ? 0.5 : b.swing;
      this.send({ cmd: 'drumPreset', rows, swing }); this.send({ cmd: 'drumStart' }); beat.value = '';
    };
    this.q('drumSwing').oninput = e => this.send({ cmd: 'drumSwing', v: Number(e.target.value) });
    this.q('drumPlay').onclick = () => this.send({ cmd: this.state?.drum.play ? 'drumStop' : 'drumStart' });
    this.q('drumMode').onclick = () => this.send({ cmd: 'drumMode' });
    this.q('drumClear').onclick = () => this.send({ cmd: 'drumClear' });
    this.q('drumBars').onchange = e => this.send({ cmd: 'drumBars', v: Number(e.target.value) });
    this.q('drumVol').oninput = e => this.send({ cmd: 'drumVol', v: Number(e.target.value) });
    this.q('exportMix').onclick = () => this.exportAudio(true);
    this.q('exportTracks').onclick = () => this.exportAudio(false);
    this.q('save').onclick = () => this.ask({ cmd: 'save' }, 'session', async s => { await idbPut('ultima', s); this.hooks.toast?.('Session saved in this browser.', 'ok'); });
    this.q('open').onclick = async () => {
      const s = await idbGet('ultima').catch(() => null);
      if (!s) { this.hooks.toast?.('No saved session in this browser.', 'bad'); return; }
      this.ask({ cmd: 'restore', session: s }, 'restored', ok => this.hooks.toast?.(ok ? 'Session opened.' : 'The saved session was recorded at another sample rate.', ok ? 'ok' : 'bad'));
    };
    this.q('mode').onclick = () => this.toggleMode();
    this.q('find').onclick = () => this.hooks.findController?.();
    this.q('audio').onclick = () => (this.ctx ? this.stopAudio() : this.startAudio());
    const off = this.q('offset'); off.value = this.offset;
    off.oninput = () => { this.offset = Number(off.value); localStorage.setItem(OFFSET_KEY, String(this.offset)); this.sendLatency(); this.render(); };
    this.q('in').onchange = this.q('out').onchange = () => { if (this.ctx) this.startAudio(); };
    const tempoField = (role, read) => { const f = this.q(role); f.onchange = () => { this.tempo[role] = read(f); this.saveTempo(); }; };
    tempoField('bpm', f => Math.max(40, Math.min(240, Math.round(Number(f.value) || 100))));
    tempoField('beats', f => Number(f.value)); tempoField('countBars', f => Number(f.value));
    for (const role of ['quantize', 'metro', 'qstart', 'fadeStop']) tempoField(role, f => f.checked);
    tempoField('clickGain', f => Number(f.value)); tempoField('decay', f => Number(f.value));
    this.q('clickGain').oninput = this.q('clickGain').onchange; this.q('decay').oninput = this.q('decay').onchange;
    this.q('tap').onclick = () => this.tapTempo();
    this.tab = localStorage.getItem('frost.looper.tab') || 'tempo';
    this.root.querySelectorAll('[data-tab]').forEach(b => { b.onclick = () => { this.tab = b.dataset.tab; try { localStorage.setItem('frost.looper.tab', this.tab); } catch { /* private */ } this.showTab(); }; });
    this.showTab(); this.showTempo();
  }

  /** The side panel shows one of its parts at a time, so the page fits the window. */
  showTab() {
    this.root.querySelectorAll('[data-tab]').forEach(b => b.setAttribute('aria-selected', String(b.dataset.tab === this.tab)));
    this.root.querySelectorAll('[data-panel]').forEach(p => p.classList.toggle('lpshown', p.dataset.panel === this.tab));
  }
  // ---- tempo: kept in this browser and sent to the engine
  showTempo() {
    const t = this.tempo;
    this.q('bpm').value = t.bpm; this.q('beats').value = String(t.beats); this.q('countBars').value = String(t.countBars);
    for (const role of ['quantize', 'metro', 'qstart', 'fadeStop']) this.q(role).checked = !!t[role];
    this.q('clickGain').value = t.clickGain; this.q('decay').value = t.decay;
  }
  saveTempo() { try { localStorage.setItem(TEMPO_KEY, JSON.stringify(this.tempo)); } catch { /* private mode */ } this.showTempo(); this.send({ cmd: 'tempo', ...this.tempo }); }
  tapTempo() {
    const now = performance.now(); if (this.taps.length && now - this.taps[this.taps.length - 1] > 2000) this.taps = [];
    this.taps.push(now); if (this.taps.length > 5) this.taps.shift();
    if (this.taps.length > 1) { this.tempo.bpm = Math.max(40, Math.min(240, Math.round(60000 * (this.taps.length - 1) / (now - this.taps[0])))); this.saveTempo(); }
  }

  // ---- modes: Effects (the GP-5 gets the presses) and Loop (the looper gets them)
  toggleMode() { this.setMode(this.mode === 'loop' ? 'effects' : 'loop'); }
  setMode(m) {
    this.mode = m; this.render(); this.keepLight(true);
    this.hooks.toast?.(m === 'loop' ? 'Loop mode: the footswitches play the looper.' : 'Effects mode: the footswitches go to the GP-5. Loops keep playing.');
  }
  press(sw) { if (!this.node) { this.hooks.toast?.('Start the audio on the Looper page first.', 'bad'); return; } this.send({ cmd: 'press', sw }); }
  release(sw) { this.send({ cmd: 'release', sw }); }

  // ---- audio: the GP-5 USB input in, the GP-5 USB output out, nothing monitored here (the pedal already plays the guitar)
  async devices() {
    const all = await navigator.mediaDevices.enumerateDevices();
    const fill = (sel, kind) => {
      const list = all.filter(d => d.kind === kind), keep = sel.value;
      sel.replaceChildren(...list.map(d => new Option(d.label || d.deviceId, d.deviceId)));
      sel.value = keep && list.some(d => d.deviceId === keep) ? keep : (list.find(d => GP5.test(d.label))?.deviceId || list[0]?.deviceId || '');
    };
    fill(this.q('in'), 'audioinput'); fill(this.q('out'), 'audiooutput');
  }
  async startAudio() {
    try {
      await this.stopAudio(true);
      if (!navigator.mediaDevices?.getUserMedia || !window.AudioWorkletNode) throw new Error('This browser cannot run the looper. Use Chrome or Edge on a computer.');
      const raw = { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 2 };
      if (!this.q('in').value) { const s = await navigator.mediaDevices.getUserMedia({ audio: raw }); s.getTracks().forEach(t => t.stop()); await this.devices(); }
      const inId = this.q('in').value, outId = this.q('out').value;
      this.stream = await navigator.mediaDevices.getUserMedia({ audio: { ...raw, deviceId: inId ? { exact: inId } : undefined } });
      let ctx;
      try { ctx = new AudioContext({ latencyHint: 'interactive', sampleRate: 44100, sinkId: outId || '' }); }
      catch { ctx = new AudioContext({ latencyHint: 'interactive', sampleRate: 44100 }); if (outId && ctx.setSinkId) await ctx.setSinkId(outId); }
      await ctx.audioWorklet.addModule(new URL('./looper_worklet.js?v=1.9.1', import.meta.url));
      const node = new AudioWorkletNode(ctx, 'frost-looper', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit' });
      node.port.onmessage = ({ data }) => this.receive(data);
      ctx.createMediaStreamSource(this.stream).connect(node); node.connect(ctx.destination);
      await ctx.resume();
      this.ctx = ctx; this.node = node;
      const inLabel = this.q('in').selectedOptions[0]?.textContent || '', outLabel = this.q('out').selectedOptions[0]?.textContent || '';
      this.warn = GP5.test(inLabel) && GP5.test(outLabel) ? '' : 'Choose the GP-5 for both input and output.';
      this.send({ cmd: 'tempo', ...this.tempo });
      this.sendLatency(); setTimeout(() => this.sendLatency(), 1000);   // Chrome reports the output latency once it runs
      this.render();
    } catch (e) { await this.stopAudio(true); this.hooks.toast?.(e.message || String(e), 'bad'); this.render(); }
  }
  receive(data) {
    if (data.state) {
      this.state = data.state;
      if (data.state.bpm && data.state.bpm !== this.tempo.bpm) { this.tempo.bpm = data.state.bpm; try { localStorage.setItem(TEMPO_KEY, JSON.stringify(this.tempo)); } catch { /* private */ } this.showTempo(); }
      this.render(); this.keepLight(); this.sendScreen();
    }
    if (data.event === 'tuner') this.pedal([3, 86, 0, 0]);
    if (data.event === 'drum') this.hooks.toast?.(this.state?.drumMode ? 'Drum mode: A, B and C play drums; D changes the set, hold D to leave.' : 'Back to Loop mode.');
    for (const key of Object.keys(this.waiting)) if (key in data) { const f = this.waiting[key]; delete this.waiting[key]; f(data[key]); }
  }
  /** A command whose answer comes back as data[key]. */
  ask(m, key, then) { if (!this.node) { this.hooks.toast?.('Start the audio on the Looper page first.', 'bad'); return; } this.waiting[key] = then; this.send(m); }
  /** The tracks of the section summed (muted ones left out), as long as the longest one; null when nothing is recorded. */
  static mixOf(x) {
    const tracks = x.tracks.filter(t => t && !t.muted);
    if (!tracks.length) return null;
    const size = Math.max(...tracks.map(t => t.mix[0].length)), out = [new Float32Array(size), new Float32Array(size)];
    for (const t of tracks) for (let c = 0; c < 2; c++) { const src = t.mix[Math.min(c, t.mix.length - 1)]; for (let j = 0; j < size; j++) out[c][j] += src[j % src.length]; }
    return out;
  }
  exportAudio(mix) {
    this.ask({ cmd: 'export' }, 'exported', x => {
      const tracks = x.tracks.map((t, i) => t && { ...t, i }).filter(Boolean), stamp = new Date().toISOString().slice(0, 16).replace(/[:T]/g, '-');
      if (!tracks.length) { this.hooks.toast?.('Nothing recorded in this section yet.', 'bad'); return; }
      const name = `frost-loop-${SECTION_NAMES[this.state?.section ?? 0]}-${stamp}`;
      if (!mix) { for (const t of tracks) download(wav(t.mix, x.rate), `${name}-track${t.i + 1}.wav`); return; }
      const out = LooperView.mixOf(x);
      if (!out) { this.hooks.toast?.('Every track of this section is muted.', 'bad'); return; }
      download(wav(out, x.rate), `${name}-mix.wav`);
    });
  }
  async stopAudio(quiet) {
    const ctx = this.ctx; this.ctx = null; this.node = null; this.state = null;
    this.stream?.getTracks().forEach(t => t.stop()); this.stream = null;
    if (ctx) try { await ctx.close(); } catch { /* closed */ }
    if (!quiet) { this.keepLight(true); this.render(); }
  }
  sendLatency() {
    if (!this.ctx || !this.node) return;
    const rate = this.ctx.sampleRate, setting = this.stream?.getAudioTracks()[0]?.getSettings?.().latency;
    const outS = (this.ctx.outputLatency || 0) + (this.ctx.baseLatency || 0);
    const inS = (typeof setting === 'number' ? setting : 0.01) + (this.ctx.baseLatency || 0) + this.offset / 1000;
    this.lat = { in: inS * 1000, out: outS * 1000 };
    this.send({ cmd: 'latency', in: inS * rate, out: outS * rate }); this.render();
  }
  send(m) { this.node?.port.postMessage(m); }

  // ---- the GP-5: its light and screen show the looper in Loop mode; a footswitch action can be run on it (the tuner)
  ledValue() {
    if (this.mode !== 'loop') return 0;
    if (this.state?.drumMode) return 6;
    const has = st => this.state?.tracks.some(k => k.state === st);
    return has('count') || has('arm') ? LED_STATE.count : has('rec') ? LED_STATE.rec : has('dub') ? LED_STATE.dub : has('play') || this.state?.drum.play ? LED_STATE.play : 1;
  }
  keepLight(force) {
    const out = this.hooks.midiOut?.(); if (!out) return;
    const v = this.ledValue();
    if (v === 0 && this.ledSent === 0 && !force) return;            // Effects mode: said once, the pedal shows its own light
    try { out.send([0xbf, 42, v]); this.ledSent = v; } catch { /* the pedal went away */ }
  }
  /** Loop position, track and section for the pedal's screen, about 8 times a second while Loop mode is on. */
  sendScreen() {
    if (this.mode !== 'loop' || !this.state) return;
    const now = performance.now(); if (now - this.screenAt < 120) return;
    const out = this.hooks.midiOut?.(); if (!out) return;
    const s = this.state, k = s.tracks[s.sel ?? 0], pos = Math.round(127 * (k && k.state !== 'empty' ? k.pos : s.pos || 0)), info = (s.sel ?? 0) + 4 * (s.section || 0);
    const key = `${pos},${info},${this.ledValue()}`; if (key === this.screenSent) return;
    this.screenAt = now; this.screenSent = key;
    try { out.send([0xbf, 42, this.ledValue()]); out.send([0xbf, 43, pos]); out.send([0xbf, 44, info]); this.ledSent = this.ledValue(); } catch { /* gone */ }
  }
  /** Run one action on the GP-5 as a footswitch the pedal does not have (n = 15) would: kind, a, b, c as on channel 16. */
  pedal(action) {
    const out = this.hooks.midiOut?.(); if (!out) return;
    const def = [...action, 0, 0, 0, 0, 0, 0, 0, 0];
    try { def.forEach((v, i) => out.send([0xbf, 16 + i, v])); out.send([0xbf, 28, 15]); out.send([0xbf, 29, 15]); } catch { /* gone */ }
  }

  // ---- drawing
  renderGrid() {
    const d = this.state?.drum, grid = this.q('grid'), rows = d ? d.pat.split('|') : PIECES.map(() => '0'.repeat(16)), steps = d?.steps || 16;
    const pages = Math.ceil(steps / 32); if (this.gridPage >= pages) this.gridPage = 0;
    const from = this.gridPage * 32, to = Math.min(steps, from + 32), shown = to - from, set = DRUM_PAGES[this.state?.drumPage ?? 0] || [];
    if (grid.dataset.shape !== `${shown}`) {
      grid.replaceChildren(); grid.dataset.shape = `${shown}`; grid.style.setProperty('--steps', shown);
      PIECES.forEach((name, p) => {
        grid.append(el('span', 'lpgname', name));
        for (let j = 0; j < shown; j++) { const b = el('button', 'lpstep'); b.type = 'button'; b.dataset.p = p; b.dataset.j = j; b.onclick = () => this.send({ cmd: 'drumToggle', p, s: from + j }); grid.append(b); }
      });
    }
    grid.querySelectorAll('.lpgname').forEach((n, p) => n.classList.toggle('inset', this.state?.drumMode && set.includes(p)));
    grid.querySelectorAll('.lpstep').forEach(b => {
      const p = +b.dataset.p, j = +b.dataset.j + from;
      b.classList.toggle('on', rows[p]?.[j] === '1'); b.classList.toggle('now', d?.step === j); b.classList.toggle('beat', j % 4 === 0);
      b.setAttribute('aria-label', `${PIECES[p]} ${j + 1}`); b.setAttribute('aria-pressed', String(rows[p]?.[j] === '1'));
    });
    const pg = this.q('gridPages'); pg.replaceChildren();
    if (pages > 1) for (let i = 0; i < pages; i++) { const b = el('button', 'btn ghost small', `${i * 2 + 1}–${i * 2 + 2}`); b.type = 'button'; b.setAttribute('aria-pressed', String(i === this.gridPage)); b.onclick = () => { this.gridPage = i; this.q('grid').dataset.shape = ''; this.renderGrid(); }; pg.append(b); }
    this.q('drumPlay').textContent = d?.play ? 'Stop drums' : 'Play drums';
    this.q('drumMode').classList.toggle('primary', !!this.state?.drumMode); this.q('drumMode').classList.toggle('ghost', !this.state?.drumMode);
    if (d && document.activeElement !== this.q('drumVol')) this.q('drumVol').value = d.vol;
    if (d && document.activeElement !== this.q('drumSwing')) this.q('drumSwing').value = d.swing;
    if (d) this.q('drumBars').value = String(d.bars);
    this.q('drumNote').textContent = this.state?.drumMode ? `Drum mode: A ${PIECES[set[0]]}, B ${PIECES[set[1]]}, C ${PIECES[set[2]]}. D changes the set; hold D to leave.`
      : 'Click the steps, or choose a beat. In Loop mode, hold D for drum mode: A, B and C play drums and write them on the grid.';
  }
  render() {
    const s = this.state, loop = this.mode === 'loop';
    const m = this.q('mode'); m.textContent = s?.drumMode && loop ? 'DRUMS' : loop ? 'LOOP' : 'EFFECTS'; m.dataset.mode = s?.drumMode && loop ? 'drums' : this.mode;
    m.setAttribute('aria-label', loop ? 'Loop mode: switch to Effects' : 'Effects mode: switch to Loop');
    this.root.classList.toggle('lploop', loop);
    this.q('audio').textContent = this.ctx ? 'Stop audio' : 'Start audio';
    this.q('audio').className = 'btn small ' + (this.ctx ? 'ghost' : 'primary');
    const ready = this.hooks.controllerReady?.();
    this.q('find').hidden = !!ready;
    this.q('status').textContent = !this.ctx ? 'Audio off. Plug the GP-5 in by USB and choose Start audio.'
      : this.warn || (ready ? 'Ready. Press B+C together on the controller for Loop mode.' : 'Ready. Find the controller to play the looper with your feet, or use the buttons.');
    this.q('offsetv').textContent = `${this.offset > 0 ? '+' : ''}${this.offset} ms`;
    this.q('latency').textContent = this.lat ? `Measured delay: in ${this.lat.in.toFixed(1)} ms, out ${this.lat.out.toFixed(1)} ms.` : '';
    this.q('length').textContent = s?.count ? 'Count-in' : s?.len ? `Loop ${s.len.toFixed(2)} s` : s?.recording ? `Recording ${s.recording.toFixed(1)} s` : 'No loop yet';
    const c = this.q('count'); c.textContent = s?.count ? String(s.count.of - s.count.beat + 1) : ''; c.hidden = !s?.count;
    this.q('pos').style.width = `${((s?.pos || 0) * 100).toFixed(1)}%`;
    this.root.querySelectorAll('.lpsection').forEach((b, i) => {
      b.setAttribute('aria-pressed', String((s?.section ?? 0) === i)); b.classList.toggle('next', s?.next === i); b.classList.toggle('used', !!s?.sections?.[i]?.used); b.disabled = !this.ctx;
    });
    this.root.querySelectorAll('.lptrack').forEach((t, i) => {
      const k = s?.tracks[i] || { state: 'empty', layers: 0, redo: 0, muted: false, reversed: false, half: false, cut: 1, mult: 1, peak: 0, pos: 0 };
      t.dataset.state = k.state; t.classList.toggle('sel', (s?.sel ?? 0) === i); t.classList.toggle('muted', k.muted); t.classList.toggle('rev', k.reversed); t.classList.toggle('half', k.half);
      const ring = t.querySelector('[data-ring]'), on = k.state !== 'empty' && s?.len;
      ring.style.strokeDasharray = `${RING}`; ring.style.strokeDashoffset = `${on ? RING * (1 - (k.pos || 0)) : RING}`;
      t.querySelector('[data-layers]').textContent = k.layers ? `${k.layers} ${k.layers === 1 ? 'layer' : 'layers'}${k.mult > 1 ? ` · ×${k.mult}` : ''}` : '';
      t.querySelector('[data-tstate]').textContent = STATE_LABEL[k.state] + (k.muted ? ' · muted' : '') + (k.reversed ? ' · reverse' : '') + (k.half ? ' · ½' : '');
      t.querySelector('[data-meter]').style.width = `${Math.min(100, k.peak * 100).toFixed(0)}%`;
      const cut = t.querySelector('[data-cut]'); if (document.activeElement !== cut) cut.value = k.cut;
      t.querySelectorAll('[data-cmd], [data-cut]').forEach(b => { b.disabled = !this.ctx; });
    });
    this.root.querySelectorAll('.lpall [data-cmd], .lpdrums button, .lpdrums select, .lpaudio button').forEach(b => { b.disabled = !this.ctx; });
    this.renderGrid();
  }
}
