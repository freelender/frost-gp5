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
// 32s: the pedal rewriting glb-0 for settings (seq only) must not block a save
{let gseq=3,group=new Uint8Array(4032);group.set([70,71,1,0,1,2,3],0);let written=null,expected=null;
 const st={scan:async()=>[],newest:()=>({seq:gseq}),readGroup:async()=>group,writeGroup:async(k,g,d,say,e)=>{written=d;expected=e}};
 const x=new SetlistStore(st);await x.read();
 gseq=9;group=Uint8Array.from(group);group[4]=77;                       // the pedal saved settings: new seq, new settings byte
 await x.write(data);assert.equal(expected,9);assert.equal(written[4],77);assert.deepEqual(decodeSetlist(written.subarray(64)),data);
 group=written;gseq=10;await x.write(data);                              // a second save without re-reading also works
 const other=structuredClone(data);other.name='Other';group=Uint8Array.from(group);group.set(encodeSetlist(other).bytes,64);gseq=11;
 await assert.rejects(()=>x.write(data),/changed elsewhere/);}        // someone else saved a different setlist
const s=new SetlistStore(store);await assert.rejects(()=>s.write(data),/Read the setlist/);assert.equal(writes,0);
// 32p: removing the setlist zeroes only the setlist part; the 64 settings bytes stay.
{const settings=new Uint8Array(4032);settings.set([70,71,1,0,9,9],0);settings.set(encodeSetlist(data).bytes,64);let written=null;
 const st={scan:async()=>[],newest:()=>({seq:7}),readGroup:async()=>settings,writeGroup:async(k,g,d)=>{written=d}};
 const x=new SetlistStore(st);await x.read();await x.clear();
 assert.deepEqual([...written.subarray(0,6)],[70,71,1,0,9,9]);assert.ok(written.subarray(64).every(b=>b===0));
 assert.deepEqual(decodeSetlist(written.subarray(64)),emptySetlist());}
console.log('setlist format and conflict checks passed');
