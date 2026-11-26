import { crc32 } from './pedal.js?v=1.9.0';
// Port of tools/cab_ir.py: RIFF decode, mono average, Blackman-windowed sinc,
// 10% onset trim, 512 taps, end fade, response-peak calibration / L1 <= 7.999.
export function irResponseGain(h) {
  const re = new Float64Array(1024), im = new Float64Array(1024); re.set(h);
  for (let i=1,j=0;i<1024;i++) {
    let bit=512; while(j&bit){j^=bit;bit>>=1;} j^=bit;
    if(i<j){const t=re[i];re[i]=re[j];re[j]=t;}
  }
  for(let n=2;n<=1024;n*=2){
    const cr=Math.cos(2*Math.PI/n),ci=-Math.sin(2*Math.PI/n);
    for(let start=0;start<1024;start+=n){let wr=1,wi=0;
      for(let k=0;k<n/2;k++){
        const a=start+k,b=a+n/2,tr=wr*re[b]-wi*im[b],ti=wr*im[b]+wi*re[b],ar=re[a],ai=im[a];
        re[a]=ar+tr;im[a]=ai+ti;re[b]=ar-tr;im[b]=ai-ti;
        const next=wr*cr-wi*ci;wi=wr*ci+wi*cr;wr=next;
      }
    }
  }
  let peak=0,l1=0;for(let i=0;i<=512;i++)peak=Math.max(peak,Math.hypot(re[i],im[i]));
  for(const v of h)l1+=Math.abs(v);
  if(!(peak>0) || !Number.isFinite(peak))throw new Error('IR response is invalid');
  return Math.min(1/peak,7.999/l1);
}
/** Mono samples of a PCM/float WAV. maxSeconds/what bound and name it in errors (IR by default). */
export function readWav(bytes, maxSeconds = 10, what = 'IR') {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes), d = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const str = (o, n) => String.fromCharCode(...b.subarray(o, o + n));
  if (b.length < 12 || str(0, 4) !== 'RIFF' || str(8, 4) !== 'WAVE') throw new Error('Not a RIFF/WAVE file');
  let fmt, data;
  for (let p = 12; p + 8 <= b.length;) {
    const n = d.getUint32(p + 4, true), o = p + 8, id = str(p, 4);
    if (o + n > b.length) throw new Error('Truncated WAV chunk');
    if (id === 'fmt ') {
      if (n < 16) throw new Error('Short WAV format');
      let tag = d.getUint16(o, true);
      if (tag === 0xfffe) { if (n < 40) throw new Error('Short extensible WAV format'); tag = d.getUint16(o + 24, true); }
      fmt = { tag, channels: d.getUint16(o + 2, true), rate: d.getUint32(o + 4, true), align: d.getUint16(o + 12, true), bits: d.getUint16(o + 14, true) };
    } else if (id === 'data') data = [o, n];
    p = o + n + (n & 1);
  }
  if (!fmt || !data) throw new Error('WAV needs format and data');
  const { tag, channels, rate, align, bits } = fmt, width = bits / 8;
  if (!channels || channels > 256 || rate < 1000 || rate > 768000 || align !== channels * width || !((tag === 1 && [8,16,24,32].includes(bits)) || (tag === 3 && [32,64].includes(bits)))) throw new Error('Unsupported WAV encoding');
  const frames = data[1] / align;
  if (!Number.isInteger(frames) || frames < 1 || frames > rate * maxSeconds) throw new Error(`${what} must be 1 sample to ${maxSeconds} seconds`);
  const x = new Float64Array(frames);
  for (let f = 0; f < frames; f++) {
    let sum = 0;
    for (let c = 0; c < channels; c++) {
      const o = data[0] + f * align + c * width;
      let v;
      if (tag === 3) v = bits === 32 ? d.getFloat32(o, true) : d.getFloat64(o, true);
      else if (bits === 8) v = (b[o] - 128) / 128;
      else if (bits === 16) v = d.getInt16(o, true) / 32768;
      else if (bits === 24) v = ((b[o] | b[o+1]<<8 | b[o+2]<<16)<<8>>8) / 8388608;
      else v = d.getInt32(o, true) / 2147483648;
      if (!Number.isFinite(v)) throw new Error('Non-finite IR sample');
      sum += v;
    }
    x[f] = sum / channels;
  }
  return { rate, x };
}
export function resample(x, rate) {
  if (rate === 44100) return Float64Array.from(x);
  const ratio = 44100 / rate, cut = Math.min(1, ratio) * .95, half = 32 / cut, y = new Float64Array(Math.ceil(x.length * ratio));
  for (let i = 0; i < y.length; i++) {
    const t = i / ratio;
    for (let k = Math.max(0, Math.floor(t-half)); k <= Math.min(x.length-1, Math.ceil(t+half)); k++) {
      const d = t-k, u = d/half; if (Math.abs(u) >= 1) continue;
      const w = .42 + .5*Math.cos(Math.PI*u) + .08*Math.cos(2*Math.PI*u);
      y[i] += x[k]*cut*(d === 0 ? 1 : Math.sin(Math.PI*cut*d)/(Math.PI*cut*d))*w;
    }
  }
  return y;
}
export function convertIr(bytes, name = 'CAB') {
  const { rate, x } = readWav(bytes), y = resample(x, rate);
  let peak = 0; for (const v of y) peak = Math.max(peak, Math.abs(v));
  if (!(peak > 1e-6)) throw new Error('IR is silent');
  const start = Math.max(0, y.findIndex(v => Math.abs(v) >= .1*peak)-2), h = new Float32Array(512);
  const a = Array.from({length:512}, (_, i) => y[start+i] || 0);
  for (let i=0; i<24; i++) a[488+i] *= .5*(1+Math.cos(Math.PI*(i+1)/24));
  const l1 = a.reduce((s,v) => s+Math.abs(v), 0); if (!(l1 > 1e-9)) throw new Error('IR has no energy in kept window');
  const gain=irResponseGain(a);
  for (let i=0; i<512; i++) h[i] = a[i]*gain;
  let pk=0;for(const v of h)pk=Math.max(pk,Math.abs(v));
  let scale=Math.fround(pk/32767), q=Int16Array.from(h, v=>Math.max(-32768, Math.min(32767, Math.round(v/scale))));
  // Preserve the bounded coefficient gain after quantization.
  const sum=Array.from(q).reduce((s,v)=>s+Math.abs(v),0);
  scale=Math.min(scale, Math.fround(7.999/sum));
  const record=new Uint8Array(1048), d=new DataView(record.buffer);
  record.set(new TextEncoder().encode(name.replace(/[^A-Za-z0-9 ._-]/g,' ').toUpperCase()).subarray(0,15));
  d.setFloat32(16,scale,true);
  for(let i=0;i<512;i++){d.setInt16(20+2*i,q[i],true);h[i]=q[i]*scale;}
  d.setUint32(1044,crc32(record.subarray(0,1044)),true);
  return { record, h, info:{sourceRate:rate, sourceFrames:x.length, trimmed:start, taps:512, fade:24, l1:Array.from(h).reduce((s,v)=>s+Math.abs(v),0)} };
}
export function irWav(record) {
  const d=new DataView(record.buffer,record.byteOffset,record.byteLength), scale=d.getFloat32(16,true), b=new Uint8Array(44+512*4), w=new DataView(b.buffer);
  for(const [o,s] of [[0,'RIFF'],[8,'WAVE'],[12,'fmt '],[36,'data']])for(let i=0;i<s.length;i++)b[o+i]=s.charCodeAt(i);
  w.setUint32(4,b.length-8,true);w.setUint32(16,16,true);w.setUint16(20,3,true);w.setUint16(22,1,true);w.setUint32(24,44100,true);w.setUint32(28,176400,true);w.setUint16(32,4,true);w.setUint16(34,32,true);w.setUint32(40,2048,true);
  const h=Float32Array.from({length:512},(_,i)=>d.getInt16(20+2*i,true)*scale), gain=irResponseGain(h);
  for(let i=0;i<512;i++)w.setFloat32(44+4*i,h[i]*gain,true);
  return b;
}
