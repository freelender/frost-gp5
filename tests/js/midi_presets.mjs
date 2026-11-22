import assert from 'node:assert/strict';
import { LiveChain } from '../../web/live.js';
let loads=0,polls=0;
const live=new LiveChain(async q=>{
  if(q[1]===0x1d){loads++;assert.equal(q[2],5);return [0x7e,0x1d,1];}
  polls++;return [0x7e,0x1c,1,polls>2?5:1,polls===2?5:255,polls===2?1:0,0,0,127,1];
});
assert.equal((await live.loadPreset(5,{delay:async()=>{}})).current,5);
assert.equal(loads,1);
await assert.rejects(()=>live.loadPreset(60),/1–60/);
await assert.rejects(()=>new LiveChain(async()=>null).loadPreset(5),/beta.2/);
loads=0;
await assert.rejects(()=>new LiveChain(async q=>{
  if(q[1]===0x1d){loads++;return [0x7e,0x1d,1];}
  return [0x7e,0x1c,1,1,255,0,loads?1:0,0,127,1];
}).loadPreset(5),/failed/);
assert.equal(loads,1);
console.log('Website preset recall: completion, old firmware, range and error checks passed');
