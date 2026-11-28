// Controller page: the M-VAVE Chocolate Plus drawn as hardware. Choose what each footswitch does on Frost (a ready-made
// function, two functions on a short and a long press, or lists of commands: effects on/off, scenes, any control of
// any effect, presets, raw MIDI), write it into the controller (it keeps the setup when plugged straight into the GP-5
// by its USB HOST port), watch each press light its switch and, while both are on this computer, pass the presses on
// to the GP-5.
import { CHOC, SWITCH_NAMES, SINGLE, ALTERNATE, HOLD, REPEAT, SHORT_LONG, KINDS, QUICK, QUICK_DEFAULTS, quickSetup, recognize, toMessage, setupOfRecord, recordOfSetup, parseRecord, buildRecord, describe, switchFor, recordAddress, pedalValue, choiceValue, ChocolateLink, SimulatedChocolate, GESTURE_KINDS, gestureRecord, gesturesOf, actionOfQuick, describeAction } from './chocolate.js?v=1.9.1';
import { loadCatalog, formatValue } from './live.js?v=1.9.1';
import { Tour } from './tour.js?v=1.9.1';
import { keepName } from './i18n.js?v=1.9.1';

// Pairs offered on this page (p: footswitches p and p+1 together). The Chocolate Plus keeps A+B and C+D for itself (its
// E and F keys, which change its group) and sends nothing for them, so only B+C reaches Frost.
const PAIRS = [1];
const PORT = /FootCtrl|Chocolate/i, GP5 = /GP-5|Open FW|Frost|OpenGP5/i;
const pad2 = i => String(i).padStart(2, '0');
const same = (x, y) => JSON.stringify(x) === JSON.stringify(y);
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const STATUS = [0xc0, 0xb0, 0x90, 0x80];                         // PC, CC, note on, note off
const raw = m => m.type === 0 ? [STATUS[0] | (m.channel - 1), m.d1] : [STATUS[m.type] | (m.channel - 1), m.d1, m.d2];
const QUICK_LABELS = { prev: 'Previous preset', next: 'Next preset', tap: 'Tap tempo', tuner: 'Tuner on/off', bypass: 'Bypass all on/off', block: 'Effect on/off', param: 'Change a control of an effect', scene: 'Scene (several effects at once)', preset: 'Load a preset', shortlong: 'Two functions: short and long press', custom: 'Custom: lists of commands' };
const GESTURE_LABELS = { none: 'Nothing', prev: 'Previous preset', next: 'Next preset', tap: 'Tap tempo', tuner: 'Tuner', bypass: 'Bypass all', block: 'Effect on/off', ctl: 'CTL effects of the preset (on/off)', scene: 'Scene', param: 'Effect control', preset: 'Load a preset', midi: 'Other MIDI message' };
const KIND_LABELS = { prev: 'Previous preset', next: 'Next preset', tap: 'Tap tempo', tuner: 'Tuner', bypass: 'Bypass all', block: 'Effect on/off', scene: 'Scene', param: 'Effect control', preset: 'Load a preset', midi: 'Other MIDI message' };
const NEW_COMMAND = { prev: { on: true }, next: { on: true }, tap: { on: true }, tuner: { on: true }, bypass: { on: true }, block: { block: 1, on: true }, scene: { blocks: [1, 2, 3, 4, 5, 6] }, param: { block: 1, knob: 1, value: 127 }, preset: { preset: 1 }, midi: { channel: 1, type: 1, d1: 0, d2: 127 } };
// What each behaviour is called, and the names of its two lists (null: the controller does not use that list).
const MODES = { [ALTERNATE]: ['Alternate: 1st press, 2nd press', '1st press', '2nd press'], [HOLD]: ['Press and release', 'On press', 'On release'],
  [SHORT_LONG]: ['Short press, long press', 'Short press', 'Long press'], [SINGLE]: ['The same at every press', 'Each press', null], [REPEAT]: ['Repeats while held', 'While held (repeats)', null] };
// The GP-5 footswitch light (GLOBAL SETTINGS -> DISPLAY -> LED ON / LED OFF), in the pedal's order, drawn as the pedal shows
// the colours (firmware/controller/slot_editor.h); null: the selected effect's category colour. Sent as CC 40 / 41 on channel 16.
// The names are the pedal's own menu words, so they are not translated.
const LED_ON = [['MINT', '#3cf0b4'], ['GREEN', '#00ff00'], ['BLUE', '#0030ff'], ['ICE', '#30b0ff'], ['PURPLE', '#9000ff'], ['PINK', '#ff1880'], ['RED', '#ff0000'], ['ORANGE', '#ff4800'], ['YELLOW', '#ffd000'], ['WHITE', '#e0ffff'], ['EFFECT', null]];
const LED_OFF = [['DIM RED', '#5c0a0a'], ['DARK', '#05070a'], ['DIM BLUE', '#0a1838'], ['DIM WHITE', '#3a3a3a'], ['DIM EFFECT', null]];
const SWITCH_ART = '<svg class="fsart" viewBox="0 0 100 100" aria-hidden="true"><circle cx="50" cy="50" r="47" fill="url(#ctl-nut)"/><circle cx="50" cy="50" r="40" fill="none" stroke="var(--ctl-edge)" stroke-width="1.5" stroke-dasharray="3 4"/><circle cx="50" cy="50" r="33" fill="url(#ctl-ring)"/><circle cx="50" cy="50" r="25" fill="url(#ctl-cap)"/><ellipse cx="43" cy="40" rx="11" ry="6" fill="#ffffff30"/></svg>';
const DEFS = '<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>'
  + '<radialGradient id="ctl-nut" cx="40%" cy="35%"><stop offset="0" stop-color="var(--ctl-metal1)"/><stop offset="1" stop-color="var(--ctl-metal3)"/></radialGradient>'
  + '<radialGradient id="ctl-ring" cx="50%" cy="40%"><stop offset="0" stop-color="var(--ctl-metal3)"/><stop offset="1" stop-color="var(--ctl-metal2)"/></radialGradient>'
  + '<radialGradient id="ctl-cap" cx="38%" cy="32%"><stop offset="0" stop-color="var(--ctl-metal1)"/><stop offset=".7" stop-color="var(--ctl-metal2)"/><stop offset="1" stop-color="var(--ctl-metal3)"/></radialGradient>'
  + '</defs></svg>';

