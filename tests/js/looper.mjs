// Website looper engine: what is recorded lands where it was played whatever the USB latency, overdubs line up with the
// loop, layers undo and redo one by one, and the footswitch map acts on the press and takes it back on a double or hold.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const root = new URL('../../web/', import.meta.url);
const app = await fs.readFile(new URL('app.js', root), 'utf8');
const version = app.match(/export const VERSION = '([^']+)'/)[1];
const { LoopEngine, LoopController, FootGestures, FOOT_MAP } = await import(new URL(`looper_engine.js?v=${version}`, root));

const RATE = 1000, IN = 10, OUT = 7, BLOCK = 10;
const sig = p => ((p * 37) % 997) / 1000 + 0.001;          // a different value for every played instant
// Run the engine until frame `to`; the guitarist plays play(p) at played time p, which arrives IN frames later.
function run(e, to, play = () => 0, out = []) {
  while (e.t < to) {
    const t0 = e.t, a = new Float32Array(BLOCK), b = new Float32Array(BLOCK);
    for (let s = 0; s < BLOCK; s++) a[s] = b[s] = play(t0 + s - IN);
    const o = [new Float32Array(BLOCK), new Float32Array(BLOCK)];
    e.process([a, b], o, BLOCK);
    for (let s = 0; s < BLOCK; s++) out[t0 + s] = o[0][s];
  }
  return out;
}
const near = (a, b, msg) => assert.ok(Math.abs(a - b) < 1e-5, `${msg}: ${a} vs ${b}`);

// The first loop: pressed at 100 and 600, so it is 500 frames long and starts with what was played at 100.
let e = new LoopEngine(RATE, 2); e.setLatency(IN, OUT);
run(e, 100, sig); e.record(0, 100); assert.equal(e.trackState(0), 'rec');
run(e, 600, sig); e.record(0, 600); e.close(0, 600);        // REC -> DUB, then DUB -> PLAY at once
assert.equal(e.len, 500); assert.equal(e.trackState(0), 'play');
run(e, 700, sig);
const base = e.tracks[0].layers[0][0];
for (const j of [150, 300, 420]) near(base[j], sig(100 + j), `position ${j} holds what was played at ${100 + j}`);
assert.ok(base[0] < sig(100) && base[499] < sig(599), 'the edges fade, so the seam does not click');
// What is heard at frame t + OUT is the loop position of played time t: it lines up with the guitarist.
const heard = run(e, 1700);
for (const t of [900, 1234, 1555]) near(heard[t], base[((t + OUT - 100) % 500 + 500) % 500], `output at ${t}`);

// An overdub lands where it was played: a note played at 1950 belongs to loop position (1950 - 100) mod 500 = 350.
const note = p => (p >= 1950 && p < 1960 ? 0.5 : 0);   // well past the 96-frame fade at this test rate
run(e, 1800); e.record(0, 1800); assert.equal(e.trackState(0), 'dub');
run(e, 2000, note); e.record(0, 2000); run(e, 2100, note);
const dub = e.tracks[0].layers[1][0];
near(dub[350], 0.5, "the overdub note is at position 350"); near(dub[349], 0, "and not before");
assert.equal(e.tracks[0].layers.length, 2);
// Undo and redo, one layer at a time; the playing mix follows.
e.undo(0); assert.equal(e.tracks[0].layers.length, 1); assert.equal(e.tracks[0].redo.length, 1); near(e.tracks[0].mix[0][350], base[350], "undone");
e.redo(0); assert.equal(e.tracks[0].layers.length, 2); near(e.tracks[0].mix[0][350], base[350] + 0.5, "redone");
// Reverse plays the loop backwards; mute silences it.
e.reverse(0); const rev = run(e, 2700); const t = 2611; near(rev[t], e.tracks[0].mix[0][499 - ((t + OUT - e.anchor) % 500 + 500) % 500], 'reversed');
e.reverse(0); e.mute(0); const muted = run(e, 2900); near(muted[2899], 0, 'muted');
// A second track records into the same length, in time with the first.
e.mute(0); e.record(1, 2900); run(e, 3100, sig); e.record(1, 3100); e.stop(1, 3100); run(e, 3200, sig);
assert.equal(e.tracks[1].layers[0][0].length, 500); assert.equal(e.trackState(1), 'stop');
// Clearing every track forgets the length, so the next recording sets a new one.
e.clearAll(); assert.equal(e.len, 0);

