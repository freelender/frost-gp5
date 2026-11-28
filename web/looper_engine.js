// Looper engine: the audio part of the website's looper, without Web Audio, so it runs in an AudioWorklet and in tests.
//
// Time is counted in frames. The guitarist plays a note at "played time" p; it reaches this engine inLatency frames
// later (USB in), and what the engine outputs at frame t is heard outLatency frames later (USB out). In a section, the
// tracks share one base length (len) and one anchor: played time p belongs to loop position (p - anchor) mod length,
// and the output at frame t reads position (t - anchor + outLatency). So what is heard lines up with what is played,
// and an overdub lands where it was played, whatever the latency. A track can be 1, 2, 4 or 8 times the base length.
//
// Each pass of recording is a layer kept on its own (unlimited undo and redo per track); a track plays the sum of its
// layers. Every recorded edge fades over about 2 ms, so the loop seam does not click.
//
// Sections (A, B, C, like verse, chorus and bridge) each hold their own tracks, length and drum pattern; a change of
// section waits for the end of the playing loop. Tempo (optional): a count-in before the first recording, the first
// loop rounded to whole bars, new tracks starting on the next bar, a metronome. The drum machine plays a pattern of
// synthesized drums (16 steps a bar) with the loop; in drum mode the footswitches play and record it. Clicks and drums
// go to the outputs only: the GP-5 does not send the computer's playback back, so they are never recorded.
export const TRACKS = 4, SECTIONS = 3, MULTS = [1, 2, 4, 8];
export const PIECES = ['Kick', 'Snare', 'Hi-hat', 'Open hat', 'Clap', 'Crash', 'Low tom', 'High tom', 'Rim'];
export const DRUM_PAGES = [[0, 1, 2], [3, 4, 5], [6, 7, 8]];
const MAX_STEPS = 128, FADE = 96, MIN_LOOP_S = 0.25, MAX_LOOP_S = 600;
const mod = (a, n) => ((a % n) + n) % n;

function emptyTrack() {
  return { layers: [], redo: [], mix: null, writer: null, tails: [], playing: false, muted: false, reversed: false, half: false,
    cut: 1, lp: [0, 0], mult: 1, gain: 0, slow: false, peak: 0, armed: 0 };
}
// swing: where the "and" of each beat falls, from 0.5 (straight eighths) to 0.75; 0.66 is the triplet shuffle of a blues.
function emptyDrum() { return { bars: 1, pat: new Uint8Array(PIECES.length * MAX_STEPS), play: false, vol: 0.8, swing: 0.5, lastStep: -1, skip: [] }; }
function emptySection(tracks) { return { len: 0, anchor: 0, first: null, tracks: Array.from({ length: tracks }, emptyTrack), drum: emptyDrum() }; }
const tlen = (S, k) => S.len * k.mult;

export class LoopEngine {
  constructor(rate, channels = 2, tracks = TRACKS) {
    this.rate = rate; this.ch = channels; this.t = 0; this.inLat = 0; this.outLat = 0;
    this.sections = Array.from({ length: SECTIONS }, () => emptySection(tracks)); this.cur = 0; this.nextSec = -1;
    this.bpm = 0; this.beats = 4; this.countBars = 0; this.quantize = false; this.metro = false; this.clickGain = 0.3;
    this.qstart = false; this.decay = 1; this.fadeStop = false;
    this.clicks = []; this.taps = []; this.voices = []; this.noise = 22222;
  }
  // The current section's state, as plain properties (the page and the tests read these).
  get sec() { return this.sections[this.cur]; }
  get len() { return this.sec.len; } set len(v) { this.sec.len = v; }
  get anchor() { return this.sec.anchor; } set anchor(v) { this.sec.anchor = v; }
  get first() { return this.sec.first; } set first(v) { this.sec.first = v; }
  get tracks() { return this.sec.tracks; } set tracks(v) { this.sec.tracks = v; }
  get drum() { return this.sec.drum; }

