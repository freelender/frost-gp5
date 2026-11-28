// Frost web: the drawn gear of the live editor (pedals, amp heads, capture heads, cabinets) as SVG.
// Every block carries its category colour, the same as the pedal's own screen (effects.json "colors",
// engine/control_catalog.h): a stompbox is painted in it, an amp or capture head and a cabinet show it on the
// piping, the control strip and the nameplate. The skin's CSS variables (rig.css, [data-skin]) only restyle
// what carries no category (tolex, grille, knobs, cables). Pure functions: they return SVG text.

const CAB = 3, NAM = 33, AMP_CATEGORY = 7;
/** What a block looks like: 'pedal', 'amp', 'capture', 'cab' or 'empty'. */
export const gearKind = (type, eff) => !eff ? 'empty' : type === CAB ? 'cab' : type === NAM ? 'capture' : eff.category === AMP_CATEGORY ? 'amp' : 'pedal';
/** [width / height, height relative to a pedal] of each kind: amps are wide and short, cabinets tall. */
export const GEAR_SHAPE = { pedal: [100 / 158, 1], amp: [170 / 104, 0.86], capture: [170 / 104, 0.86], cab: [118 / 124, 1.02], empty: [100 / 158, 1] };
const CHIPS = ['OD', 'DS', 'DLY', 'REV', 'MOD', 'DYN', 'UTL', 'AMP', 'PIT'];
/** The short tag above a block: its category and its number among the blocks of that category. */
export const gearChip = (type, eff, nth) => !eff ? '—' : type === CAB ? 'CAB' : type === NAM ? 'NAM' : `${CHIPS[eff.category] || 'FX'} ${nth}`;

let uid = 0;
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const angle = p => -135 + 270 * ((p.initial - p.min) / ((p.max - p.min) || 1));

function miniKnob(x, y, r, a, id) {
  return `<g transform="translate(${x} ${y})"><circle r="${r + 1.2}" fill="#000" opacity=".5"/><circle r="${r}" fill="url(#mk${id})"/>` +
    `<circle r="${r * 0.62}" fill="url(#mc${id})"/><line y2="${-r + 0.8}" stroke="#fff" stroke-width="${Math.max(0.8, r / 5)}" stroke-linecap="round" transform="rotate(${a})"/></g>`;
}
const miniDefs = id => `<radialGradient id="mk${id}" cx="40%" cy="35%"><stop offset="0" stop-color="#4a4f56"/><stop offset="1" stop-color="#0d0f11"/></radialGradient>` +
  `<radialGradient id="mc${id}" cx="35%" cy="30%"><stop offset="0" stop-color="#f3f6f9"/><stop offset=".6" stop-color="#9aa4ae"/><stop offset="1" stop-color="#4b525a"/></radialGradient>`;
const tolex = id => `<pattern id="tx${id}" width="3" height="3" patternUnits="userSpaceOnUse"><rect width="3" height="3" style="fill:var(--gear-tolex)"/><circle cx="1" cy="1" r=".55" style="fill:var(--gear-tolex-dot)"/></pattern>`;
const grille = (id, w) => `<pattern id="gr${id}" width="${w}" height="${w}" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="${w}" height="${w}" style="fill:var(--gear-grille)"/><line x1="0" y1="0" x2="0" y2="${w}" style="stroke:var(--gear-grille-line)" stroke-width="${w * 0.46}"/></pattern>`;
const corners = pts => pts.map(([x, y]) => `<circle cx="${x}" cy="${y}" r="4.5" style="fill:var(--gear-corner)" stroke="#0007" stroke-width=".6"/>`).join('');