// Gestures: the press acts at once; a second press within 0.4 s or a 2 s hold first takes it back.
let now = 0; const log = [];
const g = new FootGestures({ press: sw => { log.push('press' + sw); return () => log.push('undo' + sw); }, double: sw => log.push('double' + sw), hold: sw => log.push('hold' + sw) }, () => now);
g.press(0); g.release(0); now = 300; g.press(0); g.release(0);
assert.deepEqual(log, ['press0', 'undo0', 'double0']);
now = 1000; g.press(1); now = 2999; g.tick(); assert.equal(log.at(-1), 'press1'); now = 3000; g.tick(); assert.deepEqual(log.slice(-2), ['undo1', 'hold1']);
now = 4000; g.release(1); g.press(1); g.release(1); now = 4500; g.press(1); assert.equal(log.at(-1), 'press1', 'too late for a double: a new press');

// The looper's footswitch map on the engine clock (1 frame = 1 ms here).
e = new LoopEngine(RATE, 2); e.setLatency(IN, OUT);
const events = [], c = new LoopController(e, ev => events.push(ev));
const step = ms => { const to = e.t + ms; while (e.t < to) { c.tick(); run(e, e.t + BLOCK, sig); } };
c.press(0); c.release(0); step(1000);                       // A: record
c.press(0); c.release(0); step(1000);                       // A: loop closed, overdub starts
assert.equal(e.len, 1000); assert.equal(e.trackState(0), 'dub');
c.press(0); c.release(0); step(100); c.press(0); c.release(0); step(10);   // A, A within 0.4 s: double = stop the track
assert.equal(e.trackState(0), 'stop'); assert.equal(e.tracks[0].layers.length, 2, 'the overdub before the double press is kept');
c.press(2); c.release(2); assert.equal(c.sel, 1); step(500);                 // C: next track
c.press(1); c.release(1); assert.equal(c.sel, 0); step(500);                 // B: previous track
c.press(0); c.release(0); step(500); assert.equal(e.trackState(0), 'play');  // A on a stopped track: play
c.press(0); step(2100); c.release(0);                       // A held while playing: the overdub it started goes, then undo
assert.equal(e.tracks[0].layers.length, 1); assert.equal(e.trackState(0), 'play'); assert.equal(e.tracks[0].redo.length, 1);
c.press(3); step(2100); c.release(3); assert.deepEqual(events, ['drum']); assert.equal(c.drum, true, 'D held: drum mode'); assert.equal(e.tracks[0].muted, false, 'the hold took the mute back');
c.press(3); step(2100); c.release(3); assert.equal(c.drum, false, 'D held again: back to Loop mode');
c.press(1); c.release(1); step(100); c.press(1); c.release(1); step(10);     // B double: stop all
assert.equal(e.trackState(0), 'stop'); assert.equal(c.sel, 0, 'the double press took back the track change');
c.press(1); step(2100); c.release(1); assert.equal(e.tracks[0].layers.length, 2, 'B held: redo');
c.press(0); step(2100); c.release(0);                       // A held on a stopped track: clear it
assert.equal(e.trackState(0), 'empty'); assert.equal(e.len, 0, 'nothing left: the next loop sets a new length');
assert.equal(FOOT_MAP.length, 4);

