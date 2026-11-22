// Gradient worker: holds the training audio, returns summed loss/gradient for a
// subset of a batch. Same file in browsers (Web Worker, module) and Node (worker_threads).
import { makeParams, batchGrad } from './refine.js';
let P = null, xa = null, ya = null;
function handle(msg) {
  if (msg.type === 'init') {
    xa = msg.xa; ya = msg.ya;
    const npost = msg.npost, sb = { pre: new Float64Array(128), post: new Float64Array(npost), g: 1, A: [1, 1], K: [1, 1] };
    P = makeParams(sb); return { type: 'ready' };
  }
  if (msg.type === 'grad') {
    P.v.set(msg.v);
    const r = batchGrad(P, xa, ya, msg.starts, msg.seg), n = msg.starts.length;
    for (let i = 0; i < r.grad.length; i++) r.grad[i] *= n;            // sums, the caller averages
    return { type: 'grad', id: msg.id, loss: r.loss * n, grad: r.grad, n };
  }
}
if (typeof self !== 'undefined' && typeof self.postMessage === 'function' && typeof process === 'undefined') {
  self.onmessage = e => { const out = handle(e.data); if (out) self.postMessage(out, out.grad ? [out.grad.buffer] : []); };
} else {
  const { parentPort } = await import('node:worker_threads');
  parentPort.on('message', m => { const out = handle(m); if (out) parentPort.postMessage(out, out.grad ? [out.grad.buffer] : []); });
}
