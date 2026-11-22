// Live effect editing (firmware/controller/live_host.h, 7E 14..18): the pedal's
// editor does the work, so the pedal screen, SAVE and the DSP stay in sync.
// effects.json is generated from the firmware catalog (tools/export_effects.py).
const EMPTY = 255;
let catalog = null;
export async function loadCatalog() {
  if (!catalog) catalog = await (await fetch('effects.json', { cache: 'no-cache' })).json();
  return catalog;
}
const i16 = (b, o) => { const v = b[o] | (b[o + 1] << 8); return v & 0x8000 ? v - 0x10000 : v; };

export class LiveChain {
  /** ask(payload) -> reply payload or null (pedal.js MidiPedal.ask). */
  constructor(ask) { this.ask = ask; this.revision = null; }
  async chain() {
    const r = await this.ask([0x7e, 0x18]);
    if (!r || r.length < 26) return null;
    const rev = (r[21] | (r[22] << 8) | (r[23] << 16) | (r[24] << 24)) >>> 0;
    return { ready: !!r[2], types: [...r.slice(3, 9)], order: [...r.slice(9, 15)], enabled: [...r.slice(15, 21)], revision: rev, editable: !!r[25] };
  }
  async slot(s) {
    const r = await this.ask([0x7e, 0x14, s]);
    if (!r || r.length < 24) return null;
    const count = r[7], values = []; for (let i = 0; i < count; i++) values.push(i16(r, 8 + 2 * i));
    return { type: r[4], enabled: !!r[5], position: r[6], values };
  }
  async setParam(s, p, v) { const r = await this.ask([0x7e, 0x15, s, p, v & 255, (v >> 8) & 255]); return !!(r && r[2]); }
  async setEnabled(s, on) { const r = await this.ask([0x7e, 0x16, s, on ? 1 : 0]); return !!(r && r[2]); }
  async setType(s, type) { const r = await this.ask([0x7e, 0x17, s, type]); return !!(r && r[2]); }
  async presetStatus() {
    const r = await this.ask([0x7e, 0x1c]);
    if (!r || r.length !== 10 || r[2] !== 1) return null;
    return { current: r[3], queued: r[4], busy: !!r[5], error: r[6], bypass: !!r[7], volume: r[8], ready: !!r[9] };
  }
  async loadPreset(index, { timeout = 15000, delay = ms => new Promise(r => setTimeout(r, ms)) } = {}) {
    if (!Number.isInteger(index) || index < 0 || index >= 60) throw new Error('Preset must be 1–60.');
    if (!await this.presetStatus()) throw new Error('Patch loading needs Frost 0.1.0-beta.2 or newer.');
    // A recall is sent once. Poll completion; never repeat a timed-out load.
    const r = await this.ask([0x7e, 0x1d, index]);
    if (!r || r[2] !== 1) throw new Error('The pedal did not accept the preset. Reconnect and read its current sound.');
    const until = Date.now() + timeout;
    do {
      const s = await this.presetStatus();
      if (!s) throw new Error('Lost contact during preset loading. Reconnect to check the current sound.');
      if (s.queued === 255 && !s.busy) {
        if (s.error) throw new Error('Preset loading failed on the pedal.');
        if (s.current === index && s.ready) return s;
        if (s.current !== index) throw new Error('Another controller changed the preset. Read the pedal again.');
      }
      await delay(100);
    } while (Date.now() < until);
    throw new Error('The pedal is still busy. Exit the tuner or finish the current operation, then read it again.');
  }
  async cab(s) {
    const r=await this.ask([0x7e,0x19,s]);
    if(!r || r.length!==25 || !r[2])return null;
    return {number:r[4],busy:!!r[5],error:!!r[6],exists:!!r[7],override:!!r[24],name:new TextDecoder().decode(r.slice(8,24)).replace(/\0.*$/s,'')};
  }
  async setCab(s,cab) { if(!Number.isInteger(cab) || cab<0 || cab>63)return false;const r=await this.ask([0x7e,0x1a,s,cab]);return !!(r && r[2]); }
  async hasCabControl() { const r=await this.ask([0x7e,0x19,255]);return !!(r && r.length===3 && r[0]===0x7e && r[1]===0x19); }
}

