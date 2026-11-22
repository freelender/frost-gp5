import assert from 'node:assert/strict';
import { encodeSetlist, decodeSetlist, emptySetlist, SetlistStore } from '../../web/setlist.js';
const data=emptySetlist();data.songs=[{id:4,name:'Song',parts:[{name:'Intro',preset:0},{name:'Lead',preset:59}]}];data.order=[4,4];
const encoded=encodeSetlist(data);assert.deepEqual(decodeSetlist(encoded.bytes),data);
assert.equal(encoded.used,148);
for(const mutate of [d=>d.songs.push(d.songs[0]),d=>d.order.push(5),d=>d.songs[0].parts[0].preset=60,d=>d.songs[0].parts[0].preset=-1,d=>d.name='x'.repeat(17),d=>d.name='é',d=>d.songs[0].parts=[]]){
  const d=structuredClone(data);mutate(d);assert.throws(()=>encodeSetlist(d));
}
const corrupt=encoded.bytes.slice();corrupt[110]^=1;assert.throws(()=>decodeSetlist(corrupt));
const huge=emptySetlist();for(let i=0;i<64;i++)huge.songs.push({id:i,name:'Song',parts:Array.from({length:16},()=>({name:'Part',preset:0}))});assert.throws(()=>encodeSetlist(huge),/storage is full/);
let seq=3,writes=0;
const store={scan:async()=>[],newest:()=>({seq}),readGroup:async()=>new Uint8Array(4032),writeGroup:async()=>{writes++}};
const s=new SetlistStore(store);await s.read();seq++;await assert.rejects(()=>s.write(data),/changed/);assert.equal(writes,0);
console.log('setlist format and conflict checks passed');
