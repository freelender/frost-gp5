# Overdrive

Status: **implemented experimental native-rate baseline**, type ID **4**. This is an original simple saturator, not an amp/NAM backend or an emulation of a named pedal. The new firmware remains hard-muted.

| Parameter | Range | Default | Pedal detent |
| --- | --- | --- | --- |
| DRIVE | 1–20 multiplier | 4 | 0.25 |
| TONE | 0–1 normalized | 0.5 | 0.025 |
| LEVEL | 0–2 multiplier | 0.5 | 0.025 |

The chain smooths all targets and handles bypass. Each instance owns three floats: previous input, DC-blocker state and tone state (12 bytes); no workspace, allocation, IO or blocking. This fits the existing state union, so per-slot/chain storage does not grow. Reset clears exactly three floats. The kernel requires finite input and valid smoothed parameters, supplied by the chain contract.

At 44.1 kHz, processing is:

1. DC blocker: `h[n] = x[n] - x[n-1] + 0.995 h[n-1]`.
2. Drive: `u = DRIVE * h[n]`.
3. Saturate: `s = u (1.5 - 0.5 u²)` for `|u| < 1`, otherwise the corresponding ±1 rail. The odd curve and its first derivative meet continuously at both rails.
4. Tone: `a = 0.03 + 0.47 TONE²`, `t[n] = t[n-1] + a (s - t[n-1])`.
5. Output: `LEVEL * t[n]`.

TONE is a normalized coefficient control, not a calibrated Hz value. From cleared state, the tone filter is a convex combination of bounded samples, so fully wet output is bounded by LEVEL (at most ±2). This does not guarantee safe physical pedal levels; downstream float processing and final PCM clipping remain separate. Filters have phase/tail behavior; there is no lookahead, explicit delay buffer or sample-rate conversion. The fixed-rate filter coefficients are not rescaled for other rates.

Quality policy: native-rate processing only. The smooth cubic avoids a discontinuous rail slope but is **not alias-free**. No oversampling, ADAA or anti-alias filter before decimation is implemented. A coherent high-frequency sine test deliberately detects the folded third harmonic. Post-saturation TONE attenuates frequencies but cannot remove all already-folded components. This baseline is suitable for offline evaluation; high-drive/high-frequency quality and any later oversampling option need separate listening, spectral and CPU evidence. No physical cycle/latency/audio-quality measurement was made.

KEEP_HISTORY runs the kernel while bypassed, preserving compatibility. Cold opt-in CUT_AND_SUSPEND fades to exact dry within 128 valid samples, clears all three histories and skips the kernel until enabled. Re-enable starts from cleared history. Route changes cut histories at silence. Infinite recursive filter tails have no automatic WAV extension; add silence to input when evaluating the decay. FIR-only finite tail extension in the WAV runner remains unchanged.

Offline example, from the Frost root:

```sh
python tools/chain_wav.py input.wav drive.wav --effects gate overdrive eq ir --overdrive 6 0.6 0.5 --ir private/cab.wav
```

Repeats own independent state and receive the same CLI controls. The default route is unchanged and does not enable overdrive automatically. On the generic pedal UI, choose DRIVE through TYPE, enable the selected slot and press PARA to edit its three parameters. No legacy diagnostic IDs were added or renumbered.

See [validation and limits](../../docs/Build%20Firmware.md). Tests cover independent double-precision reference agreement, odd symmetry/bounds, DC rejection, tone response, harmonic generation, native aliasing, state isolation, chunk/in-place behavior, fault/reset, cut/suspend, wire/editor integration and WAV saturation/validation.

See [the project plan](../../docs/Build%20Firmware.md) and [custom-effect guidelines](../../docs/Build%20Firmware.md). The JSON status file is not a runtime registry.
