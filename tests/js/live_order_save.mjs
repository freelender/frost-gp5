// 32p website: chain reorder (7E 1F) and save-to-preset (7E 20) against the demo
// simulator and a fake save sequence. Host evidence only.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { LiveChain, LiveView } from '../../web/live.js';
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
// beta.12: the chain reply carries the pedal's refusal count; a change shows the popup once
const reply=(count,why)=>Uint8Array.from([0x7e,0x18,1,0,1,2,3,4,5,0,1,2,3,4,5,1,1,1,1,1,1,9,0,0,0,1,count,why]);
let answer=reply(4,0);const counted=new LiveChain(async()=>answer);
const c0=await counted.chain();assert.equal(c0.refusals,4);assert.equal(c0.refused,0);
assert.equal((await live.chain()).refused,0);   // the demo has no refusals
const popups=[],view={refusalsSeen:null,ui:{limit:k=>popups.push(k)}},note=c=>LiveView.prototype.noteRefusals.call(view,c);
note(c0);assert.deepEqual(popups,[]);                            // first read: baseline only
answer=reply(5,2);note(await counted.chain());assert.deepEqual(popups,[2]);   // DSP LIMIT
note(await counted.chain());assert.deepEqual(popups,[2]);        // same count: no second popup
answer=reply(6,1);note(await counted.chain());assert.deepEqual(popups,[2,1]); // CHAIN TOO BIG
answer=reply(6,0).subarray(0,26);const older=await new LiveChain(async()=>answer).chain();
assert.equal(older.refusals,null);note(older);assert.deepEqual(popups,[2,1]);   // older firmware: no popup, no error
console.log('live order and save checks passed');