  setTempo({ bpm, beats, countBars, quantize, metro, clickGain, qstart, decay, fadeStop } = {}) {
    if (bpm !== undefined) this.bpm = bpm ? Math.max(40, Math.min(240, Number(bpm))) : 0;
    if (beats !== undefined) this.beats = Math.max(1, Math.min(12, beats | 0));
    if (countBars !== undefined) this.countBars = Math.max(0, Math.min(4, countBars | 0));
    if (quantize !== undefined) this.quantize = !!quantize;
    if (metro !== undefined) this.metro = !!metro;
    if (clickGain !== undefined) this.clickGain = Math.max(0, Math.min(1, Number(clickGain)));
    if (qstart !== undefined) this.qstart = !!qstart;
    if (decay !== undefined) this.decay = Math.max(0, Math.min(1, Number(decay)));
    if (fadeStop !== undefined) this.fadeStop = !!fadeStop;
  }
  beatFrames() { return this.bpm ? Math.round(60 / this.bpm * this.rate) : 0; }
  barFrames() { return this.beatFrames() * this.beats; }
  /** Tap tempo: the average of the last taps (a pause over 2 s starts over). Returns the tempo, or 0 after one tap. */
  tap(at = this.t) {
    const last = this.taps[this.taps.length - 1];
    if (last !== undefined && at - last > 2 * this.rate) this.taps = [];
    this.taps.push(at); if (this.taps.length > 5) this.taps.shift();
    if (this.taps.length < 2) return 0;
    const avg = (this.taps[this.taps.length - 1] - this.taps[0]) / (this.taps.length - 1);
    this.bpm = Math.max(40, Math.min(240, Math.round(60 * this.rate / avg)));
    return this.bpm;
  }
  emptyTrack() { return emptyTrack(); }
  setLatency(inFrames, outFrames) { this.inLat = Math.max(0, Math.round(inFrames)); this.outLat = Math.max(0, Math.round(outFrames)); }

  // ---- what the page shows
  trackState(i, S = this.sec) {
    const k = S.tracks[i];
    if (S.first?.track === i) return this.t < S.first.start ? 'count' : 'rec';
    if (k.writer) return this.t < k.writer.start ? 'arm' : k.writer.kind;
    if (!k.layers.length) return 'empty';
    return k.playing ? 'play' : 'stop';
  }
  snapshot() {
    const S = this.sec, d = S.drum, steps = this.drumSteps(S);
    const heard = S.len ? mod(this.t - S.anchor, S.len) / S.len : 0;
    return {
      len: S.len / this.rate, pos: heard, recording: S.first ? Math.max(0, this.t - this.inLat - S.first.start) / this.rate : 0,
      bpm: this.bpm, beats: this.beats, count: this.countNow(), section: this.cur, next: this.nextSec,
      sections: this.sections.map(s => ({ len: s.len / this.rate, used: !!(s.len || s.first) })),
      tracks: S.tracks.map((k, i) => ({ state: this.trackState(i), layers: k.layers.length, redo: k.redo.length, muted: k.muted, reversed: k.reversed,
        half: k.half, cut: k.cut, mult: k.mult, peak: k.peak, pos: S.len ? mod(this.t - S.anchor, tlen(S, k)) / tlen(S, k) : 0 })),
      drum: { play: d.play, bars: d.bars, vol: d.vol, swing: d.swing, steps, step: d.play && S.len && steps ? Math.floor(mod(this.t - S.anchor, S.len) / (S.len / steps)) : -1,
        pat: Array.from({ length: PIECES.length }, (_, p) => Array.from(d.pat.subarray(p * MAX_STEPS, p * MAX_STEPS + steps)).join('')).join('|') },
    };
  }
  countNow() {                                         // during a count-in: { beat: 1.., of: beats in the count-in }
    const f = this.first, bf = this.beatFrames();
    if (!f || !f.countBeats || !bf || this.t >= f.start) return null;
    return { beat: Math.min(f.countBeats, Math.floor((this.t - f.clickFrom) / bf) + 1), of: f.countBeats, bar: this.beats };
  }
  anyPlaying(S = this.sec) { return S.tracks.some(k => k.playing && k.layers.length) || (S.drum.play && S.len > 0); }

