# Flanger

Status: **implemented experimental**, stable type ID **7**, mono 44.1 kHz. The target build remains synthetic-input and hard-muted.

| Parameter | Range | Default | Pedal detent |
| --- | --- | --- | --- |
| RATE | 0.05–5 Hz | 0.3 Hz | 0.05 Hz |
| DEPTH | 0–1 | 0.5 | 0.025 |
| FEEDBACK | −0.9–0.9 | 0.5 | 0.025 |

Read time is `3 + 2.5 * DEPTH * LFO` ms (0.5–5.5 ms at full depth). Linearly interpolated echo feeds `input + FEEDBACK * echo`, stored with a ±16 rail. Output is fixed at `0.5 * input + 0.5 * echo`. Negative feedback reverses repeat polarity; the allowed magnitude stays below unity. The ring rail bounds storage even with high upstream gain, but can add distortion. Physical gain/headroom and complete-chain levels remain unqualified.

The current three-parameter wire/editor contract exposes RATE/DEPTH/FEEDBACK; **mix is fixed 50%**, and manual base delay, stereo, through-zero, feedback freeze and resonance filtering are not implemented. This is an initial flanger, not a complete preset workflow or named-pedal emulation. Additional adjustable controls require a reviewed extension rather than silently changing existing wire frames.

The LFO/rate/slew, shared two-workspace admission, guarded fractional reads, constant-time history invalidation and KEEP/CUT bypass behavior follow the [chorus/shared kernel contract](../chorus/README.md). Each selected slot owns distinct phase, buffer and read distance; they do not share a live delay line. The helper lives under `effects/modulation/` and is not a third registered effect. There is no allocation, IO, blocking or bulk buffer clear in audio. Linear interpolation/modulation is not alias-free; physical CPU/latency/stack/audio quality remain unmeasured.

Offline example from Frost root:

```sh
python tools/chain_wav.py input.wav flange.wav --effects flanger delay ir --flanger 0.3 0.5 -0.5 --tail-seconds 3
```

Choose FLANGE through the generic pedal TYPE menu to edit all three exposed controls. The footswitch still toggles only the selected slot. Recursive tails require explicit silence; `--tail-seconds` defaults 0 and is not a decay-completion detector. See [modulation tests/resources](../../docs/Build%20Firmware.md).

See [project plan](../../docs/Build%20Firmware.md) and [custom-effect guidelines](../../docs/Build%20Firmware.md). This status file is not a runtime registry.
