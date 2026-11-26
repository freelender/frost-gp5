import { emptySetlist, encodeSetlist, SetlistStore } from './setlist.js?v=1.9.0';
const el = (tag, text, cls) => { const e = document.createElement(tag); if (text != null) e.textContent = text; if (cls) e.className = cls; return e; };
export class SetlistView {
  constructor(root, ui) {
    this.root = root; this.ui = ui; this.data = emptySetlist(); this.selected = 0; this.dirty = false; this.presets = {}; this.storage = null;
    try { const draft = localStorage.getItem('frost-setlist-draft'); if (draft) { this.data = JSON.parse(draft); encodeSetlist(this.data); this.dirty = true; } } catch { this.data = emptySetlist(); }
    this.render();
  }
  changed(render = true) { this.dirty = true; try { localStorage.setItem('frost-setlist-draft', JSON.stringify(this.data)); } catch { /* export remains available */ } if (render) this.render(); else if (this.summary) this.summary.textContent = this.summaryText(); }
  summaryText() { let usage; try { const { used, capacity } = encodeSetlist(this.data); usage = `${used} / ${capacity} song bytes`; } catch (e) { usage = e.message; } return `${this.dirty ? 'Unsaved browser draft' : 'Song library'} · ${usage}. ${this.storage ? '' : 'Connect the pedal to read or save.'}`; }
  async attach(store, presets) {
    this.presets = presets || {};
    if (store && this.storage?.store === store && this.storage.base !== undefined) { this.render(); return; }
    this.storage = store ? new SetlistStore(store) : null;
    if (this.storage) {
      try { const data = await this.storage.read(); if (!this.dirty) this.data = data; }
      catch (e) { this.ui.error(e.message); }
    }
    this.render();
  }
  button(label, action, aria) { const b = el('button', label, 'btn small ghost'); b.type = 'button'; if (aria) b.setAttribute('aria-label', aria); b.onclick = action; return b; }
  input(label, value, change) { const l = el('label'), i = el('input'); l.append(el('span', label, 'inputlabel')); i.value = value; i.maxLength = 16; i.oninput = () => { change(i.value); this.changed(false); }; l.append(i); return l; }
  move(array, index, delta) { const to = index + delta; if (to < 0 || to >= array.length) return; [array[index], array[to]] = [array[to], array[index]]; this.changed(); }
  render() {
    const r = this.root; r.replaceChildren();
    const toolbar = el('div', null, 'actions'), read = this.button('Read from pedal', async () => {
      if (this.dirty && !await this.ui.confirm('Replace this draft?', 'Read the saved songs and setlist from the pedal, replacing this browser draft.', 'Read')) return;
      await this.ui.exclusive('Reading setlist', async () => { this.data = await this.storage.read(); this.dirty = false; localStorage.removeItem('frost-setlist-draft'); this.render(); });
    }); read.disabled = !this.storage;
    const write = this.button('Save setlist to pedal', async () => {
      try { encodeSetlist(this.data); } catch (e) { this.ui.error(e.message); return; }
      const missing = this.data.songs.some(s => s.parts.some(p => !this.presets[p.preset]));
      if (!await this.ui.confirm('Save songs and setlist?', `Replace the pedal’s song library and setlist with this draft (${this.data.songs.length} songs, ${this.data.order.length} entries).${missing ? ' Some preset references are missing and cannot be played until saved.' : ''}`, 'Save')) return;
      await this.ui.exclusive('Saving setlist', async () => { await this.storage.write(this.data); this.dirty = false; localStorage.removeItem('frost-setlist-draft'); this.ui.saved(); this.render(); });
    }); write.disabled = !this.storage; write.className = 'btn small primary';
    const remove = this.button('Remove setlist from pedal', async () => {
      if (!await this.ui.confirm('Remove the setlist from the pedal?', 'The songs and setlist stored on the pedal are deleted; the Setlist home screen then shows NO SETLIST. Presets and global settings are kept. This browser draft stays, so you can save it again.', 'Remove', { danger: true })) return;
      await this.ui.exclusive('Removing setlist', async () => { await this.storage.read(); await this.storage.clear(); this.changed(false); this.ui.removed?.(); this.render(); });
    }); remove.disabled = !this.storage; remove.className = 'btn small danger';
    const exp = this.button('Export songs', () => this.ui.download(JSON.stringify(this.data, null, 2), 'frost-setlist.json'));
    const imp = el('label', 'Import songs', 'btn small ghost'), file = el('input'); file.type = 'file'; file.accept = '.json'; file.hidden = true;
    file.onchange = async () => { try { if (!file.files[0]) return; const data = JSON.parse(await file.files[0].text()); encodeSetlist(data); if (await this.ui.confirm('Import songs?', 'Replace this browser draft with the imported song library and setlist.', 'Import')) { this.data = data; this.changed(); } } catch (e) { this.ui.error(e.message); } }; imp.append(file);
    toolbar.append(read, exp, imp); r.append(toolbar);
    this.summary = el('p', this.summaryText(), 'hint'); r.append(this.summary);
    const grid = el('div', null, 'grid2'), library = el('div', null, 'panel pad'), order = el('div', null, 'panel pad'); grid.append(library, order); r.append(grid);
    library.append(el('h2', 'Songs'));
    const pick = el('select'); pick.setAttribute('aria-label', 'Song to edit');
    this.data.songs.forEach((song, i) => { const opt = el('option', song.name); opt.value = i; pick.append(opt); });
    this.selected = Math.min(this.selected, Math.max(0, this.data.songs.length - 1)); pick.value = this.selected; pick.onchange = () => { this.selected = Number(pick.value); this.render(); }; library.append(pick);
    const add = this.button('New song', () => { const id = Array.from({ length: 64 }, (_, i) => i).find(id => !this.data.songs.some(s => s.id === id)); if (id === undefined) return this.ui.error('The library supports at most 64 songs.'); this.data.songs.push({ id, name: `Song ${id + 1}`, parts: [{ name: 'Intro', preset: Number(Object.keys(this.presets)[0] || 0) }] }); this.selected = this.data.songs.length - 1; this.changed(); }); library.append(add);
    const song = this.data.songs[this.selected];
    if (song) {
      library.append(this.input('Song name', song.name, v => song.name = v));
      const actions = el('div', null, 'actions'); actions.append(this.button('Add to setlist', () => { if (this.data.order.length >= 64) return this.ui.error('The setlist supports at most 64 entries.'); this.data.order.push(song.id); this.changed(); }), this.button('Delete song', async () => { if (await this.ui.confirm('Delete song?', 'Remove this song and its entries from this draft. Presets are kept.', 'Delete')) { this.data.songs.splice(this.selected, 1); this.data.order = this.data.order.filter(id => id !== song.id); this.changed(); } })); library.append(actions);
      const parts = el('ol', null, 'setlist-parts'); library.append(parts);
      song.parts.forEach((part, i) => {
        const row = el('li'), sel = el('select'); sel.setAttribute('aria-label', `Preset for part ${i + 1}`);
        for (let p = 0; p < 60; p++) { const opt = el('option', `${String(p + 1).padStart(2, '0')} · ${this.presets[p]?.name || 'Missing preset'}`); opt.value = p; sel.append(opt); } sel.value = part.preset; sel.onchange = () => { part.preset = Number(sel.value); this.changed(); };
        row.append(this.input(`Part ${i + 1}`, part.name, v => part.name = v), sel, this.button('↑', () => this.move(song.parts, i, -1), `Move part ${i + 1} up`), this.button('↓', () => this.move(song.parts, i, 1), `Move part ${i + 1} down`));
        if (song.parts.length > 1) row.append(this.button('Remove', () => { song.parts.splice(i, 1); this.changed(); })); parts.append(row);
      });
      library.append(this.button('Add part', () => { if (song.parts.length >= 16) return this.ui.error('A song supports at most 16 parts.'); song.parts.push({ name: `Part ${song.parts.length + 1}`, preset: song.parts.at(-1).preset }); this.changed(); }));
    } else library.append(el('p', 'Create a song, name its parts and choose a saved preset for each.', 'hint'));
    order.append(el('h2', 'Setlist'), this.input('Setlist name', this.data.name, v => this.data.name = v));
    const items = el('ol', null, 'setlist-order'); order.append(items);
    this.data.order.forEach((id, i) => { const row = el('li'), song = this.data.songs.find(s => s.id === id); row.append(el('b', song?.name || 'Missing song'), this.button('↑', () => this.move(this.data.order, i, -1), `Move song ${i + 1} up`), this.button('↓', () => this.move(this.data.order, i, 1), `Move song ${i + 1} down`), this.button('Remove', () => { this.data.order.splice(i, 1); this.changed(); })); items.append(row); });
    if (!this.data.order.length) order.append(el('p', 'Add songs from the library, then arrange them in performance order. A song may appear more than once.', 'hint'));
    const onPedal = el('div', null, 'actions'); onPedal.append(write, remove); order.append(onPedal);
    if (!this.storage) order.append(el('p', 'Connect the pedal to save or remove its setlist.', 'hint'));
    order.append(el('p', 'On the pedal: GLOBAL SETTINGS → DISPLAY → HOME SCREEN → SETLIST. Tap the footswitch for the next part. DISPLAY → SETLIST HOLD chooses Previous part or Tuner. Turn PARA to select a song; press PARA to edit the sound. The first and last parts stop at the ends of the setlist.', 'hint'));
  }
}