  // ---- commands (at: the frame of the footswitch press; the default is now)
  record(i, at = this.t) {                            // REC -> DUB -> PLAY on one footswitch, as most loopers do
    const k = this.tracks[i], s = this.trackState(i);
    if (s === 'count') { this.first = null; return; }   // a press during the count-in cancels it
    if (s === 'arm') { this.undo(i); return; }           // a press while waiting for the bar cancels it
    if (s === 'rec' || s === 'dub') { const end = this.close(i, at); if (s === 'rec' && this.len) this.openLayer(i, Math.max(at, end ?? at), 'dub'); return; }
    if (s === 'play') { if (k.half) k.half = false; this.openLayer(i, at, 'dub'); return; }
    if (s === 'stop') { this.play(i, at); return; }
    if (!this.len) {                                   // the first loop sets the base length of the section
      if (this.first) return;
      const bf = this.beatFrames(), countBeats = this.countBars && bf ? this.countBars * this.beats : 0;
      // The clicks start now and are heard outLatency later; the recording starts on the beat after the last click.
      const start = countBeats ? at + this.outLat + countBeats * bf : at;
      this.first = { track: i, start, data: Array.from({ length: this.ch }, () => new Float32Array(this.rate * 4)), count: 0, clickFrom: at, countBeats };
      return;
    }
    // A new track over a loop: it may start on the next bar, and its length is rounded to 1, 2, 4 or 8 loops on close.
    if (!this.anyPlaying()) this.reanchor(at);
    const start = this.qstart ? this.nextBoundary(at) : at;
    k.mult = MULTS[MULTS.length - 1]; k.playing = true; this.openLayer(i, start, 'rec'); k.writer.grow = true;
  }
  /** The next bar (with a tempo) or loop start (without) at or after played time `at`. */
  nextBoundary(at) {
    const unit = this.bpm && this.quantize ? this.barFrames() : this.len;
    if (!unit) return at;
    const into = mod(at - this.anchor, unit);
    return into < FADE ? at - into : at + unit - into;
  }
  openLayer(i, at, kind) {
    const k = this.tracks[i], layer = Array.from({ length: this.ch }, () => new Float32Array(tlen(this.sec, k)));
    layer.g = 1; k.layers.push(layer); k.redo = []; k.writer = { kind, layer, start: at, end: Infinity, last: -1 }; k.playing = true; k.slow = false;
  }
  close(i, at = this.t) {
    const S = this.sec, k = S.tracks[i];
    if (S.first?.track === i) {
      const f = S.first, bf = this.beatFrames();
      let len = at - f.start;
      S.first = null;
      if (len < MIN_LOOP_S * this.rate) return;        // a slip of the foot (or a press during the count-in): nothing recorded
      if (this.quantize && bf) { const bar = bf * this.beats; len = Math.max(1, Math.round(len / bar)) * bar; }   // whole bars
      S.len = len; S.anchor = f.start;
      const layer = Array.from({ length: this.ch }, (_, c) => { const d = new Float32Array(len); d.set(f.data[c].subarray(0, Math.min(f.count, len))); return d; });
      layer.g = 1;
      const n = Math.min(f.count, len);                // fade in what is already there; the rest arrives as a tail
      for (let j = 0; j < Math.min(FADE, n); j++) for (let c = 0; c < this.ch; c++) layer[c][j] *= j / FADE;
      for (const kk of S.tracks) kk.mix = null;
      k.mult = 1; k.layers = [layer]; k.redo = []; k.playing = true; k.slow = false;
      k.tails.push({ kind: 'rec', layer, start: f.start, end: f.start + len, from: f.start + n });
      this.remix(i);
      return f.start + len;                            // when the loop really ends (later than the press if rounded up)
    }
    if (!k.writer) return;
    const w = k.writer; k.writer = null;
    if (at <= w.start) { k.layers.splice(k.layers.indexOf(w.layer), 1); if (w.grow) k.mult = 1; this.remix(i); return; }   // opened and closed at once
    if (w.grow) {                                      // a new track: 1, 2, 4 or 8 loops, folded from the 8-loop buffer
      const want = (at - w.start) / S.len, m = MULTS.reduce((b, x) => (Math.abs(Math.log2(x / want)) < Math.abs(Math.log2(b / want)) ? x : b), 1);
      const size = S.len * m, big = w.layer, small = Array.from({ length: this.ch }, (_, c) => {
        const d = new Float32Array(size); for (let j = 0; j < big[c].length; j++) d[j % size] += big[c][j]; return d;
      });
      small.g = 1; k.layers[k.layers.indexOf(big)] = small; w.layer = small; k.mult = m; w.grow = false;
      w.end = w.start + size; k.tails.push(w); this.remix(i);
      return w.end;
    }
    w.end = at; k.tails.push(w);
  }
  reanchor(at) { this.anchor = at + this.outLat; }     // the loop starts over: position 0 is heard right after the press
  play(i, at = this.t) {
    const k = this.tracks[i];
    if (!k.layers.length) return;
    if (!this.anyPlaying()) this.reanchor(at);
    k.playing = true; k.slow = false;
  }
  stop(i, at = this.t) { const k = this.tracks[i]; if (k.writer || this.first?.track === i) this.close(i, at); k.playing = false; k.slow = this.fadeStop; }
  stopAll(at = this.t) { this.tracks.forEach((k, i) => { if (k.writer || this.first?.track === i) this.close(i, at); k.playing = false; k.slow = true; }); this.drum.play = false; }
  playAll(at = this.t) {
    if (!this.anyPlaying()) this.reanchor(at);
    for (const k of this.tracks) if (k.layers.length) { k.playing = true; k.slow = false; }
    if (this.drumHits()) this.drum.play = true;
  }
  /** Undo: a pass being recorded is dropped; otherwise the last layer goes to the redo list. */
  undo(i) {
    const k = this.tracks[i];
    if (this.first?.track === i) { this.first = null; return; }
    let layer;
    if (k.writer) { layer = k.writer.layer; if (k.writer.grow) k.mult = 1; k.writer = null; k.layers.splice(k.layers.indexOf(layer), 1); }
    else if (k.layers.length) { layer = k.layers.pop(); k.redo.push(layer); }
    else return;
    k.tails = k.tails.filter(w => w.layer !== layer);
    if (!k.layers.length && !k.redo.length) k.mult = 1;
    this.remix(i); this.forgetIfEmpty();
  }
  redo(i) {
    const k = this.tracks[i];
    if (!k.redo.length || k.writer) return;
    const wasEmpty = !k.layers.length;
    k.layers.push(k.redo.pop()); this.remix(i);
    if (wasEmpty) { if (!this.anyPlaying()) this.reanchor(this.t); k.playing = true; }   // a stopped track stays stopped
  }
  clear(i) {
    const keep = this.tracks[i];
    if (this.first?.track === i) this.first = null;
    this.tracks[i] = emptyTrack(); this.tracks[i].muted = keep.muted; this.forgetIfEmpty();
  }
  clearAll() { this.sections[this.cur] = emptySection(this.tracks.length); if (this.nextSec === this.cur) this.nextSec = -1; }
  reverse(i) { this.tracks[i].reversed = !this.tracks[i].reversed; }
  mute(i) { this.tracks[i].muted = !this.tracks[i].muted; }
  half(i) { const k = this.tracks[i]; if (!k.writer) k.half = !k.half; }
  setCut(i, v) { this.tracks[i].cut = Math.max(0, Math.min(1, Number(v))); }
  forgetIfEmpty() {                                    // nothing left: the next recording sets a new length
    const S = this.sec;
    if (S.first || S.tracks.some(k => k.layers.length || k.writer) || this.drumHits(S)) return;
    S.len = 0; for (const k of S.tracks) { k.redo = []; k.mix = null; k.playing = false; k.mult = 1; }
    S.drum.play = false;
  }
  remix(i, S = this.sec) {
    const k = S.tracks[i], size = tlen(S, k);
    k.mix = Array.from({ length: this.ch }, () => new Float32Array(size));
    for (const layer of k.layers) {
      const g = layer.g ?? 1;
      for (let c = 0; c < this.ch; c++) { const m = k.mix[c], d = layer[c]; for (let j = 0; j < size && j < d.length; j++) m[j] += d[j] * g; }
    }
  }

