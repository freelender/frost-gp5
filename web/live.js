// Live effect editing (firmware/controller/live_host.h, 7E 14..18; 7E 1F order and
// 7E 20 save in slot_editor.h): the pedal's
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
    // beta.12: r[26] counts chains the pedal refused, r[27] says why (1 CHAIN TOO BIG, 2 DSP LIMIT).
    return { ready: !!r[2], types: [...r.slice(3, 9)], order: [...r.slice(9, 15)], enabled: [...r.slice(15, 21)], revision: rev, editable: !!r[25],
      refusals: r.length >= 28 ? r[26] : null, refused: r.length >= 28 ? r[27] : 0 };
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
  /** order[position] = slot, a permutation of 0..5 (7E 1F; Frost 32p or later). */
  async setOrder(order) { const r = await this.ask([0x7e, 0x1f, ...order]); return !!(r && r.length === 3 && r[2] === 1); }
  /** Save the live sound as preset `index` (0..59) named `name` (7E 20, the pedal's PRESETS -> SAVE). */
  async savePreset(index, name, { timeout = 20000, delay = ms => new Promise(r => setTimeout(r, ms)) } = {}) {
    if (!Number.isInteger(index) || index < 0 || index >= 60) throw new Error('Preset must be 1–60.');
    const label = [...new TextEncoder().encode((name || '').replace(/[^ -~]/g, '?').slice(0, 16))];
    const r = await this.ask([0x7e, 0x20, index, ...label]);
    if (!r) throw new Error('Saving from the website needs a newer Frost firmware. Save on the pedal: PRESETS → SAVE.');
    if (r[2] !== 1) throw new Error('The pedal is busy and did not start saving. Finish the current operation and try again.');
    const until = Date.now() + timeout;
    do {
      await delay(150);
      const s = await this.ask([0x7e, 0x20]);
      if (s && s.length >= 6 && s[2] === 1 && !s[3]) {
        if (s[4]) throw new Error(s[6] === 5 ? 'The pedal could not write the preset to its storage (storage busy or full). Try again in a moment.'
          : 'The pedal could not read the current sound back to save it (the sound kept changing). Try again in a moment.');
        return true;
      }
    } while (Date.now() < until);
    throw new Error('Saving did not finish. Read the presets again to check the result.');
  }
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
        if (s.error === 3) throw new Error('The pedal refused this preset: together its effects would overload the audio processor (DSP LIMIT). Replace a heavy effect (OCTAVE, NAM capture, a high-gain amp head, a reverb) and save it again.');
        if (s.error === 2) throw new Error('The pedal refused this preset: its effects do not fit together in the effect memory of the pedal. Replace one of the larger effects (NAM capture, OBSESS, OCTAVE, amp head, ENVWAH, MOUSE) and save it again.');
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
 * ui: { state(text), edited(), refused(text), lost(), limit(kind), changed(chain) }
 * limit(1 | 2): the pedal refused a chain (CHAIN TOO BIG / DSP LIMIT), as its popup.
 */
