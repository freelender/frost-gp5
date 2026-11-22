// NAM A2 -> FCM record, the whole pipeline (browser or Node):
// 1 teacher renders (full A2 submodel, at its own rate) of probes + 90 s of
//   guitar-like audio; 2 spectral baseline; 3 gradient refinement (cSB+3);
// 4 biquad fit 3+1+3; 5 the 400-byte record the pedal loads.
// A converted model is an APPROXIMATION of its NAM capture.
import { loadNam, renderNamBlocks as renderNam } from './nam.js';
import { resamplePoly } from './dsp.js';
import { probes, guitarish } from './signals.js';
import { fitSB, FS } from './sb.js';
import { makeParams, train, exportModel, AA } from './refine.js';
import { fitCascade } from './iir.js';
import { packRecord } from './record.js';

export const DEFAULTS = { trainSeconds: 90, steps: 1500, sections: [3, 1, 3] };
/** Analog level of a full-scale sample at the GP-5 input, dBu. The AMP block receives the
 *  converter's samples unscaled (firmware/dsp/input.h), so this is what the model sees.
 *  +8 dBu is the published maximum input level (single source, not yet measured here). */
export const GP5_INPUT_DBU = 8;
/** Input calibration (NAM's input_level_dbu: the dBu of the capture interface's full scale).
 *  A calibrated capture is rendered at the level the GP-5 would really feed it, so its gain
 *  feel matches the amp; an uncalibrated capture is used as is, like NAM's own plugin. */
export function inputCalibration(namJson, opts = {}) {
  const dbu = Number(namJson?.metadata?.input_level_dbu);
  if (opts.matchInput === false || !Number.isFinite(dbu)) return { gain: 1, db: 0, captureDbu: Number.isFinite(dbu) ? dbu : null, applied: false };
  const db = (opts.gp5InputDbu ?? GP5_INPUT_DBU) - dbu;
  return { gain: 10 ** (db / 20), db, captureDbu: dbu, applied: true };
}
const to44 = (x, rate) => rate === FS ? Float64Array.from(x) : resamplePoly(x, FS, rate);

/** Teacher I/O at 44.1 kHz. teacherIO lets validation reuse existing renders. */
export function renderTeacher(namJson, opts = {}, log = () => {}) {
  const { net, sampleRate, info } = loadNam(namJson);
  const rate = Math.round(sampleRate), p = probes(rate), out = {}, cal = inputCalibration(namJson, opts);
  info.input = cal;
  // Training audio: the user's own file (e.g. NAM's official input.wav) brought to the capture's
  // rate, so the NAM runs at the rate it was trained at; else synthetic guitar-like audio.
  const ta = opts.trainAudio, tr = ta && Math.round(ta.rate);
  const train = ta ? (tr === rate ? Float64Array.from(ta.x) : resamplePoly(ta.x, rate, tr)) : guitarish(opts.trainSeconds ?? DEFAULTS.trainSeconds, rate, 1);
  let done = 0; const total = p.ramp.length + p.noise.length + p.sweep.length + train.length;
  const run = (k, x) => { const y = renderNam(net, cal.applied ? x.map(v => v * cal.gain) : x, f => log({ stage: 'teacher', progress: (done + f * x.length) / total })); done += x.length; out['x' + k] = to44(x, rate); out['y' + k] = to44(y, rate); };
  run('r', p.ramp); run('n', p.noise); run('s', p.sweep); run('a', train);
  return { sig: out, info, rate, inputGain: cal.gain };
}
export async function fitRecord(sig, name, opts = {}, log = () => {}) {
  const sb = fitSB(sig, s => log({ stage: 'baseline', note: s }));
  const P = makeParams(sb), steps = opts.steps ?? DEFAULTS.steps;
  await train(P, sig.xa, sig.ya, { steps, gradFn: opts.gradFn, onStep: (s, loss) => log({ stage: 'refine', step: s, steps, loss }) });
  const m = exportModel(P), [kp, km, ko] = opts.sections ?? DEFAULTS.sections, fit = {};
  const cas = (k, K) => { const r = fitCascade(m[k], K); fit[k] = r.err; return r.sos; };
  const model = { pre: cas('pre', kp), mid: cas('mid', km), post: cas('post', ko), g: m.g, A: m.A, K: m.K, ea: m.ea, ek: m.ek, aa: Array.from(AA) };
  log({ stage: 'iir', fit });
  return { record: packRecord(name, model), model, fir: m, sb, fitError: fit };
}
export async function convert(namJson, name, opts = {}, log = () => {}) {
  const { sig, info } = renderTeacher(namJson, opts, log);
  return { ...(await fitRecord(sig, name, opts, log)), info };
}