  // ---- sections: A, B, C; a change waits for the end of the playing loop
  section(i, at = this.t) {
    if (i < 0 || i >= this.sections.length || i === this.cur) { this.nextSec = -1; return; }
    if (this.len && this.anyPlaying()) { this.nextSec = i; return; }
    this.switchTo(i, at + this.outLat);
  }
  switchTo(i, playedAt) {
    const old = this.sec;
    old.tracks.forEach((k, j) => { if (old.first?.track === j) old.first = null; if (k.writer) { const w = k.writer; k.writer = null; w.end = Math.max(w.start, playedAt); k.tails.push(w); } });
    this.cur = i; this.nextSec = -1;
    const S = this.sec;
    if (S.len) { S.anchor = playedAt; for (const k of S.tracks) { k.gain = 0; if (k.layers.length && !k.muted) k.playing = true; } if (this.drumHits(S)) S.drum.play = true; S.drum.lastStep = -1; }
  }

  // ---- drums: 16 steps a bar over the section's loop; drum mode records hits on the nearest step
  drumSteps(S = this.sec) {
    const bar = this.barFrames();
    const bars = S.len && bar ? Math.max(1, Math.min(8, Math.round(S.len / bar))) : S.drum.bars;
    return Math.min(MAX_STEPS, 16 * bars);
  }
  drumHits(S = this.sec) { return S.drum.pat.some(v => v); }
  /** Start the drums: without a loop yet, the section gets a length of its bars at the tempo (100 bpm if none is set). */
  drumStart(at = this.t) {
    const S = this.sec;
    if (!S.len && !S.first) { if (!this.bpm) this.bpm = 100; S.len = S.drum.bars * this.barFrames(); S.anchor = at + this.outLat; }
    S.drum.play = true; S.drum.lastStep = -1;
  }
  drumStop() { this.drum.play = false; }
  drumToggle(piece, step) { const d = this.drum, i = piece * MAX_STEPS + step; if (piece >= 0 && piece < PIECES.length && step >= 0 && step < MAX_STEPS) d.pat[i] = d.pat[i] ? 0 : 1; }
  drumClear(piece = -1) { const d = this.drum; if (piece < 0) d.pat.fill(0); else d.pat.fill(0, piece * MAX_STEPS, (piece + 1) * MAX_STEPS); this.forgetIfEmpty(); }
  drumPreset(rows, swing = 0.5) { const d = this.drum, steps = this.drumSteps(); d.pat.fill(0); d.swing = swing; rows.forEach((row, p) => { for (let s = 0; s < steps && row.length; s++) if (row[s % row.length] === 'x') d.pat[p * MAX_STEPS + s] = 1; }); }
  setSwing(v) { this.drum.swing = Math.max(0.5, Math.min(0.75, Number(v) || 0.5)); }
  /** When step j sounds, in frames from the loop start: the "and" of each beat (step 2 of 4) moves later with swing. */
  stepTime(j, stepF, d = this.drum) { return j * stepF + (j % 4 === 2 ? (d.swing - 0.5) * 4 * stepF : 0); }
  /** A hit from the footswitch: heard now, and written on the nearest step of the pattern. */
  drumHit(piece, at = this.t) {
    const S = this.sec, d = S.drum;
    this.voices.push({ p: piece, n: 0, ph: 0, hp: 0, at });
    if (!S.len) this.drumStart(at);
    const steps = this.drumSteps(S), stepF = S.len / steps, pos = mod(at - S.anchor, S.len), guess = Math.round(pos / stepF);
    let step = 0, best = Infinity;                       // the nearest step on the swung grid
    for (let j = guess - 1; j <= guess + 1; j++) { const jj = mod(j, steps), dist = Math.abs(mod(pos - this.stepTime(jj, stepF, d) + S.len / 2, S.len) - S.len / 2); if (dist < best) { best = dist; step = jj; } }
    d.pat[piece * MAX_STEPS + step] = 1; d.play = true;
    // Pressed a little early: the step has not played yet in this pass, so do not play it again right after the hit.
    if (mod(this.stepTime(step, stepF, d) - pos, S.len) < stepF) d.skip.push({ p: piece, step });
  }
  drumSeq(S, t) {
    const d = S.drum; if (!d.play || !S.len) return;
    const steps = this.drumSteps(S), stepF = S.len / steps, step = Math.floor(mod(t + this.outLat - S.anchor, S.len) / stepF);
    if (step === d.lastStep) return;
    d.lastStep = step;
    const late = step % 4 === 2 ? Math.round((d.swing - 0.5) * 4 * stepF) : 0;   // swing: the "and" sounds later
    for (let p = 0; p < PIECES.length; p++) {
      if (!d.pat[p * MAX_STEPS + step]) continue;
      const sk = d.skip.findIndex(x => x.p === p && x.step === step);
      if (sk >= 0) { d.skip.splice(sk, 1); continue; }
      this.voices.push({ p, n: 0, ph: 0, hp: 0, at: t + late });
    }
  }
  rand() { this.noise = (this.noise * 1103515245 + 12345) & 0x7fffffff; return this.noise / 0x3fffffff - 1; }
  /** One sample of a synthesized drum voice (null when it has finished). */
  voice(v) {
    const r = this.rate, n = v.n++, tt = n / r;
    const tone = f => { v.ph += 2 * Math.PI * f / r; return Math.sin(v.ph); };
    const hiss = () => { const x = this.rand(), y = x - v.hp; v.hp = x; return y; };
    switch (v.p) {
      case 0: return tt > 0.5 ? null : tone(48 + 110 * Math.exp(-tt / 0.03)) * Math.exp(-tt / 0.16) * 1.1;
      case 1: return tt > 0.3 ? null : (this.rand() * 0.55 * Math.exp(-tt / 0.08) + tone(185) * 0.45 * Math.exp(-tt / 0.05));
      case 2: return tt > 0.08 ? null : hiss() * 0.45 * Math.exp(-tt / 0.018);
      case 3: return tt > 0.45 ? null : hiss() * 0.4 * Math.exp(-tt / 0.14);
      case 4: { if (tt > 0.3) return null; const burst = tt < 0.03 ? Math.exp(-(tt % 0.01) / 0.003) : Math.exp(-(tt - 0.03) / 0.08); return this.rand() * 0.6 * burst; }
      case 5: return tt > 1.6 ? null : hiss() * 0.35 * Math.exp(-tt / 0.5);
      case 6: return tt > 0.5 ? null : tone(95 + 40 * Math.exp(-tt / 0.05)) * 0.8 * Math.exp(-tt / 0.18);
      case 7: return tt > 0.4 ? null : tone(160 + 60 * Math.exp(-tt / 0.04)) * 0.7 * Math.exp(-tt / 0.14);
      case 8: return tt > 0.05 ? null : (tone(1600) * 0.4 + this.rand() * 0.3) * Math.exp(-tt / 0.008);
    }
    return null;
  }