// Count-in: at 120 bpm (500 frames a beat here) and one bar of 4/4, a press at 100 clicks at 100, 600, 1100 and 1600
// (heard OUT frames later, on the beat) and the recording starts on the next downbeat: 100 + OUT + 2000.
e = new LoopEngine(RATE, 2); e.setLatency(IN, OUT); e.setTempo({ bpm: 120, beats: 4, countBars: 1, quantize: true });
run(e, 100); e.record(0, 100);
assert.equal(e.trackState(0), 'count'); assert.deepEqual(e.snapshot().count, { beat: 1, of: 4, bar: 4 });
const counted = run(e, 2200, sig);
for (const at of [100, 600, 1100, 1600]) assert.ok(Math.abs(counted[at + 3]) > 0.01, `a click at ${at}`);
for (const at of [400, 900, 1400, 1900, 2150]) near(counted[at], 0, `silence at ${at}`);
assert.ok(Math.abs(counted[100 + 3]) > Math.abs(counted[600 + 3]) * 0 && e.trackState(0) === 'rec', 'recording after the count-in');
assert.equal(e.first.start, 100 + OUT + 2000);
// Rounded to whole bars (2000 frames): a press 1.85 bars in makes a 2-bar loop, recorded to its real end, then the overdub.
const start = e.first.start; run(e, start + 3700, sig); e.record(0, start + 3700);
assert.equal(e.len, 4000); assert.equal(e.tracks[0].writer.start, start + 4000, 'the overdub starts where the loop really ends');
run(e, start + 4100, sig); near(e.tracks[0].layers[0][0][3900], sig(start + 3900), 'the base loop got its last bars');
// A press during the count-in cancels it.
e = new LoopEngine(RATE, 2); e.setTempo({ bpm: 120, countBars: 1 }); e.record(0, 0); run(e, 300); e.record(0, 300);
assert.equal(e.first, null); assert.equal(e.trackState(0), 'empty');
// Tap tempo on D before the first loop: four taps 500 frames apart are 120 bpm.
e = new LoopEngine(RATE, 2); const tc = new LoopController(e, () => {});
for (let k = 0; k < 4; k++) { tc.press(3); tc.release(3); run(e, e.t + 500); }
assert.equal(e.bpm, 120); assert.equal(e.tracks[0].muted, false, 'no loop yet: D tapped, it did not mute');
e = new LoopEngine(RATE, 2); const fast = new LoopController(e, () => {});
for (let k = 0; k < 6; k++) { fast.press(3); fast.release(3); run(e, e.t + 300); }   // 200 bpm: taps closer than a double press
assert.equal(e.bpm, 200);

// ---- New tracks: on the next bar, and 1, 2, 4 or 8 loops long. 120 bpm, 4/4: a bar is 2000 frames; the loop is one bar.
const { MULTS, PIECES, DRUM_PAGES } = await import(new URL(`looper_engine.js?v=${version}`, root));
e = new LoopEngine(RATE, 2); e.setLatency(IN, OUT); e.setTempo({ bpm: 120, beats: 4, quantize: true, qstart: true });
run(e, 100, sig); e.record(0, 100); run(e, 2100, sig); e.record(0, 2100); e.close(0, 2100);
assert.equal(e.len, 2000);
run(e, 2600, sig); e.record(1, 2600);                                       // half a bar in: waits for the next bar (4100)
assert.equal(e.trackState(1), 'arm'); assert.equal(e.tracks[1].writer.start, 4100);
run(e, 4150, sig); assert.equal(e.trackState(1), 'rec');
run(e, 8000, sig); e.record(1, 8000);                                       // 1.95 loops: rounded to 2, then overdub
assert.equal(e.tracks[1].mult, 2); assert.equal(e.tracks[1].layers[0][0].length, 4000);
assert.ok(MULTS.includes(e.tracks[1].mult));
run(e, 8200, sig); near(e.tracks[1].layers[0][0][((5000 - 100) % 4000)], sig(5000), 'the 2-loop track holds its second loop');
// Half speed plays the track an octave down: each sample twice.
e.stop(1); const hk = e.tracks[0]; e.half(0); const halfOut = run(e, 9000);
const hp = Math.floor((((8900 - e.anchor + OUT) % 4000) + 4000) % 4000 / 2); near(halfOut[8900], hk.mix[0][hp], 'half speed');
e.half(0);
// Decay: each turn of an overdub makes the older layers quieter.
e.setTempo({ decay: 0.5 }); const before = e.tracks[0].mix[0][500];
e.record(0, 9000); run(e, 11100); e.record(0, 11100);                        // one full turn of overdub (silent input)
assert.ok(Math.abs(e.tracks[0].mix[0][500]) < Math.abs(before) * 0.75, 'the older layer decayed');
e.setTempo({ decay: 1 });

