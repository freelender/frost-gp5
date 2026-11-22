# Reverb

Status: **implemented experimental**, stable type ID **8**, mono 44.1 kHz. An original fixed small network, not an emulation of a particular room or commercial pedal. The playable ADC profile is documented in the firmware build guide.

| Parameter | Range | Default | Pedal detent |
| --- | --- | --- | --- |
| DECAY | 0–0.95 normalized feedback | 0.75 | 0.025 |
| DAMP | 0–1 normalized damping | 0.5 | 0.025 |
| MIX | 0–1 linear dry/wet | 0.25 | 0.025 |

Four parallel feedback combs of 1,009/1,153/1,289/1,423 samples feed two serial all-pass diffusers of 149/211 samples. Per comb, read `d`, update low-pass state `h += (1 - 0.95 * DAMP) * (d - h)`, low-shelf the feedback below 300 Hz, and store `0.25 * wet_send + DECAY * shelved_h`. The four reads are averaged. Each allpass reads `d`, outputs `o = d - 0.5 * input` and stores `input + 0.5 * o`. All stored samples and final wet output are limited to ±16. Output is `(1-MIX) * dry + MIX * wet`.

DECAY is not a calibrated RT60 in seconds; DAMP is not a calibrated cutoff in Hz. Higher DAMP attenuates high frequencies inside comb feedback, while the independent feedback bass shelf shortens low-frequency decay. A two-pole 180 Hz high-pass filters excitation before the combs; the dry path is unaffected. DECAY 0 disables comb feedback, but the all-pass coefficient 0.5 remains, so a short diffuser tail still exists. Fixed feed-in/averaging gains are part of the algorithm; there is no automatic level compensation as decay changes. Rails protect storage/wet output but can add distortion under high input or feedback. Dry input remains separate; physical levels/headroom remain unqualified.

The first fully wet impulse arrival is at 1,009 samples (about 22.88 ms at the fixed rate), an algorithmic property rather than a measured pedal latency. This is a fixed mono topology without adjustable pre-delay, room size/type, stereo, modulation or freeze. The existing three-parameter protocol/editor exposes its controls without changing wire frames. No acoustic realism or production audio-quality claim is made.

One exclusive caller-owned `FrostReverb` is admitted per chain: 5,234 floats, six position/valid counter pairs and four damping states plus the shared model state = **21,232 bytes** on the tested host/target. Cold-bind using `frost_chain_bind_reverb` before configuring reverb. Chain init detaches it. Null/unbound memory or a second reverb rejects the complete cold/live route atomically. Storage must outlive the chain and remain at its final address; never share it between chains or copy initialized state. Hardware reserves it in GP5_LARGE alongside delay/modulation; hosts supply static storage. The instance stores just a pointer.

Reset clears sixteen scalar fields, not 5,234 samples. Each line returns zero until newly written history covers its full length, so old/uninitialized contents cannot return after reset or wrap. There is no allocation, blocking, IO, coefficient design or transcendental function in processing; all loops have fixed four/six-line bounds. Startup platform zeroing is separate and physically unqualified.

KEEP_HISTORY continues the network behind a dry bypass, hiding rather than spilling the tail; re-enable can reveal retained history. Cold CUT_AND_SUSPEND fades to exact dry within 128 valid samples, invalidates the network and skips kernels until enabled; re-enable starts empty. Route/fault resets invalidate tails. There is no cross-preset or routing spillover.

```sh
python tools/chain_wav.py input.wav reverb.wav --effects chorus delay reverb ir --reverb 0.85 0.6 0.25 --tail-seconds 5
```

Explicit tail silence is 0–30 seconds, default 0; no automatic infinite-tail completion is claimed. On the generic pedal menu choose REVERB through TYPE and edit DECAY/DAMP/MIX via PARA. Default routing, legacy diagnostic IDs, selected-only footswitch behavior, Frost logo and blue theme remain unchanged. The compatibility diagnostic profile's inactive REV row is not mapped to this new effect; generic TYPE selection is the actual editable path.

See [validation/resource limits](../../docs/Build%20Firmware.md) for analytic/reference, damping/decay, independent-state, resource, bypass/route/fault, WAV and real wire/editor checks. Physical CPU, deadline, stack, latency and listening qualification remain open. Full NAM plus any five effects is unproven.

See [project plan](../../docs/Build%20Firmware.md) and [custom-effect guidelines](../../docs/Build%20Firmware.md). This JSON status file is documentation, not a runtime loader.