function select(options, value, onchange) {
  const s = el('select');
  for (const [v, t] of options) s.add(new Option(t, v));
  s.value = String(value); s.onchange = () => onchange(s.value); return s;
}
function labeled(text, control, cls = '') { const l = el('label', cls); l.append(el('span', 'ctllabel', text), control); return l; }
function button(text, onclick, cls = 'btn ghost small') { const b = el('button', cls, text); b.type = 'button'; b.onclick = onclick; return b; }

export class ControllerView {
  /** hooks: presets() -> {index: {name}}, chain() -> the live chain {order, types} or null, gp5Out() -> MIDIOutput|null, toast(text, kind). */
  constructor(root, hooks) {
    this.root = root; this.hooks = hooks;
    this.link = null; this.access = null; this.input = null; this.demo = false; this.busy = false; this.catalog = null;
    this.records = null; this.saved = []; this.setups = []; this.custom = [false, false, false, false]; this.sel = 0;
    this.lit = [false, false, false, false]; this.toggled = [false, false, false, false];
    this.picks = 0; this.writes = 0;                              // footswitches chosen and setups written: the tutorial watches them
    this.led = null; this.ledReading = false; this.glowNote = '';   // the GP-5 footswitch light, as read from the pedal
    this.build(); this.render(); this.glowStatus();
    loadCatalog().then(c => { this.catalog = c; this.render(); }).catch(() => {});
  }
  q(role) { return this.root.querySelector(`[data-role="${role}"]`); }
  build() {
    this.root.innerHTML = DEFS + `
      <div class="ctlbar">
        <span class="ctlstate" data-role="state" role="status" aria-live="polite"></span>
        <label class="ctlbridge"><input type="checkbox" data-role="bridge" checked> Send presses to the GP-5</label>
        <span class="ctlgap"></span>
        <button class="btn ghost small" type="button" data-role="tour">Tutorial</button>
        <button class="btn ghost small" type="button" data-role="find">Find controller</button>
        <button class="btn ghost small" type="button" data-role="demo">Demo</button>
        <button class="btn ghost small" type="button" data-role="reread">Read again</button>
        <button class="btn primary small" type="button" data-role="write">Write to controller</button>
      </div>
      <div class="ctlstage">
        <div class="ctlbody">
          <div class="ctlback" aria-hidden="true"><span>USB</span><span>USB HOST</span><span class="ctlswitch"><b>H</b> · <b>U</b></span></div>
          <div class="ctlrow" role="group" aria-label="Footswitches" data-role="row"></div>
          <div class="ctlpairs" role="group" aria-label="Two footswitches together" data-role="pairs"></div>
          <div class="ctlname" data-role="name">Chocolate Plus</div>
        </div>
        <div class="ctllast" data-role="last" aria-live="polite"></div>
        <div class="ctlglow" data-role="glow">
          <span class="ctlglowtitle">GP-5 footswitch light</span>
          <span class="ctlglowrow"><span class="ctlglowlabel">Effect on</span><span class="ctlswatches" role="radiogroup" aria-label="GP-5 footswitch light with the effect on" data-role="ledon"></span></span>
          <span class="ctlglowrow"><span class="ctlglowlabel">Effect off</span><span class="ctlswatches" role="radiogroup" aria-label="GP-5 footswitch light with the effect off" data-role="ledoff"></span></span>
          <span class="ctlglownote" data-role="glownote" aria-live="polite"></span>
        </div>
        <p class="ctlhelp"><span>To set it up: plug the Chocolate Plus into this computer by USB with its switch on U, choose Find controller, set each footswitch and choose Write to controller.</span> <span>On stage: the setup stays in the controller. Plug its USB HOST port into the GP-5 and put its switch on H.</span> <span>Close the maker's editor (CubeSuite) while this page talks to the controller.</span></p>
      </div>
      <fieldset class="ctlpanel" data-role="panel"><legend data-role="legend"></legend><div data-role="body"></div></fieldset>`;
    const row = this.q('row');
    SWITCH_NAMES.forEach((name, i) => {
      const b = el('button', 'ctlfs'); b.type = 'button'; b.dataset.sw = i;
      b.append(el('span', 'ctlled'), el('span', 'ctlletter', name));
      b.insertAdjacentHTML('beforeend', SWITCH_ART);
      b.append(el('span', 'ctlplate'), el('span', 'ctlsub'));
      b.onclick = () => { this.sel = i; this.picks++; this.render(); };
      row.append(b);
    });
    // Two neighbours pressed together (this.sel 4 + p).
    PAIRS.forEach(p => {
      const b = el('button', 'ctlpair'); b.type = 'button'; b.dataset.pair = p;
      b.append(el('span', 'ctlpairname', `${SWITCH_NAMES[p]}+${SWITCH_NAMES[p + 1]}`), el('span', 'ctlpairdo'));
      b.onclick = () => { this.sel = 4 + p; this.picks++; this.render(); };
      this.q('pairs').append(b);
    });
    for (const [key, list] of [['on', LED_ON], ['off', LED_OFF]]) list.forEach(([name, colour], v) => {
      const b = el('button', 'ctlswatch' + (colour ? '' : ' effect') + (key === 'off' && !colour ? ' dim' : '')); b.type = 'button';
      b.setAttribute('role', 'radio'); b.setAttribute('aria-label', name); b.title = name;
      if (colour) b.style.setProperty('--c', colour);
      b.onclick = () => this.setGlow(key, v);
      this.q(key === 'on' ? 'ledon' : 'ledoff').append(b);
    });
    addEventListener('frost:status', () => this.glowStatus());
    this.q('tour').onclick = () => this.startTour();
    this.q('find').onclick = () => this.find();
    this.q('demo').onclick = () => this.startDemo();
    this.q('reread').onclick = () => this.read();
    this.q('write').onclick = () => this.write();
  }
  get dirty() { return this.setups.some((s, i) => !same(recordOfSetup(s), this.saved[i])); }

  // ---- the effects of the preset playing now on the GP-5 (names only: MIDI addresses chain positions) ----
  effect(block) {
    const c = this.hooks.chain?.(), cat = this.catalog;
    if (!c || !cat || !c.order) return null;
    return cat.effects[c.types[c.order[block - 1]]] || { empty: true };
  }
  blockName(block) { const e = this.effect(block); return !e ? `Block ${block}` : e.empty ? `${block} · (empty)` : `${block} · ${e.name}`; }
  control(block, knob) { const e = this.effect(block); return e && !e.empty ? e.params?.[knob - 1] || null : null; }
  valueText(block, knob, v) { const p = this.control(block, knob); return p ? formatValue(p, pedalValue(p, v)) : `MIDI ${v}`; }

