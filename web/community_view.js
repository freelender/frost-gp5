// Community page: loops, presets, amp models and cabinets shared by GP-5 players, each with the nickname of who made
// it. No account: this browser keeps a signing key (community.js), and the first upload of each nickname waits for the
// owner's approval. A loop can be heard, recorded over in the Looper (the take is shared as a reply) and sent as a
// link that plays in any browser; presets, models and cabinets go straight to the pedal through the usual import.
import * as C from './community.js?v=1.9.1';

const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const own = (tag, cls, text) => { const e = el(tag, cls, text); e.dataset.i18nSkip = ''; return e; };   // a name people wrote: never translated
const KIND_LABEL = { loop: 'Loop', preset: 'Preset', model: 'Amp model', cab: 'Cabinet' };
const TABS = [['loop', 'Loops'], ['preset', 'Presets'], ['model', 'Amp models'], ['cab', 'Cabinets'], ['mine', 'My uploads'], ['queue', 'Approvals']];
const SIGNATURES = { 4: '4/4', 3: '3/4', 6: '6/8', 5: '5/4', 7: '7/8' };
const MAX = { loop: 20e6, preset: 256e3, model: 4e6, cab: 2e6 };
const NICK_RE = /^[\p{L}\p{N}][\p{L}\p{N} ._-]{1,23}$/u;
const ERRORS = {
  offline: 'The community is not online yet.',
  network: 'Could not reach the community. Check the internet connection and try again.',
  'no-key': 'Choose a nickname first.',
  'nick-taken': 'That nickname belongs to someone else. Choose another one.',
  'bad-nick': 'A nickname has 2 to 24 letters or numbers (spaces, dots and dashes are fine).',
  'no-title': 'Give it a title.',
  'too-big': 'The file is too big to share.',
  'bad-file': 'This file cannot be shared. Use a .frostpreset, .nam, .fcm or .wav file.',
  limit: 'Too many uploads today. Try again tomorrow.',
  'queue-full': 'Your uploads are waiting for the owner\'s approval. Send more once they are approved.',
  banned: 'This signature can no longer publish.',
  unsigned: 'The signature of this browser was refused. Check the computer clock and try again.',
  clock: 'The computer clock is wrong. Fix the date and time and try again.',
  'not-found': 'This item is no longer available.',
  admin: 'Only the owner can do that.',
  server: 'The community had a problem. Try again in a moment.',
};
export const errorText = e => ERRORS[e?.code] || e?.message || ERRORS.server;
const kindOfFile = name => (/\.frostpreset$/i.test(name) ? 'preset' : /\.(nam|fcm)$/i.test(name) ? 'model' : /\.wav$/i.test(name) ? 'cab' : null);
const seconds = it => (it.kind === 'loop' && it.size > 44 ? Math.round((it.size - 44) / 4 / 44100) : 0);