  // ---- export and sessions
  /** The tracks of the current section as plain sample arrays (for WAV files). */
  exportTracks() {
    const S = this.sec;
    return { rate: this.rate, base: S.len, tracks: S.tracks.map(k => (k.layers.length ? { mix: (k.mix || (this.remix(S.tracks.indexOf(k)), k.mix)).map(c => c.slice()), muted: k.muted, mult: k.mult } : null)) };
  }
  /** Everything needed to rebuild the session (each track flattened to one layer). */
  serialize() {
    return { rate: this.rate, bpm: this.bpm, beats: this.beats, cur: this.cur, sections: this.sections.map((S, si) => ({
      len: S.len, drum: { bars: S.drum.bars, vol: S.drum.vol, swing: S.drum.swing, pat: Array.from(S.drum.pat) },
      tracks: S.tracks.map((k, i) => (k.layers.length ? { mix: (k.mix || (this.remix(i, S), k.mix)).map(c => c.slice()), mult: k.mult, muted: k.muted, reversed: k.reversed, half: k.half, cut: k.cut } : null)),
    })) };
  }
  restore(o) {
    if (!o || o.rate !== this.rate) return false;
    if (o.bpm) this.bpm = o.bpm; if (o.beats) this.beats = o.beats;
    this.sections = o.sections.map(s => {
      const S = emptySection(this.tracks.length); S.len = s.len; S.drum.bars = s.drum?.bars || 1; S.drum.vol = s.drum?.vol ?? 0.8; S.drum.swing = s.drum?.swing ?? 0.5;
      if (s.drum?.pat) S.drum.pat.set(s.drum.pat.slice(0, S.drum.pat.length));
      s.tracks.forEach((t, i) => {
        if (!t || i >= S.tracks.length) return;
        const k = S.tracks[i], layer = t.mix.map(c => Float32Array.from(c)); layer.g = 1;
        Object.assign(k, { layers: [layer], mult: t.mult || 1, muted: !!t.muted, reversed: !!t.reversed, half: !!t.half, cut: t.cut ?? 1 });
      });
      return S;
    });
    this.cur = Math.min(o.cur || 0, this.sections.length - 1); this.nextSec = -1;
    this.sections.forEach(S => S.tracks.forEach((k, i) => { if (k.layers.length) this.remix(i, S); }));
    return true;
  }