// ---- Sections: B has its own tracks; a change waits for the end of the playing loop.
e.section(1, 11200); assert.equal(e.cur, 0); assert.equal(e.nextSec, 1);
const left = ((e.anchor - e.t - OUT) % e.len + e.len) % e.len;
run(e, e.t + left + 20); assert.equal(e.cur, 1, 'section B after the loop end'); assert.equal(e.len, 0);
e.record(0); run(e, e.t + 1000, sig); e.record(0); e.close(0);
assert.equal(e.len, 2000, 'B has its own loop'); e.section(0); assert.equal(e.nextSec, 0);
run(e, e.t + 2100); assert.equal(e.cur, 0); assert.equal(e.tracks[1].mult, 2, 'A kept its tracks');

// ---- Drums: a pattern plays on its steps; drum mode writes footswitch hits on the nearest step.
e = new LoopEngine(RATE, 2); e.setLatency(IN, OUT); e.setTempo({ bpm: 120, beats: 4 });
e.drumStart(0); assert.equal(e.len, 2000, 'drums without a loop: one bar'); assert.equal(e.drumSteps(), 16);
e.drumToggle(0, 0); e.drumToggle(1, 4);
const drums = run(e, 2100);
const firstBeat = (e.anchor - OUT), snareAt = firstBeat + 4 * 125;
assert.ok(Math.abs(drums[firstBeat + 5]) > 0.01, 'the kick on step 1');
assert.ok(Math.abs(drums[snareAt + 5]) > 0.01, 'the snare on step 5');
const dc = new LoopController(e, () => {});
dc.press(3); run(e, e.t + 2100); dc.release(3); dc.tick();                   // (D press: next set; the hold timing runs on the engine clock)
let ev = []; const dc2 = new LoopController(e, x => ev.push(x));
dc2.press(3); for (let k = 0; k < 22; k++) { dc2.tick(); run(e, e.t + 100); } dc2.release(3);
assert.equal(dc2.drum, true); assert.deepEqual(ev, ['drum']);
const hitAt = e.t + ((e.anchor + 8 * 125 - 20 - e.t) % 2000 + 2000) % 2000;   // just before step 9 of the next pass
run(e, hitAt); dc2.press(1); dc2.release(1);                                // B = snare (first set)
assert.equal(e.drum.pat[1 * 128 + 8], 1, 'the hit landed on step 9'); assert.equal(e.drum.skip.length, 1, 'and will not double');
assert.equal(DRUM_PAGES[0][1], 1); assert.equal(PIECES.length, 9);
dc2.press(3); dc2.release(3); assert.equal(dc2.page, 1, 'D: the next set of drums'); run(e, e.t + 500);
dc2.press(3); dc2.release(3); run(e, e.t + 100); dc2.press(3); dc2.release(3);   // D twice: clear the pattern
assert.equal(e.drum.pat.some(v => v), false);

