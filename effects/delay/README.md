# Delay

Status: **implemented experimental**, stable type ID **5**. Fixed 44.1 kHz mono, one statically admitted instance per chain. The hardware build stays synthetic-input and hard-muted.

| Parameter | Range | Default | Pedal detent |
| --- | --- | --- | --- |
| TIME | 1–500 ms | 250 ms | 5 ms |
| FEEDBACK | 0–0.95 | 0.35 | 0.025 |
| MIX | 0–1 | 0.3 | 0.025 |

TIME's normalized read distance is `44.1 * time_ms` samples. Two neighboring taps are linearly interpolated before writing `input + feedback * echo` to the ring. Stored feedback is limited to ±16. Output is `(1-mix) * input + mix * echo`; MIX is a linear amplitude crossfade, not equal power. Loop clipping is part of the DSP contract and can distort high input/gain configurations. It does not qualify physical output levels. Parameter targets use the shared chain smoother; fractional values are retained on the wire even when TIME displays rounded integer milliseconds.

The read head moves by at most 0.25 sample per processed sample after its first initialized distance. Time changes produce pitch sweeps and can take about two seconds across the full range. This is not a dual-tap transition or a promise of inaudible time changes. Linear interpolation changes high-frequency response; no oversampling, analog model, ping-pong, stereo, tempo tap or freeze mode is implemented.

`FrostDelay` owns 22,052 floats, two unsigned counters and one float distance: **88,220 bytes** on the tested host/target. The small instance stores only a pointer. `frost_chain_init` detaches delay storage; call cold-only `frost_chain_bind_delay(&chain, &workspace)` before configuring delay types. The workspace must remain at its final address, belong exclusively to that chain and outlive it. Detaching/rebinding an active delay or a started chain rejects. Missing memory or a second delay rejects the whole cold/live route without mutation. Other type/default/legacy IDs stay unchanged.

The hardware adapter reserves one static workspace in `GP5_LARGE`; ordinary chain VARS grows by just a pointer. Hosts can use ordinary caller-owned static storage. No heap or blocking operations. Reset sets only position, valid count and distance; it does not clear the ring. Each tap returns zero until its distance is covered by newly written samples, so stale/uninitialized contents cannot return after reset, wrap or time changes. Per-sample work is bounded and reset is constant time, even during route/fault/bypass processing. Startup platform zeroing of the entire large section is separate from effect reset and remains physically unqualified.

KEEP_HISTORY continues feeding and circulating the delay while bypass fades dry. Tails are hidden, not spilled into dry output; re-enabling may reveal the retained tail. CUT_AND_SUSPEND fades to exact dry within 128 valid samples, invalidates history and skips the kernel while parameter smoothing continues. Re-enable starts empty. Route changes and fault resets invalidate the history, with no spillover across routing changes.

Feedback is restricted below one. Linear interpolation is a convex combination of bounded stored values, and the feedback rail caps ring contents. This bounds the stored signal even at high gain; it does not imply all time edits sound good or that a complete chain meets deadlines. No physical cycle, latency, stack or audio-quality measurement has been made. The one-delay admission is the current resource bound, not proof of the full NAM-plus-any-five target.

Offline example from Frost root:

```sh
python tools/chain_wav.py input.wav delay.wav --effects overdrive delay eq --delay 300 0.5 0.3 --tail-seconds 5
```

`--tail-seconds` appends explicit silence for recursive decay (0–30 seconds, default 0). FIR finite tails are still added separately. No automatic infinite-tail convergence is claimed: high feedback and long delay may remain audible after the chosen tail. Pad the source for longer evaluations. TIME may slew from its default during the input; begin with silence for fixed-setting impulse measurements. Default gain/gate/EQ/IR routing is unchanged. The generic UI offers DELAY in TYPE and edits all three parameters; duplicate delay selection rejects before sending a route, with DSP validation as well.

See [delay validation/resources](../../docs/Build%20Firmware.md), [project plan](../../docs/Build%20Firmware.md) and [custom-effect guidelines](../../docs/Build%20Firmware.md). The JSON status file is documentation, not a runtime registry.