  // ---- audio: input and output are arrays of channels (Float32Array), n frames each; output is overwritten
  process(input, output, n) {
    const ch = this.ch, inC = c => input[Math.min(c, input.length - 1)];
    for (const o of output) o.fill(0, 0, n);
    const F = this.first;
    if (F && input.length) {
      for (let s = 0; s < n; s++) {
        const p = this.t + s - this.inLat;
        if (p < F.start) continue;
        if (F.count >= MAX_LOOP_S * this.rate) break;
        if (F.count >= F.data[0].length) F.data = F.data.map(d => { const b = new Float32Array(d.length * 2); b.set(d); return b; });
        for (let c = 0; c < ch; c++) F.data[c][F.count] = inC(c)[s];
        F.count++;
      }
    }
    if (input.length) for (const S of this.sections) if (S.len) this.writeInput(S, inC, n);
    this.clickTick(output, n);
    // The section change waits for the playing loop's end: the block is split there.
    let from = 0;
    if (this.nextSec >= 0 && this.len) {
      const left = mod(this.anchor - this.t - this.outLat, this.len);
      if (left < n) { this.play_(output, 0, left); this.switchTo(this.nextSec, this.t + left + this.outLat); from = left; }
    }
    this.play_(output, from, n);
    this.drumOut(output, n);
    this.t += n;
  }
  writeInput(S, inC, n) {
    for (let i = 0; i < S.tracks.length; i++) {
      const k = S.tracks[i], writers = k.writer ? [k.writer, ...k.tails] : k.tails;
      if (!writers.length) continue;
      if (!k.mix) this.remix(i, S);
      for (const w of writers) {
        const size = w.layer[0].length;
        for (let s = 0; s < n; s++) {
          const p = this.t + s - this.inLat;
          if (p < w.start || p >= w.end || (w.from !== undefined && p < w.from)) continue;
          const idx = mod(p - S.anchor, size);
          // Decay: each time an overdub goes round, the older layers get quieter (the pass being recorded stays).
          if (w.kind === 'dub' && w.end === Infinity && this.decay < 1 && w.last >= 0 && idx < w.last) this.decayLayers(S, i, w.layer);
          w.last = idx;
          const g = Math.min(1, (p - w.start) / FADE, (w.end - p) / FADE);
          const sameSize = k.mix[0].length === size;
          for (let c = 0; c < this.ch; c++) { const x = inC(c)[s] * g; w.layer[c][idx] += x; if (sameSize) k.mix[c][idx] += x; }
        }
      }
      k.tails = k.tails.filter(w => this.t + n - this.inLat < w.end);
      if (k.writer?.grow && k.mix[0].length !== k.writer.layer[0].length) this.remix(i, S);
    }
  }
  decayLayers(S, i, current) {
    const k = S.tracks[i], dk = this.decay;
    for (const layer of k.layers) if (layer !== current) layer.g = (layer.g ?? 1) * dk;
    for (let c = 0; c < this.ch; c++) { const m = k.mix[c], cur = current[c]; for (let j = 0; j < m.length; j++) m[j] = (m[j] - cur[j]) * dk + cur[j]; }
  }
  play_(output, from, to) {
    const S = this.sec; if (!S.len || from >= to) return;
    const step = 1 / (0.005 * this.rate), slow = 1 / (0.25 * this.rate);
    for (let i = 0; i < S.tracks.length; i++) {
      const k = S.tracks[i];
      if (!k.mix || !k.layers.length) { k.peak = 0; continue; }
      const target = k.playing && !k.muted ? 1 : 0;
      if (k.gain === 0 && target === 0) { k.peak = 0; continue; }
      const size = k.mix[0].length, a = k.cut >= 1 ? 1 : 1 - Math.exp(-2 * Math.PI * 200 * Math.pow(2, k.cut * 6.6) / this.rate);
      let peak = 0;
      for (let s = from; s < to; s++) {
        k.gain += Math.max(-(k.slow ? slow : step), Math.min(step, target - k.gain));
        const at = this.t + s - S.anchor + this.outLat;
        let pos = k.half ? Math.floor(mod(at, 2 * size) / 2) : mod(at, size);
        if (k.reversed) pos = size - 1 - pos;
        for (let c = 0; c < output.length; c++) {
          let v = k.mix[Math.min(c, this.ch - 1)][pos] * k.gain;
          if (a < 1) { const lp = k.lp; const cc = Math.min(c, 1); lp[cc] += a * (v - lp[cc]); v = lp[cc]; }
          output[c][s] += v; if (Math.abs(v) > peak) peak = Math.abs(v);
        }
      }
      k.peak = peak;
    }
  }
  drumOut(output, n) {
    const S = this.sec, vol = S.drum.vol;
    for (let s = 0; s < n; s++) {
      this.drumSeq(S, this.t + s);
      if (!this.voices.length) continue;
      let v = 0;
      for (let j = this.voices.length - 1; j >= 0; j--) {
        const voice = this.voices[j];
        if (voice.at > this.t + s) continue;
        const x = this.voice(voice);
        if (x === null) this.voices.splice(j, 1); else v += x;
      }
      v *= vol * 0.6;
      for (const o of output) o[s] += v;
    }
  }
  /** Count-in and metronome clicks, added to the output (beat 1 higher). Each click is timed to be heard on the beat. */
  clickTick(output, n) {
    const bf = this.beatFrames(), f = this.first;
    if (bf) for (let s = 0; s < n; s++) {
      const t = this.t + s; let k = -1;
      if (f && f.countBeats && t >= f.clickFrom && t < f.clickFrom + f.countBeats * bf) { if ((t - f.clickFrom) % bf === 0) k = ((t - f.clickFrom) / bf) % this.beats; }
      else if (this.metro && f && t + this.outLat >= f.start) { const p = t + this.outLat - f.start; if (p % bf === 0) k = (p / bf) % this.beats; }
      else if (this.metro && !f && this.len && this.anyPlaying()) { const p = mod(t + this.outLat - this.anchor, this.len); if (p % bf === 0) k = (p / bf) % this.beats; }
      if (k >= 0) this.clicks.push({ at: t, f: k === 0 ? 1760 : 1175 });
    }
    if (!this.clicks.length) return;
    const L = Math.round(0.03 * this.rate), tau = 0.006 * this.rate;
    for (const c of this.clicks) {
      for (let s = Math.max(0, c.at - this.t); s < n; s++) {
        const j = this.t + s - c.at; if (j >= L) break;
        const v = this.clickGain * Math.sin(2 * Math.PI * c.f * j / this.rate) * Math.exp(-j / tau);
        for (const o of output) o[s] += v;
      }
    }
    this.clicks = this.clicks.filter(c => this.t + n - c.at < L);
  }
}

