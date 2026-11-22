#!/usr/bin/env node
// NAM A2 (.nam) -> Frost AMP model record (.fcm, 400 bytes), the same JS
// pipeline the web page runs (web/converter). EXPERIMENTAL: a converted model
// is an APPROXIMATION of its NAM capture; the report scores it against the
// full A2 model on held-out guitar-like clips (Mel log-spectral error, dB).
//
//   node tools/fcm_convert.mjs capture.nam --name "JCM800" --out jcm.fcm [--workers 8] [--steps 1500] [--train input.wav]
//   --no-input-match: ignore the capture's input_level_dbu; --gp5-dbu N: GP-5 full-scale input level (default +8)
//   --train: training audio (e.g. the user's copy of NAM's official input.wav) instead of the built-in synthetic audio
//   then: python tools/fcm_files.py add 1 jcm.fcm   (a device write)
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { Worker } from 'node:worker_threads';
import { fileURLToPath, pathToFileURL } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url)), conv = p => pathToFileURL(path.join(here, '../web/converter', p)).href;
const { renderTeacher, fitRecord, DEFAULTS } = await import(conv('convert.js'));
const { gradPool } = await import(conv('pool.js'));
const { loadNam, renderNamBlocks: renderNam } = await import(conv('nam.js'));
const { heldout } = await import(conv('signals.js'));
const { resamplePoly } = await import(conv('dsp.js'));
const { runModel, compare } = await import(conv('model.js'));
const { readWav } = await import(pathToFileURL(path.join(here, '../web/ir.js')).href);

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf('--' + k); return i >= 0 ? args[i + 1] : d; };
const file = args.find(a => a.endsWith('.nam'));
if (!file) { console.error('usage: node tools/fcm_convert.mjs capture.nam [--name NAME] [--out file.fcm] [--workers N] [--steps N] [--report report.json]'); process.exit(2); }
const name = (opt('name', path.basename(file, '.nam'))).slice(0, 15), out = opt('out', path.basename(file, '.nam') + '.fcm');
const workers = Number(opt('workers', Math.max(1, Math.min(8, os.cpus().length - 1)))), steps = Number(opt('steps', DEFAULTS.steps));
const nam = JSON.parse(fs.readFileSync(file, 'utf8')), t0 = Date.now(), say = s => console.log(`[${((Date.now() - t0) / 1000).toFixed(0)}s] ${s}`);

const trainFile = opt('train', null), trainAudio = trainFile ? readWav(new Uint8Array(fs.readFileSync(trainFile)), 600, 'Training audio') : null;
if (trainAudio) say(`training audio ${path.basename(trainFile)}: ${(trainAudio.x.length / trainAudio.rate).toFixed(1)} s at ${trainAudio.rate} Hz`);
let lastPct = -1;
const { sig, info, rate, inputGain } = renderTeacher(nam, { trainAudio, matchInput: !args.includes('--no-input-match'), gp5InputDbu: opt('gp5-dbu') == null ? undefined : Number(opt('gp5-dbu')) }, e => { const p = Math.floor(e.progress * 10); if (p !== lastPct) { lastPct = p; say(`teacher ${p * 10}%`); } });
say(`teacher done: ${info.channels} ch, ${info.layers} layers, ${info.weights} weights, ${rate} Hz`);
say(info.input.applied ? `input level matched: capture +${info.input.captureDbu} dBu full scale, GP-5 input, ${info.input.db >= 0 ? '+' : ''}${info.input.db.toFixed(1)} dB into the capture` : `input level: ${info.input.captureDbu == null ? 'capture has no input_level_dbu; used as is' : 'matching off'}`);
const spawn = () => { const w = new Worker(new URL(conv('grad_worker.js'))); return { postMessage: (m, t) => w.postMessage(m, t), on: h => w.on('message', h), terminate: () => w.terminate() }; };
const pool = workers > 1 ? await gradPool(spawn, workers, sig.xa, sig.ya, 128) : null;
let avg = null, lastT = 0;
const r = await fitRecord(sig, name, { steps, gradFn: pool && pool.gradFn }, e => {
  if (e.stage === 'refine' && Number.isFinite(e.loss)) { avg = avg == null ? e.loss : 0.97 * avg + 0.03 * e.loss; if (Date.now() - lastT > 15000 || e.step === e.steps - 1) { lastT = Date.now(); say(`refine ${e.step + 1}/${e.steps} loss ${avg.toFixed(3)}`); } }
  if (e.stage === 'iir') say('biquad fit error ' + JSON.stringify(Object.fromEntries(Object.entries(e.fit).map(([k, v]) => [k, +v.toFixed(5)]))));
});
if (pool) pool.close();
fs.writeFileSync(out, r.record);
say(`wrote ${out} (${r.record.length} bytes)`);
// quality report: held-out clips through the full NAM and the converted model (fresh state per clip)
const { net } = loadNam(nam), H = heldout(rate), clips = ['attacks', 'recovery', 'sustained'];
const cat = parts => { const o = new Float64Array(parts.reduce((s, p) => s + p.length, 0)); let k = 0; for (const p of parts) { o.set(p, k); k += p.length; } return o; };
const to44 = x => rate === 44100 ? x : resamplePoly(x, 44100, rate);
const teacher = cat(clips.map(k => to44(renderNam(net, inputGain === 1 ? H[k] : H[k].map(v => v * inputGain))))), student = cat(clips.map(k => runModel(r.model, to44(H[k]))));
const q = compare(teacher, student);
const report = { file: path.basename(file), name, record: out, steps, workers, seconds: Math.round((Date.now() - t0) / 1000), teacher: info,
  quality: Object.fromEntries(Object.entries(q).map(([k, v]) => [k, +v.toFixed(3)])), biquad_fit_error: r.fitError,
  note: 'Mel log-spectral error vs the full A2 model on held-out clips; a converted model is an approximation.' };
say('quality ' + JSON.stringify(report.quality));
const rp = opt('report'); if (rp) fs.writeFileSync(rp, JSON.stringify(report, null, 2) + '\n');