export class CommunityView {
  /** hooks: toast(text, kind), ask(title, text, ok, opts), jam(item, url) records over a loop in the Looper,
   *  use(kind, file) hands a preset, model or cabinet to the pedal's import. */
  constructor(root, hooks) {
    this.root = root; this.hooks = hooks; this.tab = 'loop'; this.items = []; this.next = null; this.me = null; this.id = null;
    this.audio = new Audio(); this.audio.loop = true; this.playing = null; this.objUrl = null; this.loaded = false; this.pending = null;
    const shared = new URLSearchParams(location.search).get('item');
    this.shared = /^[\w-]{12}$/.test(shared || '') ? shared : null;
    this.build();
  }
  q(role) { return this.root.querySelector(`[data-role="${role}"]`); }
  toast(text, kind) { this.hooks.toast?.(text, kind); }
  /** The page became visible: the first time, read the identity and the list. */
  shown() {
    if (this.loaded) return;
    this.loaded = true; this.refreshMe();
    if (this.shared) this.showShared(this.shared); else this.load();
  }
  build() {
    this.root.innerHTML = `
      <div class="cmme" data-role="me"></div>
      <form class="ctlpanel cmpublish" data-role="publish" hidden novalidate><legend>Share with the community</legend>
        <div class="cmfile"><button class="btn ghost small" type="button" data-role="pick">Choose a file</button><span class="cmfacts" data-role="fileName"></span></div>
        <p class="ctlnote" data-role="fileHint">A preset (.frostpreset, from Presets → Download), an amp model (.nam or .fcm) or a cabinet (.wav). Loops are shared from the Looper page.</p>
        <p class="ctlnote" data-role="replyNote" hidden></p>
        <div class="cmform">
          <label><span class="ctllabel">Title</span><input data-role="title" maxlength="60" autocomplete="off"></label>
          <label><span class="ctllabel">Style</span><input data-role="style" maxlength="30" placeholder="Blues, metal, clean…" autocomplete="off"></label>
          <label data-role="keyRow"><span class="ctllabel">Key of the song</span><input data-role="key" maxlength="8" placeholder="A, Em…" autocomplete="off"></label>
          <label class="cmwide"><span class="ctllabel">About it (optional)</span><textarea data-role="note" maxlength="300" rows="2"></textarea></label>
        </div>
        <p class="ctlnote">Everything shared here is public. Your first upload waits for the owner's approval; after that, your uploads appear at once.</p>
        <div class="cmactions"><button class="btn primary small" type="submit" data-role="send">Publish</button><button class="btn ghost small" type="button" data-role="cancel">Cancel</button></div>
        <input type="file" hidden data-role="file" accept=".frostpreset,.fcm,.nam,.wav">
      </form>
      <div class="cmtabs" role="tablist" aria-label="Community" data-role="tabs"></div>
      <div class="cmtools"><input type="search" data-role="search" placeholder="Search by title, nickname or style" aria-label="Search the community"><button class="btn primary small" type="button" data-role="share">Share a file</button></div>
      <p class="cmnotice" data-role="notice" hidden></p>
      <div class="cmlist" data-role="list"></div>
      <div class="cmmore"><button class="btn ghost small" type="button" data-role="more" hidden>Show more</button></div>
      <input type="file" hidden data-role="keyFile" accept=".json,application/json">`;
    this.q('share').onclick = () => this.openPublish();
    this.q('pick').onclick = () => this.q('file').click();
    this.q('file').onchange = e => { const f = e.target.files[0]; e.target.value = ''; if (f) this.takeFile(f); };
    this.q('cancel').onclick = () => this.closePublish();
    this.q('publish').onsubmit = e => { e.preventDefault(); this.submit(); };
    this.q('more').onclick = () => this.load(true);
    this.q('keyFile').onchange = async e => {
      const f = e.target.files[0]; e.target.value = ''; if (!f) return;
      try { await C.importIdentity(await f.text()); this.toast('Key opened: you publish with this signature now.', 'ok'); this.refreshMe(); }
      catch (err) { this.toast(err.message, 'bad'); }
    };
    let typing;
    this.q('search').oninput = () => { clearTimeout(typing); typing = setTimeout(() => this.load(), 350); };
    this.renderMe(); this.renderTabs();
  }