// ---- footswitch gestures for the looper, decided on the page: the press acts at once (no wait for a double press);
// a second press within 0.4 s or a 2-second hold first takes back what the press did, then does its own action.
export const DOUBLE_MS = 400, HOLD_MS = 2000;
export class FootGestures {
  /** actions: { press(sw) -> undo function or null, double(sw), hold(sw) }; now() in ms. */
  constructor(actions, now = () => Date.now()) { this.actions = actions; this.now = now; this.last = []; }
  press(sw) {
    const t = this.now(), prev = this.last[sw];
    if (prev && !prev.used && t - prev.t <= DOUBLE_MS) {
      prev.used = true; prev.down = true; prev.undo?.(); this.actions.double(sw); return 'double';
    }
    this.last[sw] = { t, down: true, used: false, undo: this.actions.press(sw) };
    return 'press';
  }
  release(sw) { const p = this.last[sw]; if (p) p.down = false; }
  tick() {
    const t = this.now();
    this.last.forEach((p, sw) => {
      if (p && p.down && !p.used && t - p.t >= HOLD_MS) { p.used = true; p.undo?.(); this.actions.hold(sw); }
    });
  }
}

// ---- the looper's footswitch map, on the engine's own clock.
// Loop mode:
//   A  press: record -> overdub -> play      double: stop the track       hold: undo (stopped: clear the track)
//   B  press: previous track                 double: stop all / play all  hold: redo
//   C  press: next track                     double: reverse the track    hold: clear the track
//   D  press: tap tempo (no loop yet) / mute  double: next section       hold: drum mode
// Drum mode: A, B and C play three drums at once (and write them on the pattern); D press: the next three drums,
// double: clear the pattern, hold: back to Loop mode.
export const FOOT_MAP = [
  ['Record / overdub / play', 'Stop the track', 'Undo (stopped: clear the track)'],
  ['Previous track', 'Stop all / play all', 'Redo'],
  ['Next track', 'Reverse the track', 'Clear the track'],
  ['Tap tempo (before the first loop), then mute the track', 'Next section (at the end of the loop)', 'Drum mode'],
];
export const DRUM_MAP = [
  ['First drum of the set', '', ''],
  ['Second drum of the set', '', ''],
  ['Third drum of the set', '', ''],
  ['Next set of drums', 'Clear the pattern', 'Back to Loop mode'],
];
export class LoopController {
  constructor(engine, onevent = () => {}) {
    this.e = engine; this.sel = 0; this.onevent = onevent; this.drum = false; this.page = 0;
    const e = engine, sel = () => this.sel, n = engine.tracks.length;
    this.gestures = new FootGestures({
      press: sw => {
        const i = sel();
        if (this.drum) { if (sw === 3) { const p = this.page; this.page = (p + 1) % DRUM_PAGES.length; return () => { this.page = p; }; } return null; }
        if (sw === 0) {
          const before = e.trackState(i); e.record(i); const after = e.trackState(i);
          if (before === 'stop' && after === 'play') return () => e.stop(i);
          if (['rec', 'dub', 'count', 'arm'].includes(after) && before !== after) return () => e.undo(i);
          return null;
        }
        if (sw === 1) { this.sel = (i + n - 1) % n; return () => { this.sel = i; }; }
        if (sw === 2) { this.sel = (i + 1) % n; return () => { this.sel = i; }; }
        if (sw === 3) {
          if (!e.len && !e.first) { e.tap(); this.onevent('bpm'); return null; }   // no loop yet: tap the tempo
          e.mute(i); return () => e.mute(i);
        }
        return null;
      },
      double: sw => {
        const i = sel();
        if (this.drum) { if (sw === 3) e.drumClear(); return; }
        if (sw === 0) e.stop(i);
        if (sw === 1) { if (e.anyPlaying()) e.stopAll(); else e.playAll(); }
        if (sw === 2) e.reverse(i);
        if (sw === 3 && !e.len && !e.first) { e.tap(); this.onevent('bpm'); return; }   // fast taps are taps, not a double
        if (sw === 3) e.section((e.cur + 1) % e.sections.length);
      },
      hold: sw => {
        const i = sel(), s = e.trackState(i);
        if (sw === 3) { this.drum = !this.drum; if (this.drum) e.drumStart(); this.onevent('drum'); return; }
        if (this.drum) return;
        if (sw === 0) { if (s === 'stop' || s === 'empty') e.clear(i); else e.undo(i); }
        if (sw === 1) e.redo(i);
        if (sw === 2) e.clear(i);
      },
    }, () => engine.t / engine.rate * 1000);
  }
  /** In drum mode A, B and C are drums: they sound at once, with no double press or hold to wait for. */
  press(sw) { if (this.drum && sw < 3) { this.e.drumHit(DRUM_PAGES[this.page][sw]); return 'hit'; } return this.gestures.press(sw); }
  release(sw) { if (!(this.drum && sw < 3)) this.gestures.release(sw); }
  tick() { this.gestures.tick(); }
}
