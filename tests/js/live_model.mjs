// Host UI/protocol regression: named capture selection and pedal-side changes.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {LiveChain,LiveView,loadCatalog} from '../../web/live.js';
import {simulatedPedal} from '../../web/sim.js';
const catalog=JSON.parse(readFileSync(new URL('../../web/effects.json',import.meta.url)));
globalThis.fetch=async()=>({json:async()=>catalog});
await loadCatalog();
class Element {
  constructor(tag){this.tag=tag;this.children=[];this.dataset={};this.listeners={};this.style={setProperty(){}};this.classList={toggle(){}};}
  set textContent(v){this.text=v;this.children=[];}
  get textContent(){return this.text;}
  append(...items){this.children.push(...items);}
  appendChild(item){this.append(item);return item;}
  add(option){this.append(option);}
  setAttribute(){}
  addEventListener(type,fn){this.listeners[type]=fn;}
  dispatchEvent(event){return this.listeners[event.type]?.(event);}
  querySelector(selector){
    const match=selector.match(/^\[data-p="(\d+)"\]$/);
    for(const child of this.children){
      if(match && String(child.dataset?.p)===match[1])return child;
      const found=child.querySelector?.(selector);if(found)return found;
    }
    return null;
  }
}
globalThis.document={createElement:tag=>new Element(tag),activeElement:null};
globalThis.Option=class {constructor(text,value){this.text=text;this.value=value;}};
const sim=simulatedPedal(catalog),sent=[];
const live=new LiveChain(q=>{sent.push(q);return sim.ask(q);});
assert.equal(await live.setType(0,33),true);
assert.equal(await live.setParam(0,0,1),true);
let models={1:{label:'CLEAN CAPTURE'},63:{label:'HIGH GAIN'}};
const view=Object.create(LiveView.prototype),knobs=[];
const imports=[];
Object.assign(view,{ui:{models:()=>models,addFile:(...args)=>imports.push(args)},live,selected:0,inspector:new Element('div'),pending:new Map(),busy:false,chainState:await live.chain()});
// Other controls are independent of the model dropdown.
view.field=(s,i)=>{knobs.push(i);return new Element('input');};
await view.renderInspector();
assert.equal(view.inspector.children[1].children[0].children[0].textContent,'Replace effect');
assert.deepEqual(knobs,[1,2,3,4,5]); // MODEL is no longer a knob
const select=view.inspector.querySelector('[data-p="0"]');
assert.equal(select.tag,'select');assert.equal(select.children.length,65);
assert.equal(select.value,1);
assert.match(select.children[0].text,/00 · Built-in default/);
assert.match(select.children[1].text,/01 · CLEAN CAPTURE/);
assert.match(select.children[2].text,/Empty — default fallback/);
assert.match(select.children[63].text,/63 · HIGH GAIN/);
// Use the real queued write and LiveChain protocol, without a timer delay.
const nativeTimeout=globalThis.setTimeout;let send;
globalThis.setTimeout=fn=>{send=fn;};
let edits=0;view.ui.edited=()=>edits++;view.ui.refused=()=>assert.fail('edit refused');
select.value='63';select.dispatchEvent(new Event('change'));
globalThis.setTimeout=nativeTimeout;
await send();
assert.deepEqual(sent.at(-1),[0x7e,0x15,0,0,63,0]);
assert.equal((await live.slot(0)).values[0],63);assert.equal(edits,1);
// A change on the pedal updates the selected name.
await live.setParam(0,0,1);await view.refresh(await live.chain());
assert.equal(select.value,1);
// Upload/replacement/removal refreshes names even when MODEL stays the same.
models={1:{label:'REPLACED CAPTURE'}};await view.refresh(await live.chain());
assert.match(select.children[1].text,/REPLACED CAPTURE/);
assert.match(select.children[63].text,/Empty — default fallback/);
models={};await view.refresh(await live.chain());
assert.match(select.children[1].text,/Empty — default fallback/);
// Polling leaves an actively used dropdown alone.
document.activeElement=select;await live.setParam(0,0,0);await view.refresh(await live.chain());
assert.equal(select.value,1);
document.activeElement=null;await view.refresh(await live.chain());assert.equal(select.value,0);
view.queue=()=>assert.fail('Add model must not write a parameter');
select.value='add';select.dispatchEvent(new Event('change'));
assert.deepEqual(imports,[['amps',0]]);assert.equal(select.value,0);
assert.equal(await live.setType(0,255),true);
view.chainState=await live.chain();await view.renderInspector();
const add=view.inspector.children[1].children[0].children[0];
assert.equal(add.textContent,'Add Effect');
let focused=false;view.fxSearch={scrollIntoView(){},focus(){focused=true;}};
add.onclick();assert.equal(focused,true);
console.log('Live MODEL names, dropdown, queued edits and polling pass (host only).');