  // ---- talking to the controller ----
  async find() {
    if (this.busy) return;
    this.stop();
    try {
      if (!navigator.requestMIDIAccess) throw new Error('This browser has no Web MIDI. Use Chrome or Edge on a computer.');
      const access = await navigator.requestMIDIAccess({ sysex: true });
      const out = [...access.outputs.values()].find(p => PORT.test(p.name)), inp = [...access.inputs.values()].find(p => PORT.test(p.name));
      if (!out || !inp) throw new Error('No Chocolate Plus on USB. Plug it into this computer with its switch on U, then try again.');
      this.access = access; this.input = inp; this.link = new ChocolateLink(out); this.demo = false;
      inp.onmidimessage = e => this.link?.receive(e.data);
      access.onstatechange = e => { if (e.port.id === inp.id && e.port.state === 'disconnected') this.lost(); };
      this.link.onshort = d => this.pressed(d);
      const name = await this.link.who();
      if (!name) throw new Error('The controller did not answer. Unplug it, plug it in again (switch on U) and try again.');
      this.q('name').textContent = name.startsWith('FootCtrlPlus') ? 'Chocolate Plus · ' + name.replace('FootCtrlPlus_', 'firmware ') : name;
      await this.read();
    } catch (e) { this.stop(); this.say(e.message || String(e), 'bad'); }
  }
  async startDemo() {
    this.stop();
    const sim = new SimulatedChocolate(); this.link = new ChocolateLink(sim); sim.onmessage = d => this.link?.receive(d); this.demo = true;
    this.link.onshort = d => this.pressed(d);
    this.q('name').textContent = 'Chocolate Plus · demo';
    await this.read();
    this.hooks.toast?.('Demo: a simulated controller. Nothing is written to a real one.');
  }
  async read() {
    if (!this.link || this.busy) return;
    this.busy = true; this.say('Reading the controller…');
    try {
      const all = await this.link.readSwitches();
      this.records = [0, 1, 2, 3].map(i => all.slice(i * CHOC.RECORD, (i + 1) * CHOC.RECORD));
      this.setups = this.records.map(r => setupOfRecord(parseRecord(r)));
      this.saved = this.setups.map(recordOfSetup);
      this.custom = this.setups.map(s => recognize(s).id === 'custom');
      this.busy = false; this.say('');
    } catch (e) { this.busy = false; this.records = null; this.say(e.message || String(e), 'bad'); }
  }
  async write() {
    if (!this.link || !this.records || this.busy || !this.dirty) return;
    this.busy = true; this.say('Writing to the controller…');
    try {
      for (let i = 0; i < 4; i++) {
        const want = recordOfSetup(this.setups[i]);
        if (same(want, this.saved[i])) continue;
        await this.link.write(recordAddress(i), buildRecord(this.records[i], want));
      }
      const wanted = this.setups.map(recordOfSetup);
      this.busy = false; await this.read();
      if (!same(this.saved, wanted)) throw new Error('The controller did not keep the new setup. Read it again and retry.');
      this.writes++;
      this.hooks.toast?.(this.demo ? 'Saved in the simulated controller.' : 'Saved in the controller. It keeps this setup on the GP-5 too.', 'ok');
    } catch (e) { this.busy = false; this.say(e.message || String(e), 'bad'); this.hooks.toast?.('Writing to the controller failed.', 'bad'); }
  }
  stop() {
    if (this.input) this.input.onmidimessage = null;
    if (this.access) this.access.onstatechange = null;
    this.link = null; this.input = null; this.access = null; this.records = null; this.demo = false;
    this.lit = [false, false, false, false]; this.toggled = [false, false, false, false];
    this.q('name').textContent = 'Chocolate Plus'; this.q('last').textContent = ''; this.say('');
  }
  lost() { this.stop(); this.say('The controller was unplugged.', 'bad'); }

  // ---- presses ----
  flash(sw) { this.lit[sw] = true; setTimeout(() => { this.lit[sw] = false; this.render(); }, 220); }
  pressed(d) {
    if (!this.records) return;
    if (d[0] === 0xbf) {                                          // channel 16: a footswitch with Frost gestures
      const cc = d[1], n = d[2], loop = this.hooks.loop, inLoop = loop?.mode() === 'loop';
      const press = cc === 28 || cc === 38, foot = (press || cc === 29) && n < 4;
      const out = !inLoop && this.q('bridge').checked && !this.demo ? this.gp5() : null;   // Loop mode: the looper has them
      if (out) { try { out.send(d); } catch { /* the pedal went away */ } }
      if (foot) {
        this.lit[n] = press;
        const who = `Footswitch ${SWITCH_NAMES[n]} ${press ? 'pressed' : 'released'}`;
        this.q('last').textContent = inLoop ? `${who}, played on the looper` : out ? `${who}, passed to the GP-5` : who;
        if (loop) this.loopFoot(cc, n, inLoop);
        this.render();
      }
      return;
    }
    const sw = switchFor(this.records.map(parseRecord), d[0], d[1]);
    if (sw >= 0) {
      const s = parseRecord(this.records[sw]), hit = list => list.some(m => same(raw(m), [...d].slice(0, raw(m).length)));
      const inA = hit(s.a), inB = hit(s.b);
      if (s.mode === HOLD && !same(s.a, s.b)) this.lit[sw] = inA && !inB;
      else if (s.mode === ALTERNATE && !same(s.a, s.b)) this.toggled[sw] = inA && !inB;
      else this.flash(sw);
    }
    const out = this.q('bridge').checked && !this.demo ? this.gp5() : null;
    if (out) { try { out.send(d); } catch { /* the pedal went away */ } }
    const kind = d[0] & 0xf0, what = kind === 0xc0 ? `PC ${d[1]}` : kind === 0xb0 ? `CC ${d[1]} = ${d[2]}` : [...d].map(x => x.toString(16)).join(' ');
    const who = sw >= 0 ? `Footswitch ${SWITCH_NAMES[sw]} sent ${what}` : `The controller sent ${what}`;
    this.q('last').textContent = out ? `${who}, passed to the GP-5` : who;
    this.render();
  }
  /** B+C pressed together (both presses within 0.1 s) switch between Effects and Loop mode. In Loop mode every press goes
   * to the looper instead of the GP-5; B and C first wait those 0.1 s for each other. */
  loopFoot(cc, n, inLoop) {
    const loop = this.hooks.loop, now = performance.now(), w = this.pairWait;
    if (cc === 38 && (n === 1 || n === 2)) {
      if (w && w.n === 3 - n && now - w.t <= 100) { clearTimeout(w.timer); this.pairWait = null; loop.toggle(); return; }
      if (w?.timer) { clearTimeout(w.timer); loop.press(w.n); }
      this.pairWait = { n, t: now, timer: inLoop ? setTimeout(() => { this.pairWait = null; loop.press(n); }, 100) : null };
      return;
    }
    if (!inLoop) return;
    if (cc === 29 && w?.n === n && w.timer) { clearTimeout(w.timer); this.pairWait = null; loop.press(n); }
    if (cc === 28 || cc === 38) loop.press(n); else if (cc === 29) loop.release(n);
  }
  gp5() { return this.hooks.gp5Out?.() || (this.access ? [...this.access.outputs.values()].find(p => GP5.test(p.name)) : null); }
  /** What pressing the selected footswitch would send (as the setup on this page stands), also to the GP-5.
   * long: on a short/long footswitch, the long press. */
  tryIt(long = false) {
    if (!this.records) return;
    const r = recordOfSetup(this.setups[this.sel]), sw = this.sel;
    const play = list => { const out = this.q('bridge').checked && !this.demo ? this.gp5() : null; for (const m of list) { try { out?.send(raw(m)); } catch { /* gone */ } } return list; };
    let sent;
    if (r.mode === HOLD) { sent = play(r.a); this.lit[sw] = true; setTimeout(() => { play(r.b); this.lit[sw] = false; this.render(); }, 250); }
    else if (r.mode === ALTERNATE && !same(r.a, r.b)) { const on = !this.toggled[sw]; sent = play(on ? r.a : r.b); this.toggled[sw] = on; }
    else { sent = play(long && r.mode === SHORT_LONG ? r.b : r.a); this.flash(sw); }
    this.q('last').textContent = `Footswitch ${SWITCH_NAMES[sw]} sent ${sent.map(describe).join(', ') || 'nothing'}`;
    this.render();
  }