export function formatValue(p, v) {
  if (p.labels) return p.labels[v - p.min] ?? String(v);
  const x = v / p.scale, digits = p.scale >= 100 ? 2 : p.scale >= 10 ? 1 : 0;
  return x.toFixed(digits) + (p.unit ? ' ' + p.unit.toLowerCase().replace('db', 'dB').replace('hz', 'Hz').replace('ms', 'ms') : '');
}

const ICONS = ['◈', '▲', '◷', '✧', '≋', '▦', '▤', '▥', '↕'];   // by catalog category
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };

/**
 * The live editor: a six-block chain strip and an inspector for the selected
 * slot, kept in sync with the pedal by polling 7E 18 / 7E 14.
 * ui: { state(text), edited(), refused(text), lost() }
 */
export class LiveView {
  constructor(strip, inspector, ui) {
    Object.assign(this, { strip, inspector, ui, live: null, timer: null, busy: false, paused: false, misses: 0, selected: null, inspType: null, pending: new Map() });
    strip.addEventListener('keydown', e => this.key(e));
  }
  get attached() { return !!this.live; }
  async attach(ask) {
    this.live = new LiveChain(ask); this.misses = 0; await loadCatalog();
    const c = await this.live.chain();
    if (!c) { this.live = null; return false; }
    this.selected = c.order[0]; await this.render(c); this.poll(); return true;
  }
  detach() { clearTimeout(this.timer); this.timer = null; this.live = null; this.chainState = null; this.strip.textContent = ''; this.inspector.textContent = ''; }
  pause(on) { this.paused = on; }

  poll() {
    clearTimeout(this.timer);
    this.timer = setTimeout(async () => {
      if (!this.live) return;
      if (!this.busy && !this.paused && !this.pending.size) {
        let c = null;
        try { c = await this.live.chain(); } catch { c = null; }
        if (!this.live) return;
        if (!c) { if (++this.misses >= 3) { this.ui.lost(); return; } }
        else {
          this.misses = 0;
          try { if (this.changed(c)) await this.render(c); else await this.refresh(c); } catch { /* next round */ }
        }
      }
      this.poll();
    }, 1000);
  }
  changed(c) {
    const o = this.chainState;
    return !o || o.revision !== c.revision || o.editable !== c.editable || ['types', 'order', 'enabled'].some(k => o[k].join() !== c[k].join());
  }