  // ---- who publishes from this browser
  async refreshMe() {
    this.id = await C.identity();
    this.me = null;
    if (this.id && C.serverUrl()) try { this.me = await C.me(); } catch { this.me = null; }
    this.renderMe(); this.renderTabs();
  }
  renderMe() {
    const box = this.q('me'); box.textContent = '';
    const button = (text, cls, fn) => { const b = el('button', 'btn small ' + cls, text); b.type = 'button'; b.onclick = fn; return b; };
    if (!this.id) {
      box.append(el('p', 'cmintro', 'To publish, choose a nickname. There is no account: this browser signs what you share, and the nickname becomes yours.'));
      const row = el('div', 'cmrow'), nick = el('input'); nick.maxLength = 24; nick.placeholder = 'Your nickname'; nick.setAttribute('aria-label', 'Your nickname'); nick.dataset.role = 'nick';
      row.append(nick, button('Create my signature', 'primary', () => this.createIdentity(nick.value)), button('Use a saved key', 'ghost', () => this.q('keyFile').click()));
      nick.onkeydown = e => { if (e.key === 'Enter') this.createIdentity(nick.value); };
      box.append(row); return;
    }
    const row = el('div', 'cmrow');
    row.append(el('span', 'cmlabel', 'You publish as'), own('b', 'cmnick', this.id.nick));
    if (this.me?.admin) row.append(el('span', 'pill ok', 'Owner'));
    else if (this.me && !this.me.trusted) row.append(el('span', 'cmhint', 'Your first upload waits for approval'));
    row.append(el('span', 'ctlgap'), button('Change the nickname', 'ghost', () => this.changeNick()), button('Save my key', 'ghost', () => this.saveKey()), button('Use a saved key', 'ghost', () => this.q('keyFile').click()));
    const code = el('div', 'cmcode'); code.append(el('span', '', 'Author code'), own('code', '', this.id.key.id));
    box.append(row, code);
  }
  async createIdentity(nick) {
    nick = String(nick || '').trim();
    if (!NICK_RE.test(nick)) { this.toast(ERRORS['bad-nick'], 'bad'); return; }
    await C.createIdentity(nick);
    this.toast(`You publish as ${nick}. Save your key to use the same name on another computer.`, 'ok');
    await this.refreshMe();
  }
  async changeNick() {
    const nick = await this.hooks.ask('Change the nickname', 'Your next upload carries the new nickname, and your earlier uploads show it too.', 'Change',
      { input: { label: 'Nickname', value: this.id.nick, maxLength: 24, validate: v => (NICK_RE.test(v.trim()) ? '' : ERRORS['bad-nick']) } });
    if (nick == null || !nick.trim()) return;
    await C.setNick(nick.trim()); this.refreshMe();
  }
  async saveKey() {
    const text = await C.exportIdentity(); if (!text) return;
    const a = el('a'); a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    a.download = `frost-community-key-${this.id.nick.replace(/[^\w-]+/g, '-')}.json`; document.body.append(a); a.click();
    setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
    this.toast('Key saved. Keep it private: whoever has this file publishes with your name.', 'ok');
  }

  // ---- lists
  renderTabs() {
    const box = this.q('tabs'); box.textContent = '';
    for (const [key, label] of TABS) {
      if (key === 'mine' && !this.id) continue;
      if (key === 'queue' && !this.me?.admin) continue;
      const b = el('button', '', label); b.type = 'button'; b.setAttribute('role', 'tab'); b.setAttribute('aria-selected', String(this.tab === key));
      b.onclick = () => this.setTab(key); box.append(b);
    }
    this.q('search').hidden = this.tab === 'mine' || this.tab === 'queue';
  }
  setTab(key) {
    this.tab = key; this.shared = null; this.q('search').value = '';
    if (location.search.includes('item=')) history.replaceState(null, '', location.pathname + location.hash);
    this.renderTabs(); this.load();
  }
  notice(text, retry) {
    const n = this.q('notice'); n.hidden = !text; n.textContent = text || '';
    if (text && retry) { const b = el('button', 'btn ghost small', 'Try again'); b.type = 'button'; b.onclick = retry; n.append(' ', b); }
  }
  async load(more = false) {
    if (!C.serverUrl()) { this.notice(ERRORS.offline); this.q('list').textContent = ''; return; }
    const list = this.q('list'), tab = this.tab, q = this.q('search').value.trim();
    if (!more) { this.items = []; this.next = null; list.textContent = ''; this.stopListening(); }
    this.notice(''); this.q('more').hidden = true;
    let got;
    try {
      if (tab === 'mine') got = { items: (await C.me()).items, next: null };
      else if (tab === 'queue') got = { items: (await C.queue()).items, next: null };
      else got = await C.feed({ kind: tab, q, before: more ? this.next : null });
    } catch (e) { this.notice(errorText(e), () => this.load(more)); return; }
    if (tab !== this.tab) return;                                   // the person changed tabs meanwhile
    this.items.push(...got.items); this.next = got.next;
    for (const it of got.items) list.append(this.card(it, { mine: tab === 'mine', admin: tab === 'queue' }));
    if (!this.items.length) list.append(el('p', 'cmempty', tab === 'mine' ? 'You have not shared anything yet.' : tab === 'queue' ? 'Nothing waiting. All clear.'
      : q ? 'Nothing found.' : 'Nothing here yet. Be the first to share.'));
    this.q('more').hidden = !this.next;
  }
  async showShared(id) {
    const list = this.q('list'); list.textContent = ''; this.notice('');
    if (!C.serverUrl()) { this.notice(ERRORS.offline); return; }
    let t; try { t = await C.item(id); } catch (e) { this.notice(errorText(e)); this.shared = null; this.load(); return; }
    this.tab = t.item.kind; this.renderTabs();
    const head = el('div', 'cmshared'); head.append(el('b', '', 'Shared with you'));
    const all = el('button', 'btn ghost small', 'See the whole community'); all.type = 'button'; all.onclick = () => this.setTab(t.item.kind); head.append(all);
    list.append(head);
    if (t.parent) list.append(this.card(t.parent, { label: 'It answers this loop' }));
    const main = this.card(t.item); list.append(main);
    if (t.replies.length) this.openReplies(t.item, main, t.replies);
  }