/** A stompbox in its category colour: knob panel, LED, name and a metal footswitch. */
export function pedalSvg(eff, color, on) {
  const id = ++uid;
  const knobs = (eff.params || []).filter(p => !p.labels && !['TAIL', 'HOLD', 'FOOT'].includes(p.name)).slice(0, 4);
  const n = Math.max(1, knobs.length), xs = n === 1 ? [50] : n === 2 ? [32, 68] : n === 3 ? [24, 50, 76] : [20, 40, 60, 80];
  const words = eff.name.split(' '), cut = Math.ceil(words.length / 2), l1 = words.slice(0, cut).join(' '), l2 = words.slice(cut).join(' ');
  return `<svg viewBox="0 0 100 158" aria-hidden="true" focusable="false"><defs>${miniDefs(id)}
    <linearGradient id="bd${id}" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="color-mix(in srgb, ${color} 82%, #fff)"/><stop offset=".55" stop-color="${color}"/><stop offset="1" stop-color="color-mix(in srgb, ${color} 50%, #000)"/></linearGradient>
    <radialGradient id="fs${id}" cx="40%" cy="35%"><stop offset="0" stop-color="#fff"/><stop offset=".5" stop-color="#a9b2bb"/><stop offset="1" stop-color="#3e444b"/></radialGradient></defs>
    <ellipse cx="50" cy="156" rx="44" ry="3" fill="#000" opacity=".6"/>
    <rect x="1" y="30" width="5" height="14" rx="1" fill="#2b2e32"/><rect x="94" y="30" width="5" height="14" rx="1" fill="#2b2e32"/>
    <rect x="4" y="2" width="92" height="152" rx="7" style="fill:url(#bd${id})"/>
    <rect x="4.5" y="2.5" width="91" height="151" rx="6.5" fill="none" stroke="#fff" stroke-opacity=".35" stroke-width=".8"/>
    <rect x="9" y="8" width="82" height="40" rx="4" fill="#0c0e11" opacity=".88"/>
    ${knobs.map((p, i) => miniKnob(xs[i], 23, n > 3 ? 6.5 : 8, angle(p), id)).join('')}
    ${knobs.map((p, i) => `<text x="${xs[i]}" y="${n > 3 ? 40 : 42}" text-anchor="middle" class="gear-font" font-size="4.6" fill="#cfd5db">${esc(p.name.slice(0, 7))}</text>`).join('')}
    <circle cx="50" cy="56" r="2.6" fill="${on ? '#ff3b30' : '#3b1210'}"${on ? ' style="filter:drop-shadow(0 0 3px #ff3b30)"' : ''}/>
    <text x="50" y="${l2 ? 77 : 84}" text-anchor="middle" class="gear-font" font-weight="700" font-size="${l1.length > 9 ? 9 : 11}" fill="#fff" stroke="#0005" stroke-width="1.2" style="paint-order:stroke">${esc(l1)}</text>
    ${l2 ? `<text x="50" y="89" text-anchor="middle" class="gear-font" font-weight="600" font-size="${l2.length > 10 ? 7 : 8.5}" fill="#ffffffd9">${esc(l2)}</text>` : ''}
    <polygon points="50,110 63,117.5 63,132.5 50,140 37,132.5 37,117.5" fill="#6d757e" stroke="#2a2e33" stroke-width="1"/>
    <circle cx="50" cy="125" r="10" style="fill:url(#fs${id})" stroke="#2a2e33" stroke-width="1"/>
    <circle cx="50" cy="125" r="5" fill="none" stroke="#fff" stroke-opacity=".5" stroke-width=".8"/>
  </svg>`;
}

/** An amp head (or a NAM capture head): handle, control strip, pilot light, grille and nameplate, the piping
 *  and the nameplate in the category colour. */