  async render(c) {
    this.chainState = c;
    this.ui.state(c.editable ? 'Live: changes apply immediately.' : 'The pedal is busy (a menu or transfer). Editing resumes when it is idle.');
    if (!c.order.includes(this.selected)) this.selected = c.order[0];
    this.strip.textContent = '';
    c.order.forEach((s, pos) => {
      const type = c.types[s], eff = catalog.effects[type], empty = !eff, on = !!c.enabled[s];
      const b = el('button', 'block' + (empty ? ' empty' : on ? '' : ' off') + (s === this.selected ? ' active' : ''));
      b.type = 'button'; b.dataset.slot = s; b.setAttribute('aria-pressed', s === this.selected);
      if (!empty) b.style.setProperty('--effect-color', catalog.colors[eff.category]);
      b.setAttribute('aria-label', `Block ${pos + 1}: ${empty ? 'empty' : `${eff.name}, ${on ? 'on' : 'off'}`}`);
      const num = el('div', 'num'); num.append(el('i'), `${String(pos + 1).padStart(2, '0')} · ${empty ? 'EMPTY' : on ? 'ON' : 'OFF'}`);
      b.append(num, el('div', 'icon', empty ? '＋' : ICONS[eff.category] || '◈'), el('div', 'name', empty ? 'Empty slot' : eff.name),
        el('div', 'cat', empty ? 'ADD AN EFFECT' : catalog.categories[eff.category]));
      b.onclick = async () => {
        await this.select(s);
        if (matchMedia('(max-width: 760px)').matches) this.inspector.scrollIntoView({ behavior: 'smooth', block: 'start' });
      };
      this.strip.appendChild(b);
    });
    await this.renderInspector();
  }
  key(e) {
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key) || !this.chainState) return;
    const order = this.chainState.order, from = e.target.closest?.('.block')?.dataset.slot;
    const pos = order.indexOf(from != null ? Number(from) : this.selected);
    const next = e.key === 'Home' ? 0 : e.key === 'End' ? 5 : Math.max(0, Math.min(5, pos + (e.key === 'ArrowLeft' ? -1 : 1)));
    e.preventDefault();
    this.select(order[next]).then(() => this.strip.querySelector(`[data-slot="${order[next]}"]`)?.focus());
  }
  async select(s) {
    this.selected = s;
    for (const b of this.strip.children) { const on = Number(b.dataset.slot) === s; b.classList.toggle('active', on); b.setAttribute('aria-pressed', on); }
    await this.renderInspector();
  }

  async renderInspector() {
    const s = this.selected, d = await this.live?.slot(s);
    if (s !== this.selected) return;
    this.inspector.textContent = '';
    if (!d) { this.inspType = null; this.switchEl = null; this.inspector.appendChild(el('p', 'noparams', 'Could not read this block from the pedal.')); return; }
    const c = this.chainState, eff = catalog.effects[d.type], pos = c.order.indexOf(s);
    this.inspType = d.type;
    const head = el('div', 'inspectorhead'), title = el('div');
    title.append(el('div', 'eyebrow', `Block ${pos + 1}${eff ? ' · ' + catalog.categories[eff.category] : ''}`), el('h2', '', eff ? eff.name : 'Empty slot'));
    head.appendChild(title);
    const box = el('fieldset'); box.disabled = !c.editable;
    this.switchEl = null;
    if (eff) {
      const sw = el('button', 'switch'); sw.type = 'button'; sw.setAttribute('role', 'switch'); sw.setAttribute('aria-checked', d.enabled);
      sw.setAttribute('aria-label', `${eff.name} on`); sw.title = 'Switch this block on or off'; sw.disabled = !c.editable;
      sw.onclick = () => { const on = sw.getAttribute('aria-checked') !== 'true'; sw.setAttribute('aria-checked', on); this.act(() => this.live.setEnabled(s, on), 'switch', true); };
      const rm = el('button', 'btn small danger', 'Remove'); rm.type = 'button'; rm.disabled = !c.editable;
      rm.title = 'Remove this effect from the chain'; rm.setAttribute('aria-label', `Remove ${eff.name} from block ${pos + 1}`);
      rm.onclick = async () => {
        const yes = this.ui.confirm ? await this.ui.confirm(`Remove ${eff.name}?`, `Block ${pos + 1} becomes empty. Like every live edit, this is not saved until you save a preset on the pedal.`) : confirm(`Remove ${eff.name}?`);
        if (yes && this.selected === s) this.act(() => this.live.setType(s, EMPTY), 'effect', true);
      };
      const side = el('div', 'headbtns'); side.append(rm, sw);
      this.switchEl = sw; head.appendChild(side);
    }
    this.inspector.append(head, box);

    const picker = el('label', 'picker'), sel = el('select');
    picker.append(el('span', 'inputlabel', 'Effect in this block'), sel);
    sel.add(new Option('— Empty —', EMPTY));
    catalog.categories.forEach((name, k) => {
      const g = document.createElement('optgroup'); g.label = name;
      for (const t of catalog.picker[k]) if (t !== EMPTY && catalog.effects[t]) g.appendChild(new Option(catalog.effects[t].name, t));
      if (g.children.length) sel.appendChild(g);
    });
    sel.value = d.type;
    sel.onchange = () => this.act(() => this.live.setType(s, Number(sel.value)), 'effect', true);
    box.appendChild(picker);
    if (!eff) { box.appendChild(el('p', 'noparams', 'Choose an effect to fill this block. A new block starts switched off.')); return; }
    if (!eff.params.length) box.appendChild(el('p', 'noparams', 'This effect has no settings.'));
    if(d.type===3){
      const cab=await this.live.cab(s);if(s!==this.selected)return;
      if(cab)box.appendChild(this.cabField(s,cab));
      else box.appendChild(el('p','noparams','Cabinet selection requires the IR-control firmware update.'));
    }
    eff.params.forEach((p, i) => box.appendChild(this.field(s, i, p, d.values[i] ?? p.initial)));
  }
  cabField(s,cab) {
    const row=el('label','picker'), sel=el('select'), status=el('p','noparams');
    row.append(el('span','inputlabel','CAB'),sel,status);sel.dataset.cab='1';
    sel.add(new Option('00 · Built in',0));const cabs=this.ui.cabs?.() || {};
    for(let i=1;i<=63;i++)sel.add(new Option(`${String(i).padStart(2,'0')} · ${cabs[i-1]?.label || (cab.number===i && cab.exists?cab.name:'Empty — built-in fallback')}`,i));
    const show=c=>{
      const latest=this.ui.cabs?.() || {}, options=sel.options || sel.children;
      for(let i=1;i<=63;i++)options[i].text=`${String(i).padStart(2,'0')} · ${latest[i-1]?.label || (c.number===i && c.exists?c.name:'Empty — built-in fallback')}`;
      sel.value=c.number;status.textContent=c.override?'Cabinet bypassed: CAB OVERRIDE is on in pedal settings.':c.busy?'Loading cabinet…':c.error?'Cabinet unavailable — using built in.':!c.exists?'Empty cabinet slot — using built in.':'Cabinet choice is saved with the preset.';
    };
    show(cab);this.cabSync=show;
    sel.onchange=()=>this.act(()=>this.live.setCab(s,Number(sel.value)),'cabinet',true);
    return row;
  }
  field(s, i, p, v) {
    const row = el('div', 'field'), top = el('div', 'fieldtop'), id = `p-${s}-${i}`, label = el('label', '', p.name), out = el('output');
    label.htmlFor = id; top.append(label, out); row.appendChild(top);
    let inp;
    if (p.labels) { inp = el('select'); p.labels.forEach((l, k) => inp.add(new Option(l, p.min + k))); }
    else { inp = el('input'); inp.type = 'range'; inp.min = p.min; inp.max = p.max; inp.step = p.step; }
    inp.id = id; inp.value = v; inp.dataset.p = i;
    const show = () => { const t = formatValue(p, Number(inp.value)); out.textContent = p.labels ? '' : t; if (!p.labels) inp.setAttribute('aria-valuetext', t); };
    show(); inp.addEventListener('sync', show);
    inp.addEventListener(p.labels ? 'change' : 'input', () => { show(); this.queue(s, i, Number(inp.value)); });
    row.appendChild(inp); return row;
  }
  async refresh(c) {
    this.chainState = c;
    const s = this.selected, d = await this.live?.slot(s);
    if (!d || s !== this.selected || this.busy || this.pending.size) return;
    if (d.type !== this.inspType) { await this.renderInspector(); return; }
    this.switchEl?.setAttribute('aria-checked', d.enabled);
    if(d.type===3){const cab=await this.live.cab(s);if(cab && s===this.selected && !this.busy && document.activeElement!==this.inspector.querySelector('[data-cab]'))this.cabSync?.(cab);}
    d.values.forEach((v, p) => {
      const inp = this.inspector.querySelector(`[data-p="${p}"]`);
      if (inp && document.activeElement !== inp && Number(inp.value) !== v) { inp.value = v; inp.dispatchEvent(new Event('sync')); }
    });
  }

  queue(s, p, v) {
    const key = `${s}:${p}`, was = this.pending.get(key); this.pending.set(key, v);
    if (was !== undefined) return;
    const send = async () => {
      const value = this.pending.get(key);
      await this.act(() => this.live.setParam(s, p, value), 'value');
      if (this.pending.get(key) !== value) send(); else this.pending.delete(key);
    };
    setTimeout(send, 40);
  }
  async act(fn, what, rerender = false) {
    if (!this.live) return;
    this.busy = true; let ok = false;
    try { ok = await fn(); } catch { ok = false; }
    finally { this.busy = false; }
    if (ok) this.ui.edited();
    else this.ui.refused(what === 'effect' ? 'The pedal did not accept that effect in this block (it may be busy, or the chain would not fit).' : 'The pedal is busy; try again in a moment.');
    if ((rerender || !ok) && this.live) { const c = await this.live.chain(); if (c) await this.render(c); }
  }
}

