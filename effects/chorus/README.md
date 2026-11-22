# Chorus

Status: **implemented experimental**, stable type ID **6**, mono 44.1 kHz. This is an original simple modulated delay, not a named-pedal emulation. The target build stays synthetic-input and hard-muted.

| Parameter | Range | Default | Pedal detent |
| --- | --- | --- | --- |
| RATE | 0.05–5 Hz | 0.8 Hz | 0.05 Hz |
| DEPTH | 0–1 | 0.5 | 0.025 |
| MIX | 0–1 | 0.3 | 0.025 |

Read time is `15 + 10 * DEPTH * LFO` ms (5–25 ms at full depth), with linear fractional interpolation. The periodic LFO is a cubic easing of a triangle, not a mathematical sine; it is C1 and bounded by ±1, with no per-sample transcendental calls. Rate/depth/mix use shared parameter smoothing. Read-distance movement is additionally capped at 0.5 sample per processed sample after initialization, retaining continuity during rapid edits. Parameter edits can produce pitch sweeps. The dry/wet mix is a linear amplitude crossfade, not equal power. No chorus feedback, stereo/multiple voices or rate freeze is implemented.

Chorus and flanger share a caller-owned pool of two distinct workspaces, 4,624 bytes each: 1,152 floats and position/valid/phase/distance scalars. Two choruses, two flangers or one of each can coexist alongside the separate 500 ms delay. `frost_chain_init` detaches storage. Cold-bind an exclusive array of `FROST_MOD_WORKSPACES` objects using `frost_chain_bind_modulation` before configuring types. Null/unbound storage or a third combined modulation instance rejects the entire route. Never share/copy/move initialized chain/workspace storage. Hardware reserves the pool in GP5_LARGE; hosts supply static storage.

Reset invalidates position/valid/phase/distance in constant time, leaving sample memory untouched. Tap guards prevent old/uninitialized data returning after reset, time edits or wrap. Stored input is limited to ±16; output still includes the dry input branch. This is a feedback/storage safety boundary, not physical gain qualification.

KEEP_HISTORY continues phase/history while bypass fades dry. CUT_AND_SUSPEND fades to exact dry within 128 valid samples, invalidates history and freezes work/phase until enabled; re-enable starts empty at phase 0. Route/fault resets invalidate history and reset phase with no spillover. Native-rate linear interpolation/modulation can color or alias high-frequency input; no alias-free, oversampled or physical audio-quality claim is made.

Offline example from Frost root:

```sh
python tools/chain_wav.py input.wav chorus.wav --effects overdrive chorus delay ir --chorus 0.8 0.5 0.3 --tail-seconds 3
```

On the generic UI choose CHORUS in TYPE, enable that selected slot and edit RATE/DEPTH/MIX with PARA. Default route and existing wire IDs remain unchanged. See [modulation verification/resources](../../docs/Build%20Firmware.md) for reference, comb/sideband, duplicate/route/bypass/fault, WAV and pedal tests.

See [project plan](../../docs/Build%20Firmware.md) and [custom-effect guidelines](../../docs/Build%20Firmware.md). This status file is not a runtime registry.