export function headSvg(eff, on, capture, color = '#E8B85A') {
  const id = ++uid;
  const top = capture ? 'NAM CAPTURE' : eff.short.replace('-', ' ');
  const sub = capture ? 'CAPTURE MODEL' : eff.name;
  const plate = Math.min(128, 20 + top.length * 10.8);
  return `<svg viewBox="0 0 170 104" aria-hidden="true" focusable="false"><defs>${miniDefs(id)}${tolex(id)}${grille(id, 2.6)}
    <linearGradient id="hd${id}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#5d646c"/><stop offset="1" stop-color="#1c1f23"/></linearGradient></defs>
    <ellipse cx="85" cy="102" rx="80" ry="2.5" fill="#000" opacity=".6"/>
    <path d="M58 12 v-6 q0-4 4-4 h46 q4 0 4 4 v6" fill="none" stroke="url(#hd${id})" stroke-width="4" stroke-linecap="round"/>
    <rect x="3" y="11" width="164" height="89" rx="7" style="fill:url(#tx${id})" stroke="#000" stroke-width="1.5"/>
    <rect x="9" y="16" width="152" height="19" rx="2.5" fill="#0c0d0f" stroke="#ffffff22" stroke-width=".6"/><rect x="9" y="33" width="152" height="2" rx="1" fill="${color}"/>
    ${Array.from({ length: 8 }, (_, i) => miniKnob(36 + i * 14.5, 25.5, 4.2, -40 + i * 25, id)).join('')}
    <rect x="14" y="21.5" width="8" height="8" rx="1" fill="#26292d"/><circle cx="18" cy="25.5" r="2.2" fill="#111"/>
    <circle cx="153" cy="25.5" r="3.4" fill="${on ? '#ff2d22' : '#3a0d0a'}"${on ? ' style="filter:drop-shadow(0 0 4px #ff2d22)"' : ''}/>
    <rect x="9" y="39" width="152" height="55" rx="3" style="fill:url(#gr${id})" stroke="${color}" stroke-width="1.6"/>
    ${capture ? '<path d="M24 66 q8 -14 16 0 t16 0 t16 0" fill="none" style="stroke:var(--accent-ice, #64caff)" stroke-width="1.6" opacity=".8"/>' : ''}
    <rect x="${85 - plate / 2}" y="54" width="${plate}" height="25" rx="2" fill="#0009" stroke="${color}" stroke-opacity=".7" stroke-width=".8"/>
    <text x="85" y="69" text-anchor="middle" class="gear-font" font-weight="600" font-size="13" letter-spacing="1" fill="${color}">${esc(top)}</text>
    <text x="85" y="76" text-anchor="middle" class="gear-font" font-size="4.6" letter-spacing="1.2" style="fill:var(--gear-logo)" opacity=".75">${esc(sub.toUpperCase())}</text>
    ${corners([[7, 15], [163, 15], [7, 96], [163, 96]])}
  </svg>`;
}

/** A 4x12 cabinet: the speakers show through the grille cloth; piping and label in the category colour. */
export function cabSvg(on, label = '4X12', color = '#C5D0DE') {
  const id = ++uid;
  return `<svg viewBox="0 0 118 124" aria-hidden="true" focusable="false"><defs>${tolex(id)}${grille(id, 2.4)}
    <radialGradient id="cn${id}"><stop offset="0" stop-color="#7a828c"/><stop offset=".16" stop-color="#24272b"/><stop offset=".24" stop-color="#3a3f45"/><stop offset=".8" stop-color="#16181b"/><stop offset=".93" stop-color="#5b626b"/><stop offset="1" stop-color="#2a2e33"/></radialGradient>
    <linearGradient id="sh${id}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".10"/><stop offset=".5" stop-color="#fff" stop-opacity="0"/></linearGradient></defs>
    <ellipse cx="59" cy="122" rx="56" ry="2.5" fill="#000" opacity=".6"/>
    <rect x="3" y="3" width="112" height="117" rx="6" style="fill:url(#tx${id})" stroke="#000" stroke-width="1.5"/>
    <rect x="10" y="10" width="98" height="103" rx="3" fill="#060607"/>
    ${[[35, 37], [83, 37], [35, 86], [83, 86]].map(([x, y]) => `<circle cx="${x}" cy="${y}" r="21" fill="url(#cn${id})"/><circle cx="${x}" cy="${y}" r="5" fill="#2e3237"/>`).join('')}
    <rect x="10" y="10" width="98" height="103" rx="3" style="fill:url(#gr${id})" opacity=".68"/>
    <rect x="10" y="10" width="98" height="103" rx="3" fill="url(#sh${id})" stroke="${color}" stroke-width="1.6"/>
    <rect x="16" y="102" width="26" height="7" rx="1.2" fill="#0009"/><text x="29" y="107.6" text-anchor="middle" class="gear-font" font-size="5" letter-spacing=".6" fill="${color}">${esc(label)}</text>
    ${corners([[7, 7], [111, 7], [7, 116], [111, 116]])}
    ${on ? '' : '<rect x="3" y="3" width="112" height="117" rx="6" fill="#0006"/>'}
  </svg>`;
}

