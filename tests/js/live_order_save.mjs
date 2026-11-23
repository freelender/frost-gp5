// 32p website: chain reorder (7E 1F) and save-to-preset (7E 20) against the demo
// simulator and a fake save sequence. Host evidence only.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { LiveChain } from '../../web/live.js';
import { simulatedPedal } from '../../web/sim.js';
const catalog=JSON.parse(readFileSync(new URL('../../web/effects.json',import.meta.url)));
const sim=simulatedPedal(catalog),live=new LiveChain(q=>sim.ask(q));
const before=await live.chain();
const order=[before.order[3],...before.order.filter(s=>s!==before.order[3])];
assert.equal(await live.setOrder(order),true);
const after=await live.chain();assert.deepEqual(after.order,order);assert.deepEqual(after.types,before.types);
assert.equal(await live.setOrder([0,0,1,2,3,4]),false);assert.deepEqual((await live.chain()).order,order);
// save: busy twice, then done; the request carries index and name
let sent=null,polls=0;
const fake=new LiveChain(async q=>{
  if(q[1]!==0x20)return null;
  if(q.length>2){sent=q;return Uint8Array.from([0x7e,0x20,1]);}
  polls++;return Uint8Array.from([0x7e,0x20,1,polls<3?1:0,0,q.length?7:0]);
});
assert.equal(await fake.savePreset(7,'MY SOUND',{delay:async()=>{}}),true);
assert.deepEqual([...sent],[0x7e,0x20,7,...new TextEncoder().encode('MY SOUND')]);assert.equal(polls,3);
const failing=new LiveChain(async q=>q.length>2?Uint8Array.from([0x7e,0x20,1]):Uint8Array.from([0x7e,0x20,1,0,1,7]));
await assert.rejects(()=>failing.savePreset(7,'X',{delay:async()=>{}}),/could not read the current sound/);
const old=new LiveChain(async()=>null);
await assert.rejects(()=>old.savePreset(7,'X',{delay:async()=>{}}),/newer Frost/);
await assert.rejects(()=>live.savePreset(60,'X'),/Preset must be/);
console.log('live order and save checks passed');
