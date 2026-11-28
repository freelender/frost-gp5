// AudioWorklet of the website's looper: runs LoopEngine and the footswitch map on the audio clock, so a press acts on the
// exact block it arrives in. The page sends commands and presses; this posts the state about 20 times a second, and
// answers export, save and restore with the data.
import { LoopEngine, LoopController } from './looper_engine.js?v=1.9.1';

class LooperProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.engine = new LoopEngine(sampleRate, 2);
    this.ctl = new LoopController(this.engine, event => this.port.postMessage({ event }));
    this.next = 0;
    this.port.onmessage = ({ data: m }) => {
      const e = this.engine, i = m.track ?? this.ctl.sel;
      switch (m.cmd) {
        case 'latency': e.setLatency(m.in, m.out); break;
        case 'tempo': e.setTempo(m); break;
        case 'tap': e.tap(); break;
        case 'press': this.ctl.press(m.sw); break;
        case 'release': this.ctl.release(m.sw); break;
        case 'select': this.ctl.sel = Math.max(0, Math.min(e.tracks.length - 1, m.track)); break;
        case 'record': e.record(i); break;
        case 'stop': e.stop(i); break;
        case 'play': e.play(i); break;
        case 'undo': e.undo(i); break;
        case 'redo': e.redo(i); break;
        case 'clear': e.clear(i); break;
        case 'reverse': e.reverse(i); break;
        case 'half': e.half(i); break;
        case 'mute': e.mute(i); break;
        case 'cut': e.setCut(i, m.v); break;
        case 'stopAll': e.stopAll(); break;
        case 'playAll': e.playAll(); break;
        case 'clearAll': e.clearAll(); break;
        case 'section': e.section(m.i); break;
        case 'drumStart': e.drumStart(); break;
        case 'drumStop': e.drumStop(); break;
        case 'drumToggle': e.drumToggle(m.p, m.s); break;
        case 'drumClear': e.drumClear(); break;
        case 'drumPreset': e.drumPreset(m.rows, m.swing); break;
        case 'drumSwing': e.setSwing(m.v); break;
        case 'drumVol': e.drum.vol = Math.max(0, Math.min(1, Number(m.v))); break;
        case 'drumBars': if (!e.len) e.drum.bars = [1, 2, 4].includes(m.v) ? m.v : 1; break;
        case 'drumMode': this.ctl.drum = !this.ctl.drum; if (this.ctl.drum) e.drumStart(); this.port.postMessage({ event: 'drum' }); break;
        case 'export': this.port.postMessage({ exported: e.exportTracks() }); break;
        case 'save': this.port.postMessage({ session: e.serialize() }); break;
        case 'restore': this.port.postMessage({ restored: e.restore(m.session) }); break;
      }
      this.post(true);
    };
  }
  post(now) {
    if (!now && currentFrame < this.next) return;
    this.next = currentFrame + sampleRate / 20;
    this.port.postMessage({ state: { ...this.engine.snapshot(), sel: this.ctl.sel, drumMode: this.ctl.drum, drumPage: this.ctl.page } });
  }
  process(inputs, outputs) {
    const out = outputs[0];
    this.ctl.tick();
    this.engine.process(inputs[0] || [], out, out[0].length);
    this.post(false);
    return true;
  }
}
registerProcessor('frost-looper', LooperProcessor);
