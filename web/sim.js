// index.html?demo: a simulated pedal for the live effects editor (7E 14..18,
// same replies as firmware/controller/live_host.h), to try the page without
// hardware. Nothing here talks to a real pedal.
export function simulatedPedal(catalog) {
  const types = [34, 13, 33, 3, 22, 19], order = [0, 1, 2, 3, 4, 5], enabled = [1, 1, 1, 1, 0, 1];
  const values = types.map(t => (catalog.effects[t]?.params || []).map(p => p.initial));
  const cabs=Array(6).fill(0);
  let revision = 1;
  return {
    async ask(q) {
      await new Promise(r => setTimeout(r, 15));
      const [, op, s, a, lo, hi] = q, ok = s < 6;
      if (op === 0x18) return Uint8Array.from([0x7e, 0x18, 1, ...types, ...order, ...enabled, revision & 255, (revision >> 8) & 255, 0, 0, 1, 0, 0]);   // no refusals in the demo
      if (op === 0x14) {
        const v = values[s] || [], out = [0x7e, 0x14, 1, s, types[s], enabled[s], order.indexOf(s), v.length];
        for (let i = 0; i < 8; i++) { const x = v[i] ?? 0; out.push(x & 255, (x >> 8) & 255); }
        return Uint8Array.from(out);
      }
      if(op===0x19 && ok && types[s]===3){const out=[0x7e,0x19,1,s,cabs[s],0,0,cabs[s]===0?1:0];const name=new TextEncoder().encode(cabs[s]?'EMPTY':'BUILT IN');for(let i=0;i<16;i++)out.push(name[i]||0);out.push(0);return Uint8Array.from(out);}
      if(op===0x19)return Uint8Array.from([0x7e,0x19,0]);
      if(op===0x1a){const accepted=ok && types[s]===3 && Number.isInteger(a) && a>=0 && a<=63;if(accepted)cabs[s]=a;return Uint8Array.from([0x7e,op,accepted?1:0]);}
      if (op === 0x15 && ok) { const p = catalog.effects[types[s]]?.params[a]; if (p) { let x = (lo | (hi << 8)) << 16 >> 16; values[s][a] = Math.max(p.min, Math.min(p.max, x)); } return Uint8Array.from([0x7e, 0x15, p ? 1 : 0]); }
      if (op === 0x16 && ok) { enabled[s] = a ? 1 : 0; return Uint8Array.from([0x7e, 0x16, 1]); }
      if (op === 0x1f && q.length === 8) { const o = [...q.slice(2)], good = [...o].sort().join() === '0,1,2,3,4,5'; if (good) { order.splice(0, 6, ...o); revision++; } return Uint8Array.from([0x7e, 0x1f, good ? 1 : 0]); }
      if (op === 0x20) return q.length === 2 ? Uint8Array.from([0x7e, 0x20, 1, 0, 0, 255, 0, 0]) : Uint8Array.from([0x7e, 0x20, 0]);   // the demo cannot save
      if (op === 0x17 && ok) { types[s] = a; values[s] = (catalog.effects[a]?.params || []).map(p => p.initial); enabled[s] = 0; revision++; return Uint8Array.from([0x7e, 0x17, 1]); }
      return null;
    },
  };
}