const emptySvg = () => `<svg viewBox="0 0 100 158" aria-hidden="true" focusable="false"><rect x="5" y="3" width="90" height="150" rx="8" fill="#ffffff05" style="stroke:var(--gear-empty)" stroke-width="1.6" stroke-dasharray="6 5"/><text x="50" y="84" text-anchor="middle" class="gear-font" font-size="30" style="fill:var(--gear-empty)">+</text></svg>`;

/** The drawing of one block. */
export function gearSvg(type, eff, color, on) {
  const kind = gearKind(type, eff);
  return kind === 'empty' ? emptySvg() : kind === 'cab' ? cabSvg(on, undefined, color || undefined) : kind === 'pedal' ? pedalSvg(eff, color, on) : headSvg(eff, on, kind === 'capture', color || undefined);
}

/** A big panel knob: tick scale, knurled skirt, cap and pointer (rotate .knob-pointer to show the value). */
export function knobSvg() {
  const id = ++uid, ticks = Array.from({ length: 11 }, (_, i) => {
    const a = (-135 + 27 * i) * Math.PI / 180, s = Math.sin(a), c = Math.cos(a);
    return `M${(60 + 50 * s).toFixed(1)} ${(60 - 50 * c).toFixed(1)}L${(60 + 57 * s).toFixed(1)} ${(60 - 57 * c).toFixed(1)}`;
  }).join('');
  return `<svg class="knob-svg" viewBox="0 0 120 120" aria-hidden="true" focusable="false"><defs>
    <radialGradient id="cap${id}" cx="38%" cy="32%"><stop offset="0" style="stop-color:var(--knob-cap1)"/><stop offset=".55" style="stop-color:var(--knob-cap2)"/><stop offset="1" style="stop-color:var(--knob-cap3)"/></radialGradient></defs>
    <path d="${ticks}" style="stroke:var(--knob-tick)" stroke-width="2.4" stroke-linecap="round"/>
    <circle cx="60" cy="63" r="41" fill="#000" opacity=".45"/>
    <circle cx="60" cy="60" r="40" style="fill:var(--knob-skirt)"/>
    <circle cx="60" cy="60" r="37" fill="none" stroke="#ffffff22" stroke-width="3" stroke-dasharray="1.2 2.2"/>
    <circle cx="60" cy="60" r="29" style="fill:url(#cap${id})"/>
    <circle cx="60" cy="60" r="29" fill="none" stroke="#0006" stroke-width="1"/>
    <g class="knob-pointer"><line x1="60" y1="60" x2="60" y2="22" style="stroke:var(--knob-pointer)" stroke-width="4.5" stroke-linecap="round"/></g>
  </svg>`;
}

/** A bat-lever toggle for a two-way choice; `up` = the second choice. */
export const toggleSvg = up => `<svg viewBox="0 0 40 70" aria-hidden="true" focusable="false"><rect x="4" y="18" width="32" height="34" rx="4" fill="#0b0b0c" stroke="#ffffff22"/><circle cx="20" cy="35" r="8" fill="#9aa3ad"/><rect class="lever" x="16" y="${up ? 6 : 35}" width="8" height="30" rx="4" style="fill:var(--knob-lever)" stroke="#0008"/></svg>`;
