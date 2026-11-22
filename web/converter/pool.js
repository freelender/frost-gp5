// A pool of gradient workers. spawn(): returns an object with
// postMessage(msg, transfer) and on(handler) (browser Worker or Node worker_threads).
export async function gradPool(spawn, count, xa, ya, npost) {
  const workers = [];
  for (let i = 0; i < count; i++) {
    const w = spawn(); let pending = null;
    w.on(m => { const p = pending; pending = null; if (p) p(m); });
    w.call = msg => new Promise(res => { pending = res; w.postMessage(msg); });
    await w.call({ type: 'init', xa: Float32Array.from(xa), ya: Float32Array.from(ya), npost });   // float32 copies: half the memory per worker
    workers.push(w);
  }
  let id = 0;
  const gradFn = async (v, starts, seg) => {
    const parts = workers.map(() => []); starts.forEach((s, i) => parts[i % workers.length].push(s));
    const res = await Promise.all(workers.map((w, i) => parts[i].length ? w.call({ type: 'grad', id: ++id, v: Float64Array.from(v), starts: parts[i], seg }) : null));
    const grad = new Float64Array(v.length); let loss = 0, n = 0;
    for (const r of res) if (r) { for (let i = 0; i < grad.length; i++) grad[i] += r.grad[i]; loss += r.loss; n += r.n; }
    for (let i = 0; i < grad.length; i++) grad[i] /= n;
    return { loss: loss / n, grad };
  };
  return { gradFn, close: () => workers.forEach(w => w.terminate()) };
}
