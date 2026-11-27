// Frost web: interface language (English source, Portuguese translation).
//
// The page and its scripts keep writing English: English stays the source text everywhere. When another
// language is chosen, this module swaps each piece of English the page shows (text nodes, and the
// placeholder, title, aria-label and alt attributes) for its translation, and keeps doing so as the page
// changes (toasts, dialogs, lists). Text without a translation stays in English, so a new or changed
// English string never breaks anything: it just shows in English until the table catches up.
//
// The tables translate interface text only. Names that belong to the user (presets, songs, parts,
// models, cabinets) and the pedal's own words (effect and knob names, menu words such as PRESETS or SAVE)
// are not in them, so they stay exactly as the pedal shows them.
import { PT } from './i18n_pt.js?v=1.9.0';

const TABLES = { pt: PT };
const LANGS = ['en', 'pt'];
const KEY = 'frost.lang';
const ATTRS = ['placeholder', 'title', 'aria-label', 'alt'];
const SKIP = 'script, style, textarea, code, pre, [contenteditable], [data-i18n-skip]';

let lang = 'en';
let current = () => null;          // the translator for the language in use
const textShown = new WeakMap();   // text node -> the translation this module wrote into it
const textSource = new WeakMap();  // text node -> the English the page wrote
const attrShown = new WeakMap();   // element -> { attribute: translation written }
const attrSource = new WeakMap();  // element -> { attribute: English written by the page }

const escapeRe = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

/** A translator for one table: (English text) -> translation, or null to leave the text as it is.
 *  Leading and trailing spaces are kept; spaces inside are normalized, as the browser shows them anyway.
 *  Patterns: "{} of {} used" matches /^(.+?) of (.+?) used$/; the translation says {0}, {1}, ... in its
 *  own order, and a captured piece that is itself interface text (an operation name) is translated too. */
export function makeTranslator(table) {
  const exact = new Map(Object.entries(table.text || {}));
  // When several patterns match, the one with the most fixed text (the most specific) wins.
  const patterns = Object.entries(table.patterns || {}).map(([en, out]) =>
    [new RegExp('^' + en.split('{}').map(escapeRe).join('([\\s\\S]*?)') + '$'), out, en.replace(/\{\}/g, '').length]);
  const lookup = (core, depth) => {
    const hit = exact.get(core);
    if (hit !== undefined) return hit;
    if (depth > 1) return null;
    let best = null;
    for (const [re, out, fixed] of patterns) {
      if (best && fixed <= best.fixed) continue;
      const m = core.match(re);
      if (m) best = { m, out, fixed };
    }
    if (!best) return null;
    return best.out.replace(/\{(\d+)\}/g, (_, n) => {
      const piece = best.m[+n + 1] ?? '';
      const inner = /[A-Za-z]/.test(piece) ? lookup(piece, depth + 1) : null;
      return inner === null ? piece : inner;
    });
  };
  return text => {
    if (typeof text !== 'string' || !/[A-Za-z]/.test(text)) return null;
    const lead = text.match(/^\s*/)[0], trail = text.match(/\s*$/)[0];
    const out = lookup(text.slice(lead.length, text.length - trail.length).replace(/\s+/g, ' '), 0);
    return out === null ? null : lead + out + trail;
  };
}

/** The translation of one piece of English in the language in use, or null to leave it as it is. */
export const translate = text => current(text);

const skipped = node => {
  const el = node.nodeType === 1 ? node : node.parentElement;
  return !el || !!el.closest(SKIP);
};

function doText(node) {
  const now = node.data;
  if (textShown.get(node) === now) return;            // this module's own write
  textSource.set(node, now);                          // the page wrote new English
  const out = skipped(node) ? null : translate(now);
  if (out !== null && out !== now) { textShown.set(node, out); node.data = out; }
  else textShown.delete(node);
}