  // ---- one item
  card(it, { mine = false, admin = false, reply = false, label = '' } = {}) {
    const c = el('article', 'cmcard' + (reply ? ' cmreply' : '')); c.dataset.id = it.id;
    if (label) c.append(el('div', 'cmlabel', label));
    const head = el('div', 'cmhead');
    head.append(el('span', 'cmkind', KIND_LABEL[it.kind]), own('h3', 'cmtitle', it.title));
    if (mine || admin) head.append(el('span', 'pill ' + (it.status === 'ok' ? 'ok' : 'warn'), it.status === 'ok' ? 'Public' : it.status === 'pending' ? 'Waiting for approval' : 'Hidden after a report'));
    const by = el('div', 'cmby'); by.append(el('span', '', 'by'), ' ', own('b', '', it.nick), ' · ', el('span', '', new Date(it.created).toLocaleDateString()));
    const facts = el('div', 'cmfacts');
    const fact = (text, skip) => facts.append(skip ? own('span', '', text) : el('span', '', text));
    if (it.kind === 'loop') {
      if (it.bpm) fact(`${it.bpm} BPM`); if (SIGNATURES[it.beats]) fact(SIGNATURES[it.beats]); if (it.bars) fact(`${it.bars} bars`);
      if (it.key) fact(it.key, true); if (seconds(it)) fact(`${seconds(it)} s`);
    } else fact(it.file, true);
    if (it.style) fact(it.style, true);
    c.append(head, by, facts);
    if (it.note) c.append(own('p', 'cmnote', it.note));
    if (admin && it.reasons?.length) c.append(own('p', 'cmreasons', it.reasons.join(' · ')));
    if (admin && it.reports) c.append(el('p', 'cmhint', `Reports: ${it.reports}`));
    const acts = el('div', 'cmactions'), button = (text, cls, fn, title) => { const b = el('button', 'btn small ' + cls, text); b.type = 'button'; if (title) b.title = title; b.onclick = () => fn(b); acts.append(b); return b; };
    if (it.kind === 'loop') button(this.playing === it.id ? 'Stop' : 'Listen', 'ghost', b => this.listen(it, b));
    if (!mine && !admin) {
      if (it.kind === 'loop') {
        button('Record over it', 'primary', () => this.jam(it), 'Opens this loop in the Looper: record your part and share it as a reply');
        if (!reply) button(it.replies ? `Replies (${it.replies})` : 'Replies', 'ghost', b => this.toggleReplies(it, c, b));
      } else button('Use on the GP-5', 'primary', () => this.use(it), 'Sends it to the pedal through the usual import');
      button('Copy link', 'ghost', () => this.copyLink(it));
      button('Download', 'ghost', () => this.download(it));
      button('Report', 'ghost cmreport', () => this.report(it, c));
    }
    if (mine) {
      if (it.status === 'ok') button('Copy link', 'ghost', () => this.copyLink(it));
      button('Remove', 'danger', () => this.remove(it, c));
    }
    if (admin) {
      if (it.kind !== 'loop') button('Download', 'ghost', () => this.download(it));
      if (it.status === 'pending') { button('Approve', 'primary', () => this.act('approve', it, c)); button('Reject', 'danger', () => this.act('reject', it, c)); }
      else { button('Restore', 'primary', () => this.act('restore', it, c)); button('Delete', 'danger', () => this.act('delete', it, c)); }
      button('Ban the author', 'ghost', () => this.act('ban', it, c));
    }
    c.append(acts);
    return c;
  }
  async listen(it, b) {
    const was = this.playing; this.stopListening();
    if (was === it.id) return;
    try {
      let src = C.fileUrl(it.id);
      if (it.status && it.status !== 'ok') { this.objUrl = URL.createObjectURL(await C.fileBlob(it)); src = this.objUrl; }
      this.audio.src = src; await this.audio.play();
      this.playing = it.id; this.playButton = b; b.textContent = 'Stop';
    } catch (e) { this.toast(e instanceof C.CommunityError ? errorText(e) : 'This loop could not be played.', 'bad'); }
  }
  stopListening() {
    this.audio.pause(); this.audio.removeAttribute('src'); this.audio.load();
    if (this.objUrl) { URL.revokeObjectURL(this.objUrl); this.objUrl = null; }
    if (this.playButton) this.playButton.textContent = 'Listen';
    this.playing = null; this.playButton = null;
  }
  /** A card leaves the list: its loop stops playing with it. */
  drop(it, card) { if (this.playing === it.id) this.stopListening(); card.remove(); }
  jam(it) { this.stopListening(); this.hooks.jam?.(it, C.fileUrl(it.id)); }
  async toggleReplies(it, card, b) {
    const open = card.querySelector('.cmreplies');
    if (open) { open.remove(); b.setAttribute('aria-expanded', 'false'); return; }
    b.disabled = true;
    try { const t = await C.item(it.id); this.openReplies(it, card, t.replies); b.setAttribute('aria-expanded', 'true'); }
    catch (e) { this.toast(errorText(e), 'bad'); }
    finally { b.disabled = false; }
  }
  openReplies(it, card, replies) {
    const box = el('div', 'cmreplies');
    box.append(el('div', 'cmlabel', replies.length ? 'Recorded over this loop' : 'Nobody has recorded over it yet. Be the first.'));
    for (const r of replies) box.append(this.card(r, { reply: true }));
    card.append(box);
  }
  async use(it) {
    try { const blob = await C.fileBlob(it); this.hooks.use?.(it.kind, new File([blob], it.file, { type: blob.type })); }
    catch (e) { this.toast(errorText(e), 'bad'); }
  }
  async download(it) {
    try {
      const blob = await C.fileBlob(it), a = el('a'); a.href = URL.createObjectURL(blob); a.download = it.file;
      document.body.append(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
    } catch (e) { this.toast(errorText(e), 'bad'); }
  }
  async copyLink(it) {
    const link = C.pageUrl(it.id);
    try { await navigator.clipboard.writeText(link); this.toast('Link copied. It plays in any browser: send it on WhatsApp, Instagram or anywhere.', 'ok'); }
    catch { await this.hooks.ask('Link of this item', 'Copy the link below.', 'Close', { input: { label: 'Link', value: link, maxLength: 300 } }); }
  }
  async report(it, card) {
    const why = await this.hooks.ask(`Report "${it.title}"?`, 'It is hidden at once, until a moderator reviews it.', 'Report',
      { danger: true, input: { label: 'Why (optional)', value: '', maxLength: 200 } });
    if (why == null) return;
    try { await C.report(it.id, why); this.drop(it, card); this.toast('Thank you. It is hidden until the owner reviews it.', 'ok'); }
    catch (e) { this.toast(errorText(e), 'bad'); }
  }
  async remove(it, card) {
    if (!(await this.hooks.ask(`Remove "${it.title}"?`, 'It leaves the community for good, and its link stops working.', 'Remove', { danger: true }))) return;
    try { await C.remove(it.id); this.drop(it, card); this.toast('Removed.', 'ok'); } catch (e) { this.toast(errorText(e), 'bad'); }
  }
  async act(what, it, card) {
    if (what === 'ban' && !(await this.hooks.ask(`Ban ${it.nick}?`, 'Everything this author shared is hidden, and their signature can no longer publish.', 'Ban', { danger: true }))) return;
    if ((what === 'reject' || what === 'delete') && !(await this.hooks.ask(`Delete "${it.title}"?`, 'The file is deleted for good.', 'Delete', { danger: true }))) return;
    try { await C.act(what, what === 'ban' ? { author: it.author } : { id: it.id }); this.drop(it, card); this.toast('Done.', 'ok'); }
    catch (e) { this.toast(errorText(e), 'bad'); }
  }

  // ---- publishing
  openPublish() {
    this.pending = null; this.resetForm();
    this.q('publish').hidden = false; this.q('title').focus();
    this.warnNoNick();
  }
  /** Warn only when this browser really has no signature (the page may still be reading it). */
  async warnNoNick() { if (!(await C.identity())) this.toast('Choose a nickname first.', 'bad'); }
  closePublish() { this.q('publish').hidden = true; this.pending = null; }
  resetForm() {
    for (const r of ['title', 'style', 'key', 'note']) this.q(r).value = '';
    this.q('fileName').textContent = ''; this.q('fileHint').hidden = false; this.q('replyNote').hidden = true; this.q('keyRow').hidden = true; this.q('pick').hidden = false;
  }
  takeFile(f) {
    const kind = kindOfFile(f.name);
    if (!kind) { this.toast(ERRORS['bad-file'], 'bad'); return; }
    if (f.size > MAX[kind]) { this.toast(ERRORS['too-big'], 'bad'); return; }
    this.pending = { kind, blob: f, name: f.name, meta: {} };
    this.q('fileName').replaceChildren(own('span', '', f.name), el('span', '', KIND_LABEL[kind]));
    if (!this.q('title').value) this.q('title').value = f.name.replace(/\.[^.]+$/, '').replace(/^(frost-preset-\d+-|model-\d+-|cab-\d+-)/, '').replace(/[-_]+/g, ' ').slice(0, 60);
  }
  /** From the Presets or NAMs & IRs page: a file read from the pedal, ready to publish under its own name. */
  startPublishFile(file, title) {
    this.shown(); this.openPublish(); this.takeFile(file);
    if (title) this.q('title').value = String(title).trim().slice(0, 60);
    this.q('publish').scrollIntoView({ block: 'start' });
  }
  /** From the Looper: the mix as a WAV, with its tempo, and the loop it answers when it was recorded over one. */
  startPublish(blob, meta) {
    this.shown();
    this.resetForm();
    this.pending = { kind: 'loop', blob, name: 'loop.wav', meta: { bpm: meta.bpm, beats: meta.beats, bars: meta.bars, replyTo: meta.replyTo || undefined } };
    this.q('pick').hidden = true; this.q('fileHint').hidden = true; this.q('keyRow').hidden = false;
    this.q('fileName').replaceChildren(...['Loop', `${meta.seconds} s`, meta.bpm && `${meta.bpm} BPM`, SIGNATURES[meta.beats]].filter(Boolean).map(t => el('span', '', t)));
    if (meta.replyTo) { this.q('replyNote').hidden = false; this.q('replyNote').textContent = `A reply to "${meta.replyTitle}" by ${meta.replyNick}.`; }
    this.q('title').value = meta.title || '';
    this.q('publish').hidden = false; this.q('publish').scrollIntoView({ block: 'start' }); this.q('title').focus();
    this.warnNoNick();
    if (blob.size > MAX.loop) this.toast(ERRORS['too-big'], 'bad');
  }
  async submit() {
    if (!this.id) this.id = await C.identity();
    if (!this.id) { this.toast(ERRORS['no-key'], 'bad'); this.q('nick')?.focus(); return; }
    if (!this.pending) { this.toast('Choose a file first.', 'bad'); return; }
    const title = this.q('title').value.trim();
    if (!title) { this.toast(ERRORS['no-title'], 'bad'); this.q('title').focus(); return; }
    const p = this.pending, send = this.q('send'); send.disabled = true; send.textContent = 'Sending…';
    try {
      const r = await C.publish({ ...p.meta, kind: p.kind, file: p.name, title, style: this.q('style').value, note: this.q('note').value, key: p.kind === 'loop' ? this.q('key').value : undefined }, p.blob);
      this.toast(r.status === 'ok' ? 'Published. It is on the community now.' : 'Sent. It appears once the owner approves it.', 'ok');
      this.closePublish(); await this.refreshMe(); this.setTab(r.status === 'ok' ? p.kind : 'mine');
    } catch (e) { this.toast(errorText(e), 'bad'); }
    finally { send.disabled = false; send.textContent = 'Publish'; }
  }
}