  // ---- the GP-5 footswitch light: read from the pedal once connected, changed live (CC 40 / 41 on channel 16) ----
  async glowStatus() {
    if (!this.hooks.gp5Out?.()) { this.led = null; this.glowNote = ''; this.renderGlow(); return; }
    if (this.led || this.ledReading || !this.hooks.ledColours) { this.renderGlow(); return; }
    this.ledReading = true; this.renderGlow();
    try { this.led = await this.hooks.ledColours(); } catch { this.led = null; } finally { this.ledReading = false; }
    this.renderGlow();
  }
  setGlow(key, v) {
    const out = this.gp5(); if (!out) return;
    try { out.send([0xbf, key === 'on' ? 40 : 41, v]); } catch { return; }
    this.led = { on: 0, off: 0, ...this.led, [key]: v };
    this.glowNote = 'Shown on the GP-5 at once and kept by the pedal. Needs Frost with footswitch gestures.';
    this.renderGlow();
  }
  renderGlow() {
    const out = this.gp5();
    for (const [key, role] of [['on', 'ledon'], ['off', 'ledoff']]) this.q(role).querySelectorAll('.ctlswatch').forEach((b, v) => {
      b.disabled = !out; b.setAttribute('aria-checked', String(this.led?.[key] === v));
    });
    this.q('glownote').textContent = !out ? 'Connect the GP-5 (Connect GP-5) to change its footswitch light.'
      : this.ledReading ? 'Reading the GP-5…' : this.glowNote || 'The colour of the GP-5\'s own footswitch with the selected effect on and off.';
  }

  // ---- the tutorial: each step lights the part of the page it talks about; the page keeps working underneath ----
  startTour() {
    this.tour?.close();
    const at = sel => () => this.root.querySelector(sel);
    const demo = { label: 'Open the demo', run: () => this.startDemo(), show: () => !this.records && !this.busy };
    let picks = 0, before = '', writes = 0;
    const footswitch = () => { if (this.sel >= 4) { this.sel -= 4; this.render(); } };   // these steps talk about one footswitch, not a pair
    const steps = [
      { target: at('.ctlbody'), title: 'The Chocolate Plus on Frost',
        text: 'This tutorial shows how to give each footswitch of the controller up to three actions on the GP-5 and take it to the stage. You can use the page while it is open; Tutorial opens it again any time.' },
      { target: at('[data-role="find"]'), title: 'Connect it', action: demo, done: () => !!this.records, auto: true,
        text: 'Put the controller\'s switch on U and plug it into this computer by USB. Close CubeSuite, then choose Find controller. No controller at hand? Open the demo: it works the same and writes nothing.' },
      { target: at('.ctlrow'), title: 'Choose a footswitch', action: demo, enter: () => { picks = this.picks; }, done: () => this.picks > picks, auto: true,
        text: 'Click one of the four footswitches. Its plate shows the press; the line below shows the double press and the hold.' },
      { target: at('.ctlgestures'), title: 'Three actions per footswitch', action: demo, enter: footswitch,
        text: 'Press: a normal press. Double press: two presses within 0.4 s. Hold 2 s: keep it down for two seconds. Leave Nothing on a line you do not need.' },
      { target: at('.ctlgesture[data-gesture="tap"]'), title: 'Choose an action', action: demo,
        enter: () => { footswitch(); before = JSON.stringify(this.setups); }, done: () => JSON.stringify(this.setups) !== before,
        text: 'Change the Press line. There are previous and next preset, tap tempo, tuner, bypass all, one effect on/off, the preset\'s CTL effects, a scene, a control of an effect with one or two values, loading a preset and any MIDI message. Effect and control names come from the preset playing on the GP-5.' },
      { target: at('.ctlpanel .ctlnote'), title: 'When each action runs', action: demo, enter: footswitch,
        text: 'A footswitch with only a press acts at once. With a double press, the pedal waits 0.4 s for a second press; with a hold, the press acts when you lift your foot. Tap tempo always acts on the press, so its double press stays off.' },
      { target: at('.ctlpairs'), title: 'B and C together', action: demo,
        text: 'Pressing B and C at the same time is one more action. Click B+C to choose what it does. Pressed alone, each footswitch keeps its own actions. A+B and C+D belong to the controller itself, which uses them to change its group.' },
      { target: () => document.querySelector('nav a[data-view="live"], a[data-view="live"]'), title: 'CTL: a different job in each preset',
        text: 'With CTL effects of the preset, the footswitch switches the effects each preset marks as CTL: the drive in one preset, the delay in another. To mark one, open the Editor, select the effect, turn on its CTL line and save the preset on the pedal.' },
      { target: at('.ctlgesture[data-gesture="tap"] button'), title: 'Try it before writing', action: demo, enter: footswitch,
        text: 'Try it plays the action on the GP-5 at once, while both are on this computer and Send presses to the GP-5 is on. Pressing the real footswitch also lights it here.' },
      { target: at('[data-role="write"]'), title: 'Write to the controller', action: demo, enter: () => { writes = this.writes; }, done: () => this.writes > writes,
        text: 'Changed footswitches are marked. Write to controller saves the setup in the Chocolate and reads it back to check it. Until then the controller keeps its old setup.' },
      { target: at('.ctlback'), title: 'On stage',
        text: 'Unplug it from the computer, plug its USB HOST port into the GP-5 and put the switch on H. The setup lives in the controller; the GP-5 needs Frost with footswitch gestures.' },
      { target: at('.ctladvanced summary'), title: 'Other gear: Advanced', action: demo, enter: footswitch,
        text: 'Advanced keeps the controller\'s own modes, for pedals without Frost gestures: alternate, press and release, short and long press, repeat while held, and lists of up to 16 MIDI messages.' },
      { target: at('.ctlglow'), title: 'The GP-5 footswitch colour',
        text: 'Here you choose the colour of the GP-5\'s own footswitch with its effect on and off: it changes at once and the pedal keeps it. The same choice is on the pedal, in GLOBAL SETTINGS → DISPLAY → LED ON and LED OFF.' },
    ];
    this.tour = new Tour(steps, { onclose: () => { this.tour = null; } });
    this.tour.start();
  }

