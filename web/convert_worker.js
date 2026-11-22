// Browser worker: NAM A2 -> AMP model record, the whole pipeline off the page's
// main thread. Training batches are spread over nested gradient workers.
import { renderTeacher, fitRecord, DEFAULTS } from './converter/convert.js';
import { gradPool } from './converter/pool.js';
import { loadNam, renderNamBlocks } from './converter/nam.js';
import { heldout } from './converter/signals.js';
import { resamplePoly } from './converter/dsp.js';
import { runModel, compare } from './converter/model.js';

const post = m => self.postMessage(m);
self.onmessage = async e => {
  const { nam, name, steps = DEFAULTS.steps, workers = 4, trainAudio = null } = e.data;
  try {
    const t0 = performance.now(), el = () => Math.round((performance.now() - t0) / 1000);
    post({ type: 'stage', stage: 'teacher', text: 'Rendering the NAM capture (full A2 model)' });
    let lastP = -1;
    const { sig, info, rate, inputGain } = renderTeacher(nam, { trainAudio }, ev => { const p = Math.floor(ev.progress * 100); if (p !== lastP) { lastP = p; post({ type: 'progress', stage: 'teacher', value: ev.progress }); } });
    post({ type: 'info', info, rate });
    let pool = null;
    if (workers > 1 && typeof Worker !== 'undefined') {
      try {
        const spawn = () => { const w = new Worker(new URL('./converter/grad_worker.js', import.meta.url), { type: 'module' }); return { postMessage: (m, t) => w.postMessage(m, t), on: h => { w.onmessage = ev => h(ev.data); }, terminate: () => w.terminate() }; };
        pool = await gradPool(spawn, workers, sig.xa, sig.ya, 128);
      } catch (err) { pool = null; post({ type: 'note', text: 'Parallel workers unavailable here; training on one thread (slower).' }); }
    }
    post({ type: 'stage', stage: 'fit', text: 'Fitting the amp model' });
    let lastS = 0;
    const r = await fitRecord(sig, name, { steps, gradFn: pool && pool.gradFn }, ev => {
      if (ev.stage === 'baseline') post({ type: 'progress', stage: 'baseline', value: 0 });
      if (ev.stage === 'refine' && (ev.step - lastS >= 10 || ev.step === ev.steps - 1)) { lastS = ev.step; post({ type: 'progress', stage: 'refine', value: (ev.step + 1) / ev.steps, loss: ev.loss }); }
    });
    if (pool) pool.close();
    post({ type: 'stage', stage: 'report', text: 'Measuring the result against the NAM capture' });
    const { net } = loadNam(nam), H = heldout(rate), clips = ['attacks', 'recovery', 'sustained'];
    const to44 = x => rate === 44100 ? x : resamplePoly(x, 44100, rate);
    const cat = parts => { const o = new Float64Array(parts.reduce((s, p) => s + p.length, 0)); let k = 0; for (const p of parts) { o.set(p, k); k += p.length; } return o; };
    const tParts = clips.map(k => to44(renderNamBlocks(net, inputGain === 1 ? H[k] : H[k].map(v => v * inputGain)))), sParts = clips.map(k => runModel(r.model, to44(H[k])));
    const q = compare(cat(tParts), cat(sParts));
    // preview: the sustained-chord clip through both (for A/B listening)
    const preview = { input: to44(H.sustained), nam: tParts[2], amp: sParts[2] };
    post({ type: 'done', record: r.record, quality: q, seconds: el(), preview }, );
  } catch (err) {
    post({ type: 'error', text: err && err.message ? err.message : String(err) });
  }
};