function doAttr(el, name) {
  const now = el.getAttribute(name);
  if (now === null) return;
  const shown = attrShown.get(el) || {}, source = attrSource.get(el) || {};
  if (shown[name] === now) return;
  source[name] = now; attrSource.set(el, source);
  const out = skipped(el) ? null : translate(now);
  if (out !== null && out !== now) { shown[name] = out; attrShown.set(el, shown); el.setAttribute(name, out); }
  else { delete shown[name]; attrShown.set(el, shown); }
}

function walk(root) {
  if (root.nodeType === 3) return doText(root);
  if (root.nodeType !== 1 || root.matches(SKIP)) return;
  for (const name of ATTRS) if (root.hasAttribute(name)) doAttr(root, name);
  const texts = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  for (let n = texts.nextNode(); n; n = texts.nextNode()) doText(n);
  for (const el of root.querySelectorAll(ATTRS.map(a => `[${a}]`).join(','))) for (const name of ATTRS) if (el.hasAttribute(name)) doAttr(el, name);
}

// Back to the English the page wrote, wherever this module changed it.
function restore(root) {
  const texts = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  for (let n = texts.nextNode(); n; n = texts.nextNode()) {
    if (textShown.get(n) === n.data && textSource.has(n)) { const en = textSource.get(n); textShown.delete(n); n.data = en; }
  }
  for (const el of root.querySelectorAll(ATTRS.map(a => `[${a}]`).join(','))) {
    const shown = attrShown.get(el), source = attrSource.get(el);
    if (!shown || !source) continue;
    for (const name of Object.keys(shown)) if (el.getAttribute(name) === shown[name]) { delete shown[name]; el.setAttribute(name, source[name]); }
  }
}

let titleSource = '';
function apply() {
  document.documentElement.lang = lang === 'pt' ? 'pt-BR' : 'en';
  if (lang === 'en') { current = () => null; restore(document.body); if (titleSource) document.title = titleSource; }
  else { current = makeTranslator(TABLES[lang]); titleSource ||= document.title; document.title = translate(titleSource) ?? titleSource; walk(document.body); }
  for (const b of document.querySelectorAll('[data-lang]')) b.setAttribute('aria-pressed', String(b.dataset.lang === lang));
}

/** The language in use: 'en' or 'pt'. */
export const currentLanguage = () => lang;

/** Choose the interface language; it is remembered in this browser. */
export function setLanguage(next) {
  if (!LANGS.includes(next) || next === lang) return;
  if (next !== 'en' && lang !== 'en') { lang = 'en'; current = () => null; restore(document.body); }
  lang = next;
  try { localStorage.setItem(KEY, next); } catch {}
  apply();
}

/** Start once, after the page is in place: the saved choice, else the browser's language. */
export function startI18n() {
  let saved = null;
  try { saved = localStorage.getItem(KEY); } catch {}
  const browser = (navigator.languages || [navigator.language || 'en']).some(l => /^pt\b/i.test(l)) ? 'pt' : 'en';
  lang = LANGS.includes(saved) ? saved : browser;
  for (const b of document.querySelectorAll('[data-lang]')) b.addEventListener('click', () => setLanguage(b.dataset.lang));
  // Messages the browser shows by itself (form validation) go through here too.
  const setValidity = HTMLInputElement.prototype.setCustomValidity;
  HTMLInputElement.prototype.setCustomValidity = function (message) { return setValidity.call(this, message ? translate(message) ?? message : message); };
  new MutationObserver(list => {
    if (lang === 'en') return;
    for (const m of list) {
      if (m.type === 'characterData') doText(m.target);
      else if (m.type === 'attributes') doAttr(m.target, m.attributeName);
      else for (const n of m.addedNodes) walk(n);
    }
  }).observe(document.body, { subtree: true, childList: true, characterData: true, attributes: true, attributeFilter: ATTRS });
  if (lang !== 'en') apply();
  else for (const b of document.querySelectorAll('[data-lang]')) b.setAttribute('aria-pressed', String(b.dataset.lang === 'en'));
}