// ---- Swing (blues shuffle): the "and" of each beat (step 3 of 16) sounds late, and a hit there lands on it.
e = new LoopEngine(RATE, 2); e.setLatency(IN, OUT); e.setTempo({ bpm: 120, beats: 4 });
e.drumStart(0); e.drumPreset(['..x.............'], 0.66); assert.equal(e.drum.swing, 0.66);
const sw = run(e, 2100), beat0 = e.anchor - OUT, late = Math.round(0.16 * 4 * 125);   // 80 of the 125 frames of a step
const peak = (a, b) => Math.max(...sw.slice(a, b).map(Math.abs));
assert.ok(peak(beat0 + 250, beat0 + 250 + late - 5) < 1e-6, 'no kick on the straight "and"');
assert.ok(peak(beat0 + 250 + late, beat0 + 250 + late + 20) > 0.01, 'the kick on the swung "and"');
e.drumHit(1, e.anchor + 250 + late + 2000);                                   // B = snare, right on the swung "and"
assert.equal(e.drum.pat[1 * 128 + 2], 1, 'a hit on the swung "and" lands on step 3, not 4');
e.setSwing(2); assert.equal(e.drum.swing, 0.75); e.drumPreset(['x']); assert.equal(e.drum.swing, 0.5, 'a straight beat resets the swing');

// ---- Export, save and restore.
e = new LoopEngine(RATE, 2); e.setLatency(IN, OUT);
run(e, 100, sig); e.record(0, 100); run(e, 600, sig); e.record(0, 600); e.close(0, 600); run(e, 700, sig);
const ex = e.exportTracks(); assert.equal(ex.tracks[0].mix[0].length, 500); assert.equal(ex.tracks[1], null);
const saved = JSON.parse(JSON.stringify(e.serialize(), (k, v) => (v instanceof Float32Array ? Array.from(v) : v)));
const back = new LoopEngine(RATE, 2); assert.equal(back.restore(saved), true);
assert.equal(back.len, 500); near(back.tracks[0].mix[0][300], e.tracks[0].mix[0][300], 'restored');
assert.equal(new LoopEngine(44100, 2).restore(saved), false, 'another sample rate is refused');

// ---- A loop opened as a session with its audio on track 1: it plays from its start, a new take goes on another track
// at the same length, and the exported mix sums them.
const { LooperView } = await import(new URL(`looper_view.js?v=${version}`, root));
const L = Float32Array.from({ length: 400 }, (_, i) => (i + 1) / 1000), R = L.map(v => -v);
const jam = { rate: RATE, bpm: 92, beats: 4, cur: 0, sections: [{ len: 400, drum: { bars: 1 }, tracks: [{ mix: [L, R], mult: 1 }, null, null, null] }, { len: 0, tracks: [] }, { len: 0, tracks: [] }] };
e = new LoopEngine(RATE, 2); e.setLatency(IN, OUT); run(e, 50);
assert.equal(e.restore(jam), true); assert.equal(e.len, 400); assert.equal(e.bpm, 92); assert.equal(e.trackState(0), 'stop');
e.play(0); const jamOut = run(e, e.t + 800);   // the second pass is at full volume
assert.ok(jamOut.some(v => Math.abs(v - L[100]) < 1e-6) && jamOut.some(v => Math.abs(v - L[300]) < 1e-6), 'the opened loop plays');
e.record(1); run(e, e.t + 400, sig); e.record(1);
assert.equal(e.tracks[1].layers[0][0].length, 400, 'a take over it has its length');
const mix = LooperView.mixOf(e.exportTracks());
assert.equal(mix[0].length, 400); near(mix[1][10], R[10] + e.tracks[1].mix[1][10], 'the exported mix sums the tracks');
e.mute(0); near(LooperView.mixOf(e.exportTracks())[0][10], e.tracks[1].mix[0][10], 'a muted track is left out');

console.log('Looper engine: latency, overdub alignment, layers, reverse, tracks, count-in, bars, tap tempo, gestures, bar starts, 2x tracks, half speed, decay, sections, drums, swing, drum mode, export, sessions and opened loops checked');
