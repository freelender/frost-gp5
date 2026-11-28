// A guided tour drawn over the page. The element of each step stays lit inside a dimmed page and a card beside it
// explains it, with Back, Next and Close. The page keeps working underneath, so each step can be done for real while
// the card is open; a step that watches for it (done) is ticked when it happens, and an `auto` step then moves on.
//
// A step: { target: () => Element|null (null or hidden: the card sits in the middle), title, text,
//           action?: { label, run, show?: () => boolean }, done?: () => boolean, auto?: boolean, enter?: () => void }
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const GAP = 12, PAD = 6;

export class Tour {
  constructor(steps, { onclose } = {}) { this.steps = steps; this.onclose = onclose; this.i = -1; this.frame = 0; }
  get open() { return this.i >= 0; }

  start(at = 0) {
    if (!this.root) this.build();
    document.body.append(this.root);
    document.addEventListener('keydown', this.keys);
    this.go(at);
    const tick = () => { this.update(); this.frame = requestAnimationFrame(tick); };
    cancelAnimationFrame(this.frame); this.frame = requestAnimationFrame(tick);
  }
  close() {
    if (!this.open) return;
    this.i = -1; cancelAnimationFrame(this.frame); clearTimeout(this.timer);
    document.removeEventListener('keydown', this.keys);
    this.root.remove(); this.onclose?.();
  }

  build() {
    this.root = el('div', 'tour');
    this.hole = el('div', 'tourhole');
    this.card = el('section', 'tourcard'); this.card.setAttribute('role', 'dialog'); this.card.setAttribute('aria-labelledby', 'tour-title');
    const head = el('div', 'tourhead');
    this.count = el('span', 'tourcount');
    const x = el('button', 'tourx', '✕'); x.type = 'button'; x.setAttribute('aria-label', 'Close the tutorial'); x.onclick = () => this.close();
    head.append(this.count, x);
    this.title = el('h3', 'tourtitle'); this.title.id = 'tour-title';
    this.text = el('p', 'tourtext');
    this.tick = el('p', 'tourtick', '✓ Done.'); this.tick.hidden = true;
    const bar = el('div', 'tourbar');
    this.back = el('button', 'btn ghost small', 'Back'); this.back.type = 'button'; this.back.onclick = () => this.go(this.i - 1);
    this.act = el('button', 'btn ghost small touract'); this.act.type = 'button';
    this.next = el('button', 'btn primary small', 'Next'); this.next.type = 'button';
    this.next.onclick = () => this.i === this.steps.length - 1 ? this.close() : this.go(this.i + 1);
    bar.append(this.back, el('span', 'tourgap'), this.act, this.next);
    this.dots = el('div', 'tourdots'); this.dots.setAttribute('aria-hidden', 'true');
    this.card.append(head, this.title, this.text, this.tick, bar, this.dots);
    this.root.append(this.hole, this.card);
    this.keys = e => {
      if (e.key === 'Escape') { e.preventDefault(); this.close(); return; }
      if (e.target.closest?.('input, select, textarea, [contenteditable]')) return;
      if (e.key === 'ArrowRight' && this.i < this.steps.length - 1) this.go(this.i + 1);
      if (e.key === 'ArrowLeft' && this.i > 0) this.go(this.i - 1);
    };
  }

  go(i) {
    clearTimeout(this.timer);
    this.i = Math.max(0, Math.min(this.steps.length - 1, i));
    const s = this.steps[this.i];
    s.enter?.();
    this.count.textContent = `Step ${this.i + 1} of ${this.steps.length}`;
    this.title.textContent = s.title; this.text.textContent = s.text;
    this.back.disabled = this.i === 0;
    this.next.textContent = this.i === this.steps.length - 1 ? 'Finish' : 'Next';
    this.waiting = !!s.done && !s.done(); this.tick.hidden = true; this.card.classList.remove('done');
    this.act.textContent = s.action?.label || ''; this.act.onclick = () => s.action?.run();
    this.dots.textContent = '';
    this.steps.forEach((_, k) => { const d = el('span'); if (k === this.i) d.className = 'on'; else if (k < this.i) d.className = 'past'; this.dots.append(d); });
    this.shown = null; this.scrolled = false;
    this.next.focus({ preventScroll: true });
    this.update();
  }

  /** Every frame: follow the element (the page may redraw or move it), show the action, tick a step that was done. */
  update() {
    const s = this.steps[this.i]; if (!s) return;
    this.act.hidden = !s.action || (s.action.show ? !s.action.show() : false);
    if (this.waiting && s.done?.()) {
      this.waiting = false; this.tick.hidden = false; this.card.classList.add('done');
      if (s.auto && this.i < this.steps.length - 1) { const at = this.i; this.timer = setTimeout(() => { if (this.i === at) this.go(at + 1); }, 700); }
    }
    let t = s.target?.() || null, r = t?.getBoundingClientRect();
    if (r && !r.width && !r.height) { t = null; r = null; }
    if (t && !this.scrolled) {
      this.scrolled = true;
      if (r.top < 0 || r.bottom > innerHeight) { t.scrollIntoView({ block: 'center', behavior: 'smooth' }); }
    }
    const c = this.card.getBoundingClientRect();
    const key = r ? [r.left, r.top, r.width, r.height, c.width, c.height, innerWidth, innerHeight].map(Math.round).join() : `none,${Math.round(c.height)},${innerWidth},${innerHeight}`;
    if (key === this.shown) return;
    this.shown = key;
    this.place(r, c);
  }

  place(r, c) {
    const W = innerWidth, H = innerHeight, w = c.width, h = c.height;
    const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
    if (!r) {
      Object.assign(this.hole.style, { left: W / 2 + 'px', top: H / 2 + 'px', width: '0px', height: '0px' });
      this.hole.classList.add('none');
      Object.assign(this.card.style, { left: Math.round((W - w) / 2) + 'px', top: Math.round((H - h) / 2) + 'px' });
      return;
    }
    this.hole.classList.remove('none');
    const hole = { left: r.left - PAD, top: r.top - PAD, width: r.width + 2 * PAD, height: r.height + 2 * PAD };
    Object.assign(this.hole.style, Object.fromEntries(Object.entries(hole).map(([k, v]) => [k, Math.round(v) + 'px'])));
    const bottom = hole.top + hole.height, right = hole.left + hole.width;
    let x, y;
    if (bottom + GAP + h <= H - GAP) { y = bottom + GAP; x = hole.left + hole.width / 2 - w / 2; }               // below
    else if (hole.top - GAP - h >= GAP) { y = hole.top - GAP - h; x = hole.left + hole.width / 2 - w / 2; }      // above
    else if (right + GAP + w <= W - GAP) { x = right + GAP; y = hole.top; }                                       // right
    else if (hole.left - GAP - w >= GAP) { x = hole.left - GAP - w; y = hole.top; }                              // left
    else { x = W - w - GAP; y = H - h - GAP; }                                                                     // over it, in a corner
    Object.assign(this.card.style, { left: Math.round(clamp(x, GAP, W - w - GAP)) + 'px', top: Math.round(clamp(y, GAP, H - h - GAP)) + 'px' });
  }
}