export class LiveView {
  constructor(strip, inspector, ui) {
    Object.assign(this, { strip, inspector, ui, live: null, timer: null, busy: false, paused: false, misses: 0, selected: null, inspType: null, pending: new Map() });
    strip.addEventListener('keydown', e => this.key(e));
    this.fxCategory = -1;
    this.fxSearch = document.getElementById('fx-search');
    this.fxResults = document.getElementById('fx-results');
    this.fxSearch.addEventListener('input', () => this.renderLibrary());
  }
  get attached() { return !!this.live; }
  async attach(ask) {
    this.live = new LiveChain(ask); this.misses = 0; await loadCatalog();
    const c = await this.live.chain();
    if (!c) { this.live = null; return false; }
    this.selected = c.order[0]; this.refusalsSeen = c.refusals; await this.render(c); this.poll(); return true;
  }
  detach() { clearTimeout(this.timer); this.timer = null; this.live = null; this.chainState = null; this.strip.textContent = ''; this.inspector.textContent = ''; }
  pause(on) { this.paused = on; }
  /** The pedal's refusal count has moved: show its popup once (website edits and pedal edits alike). */
  noteRefusals(c) {
    if (c.refusals == null) return;
    if (this.refusalsSeen != null && c.refusals !== this.refusalsSeen && c.refused) this.ui.limit?.(c.refused);
    this.refusalsSeen = c.refusals;
  }
  /** Take the current count as seen (after a preset load that reports its own error). */
  async ackRefusals() { try { const c = await this.live?.chain(); if (c) this.refusalsSeen = c.refusals; } catch { /* next poll */ } }

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
          this.misses = 0; this.noteRefusals(c);
          try { if (this.changed(c)) { await this.render(c); this.ui.changed?.(c); } else await this.refresh(c); } catch { /* next round */ }
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
      if (!empty) {
        const knobs = el('div', 'pedal-knobs'); knobs.setAttribute('aria-hidden','true');
        for (let i=0;i<Math.min(3,eff.params.length);i++) knobs.append(el('i'));
        b.insertBefore(knobs,b.children[1]);
        const foot=el('div','pedal-foot'); foot.setAttribute('aria-hidden','true'); b.append(foot);
      }
      b.onclick = async () => {
        await this.select(s);
        if (matchMedia('(max-width: 760px)').matches) this.inspector.scrollIntoView({ behavior: 'smooth', block: 'start' });
      };
      // Drag a block onto another position to reorder the chain (7E 1F).
      b.draggable = c.editable;
      // An effect card dragged from the library onto a block loads it there (7E 15).
      b.ondragstart = e => { this.dragging = s; b.classList.add('dragging'); e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', String(s)); };
      b.ondragend = () => this.clearDrag();
      b.ondragover = e => {
        if (this.dragEffect != null) {
          if (!this.chainState?.editable) return;
          e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; b.classList.add('dropinto'); return;
        }
        if (this.dragging == null || this.dragging === s) return;
        e.preventDefault(); e.dataTransfer.dropEffect = 'move';
        const after = c.order.indexOf(s) > c.order.indexOf(this.dragging);
        b.classList.toggle('dropafter', after); b.classList.toggle('dropbefore', !after);
      };
      b.ondragleave = e => { if (!b.contains(e.relatedTarget)) b.classList.remove('dropbefore', 'dropafter', 'dropinto'); };
      b.ondrop = e => {
        e.preventDefault(); const from = this.dragging, t = this.dragEffect; this.clearDrag();
        if (t != null) this.loadInto(s, t);
        else if (from != null && from !== s) this.moveTo(from, c.order.indexOf(s));
      };
      this.strip.appendChild(b);
    });
    this.renderLibrary();
    await this.renderInspector();
  }
  clearDrag() {
    this.dragging = null; this.dragEffect = null;
    for (const x of this.strip.children) x.classList.remove('dragging', 'dropbefore', 'dropafter', 'dropinto');
    for (const x of this.fxResults.children) x.classList.remove('dragging');
  }
  /** Load effect type `t` into block `s` (library click or drop); the block becomes selected. */
  async loadInto(s, t) {
    if (this.busy || this.paused || this.pending.size || !this.chainState?.editable) return;
    if (this.selected !== s) await this.select(s);
    if (this.chainState.types[s] === t) return;
    this.fxResults.setAttribute('aria-busy', 'true');
    try { await this.act(() => this.live.setType(s, t), 'effect', true); } finally { this.fxResults.removeAttribute('aria-busy'); }
  }
  /** Move block `slot` to chain position `pos` (0..5); the other blocks keep their order. */
  moveTo(slot, pos) {
    const order = this.chainState.order.filter(x => x !== slot);
    order.splice(Math.max(0, Math.min(5, pos)), 0, slot);
    if (order.join() === this.chainState.order.join()) return;
    this.selected = slot;
    return this.act(() => this.live.setOrder(order), 'order', true);
  }
  key(e) {
    if (e.altKey && (e.key === 'ArrowLeft' || e.key === 'ArrowRight') && this.chainState?.editable) {   // Alt+arrow moves the block
      e.preventDefault(); const pos = this.chainState.order.indexOf(this.selected);
      this.moveTo(this.selected, pos + (e.key === 'ArrowLeft' ? -1 : 1))?.then(() => this.strip.querySelector(`[data-slot="${this.selected}"]`)?.focus());
      return;
    }
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
    this.renderLibrary();
    await this.renderInspector();
  }

  renderLibrary() {
    if (!catalog || !this.chainState) return;
    const c=this.chainState,s=this.selected,pos=c.order.indexOf(s),current=c.types[s];
    document.getElementById('fx-target').textContent=`Slot ${pos+1} · ${catalog.effects[current]?.name || 'Empty'}${c.editable?'':' · Pedal busy'}`;
    const filters=document.getElementById('fx-filters');
    if (!filters.children.length) ['All',...catalog.categories].forEach((name,i)=>{
      const b=el('button','fx-filter',name);b.type='button';b.dataset.category=i-1;
      b.onclick=()=>{this.fxCategory=i-1;this.renderLibrary();};filters.append(b);
    });
    for(const b of filters.children)b.setAttribute('aria-pressed',Number(b.dataset.category)===this.fxCategory);
    const q=this.fxSearch.value.trim().toLowerCase();
    const ids=[...new Set(catalog.picker.flat())].filter(t=>{const e=catalog.effects[t];return e&&t!==EMPTY&&(this.fxCategory===-1||e.category===this.fxCategory)&&`${e.name} ${e.short} ${catalog.categories[e.category]}`.toLowerCase().includes(q);});
    document.getElementById('fx-count').textContent=`${ids.length} effects`;
    this.fxResults.replaceChildren();
    for(const t of ids){
      const e=catalog.effects[t],b=el('button','fx-card');b.type='button';b.dataset.effect=t;b.style.setProperty('--effect-color',catalog.colors[e.category]);b.disabled=!c.editable;if(t===current){b.classList.add('current');b.setAttribute('aria-current','true');}
      // Drag the card onto any block in the chain to load it there.
      b.draggable=c.editable;
      b.ondragstart=ev=>{this.dragEffect=t;this.dragging=null;b.classList.add('dragging');ev.dataTransfer.effectAllowed='copy';ev.dataTransfer.setData('text/plain',e.name);};
      b.ondragend=()=>this.clearDrag();
      const art=el('span','fx-mini',ICONS[e.category]||'◈');art.setAttribute('aria-hidden','true');
      const info=el('span','fx-info');info.append(el('strong','',e.name),el('small','',catalog.categories[e.category]));
      b.append(art,info,el('span','fx-add',t===current?'✓':'+'));
      b.setAttribute('aria-label',`${e.name}, ${t===current?'currently loaded':'load into slot '+(pos+1)}`);
      b.title=t===current?'Loaded in the selected block':'Click to load into the selected block, or drag onto any block';
      b.onclick=()=>{if(t!==current&&this.selected===s)this.loadInto(s,t);};
      this.fxResults.append(b);
    }
    if(!ids.length)this.fxResults.append(el('p','fx-no-results','No matching effects. Try another search or category.'));
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
    const box = el('fieldset', 'pedalface' + (eff ? '' : ' emptyface') + (eff && !d.enabled ? ' bypassed' : '')); box.disabled = !c.editable;
    if (eff) box.style.setProperty('--effect-color', catalog.colors[eff.category]);
    this.switchEl = null;
    if (eff) {
      const sw = el('button', 'switch'); sw.type = 'button'; sw.setAttribute('role', 'switch'); sw.setAttribute('aria-checked', d.enabled);
      sw.setAttribute('aria-label', `${eff.name} on`); sw.title = 'Switch this block on or off'; sw.disabled = !c.editable;
      sw.onclick = () => { const on = sw.getAttribute('aria-checked') !== 'true'; sw.setAttribute('aria-checked', on); box.classList.toggle('bypassed', !on); this.act(() => this.live.setEnabled(s, on), 'switch', true); };
      const rm = el('button', 'btn small danger', 'Remove'); rm.type = 'button'; rm.disabled = !c.editable;
      rm.title = 'Remove this effect from the chain'; rm.setAttribute('aria-label', `Remove ${eff.name} from block ${pos + 1}`);
      rm.onclick = async () => {
        const yes = this.ui.confirm ? await this.ui.confirm(`Remove ${eff.name}?`, `Block ${pos + 1} becomes empty. Like every live edit, this is not saved until you save a preset on the pedal.`) : confirm(`Remove ${eff.name}?`);
        if (yes && this.selected === s) this.act(() => this.live.setType(s, EMPTY), 'effect', true);
      };
      const left = el('button', 'btn small ghost', '◀'), right = el('button', 'btn small ghost', '▶');
      left.type = right.type = 'button'; left.title = 'Move this block one place earlier (or drag it in the chain)'; right.title = 'Move this block one place later';
      left.setAttribute('aria-label', `Move ${eff.name} earlier`); right.setAttribute('aria-label', `Move ${eff.name} later`);
      left.disabled = !c.editable || pos === 0; right.disabled = !c.editable || pos === 5;
      left.onclick = () => this.moveTo(s, pos - 1); right.onclick = () => this.moveTo(s, pos + 1);
      const side = el('div', 'headbtns'); side.append(left, right, rm, sw);
      this.switchEl = sw; head.appendChild(side);
    }
    this.inspector.append(head, box);

    const actions=el('div','effect-actions'), browse=el('button','btn ghost browse-effects',eff?'Replace effect':'Add Effect');
    browse.type='button'; browse.onclick=()=>{this.fxSearch.scrollIntoView({block:'nearest'});this.fxSearch.focus();};
    actions.append(browse);box.append(actions);
    if (!eff) { box.appendChild(el('p', 'noparams', 'Choose an effect to fill this block. A new block starts switched off.')); return; }
    if (!eff.params.length) box.appendChild(el('p', 'noparams', 'This effect has no settings.'));
    if(d.type===3){
      const cab=await this.live.cab(s);if(s!==this.selected)return;
      if(cab)box.appendChild(this.cabField(s,cab));
      else box.appendChild(el('p','noparams','Cabinet selection requires the IR-control firmware update.'));
    }
    if(d.type===33)box.appendChild(this.modelField(s,d.values[0] ?? eff.params[0].initial));
    const knobs = el('div', 'knobrow');
    eff.params.forEach((p, i) => { if(d.type!==33 || i!==0)knobs.appendChild(this.field(s, i, p, d.values[i] ?? p.initial)); });
    if (eff.params.length) box.appendChild(knobs);
    box.appendChild(el('p', 'knobhint', 'Drag a knob up or down or scroll on it (Shift: fine), double-click for the default, or use the arrow keys.'));
  }
  modelField(s,value) {
    const row=el('label','picker'), sel=el('select');
    row.append(el('span','inputlabel','MODEL'),sel);
    sel.id=`p-${s}-0`;sel.dataset.p=0;
    sel.setAttribute('aria-label','MODEL');
    for(let i=0;i<=63;i++)sel.add(new Option('',i));
    if(this.ui.addFile)sel.add(new Option('Add a model…','add'));
    const show=()=>{
      const models=this.ui.models?.() || {}, options=sel.options || sel.children;
      for(let i=0;i<=63;i++)options[i].text=`${String(i).padStart(2,'0')} · ${models[i]?.label || (i===0?'Built-in default':'Empty — default fallback')}`;
    };
    show();sel.value=value;
    sel.addEventListener('sync',()=>{value=Number(sel.value);show();});
    sel.addEventListener('change',()=>{
      if(sel.value==='add'){sel.value=value;this.ui.addFile('amps',s);return;}
      value=Number(sel.value);this.queue(s,0,value);
    });
    return row;
  }
  cabField(s,cab) {
    const row=el('label','picker'), sel=el('select'), status=el('p','noparams');
    row.append(el('span','inputlabel','CAB'),sel,status);sel.dataset.cab='1';
    sel.setAttribute('aria-label','CAB');
    sel.add(new Option('00 · Built in',0));const cabs=this.ui.cabs?.() || {};
    for(let i=1;i<=63;i++)sel.add(new Option(`${String(i).padStart(2,'0')} · ${cabs[i-1]?.label || (cab.number===i && cab.exists?cab.name:'Empty — built-in fallback')}`,i));
    if(this.ui.addFile)sel.add(new Option('Add a cabinet…','add'));
    const show=c=>{
      const latest=this.ui.cabs?.() || {}, options=sel.options || sel.children;
      for(let i=1;i<=63;i++)options[i].text=`${String(i).padStart(2,'0')} · ${latest[i-1]?.label || (c.number===i && c.exists?c.name:'Empty — built-in fallback')}`;
      cab=c;sel.value=c.number;status.textContent=c.override?'Cabinet bypassed: CAB OVERRIDE is on in pedal settings.':c.busy?'Loading cabinet…':c.error?'Cabinet unavailable — using built in.':!c.exists?'Empty cabinet slot — using built in.':'Cabinet choice is saved with the preset.';
    };
    show(cab);this.cabSync=show;
    sel.onchange=()=>{
      if(sel.value==='add'){sel.value=cab.number;this.ui.addFile('cabs',s);return;}
      return this.act(()=>this.live.setCab(s,Number(sel.value)),'cabinet',true);
    };
    return row;
  }
  /**
   * One parameter as a pedal control. Choices: a drop-down pill. Ranges: a knob.
   * The knob is drawn over a real range input (visually hidden), which stays the
   * value, the keyboard control and what refresh()/'sync' update. Drag up/down or
   * use the wheel to turn it (Shift: fine), double-click: back to the default.
   */
  field(s, i, p, v) {
    const id = `p-${s}-${i}`, label = el('label', 'knoblabel', p.name);
    label.htmlFor = id;
    if (p.labels) {
      const row = el('div', 'field choicefield'), inp = el('select'), wrap = el('div', 'choicepill');
      p.labels.forEach((l, k) => inp.add(new Option(l, p.min + k)));
      inp.id = id; inp.value = v; inp.dataset.p = i;
      inp.addEventListener('change', () => this.queue(s, i, Number(inp.value)));
      wrap.append(inp, el('span', 'choicecaret', '▾'));
      row.append(el('div', 'knobvalue choicehead', ' '), wrap, label); return row;
    }
    const row = el('div', 'field knobfield'), out = el('output', 'knobvalue'), inp = el('input', 'knobinput');
    inp.type = 'range'; inp.min = p.min; inp.max = p.max; inp.step = p.step; inp.id = id; inp.value = v; inp.dataset.p = i;
    out.htmlFor = id;
    const knob = el('div', 'knob'), cap = el('div', 'knobcap');
    knob.append(cap); knob.title = `${p.name}: drag up or down, or scroll (Shift: fine). Double-click: default.`;
    const span = p.max - p.min || 1;
    const show = () => {
      const x = Number(inp.value), t = formatValue(p, x);
      out.textContent = t; inp.setAttribute('aria-valuetext', t);
      knob.style.setProperty('--pct', ((x - p.min) / span).toFixed(4));
    };
    const set = x => {
      const before = inp.value; inp.value = Math.min(p.max, Math.max(p.min, x));
      if (inp.value !== before) { show(); this.queue(s, i, Number(inp.value)); }
    };
    show(); inp.addEventListener('sync', show);
    inp.addEventListener('input', () => { show(); this.queue(s, i, Number(inp.value)); });
    let drag = null;
    knob.addEventListener('pointerdown', e => {
      if (inp.matches(':disabled') || e.button) return;
      e.preventDefault(); inp.focus({ preventScroll: true });
      knob.setPointerCapture(e.pointerId); drag = { y: e.clientY, v: Number(inp.value) }; knob.classList.add('turning');
    });
    knob.addEventListener('pointermove', e => {
      if (!drag) return;
      const px = e.shiftKey ? 600 : 160;          // pixels for a full sweep
      set(drag.v + (drag.y - e.clientY) / px * span);
    });
    const end = () => { drag = null; knob.classList.remove('turning'); };
    knob.addEventListener('pointerup', end); knob.addEventListener('pointercancel', end);
    knob.addEventListener('wheel', e => {
      if (inp.matches(':disabled')) return;
      e.preventDefault();
      const step = Math.max(Number(p.step) || 1, e.shiftKey ? 0 : span / 50);
      set(Number(inp.value) + (e.deltaY < 0 ? step : -step));
    }, { passive: false });
    knob.addEventListener('dblclick', () => { if (!inp.matches(':disabled')) set(p.initial); });
    const dial = el('div', 'knobdial'); dial.append(knob, inp);
    row.append(out, dial, label); return row;
  }
  async refresh(c) {
    this.chainState = c;
    const s = this.selected, d = await this.live?.slot(s);
    if (!d || s !== this.selected || this.busy || this.pending.size) return;
    if (d.type !== this.inspType) { await this.renderInspector(); return; }
    this.switchEl?.setAttribute('aria-checked', d.enabled);
    this.inspector.querySelector('.pedalface')?.classList.toggle('bypassed', !d.enabled);
    if(d.type===3){const cab=await this.live.cab(s);if(cab && s===this.selected && !this.busy && document.activeElement!==this.inspector.querySelector('[data-cab]'))this.cabSync?.(cab);}
    d.values.forEach((v, p) => {
      const inp = this.inspector.querySelector(`[data-p="${p}"]`);
      if (inp && document.activeElement !== inp) {
        const changed=Number(inp.value)!==v;
        if(changed)inp.value=v;
        if(changed || (d.type===33 && p===0))inp.dispatchEvent(new Event('sync'));
      }
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
    else this.ui.refused(what === 'effect' ? 'The pedal did not accept that effect in this block (it may be busy, or the chain would not fit).'
      : what === 'order' ? 'The pedal did not accept the new order (it may be busy). Reordering from the website needs Frost 32p or later.'
      : 'The pedal is busy; try again in a moment.');
    if ((rerender || !ok) && this.live) { const c = await this.live.chain(); if (c) { this.noteRefusals(c); await this.render(c); } }
  }
}

