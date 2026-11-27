// Read-only identity check. Never prompts for permission or starts an editor session.
import { buildSysex, parseSysex } from './pedal.js?v=1.9.1';

export async function detectFrost(nav = navigator, idle = () => true, timeout = 1500) {
  if (!nav.requestMIDIAccess || !nav.permissions?.query || !idle()) return false;
  try {
    const permission = await nav.permissions.query({ name: 'midi', sysex: true });
    if (permission.state !== 'granted' || !idle()) return false;
    const access = await nav.requestMIDIAccess({ sysex: true });
    if (!idle()) return false;
    const ports = map => [...map.values()].filter(p => p.state === 'connected' && /GP-5/.test(p.name));
    const outs = ports(access.outputs), ins = ports(access.inputs);
    const out = outs.find(p => /Open FW/.test(p.name)) || outs[0];
    const inp = ins.find(p => p.name === out?.name) || ins.find(p => /Open FW/.test(p.name)) || ins[0];
    if (!out || !inp) return false;
    return await new Promise(resolve => {
      let timer;
      const finish = found => {
        clearTimeout(timer); inp.removeEventListener('midimessage', received); resolve(found);
      };
      const received = event => {
        const reply = parseSysex(event.data);
        if (!reply || reply.length < 7 || reply[0] !== 0x7e || reply[1] !== 0x0d || reply[2] !== 1) return;
        let end = 6; while (end < reply.length && reply[end]) end++;
        finish(new TextDecoder().decode(reply.subarray(6, end)) === 'frost');
      };
      inp.addEventListener('midimessage', received);
      timer = setTimeout(() => finish(false), timeout);
      try { out.send(buildSysex([0x7e, 0x0d])); } catch { finish(false); }
    });
  } catch { return false; } // Denied, unsupported or unplugged: keep the first-install path.
}