  // ---- drawing ----
  /** A message that stays in the bar until the next action (empty: the bar shows the controller's state). */
  say(text, kind = '') { this.note = text ? { text, kind } : null; this.render(); }
  status() {
    const n = this.note || (this.busy ? { text: '', kind: '' }
      : !this.link ? { text: 'Not connected. Plug the controller in by USB (switch on U).', kind: '' }
      : !this.records ? { text: 'Could not read the controller.', kind: 'bad' }
      : this.dirty ? { text: 'Changes not written to the controller yet.', kind: 'warn' }
      : { text: this.demo ? 'Demo controller.' : 'Controller ready.', kind: 'ok' });
    const s = this.q('state'); s.textContent = n.text; s.dataset.kind = n.kind;
  }
  /** A footswitch label for one command, as short as a scribble strip. */
  commandLabel(c) {
    switch (c.kind) {
      case 'prev': return '◀ PRESET';
      case 'next': return 'PRESET ▶';
      case 'tap': return 'TAP';
      case 'tuner': return 'TUNER';
      case 'bypass': return 'BYPASS';
      case 'block': return `BLOCK ${c.block}`;
      case 'scene': return 'SCENE';
      case 'param': { const p = this.control(c.block, c.knob); return p ? p.name : `KNOB ${c.knob}`; }
      case 'preset': return `PRESET ${pad2(c.preset)}`;
    }
    return 'MIDI';
  }
  plate(i) {
    const s = this.setups[i], g = gesturesOf(recordOfSetup(s));
    if (g) {
      const more = [g.double.kind !== 'none' ? `2x: ${this.actionLabel(g.double)}` : '', g.hold.kind !== 'none' ? `hold: ${this.actionLabel(g.hold)}` : ''].filter(Boolean).join(' · ');
      return [this.actionLabel(g.tap), more];
    }
    const q = this.custom[i] ? { id: 'custom' } : recognize(s);
    switch (q.id) {
      case 'prev': case 'next': case 'tap': return [this.commandLabel({ kind: q.id }), ''];
      case 'tuner': return ['TUNER', q.hold ? 'while held' : 'on / off'];
      case 'bypass': return ['BYPASS', q.hold ? 'while held' : 'on / off'];
      case 'block': { const e = this.effect(q.block); return [`BLOCK ${q.block}`, e && !e.empty ? e.name : q.hold ? 'while held' : 'on / off']; }
      case 'param': { const p = this.control(q.block, q.knob); return [p ? p.name : `KNOB ${q.knob}`, `${this.valueText(q.block, q.knob, q.value)} / ${this.valueText(q.block, q.knob, q.value2)}`]; }
      case 'scene': return ['SCENE', q.blocks.length ? q.blocks.join(' ') : 'all off'];
      case 'preset': { const p = this.hooks.presets?.()[q.preset - 1]; return [`PRESET ${pad2(q.preset)}`, p?.name || '', true]; }   // true: the line under it is a preset name
      case 'shortlong': return [this.commandLabel(q.short), `long: ${this.commandLabel(q.long)}`];
    }
    if (s.mode === SHORT_LONG && s.a.length && s.b.length) return [this.commandLabel(s.a[0]), `long: ${this.commandLabel(s.b[0])}`];
    return ['CUSTOM', `${s.a.length + s.b.length} commands`];
  }
  render() {
    if (!this.root.querySelector('.ctlfs')) return;              // still being built
    const on = !!this.records;
    this.status();
    this.root.querySelectorAll('.ctlfs').forEach((b, i) => {
      const [plate, sub, own] = on ? this.plate(i) : ['—', ''];
      b.querySelector('.ctlplate').textContent = plate; keepName(b.querySelector('.ctlsub'), !!own).textContent = sub;
      b.setAttribute('aria-pressed', String(i === this.sel));
      b.setAttribute('aria-label', `Footswitch ${SWITCH_NAMES[i]}: ${plate}${sub ? ' (' + sub + ')' : ''}`);
      b.classList.toggle('lit', this.lit[i] || this.toggled[i]);
      b.classList.toggle('changed', on && !same(recordOfSetup(this.setups[i]), this.saved[i]));
    });
    this.root.querySelectorAll('.ctlpair').forEach(b => {
      const p = Number(b.dataset.pair);
      const x = on ? this.gestures(p).pair : { kind: 'none' }, label = x.kind === 'none' ? '—' : this.actionLabel(x);
      b.querySelector('.ctlpairdo').textContent = label;
      b.setAttribute('aria-pressed', String(this.sel === 4 + p));
      b.setAttribute('aria-label', `Footswitches ${SWITCH_NAMES[p]} and ${SWITCH_NAMES[p + 1]} together: ${label === '—' ? 'nothing' : label}`);
      b.classList.toggle('set', x.kind !== 'none');
      b.classList.toggle('lit', (this.lit[p] || this.toggled[p]) && (this.lit[p + 1] || this.toggled[p + 1]));
    });
    this.root.classList.toggle('offline', !on);
    this.q('reread').disabled = !on || this.busy;
    this.q('write').disabled = !on || this.busy || !this.dirty;
    this.q('find').disabled = this.q('demo').disabled = this.busy;
    this.q('legend').textContent = this.sel < 4 ? `Footswitch ${SWITCH_NAMES[this.sel]}` : `Footswitches ${SWITCH_NAMES[this.sel - 4]}+${SWITCH_NAMES[this.sel - 3]} together`;
    this.q('panel').disabled = !on || this.busy;
    this.renderPanel();
  }
  renderPanel() {
    const body = this.q('body'), focus = document.activeElement && body.contains(document.activeElement) ? document.activeElement.dataset.key : null;
    body.textContent = '';
    if (!this.records) { body.append(el('p', 'ctlnote', 'Find the controller (or open the demo) to set its footswitches. New here? Choose Tutorial.')); return; }
    if (this.sel >= 4) { this.renderPair(this.sel - 4); if (focus) body.querySelector(`[data-key="${focus}"]`)?.focus(); return; }
    const i = this.sel, s = this.setups[i], g = this.gestures(i);
    // The three gestures, recognised by Frost: what most footswitches need.
    const rows = el('div', 'ctlgestures');
    const setG = patch => { const next = { tap: g.tap, double: g.double, hold: g.hold, pair: g.pair, joins: g.joins, ...patch }; this.setups[i] = setupOfRecord(gestureRecord(i, next)); this.custom[i] = false; this.relink(); this.render(); };
    const tapTempo = g.tap.kind === 'tap';
    for (const [k, label] of [['tap', 'Press'], ['double', 'Double press'], ['hold', 'Hold 2 s']]) {
      const row = el('div', 'ctlgesture'); row.dataset.gesture = k;
      row.append(el('span', 'ctlgname', label));
      if (k === 'double' && tapTempo) { row.append(el('span', 'ctlhint', 'Off: with tap tempo on the press, two presses are two taps.')); rows.append(row); continue; }
      row.append(this.gestureRow(g[k], x => setG({ [k]: x }), k));
      row.append(button('Try it', () => this.tryGesture(k)));
      rows.append(row);
    }
    body.append(rows);
    body.append(el('p', 'ctlnote', g.legacy ? 'This footswitch still uses a setup of the controller itself (see Advanced). Change any line above to give it the three gestures.'
      : 'Frost on the GP-5 recognises the gestures: a press acts at once unless the footswitch has a double press (then it waits 0.4 s) or a hold (then it acts on release). CTL effects follow each preset: mark them in the Editor. Needs Frost with footswitch gestures.'));
    // The controller's own setups, as before.
    const adv = el('details', 'ctladvanced'); adv.open = !!(this.advancedOpen && this.advancedOpen[i]);
    adv.ontoggle = () => { (this.advancedOpen ||= [])[i] = adv.open; };
    adv.append(el('summary', '', 'Advanced: the controller\'s own modes (without Frost gestures)'));
    const q = this.custom[i] ? { ...QUICK_DEFAULTS, ...recognize(s), id: 'custom' } : recognize(s);
    const set = patch => { this.setups[i] = quickSetup({ ...q, ...patch }); this.render(); };
    const row = el('div', 'ctlfields');
    const fn = select(QUICK.map(id => [id, QUICK_LABELS[id]]), g.legacy ? q.id : 'custom', v => {
      if (v === 'custom') { this.custom[i] = true; this.render(); return; }
      this.custom[i] = false; set({ id: v });
    });
    fn.dataset.key = 'fn'; row.append(labeled('Function', fn));
    if (g.legacy && q.id === 'shortlong') {
      row.append(labeled('Short press', this.commandRow(q.short, c => set({ short: c }))));
      row.append(labeled('Long press', this.commandRow(q.long, c => set({ long: c }))));
    } else if (g.legacy && q.id !== 'custom') {
      if (q.id === 'block' || q.id === 'param') row.append(labeled('Effect', this.blockSelect(q.block, v => set({ block: v, knob: 1 }))));
      if (q.id === 'param') {
        row.append(labeled('Control', this.knobSelect(q.block, q.knob, v => set({ knob: v, value: 127, value2: 0 }))));
        row.append(labeled(q.hold ? 'While held' : '1st press', this.valueInput(q.block, q.knob, q.value, v => set({ value: v }))));
        row.append(labeled(q.hold ? 'Released' : '2nd press', this.valueInput(q.block, q.knob, q.value2, v => set({ value2: v }))));
      }
      if (q.id === 'scene') row.append(labeled('Effects that stay on', this.sceneBoxes(q.blocks, v => set({ blocks: v }))));
      if (q.id === 'preset') row.append(labeled('Preset', this.presetSelect(q.preset, v => set({ preset: v }))));
      if (['tuner', 'bypass', 'block', 'param'].includes(q.id)) row.append(labeled('Behaviour', select([['0', 'Alternates at each press'], ['1', 'Only while held']], q.hold ? '1' : '0', v => set({ hold: v === '1' }))));
    }
    adv.append(row);
    if (!g.legacy || q.id === 'custom') adv.append(this.listEditor(s));
    const r = recordOfSetup(s), msgs = el('div', 'ctlmsgs');
    const line = (t, list) => { const p = el('span', 'ctlmsg'); p.append(el('b', '', t), document.createTextNode(list.map(describe).join(', ') || 'nothing')); msgs.append(p); };
    const names = MODES[r.mode];
    if (r.mode === ALTERNATE && same(r.a, r.b)) line('Each press:', r.a);
    else if (names) { line(names[1] + ':', r.a); if (names[2]) line(names[2] + ':', r.b); }
    else { line('List A:', r.a); line('List B:', r.b); }
    adv.append(msgs);
    body.append(adv);
    if (focus) body.querySelector(`[data-key="${focus}"]`)?.focus();
  }
  /** The gestures of footswitch i (and the pair with i+1): as stored, or (legacy) what its own setup does on a press. */
  gestures(i) {
    const s = this.setups[i], g = gesturesOf(recordOfSetup(s));
    if (g) return { tap: g.tap, double: g.double, hold: g.hold, pair: i < 3 ? g.pair : { kind: 'none' }, joins: g.joins, legacy: false };
    return { tap: actionOfQuick(recognize(s)), double: { kind: 'none' }, hold: { kind: 'none' }, pair: { kind: 'none' }, joins: false, legacy: true };
  }
  /** After a change: every footswitch in a pair (its own with the next one, or the previous one's) reports its press
   * as part of a pair, so Frost waits for its neighbour; one that is not stops waiting. A footswitch still on a setup
   * of the controller itself gets gestures when a pair needs it, keeping what its press did. */
  relink() {
    const pairs = [0, 1, 2].map(p => this.gestures(p).pair.kind !== 'none');
    for (let i = 0; i < 4; i++) {
      const g = this.gestures(i), joins = !!(pairs[i] || pairs[i - 1]);
      if (g.legacy ? !joins : g.joins === joins) continue;
      this.setups[i] = setupOfRecord(gestureRecord(i, { ...g, joins })); this.custom[i] = false;
    }
  }
  setPair(p, x) {
    this.setups[p] = setupOfRecord(gestureRecord(p, { ...this.gestures(p), pair: x })); this.custom[p] = false;
    this.relink(); this.render();
  }
  /** The panel of a pair: one action for footswitches p and p+1 pressed together. */
  renderPair(p) {
    const body = this.q('body'), x = this.gestures(p).pair, a = SWITCH_NAMES[p], b = SWITCH_NAMES[p + 1];
    const rows = el('div', 'ctlgestures'), row = el('div', 'ctlgesture'); row.dataset.gesture = 'pair';
    row.append(el('span', 'ctlgname', 'Together'), this.gestureRow(x, v => this.setPair(p, v), 'pair'), button('Try it', () => this.tryPair(p)));
    rows.append(row); body.append(rows);
    body.append(el('p', 'ctlnote', `Press ${a} and ${b} at the same time (within 0.1 s). Pressed alone, each keeps its own actions. A footswitch in a pair acts 0.1 s after its press, so its neighbour can still join it; a tap tempo keeps its spacing. Needs Frost with footswitch pairs.`));
    if (this.gestures(p).legacy || this.gestures(p + 1).legacy) body.append(el('p', 'ctlnote', 'A pair gives both footswitches Frost gestures; their press keeps what it did.'));
  }
  /** Press footswitches p and p+1 together on the GP-5, as the controller would send it. */
  tryPair(p) {
    if (!this.records) return;
    const out = this.q('bridge').checked && !this.demo ? this.gp5() : null, r = [p, p + 1].map(i => recordOfSetup(this.setups[i]));
    const send = list => { for (const m of list) { try { out?.send(raw(m)); } catch { /* gone */ } } };
    send(r[0].a); this.lit[p] = true; this.render();
    setTimeout(() => { send(r[1].a); this.lit[p + 1] = true; this.render(); }, 30);
    setTimeout(() => { send(r[0].b); send(r[1].b); this.lit[p] = this.lit[p + 1] = false; this.render(); }, 300);
    this.q('last').textContent = `Footswitches ${SWITCH_NAMES[p]}+${SWITCH_NAMES[p + 1]} together → ${describeAction(this.gestures(p).pair)}` + (out ? '' : ' (not sent: no GP-5)');
  }
  actionLabel(x) {
    switch (x.kind) {
      case 'none': return '—';
      case 'block': { const e = this.effect(x.block); return e && !e.empty ? e.name : `BLOCK ${x.block}`; }
      case 'param': { const p = this.control(x.block, x.knob); return p ? p.name : `KNOB ${x.knob}`; }
      case 'midi': return 'MIDI';
      case 'ctl': return 'CTL';
    }
    return this.commandLabel({ kind: x.kind, preset: x.preset, block: x.block });
  }
  /** One gesture's action: what it does and its settings. */
  gestureRow(x, set, key) {
    const row = el('div', 'ctlcmd'), patch = p => set({ ...x, ...p });
    const fresh = { none: {}, prev: {}, next: {}, tap: {}, tuner: { mode: 'toggle' }, bypass: { mode: 'toggle' }, block: { block: 1, mode: 'toggle' }, ctl: {}, scene: { blocks: [1, 2, 3, 4, 5, 6] }, param: { block: 1, knob: 1, value: 127, value2: 0 }, preset: { preset: 1 }, midi: { type: 1, d1: 0, d2: 127 } };
    const kind = select(GESTURE_KINDS.map(k => [k, GESTURE_LABELS[k]]), x.kind, k => set({ kind: k, ...fresh[k] }));
    kind.dataset.key = 'g-' + key; row.append(kind);
    const mode = () => select([['toggle', 'On / off (toggles)'], ['on', 'On'], ['off', 'Off']], x.mode, v => patch({ mode: v }));
    if (x.kind === 'tuner' || x.kind === 'bypass') row.append(mode());
    if (x.kind === 'block') row.append(this.blockSelect(x.block, v => patch({ block: v })), mode());
    if (x.kind === 'scene') row.append(this.sceneBoxes(x.blocks, v => patch({ blocks: v })));
    if (x.kind === 'param') {
      row.append(this.blockSelect(x.block, v => patch({ block: v, knob: 1 })), this.knobSelect(x.block, x.knob, v => patch({ knob: v })), this.valueInput(x.block, x.knob, x.value, v => patch({ value: v })));
      const alt = el('label', 'ctlmini'), box = el('input'); box.type = 'checkbox'; box.checked = x.value2 != null;
      box.onchange = () => patch({ value2: box.checked ? 0 : null }); alt.append(box, document.createTextNode('alternate with')); row.append(alt);
      if (x.value2 != null) row.append(this.valueInput(x.block, x.knob, x.value2, v => patch({ value2: v })));
    }
    if (x.kind === 'preset') row.append(this.presetSelect(x.preset, v => patch({ preset: v })));
    if (x.kind === 'midi') {
      const num = (v, k) => { const n = el('input', 'ctlnum'); n.type = 'number'; n.min = 0; n.max = 127; n.value = v; n.onchange = () => patch({ [k]: Math.max(0, Math.min(127, Number(n.value) || 0)) }); return n; };
      row.append(select([[1, 'CC'], [0, 'PC']], x.type, v => patch({ type: Number(v) })), num(x.d1, 'd1'));
      if (x.type !== 0) row.append(num(x.d2, 'd2'));
    }
    return row;
  }
  /** Play a gesture of the selected footswitch on the GP-5, as the controller would send it. */
  tryGesture(k) {
    if (!this.records) return;
    const i = this.sel, g = this.gestures(i);
    if (g.legacy) { this.tryIt(); return; }
    const r = recordOfSetup(this.setups[i]), out = this.q('bridge').checked && !this.demo ? this.gp5() : null;
    const send = list => { for (const m of list) { try { out?.send(raw(m)); } catch { /* gone */ } } };
    const press = () => { send(r.a); this.lit[i] = true; this.render(); }, release = () => { send(r.b); this.lit[i] = false; this.render(); };
    if (k === 'tap') { press(); setTimeout(release, 120); }
    if (k === 'double') { press(); setTimeout(release, 80); setTimeout(press, 160); setTimeout(release, 240); }
    if (k === 'hold') { press(); setTimeout(release, 2200); }
    const name = { tap: 'press', double: 'double press', hold: 'hold' }[k];
    this.q('last').textContent = `Footswitch ${SWITCH_NAMES[i]}: ${name} → ${describeAction(g[k])}` + (out ? '' : ' (not sent: no GP-5)');
  }

