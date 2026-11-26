import assert from 'node:assert/strict';
import fs from 'node:fs';
import {LiveChain,LiveView} from '../../web/live.js';
import {simulatedPedal} from '../../web/sim.js';
const catalog=JSON.parse(fs.readFileSync(new URL('../../web/effects.json',import.meta.url)));
const pedal=simulatedPedal(catalog), sent=[];
const live=new LiveChain(q=>{sent.push(q);return pedal.ask(q);});
assert.equal((await live.cab(3)).number,0);
assert.equal(await live.hasCabControl(),true);
assert.equal(await new LiveChain(async()=>null).hasCabControl(),false);
assert.equal(await live.setCab(3,63),true);
assert.equal((await live.cab(3)).number,63);
assert.equal(await live.setCab(0,1),false);
assert.equal(await live.setCab(3,64),false);
assert.equal(await live.setCab(3,1.5),false);
class Element {
  constructor(tag){this.tag=tag;this.children=[];this.dataset={};}
  append(...items){this.children.push(...items);}
  add(o){this.children.push(o);}
  setAttribute(){}
}
globalThis.document={createElement:tag=>new Element(tag)};
globalThis.Option=class {constructor(text,value){this.text=text;this.value=value;}};
const view=Object.create(LiveView.prototype);view.ui={cabs:()=>({0:{label:'V30'},62:{label:'OPEN BACK'}})};view.live=live;
view.act=async fn=>fn();
const row=view.cabField(3,{number:1,busy:false,error:false,exists:true,override:false,name:'V30'});
const [label,select,status]=row.children;
assert.equal(label.textContent,'CAB');assert.equal(select.children.length,64);
assert.match(select.children[1].text,/V30/);assert.match(select.children[63].text,/OPEN BACK/);
select.value=63;await select.onchange();assert.deepEqual(sent.at(-1),[0x7e,0x1a,3,63]);
view.cabSync({number:63,busy:true,exists:true});assert.match(status.textContent,/Loading/);
view.cabSync({number:63,busy:false,exists:false});assert.match(status.textContent,/built in/);
view.cabSync({number:63,exists:true,override:true});assert.match(status.textContent,/CAB OVERRIDE/);
view.ui.cabs=()=>({0:{label:'REPLACED'}});view.cabSync({number:1,exists:true,name:'REPLACED'});
assert.match(select.children[1].text,/REPLACED/);
const imports=[];view.ui.addFile=(...args)=>imports.push(args);
const importRow=view.cabField(3,{number:63,exists:true,name:'OPEN BACK'});
const importSelect=importRow.children[1];assert.equal(importSelect.children.length,65);
const beforeImport=sent.length;importSelect.value='add';await importSelect.onchange();
assert.deepEqual(imports,[['cabs',3]]);assert.equal(importSelect.value,63);assert.equal(sent.length,beforeImport);
console.log('Live CAB protocol, dropdown, labels, edits and status pass.');