  // ---- field widgets ----
  blockSelect(cur, onchange) { return select([1, 2, 3, 4, 5, 6].map(b => [b, this.blockName(b)]), cur, v => onchange(Number(v))); }
  knobSelect(block, cur, onchange) {
    const e = this.effect(block), n = e && !e.empty && e.params ? e.params.length : 8;
    return select(Array.from({ length: n }, (_, k) => [k + 1, e?.params?.[k] ? `${k + 1} · ${e.params[k].name}` : `Control ${k + 1}`]), Math.min(cur, n), v => onchange(Number(v)));
  }
  valueInput(block, knob, value, onchange) {
    const p = this.control(block, knob);
    if (p?.labels) {
      const n = p.labels.length, k = Math.round(value * (n - 1) / 127);
      return select(p.labels.map((l, j) => [choiceValue(j, n), l]), choiceValue(Math.min(k, n - 1), n), v => onchange(Number(v)));
    }
    const box = el('span', 'ctlvalue'), range = el('input'), out = el('output', '', this.valueText(block, knob, value));
    range.type = 'range'; range.min = 0; range.max = 127; range.value = value;
    range.oninput = () => { out.textContent = this.valueText(block, knob, Number(range.value)); };
    range.onchange = () => onchange(Number(range.value));
    box.append(range, out); return box;
  }
  sceneBoxes(blocks, onchange) {
    const box = el('span', 'ctlscene');
    for (let b = 1; b <= 6; b++) {
      const l = el('label'), c = el('input'); c.type = 'checkbox'; c.checked = blocks.includes(b);
      c.onchange = () => onchange([1, 2, 3, 4, 5, 6].filter(x => x === b ? c.checked : blocks.includes(x)));
      l.append(c, document.createTextNode(this.blockName(b))); box.append(l);
    }
    return box;
  }
  presetSelect(cur, onchange) {
    const presets = this.hooks.presets?.() || {};
    const s = select(Array.from({ length: 60 }, (_, k) => [k + 1, presets[k]?.name ? `${pad2(k + 1)} · ${presets[k].name}` : pad2(k + 1)]), cur, v => onchange(Number(v)));
    [...s.options].forEach((o, k) => keepName(o, !!presets[k]?.name));   // preset names are never translated
    return s;
  }
  listEditor(s) {
    const box = el('div', 'ctlcustom');
    const known = Object.keys(MODES).map(Number), modes = known.includes(s.mode) ? [] : [[s.mode, 'As set in CubeSuite']];
    box.append(labeled('Behaviour', select([...modes, ...[ALTERNATE, HOLD, SHORT_LONG, SINGLE, REPEAT].map(m => [m, MODES[m][0]])], s.mode, v => { s.mode = Number(v); this.render(); })));
    const lists = el('div', 'ctllists'), names = MODES[s.mode] || [null, 'List A', 'List B'];
    for (const [key, title] of [['a', names[1]], ['b', names[2]]]) {
      if (!title) continue;
      const col = el('div', 'ctllist'); col.append(el('div', 'ctllisttitle', title));
      s[key].forEach((c, n) => col.append(this.commandRow(c, nc => { s[key][n] = nc; this.render(); }, () => { s[key].splice(n, 1); this.render(); })));
      const add = button('+ Add a command', () => { s[key].push({ kind: 'tap', on: true }); this.render(); });
      add.disabled = s[key].length >= CHOC.MAX_MESSAGES; col.append(add); lists.append(col);
    }
    box.append(lists); return box;
  }
  /** One command: what it is and its settings. remove: a ✕ button that drops it from its list (omitted: no button). */
  commandRow(c, set, remove = null) {
    const row = el('div', 'ctlcmd'), patch = p => set({ ...c, ...p });
    row.append(select(KINDS.map(k => [k, KIND_LABELS[k]]), c.kind, k => set({ kind: k, ...NEW_COMMAND[k] })));
    const onoff = () => select([['1', 'on'], ['0', 'off']], c.on ? '1' : '0', v => patch({ on: v === '1' }));
    if ((c.kind === 'prev' || c.kind === 'next' || c.kind === 'tap') && c.on === false) row.append(el('span', 'ctlhint', '(value 0: Frost ignores it)'));
    if (c.kind === 'tuner' || c.kind === 'bypass') row.append(onoff());
    if (c.kind === 'block') row.append(this.blockSelect(c.block, v => patch({ block: v })), onoff());
    if (c.kind === 'scene') row.append(this.sceneBoxes(c.blocks, v => patch({ blocks: v })));
    if (c.kind === 'param') row.append(this.blockSelect(c.block, v => patch({ block: v, knob: 1 })), this.knobSelect(c.block, c.knob, v => patch({ knob: v })), this.valueInput(c.block, c.knob, c.value, v => patch({ value: v })));
    if (c.kind === 'preset') row.append(this.presetSelect(c.preset, v => patch({ preset: v })));
    if (c.kind === 'midi') {
      const num = (v, max, key) => { const i = el('input', 'ctlnum'); i.type = 'number'; i.min = key === 'channel' ? 1 : 0; i.max = max; i.value = v; i.onchange = () => patch({ [key]: Math.max(Number(i.min), Math.min(max, Number(i.value) || 0)) }); return i; };
      row.append(select([[0, 'PC'], [1, 'CC'], [2, 'Note on'], [3, 'Note off']], c.type, v => patch({ type: Number(v) })), labeled('ch', num(c.channel, 16, 'channel'), 'ctlmini'), num(c.d1, 127, 'd1'));
      if (c.type !== 0) row.append(num(c.d2, 127, 'd2'));
    }
    if (remove) { const x = button('✕', remove, 'btn ghost small ctlremove'); x.setAttribute('aria-label', 'Remove this command'); row.append(x); }
    return row;
  }
}
