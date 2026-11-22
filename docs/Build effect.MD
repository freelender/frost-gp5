# Build an effect for Frost

This tutorial adds a simple **TRIM** effect on a development branch. One LEVEL
control reduces the signal. It covers the C function, registry, pedal menu,
website catalog, tests and firmware build.

Frost effects are compiled into firmware. USB does not load executable C
plugins. Read [Build Firmware](Build%20Firmware.md) first to set up the
compilers and an external `FROST_WORK` directory.

## 1. Understand the effect interface

- Processing is mono, 44.1 kHz; the hardware adapter uses 64-sample blocks.
- A basic effect provides reset and per-sample callbacks. Frost's engine
  handles chain order, continuous-parameter smoothing and bypass transitions.
- Audio uses floats. Normalized unity is about -1 to +1, but earlier effects
  can produce larger finite signals. `FROST_SIGNAL_LIMIT` is currently 16;
  this internal guard is not an instruction to clip every effect at unity.
- Each slot owns its state. Do not share mutable histories or LFOs globally.
- Add to the existing registry; do not replace the firmware's `fw_process`
  callback to introduce one effect.

Use a Git branch. The example is not part of the released catalog unless you
perform the source changes below and build a new image.

## 2. Write the DSP function

Create `effects/trim/` with these two files.

### effects/trim/trim.h

```c
#ifndef FROST_TRIM_H
#define FROST_TRIM_H

/* level: 0..100; 100 leaves the level unchanged. */
float frost_trim_sample(float input, float level);

#endif
```

### effects/trim/trim.c

```c
#include "trim.h"
#include "../../engine/hot_code.h"

FROST_HOT float frost_trim_sample(float input, float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 100.0f) level = 100.0f;
    return input * (level * 0.01f);
}
```

LEVEL 100 passes the input, 50 halves its amplitude, and 0 silences it.
There is no history, additional latency, tail or allocation. The engine
already validates controls; the local clamp also makes finite out-of-range
standalone calls predictable. Non-finite audio remains subject to the engine's
fault guards rather than being silently hidden by the kernel.

## 3. Test the function first

Save this as `$FROST_WORK/trim_test.c`, outside the source tree:

```c
#include <assert.h>
#include <math.h>
#include "effects/trim/trim.h"

int main(void)
{
    assert(frost_trim_sample(0.0f, 100.0f) == 0.0f);
    assert(frost_trim_sample(0.75f, 0.0f) == 0.0f);
    assert(fabsf(frost_trim_sample(0.75f, 100.0f) - 0.75f) < 1e-6f);
    assert(fabsf(frost_trim_sample(-0.8f, 50.0f) + 0.4f) < 1e-6f);
    assert(frost_trim_sample(0.5f, -10.0f) == 0.0f);
    assert(frost_trim_sample(0.5f, 120.0f) == 0.5f);
    return 0;
}
```

Compile and run from the repository root on Linux/WSL:

```sh
cc -std=c99 -Wall -Wextra -Werror -I. \
  "$FROST_WORK/trim_test.c" effects/trim/trim.c \
  -lm -o "$FROST_WORK/trim_test"
"$FROST_WORK/trim_test"
```

Exit code 0 means these checks passed. For a contribution, add a meaningful
automated regression fixture under `tests/`; keep one-off executables and
recordings outside the repository.

## 4. Append a stable effect ID

In `engine/metadata.h`, append `FROST_TRIM` to the enum immediately before
`FROST_EFFECTS`, after the existing last effect. The end becomes:

```c
FROST_DELUXE, FROST_TWIN, FROST_AC30, FROST_JC120,
FROST_JCM800, FROST_RECTIFIER, FROST_5150,
FROST_TRIM,
FROST_EFFECTS,
FROST_EMPTY = 255, FROST_PARAMETERS = 8
```

At this revision TRIM receives ID 52 and the effect count becomes 53. If more
effects have since been appended, use the next ID. Never renumber old effects,
reuse an ID or change `FROST_EMPTY`: presets and wire messages store these IDs.

TRIM needs no exclusive workspace. The default branch of
`frost_type_workspace()` already returns `FROST_WORKSPACE_NONE`.

## 5. Define the parameter and category

Add this definition in `engine/library.h`:

```c
#define FROST_LIB_TRIM(X) FROST_KNOB(X, "LEVEL", 100)
```

`FROST_KNOB` defines 0–100, step 1, default 100 and scale 1. Appended effects
receive the human control value divided by its scale. Here the kernel receives
**50.0**, not 0.5, when the knob reads 50; the function converts percent to a
multiplier. Do not copy the original GAIN block's special UI mapping.

Append TRIM to the end of `FROST_LIBRARY_MODELS(M)`. Add a continuation
backslash to the former final row, so the last two rows are:

```c
    M(FROST_5150,"5150","EVH 5150 HEAD",FROST_LIB_5150,FROST_CATEGORY_AMP) \
    M(FROST_TRIM,"TRIM","LEVEL TRIM",FROST_LIB_TRIM,FROST_CATEGORY_UTILITY)
```

This shared table supplies parameter metadata, controller controls and
category lookup. Its order must match the appended IDs. Use short uppercase
labels; the current pedal font does not draw lowercase. There are at most
eight parameters per effect.

## 6. Register the DSP callbacks

Add the header to `engine/effect.c` alongside its other effect headers:

```c
#include "../effects/trim/trim.h"
```

After the existing wrappers, before the parameter arrays/registry, add:

```c
void frost_trim_state_reset(FrostEffectState *state)
{
    (void)state;
}

FROST_HOT float frost_trim_state_sample(FrostEffectState *state,
                                      float input, const float *parameters)
{
    (void)state;
    return frost_trim_sample(input, parameters[0]);
}
```

The library macro expansion creates `frost_params_FROST_TRIM` automatically.
Append this entry at the end of the `frost_effects` array, adding a comma to
the former final effect entry first:

```c
    {"TRIM", FROST_LIB_COUNT(FROST_LIB_TRIM), frost_params_FROST_TRIM,
     0u, 0u, frost_trim_state_reset, frost_trim_state_sample}
```

The zeros are the state-byte and external-workspace-byte counts. A stateless
effect does not need a dummy member in `FrostEffectState`. The generic
processing path handles this example; no custom block fast path is required.

## 7. Include it in both build paths

1. Add `effects/trim/trim.c` to the `frost_dsp` source list in `CMakeLists.txt`.
2. Append this include to `engine/bundle_library.c`:

   ```c
   #include "../effects/trim/trim.c"
   ```

CMake compiles separate modules; the firmware and host fixtures use the
bundle. Do not include the C file twice, or add it to both bundle files.
This small effect can use the ordinary code path. A heavy effect may need
overlay/linker changes; do not copy RAM-placement attributes or change
boot-sensitive function order without checking the target layout.

## 8. Add the pedal picker entry

In `engine/control_catalog.h`, insert `FROST_TRIM` in the Utility/Cab section
of `frost_picker_types`, for example before `FROST_EMPTY`:

```c
FROST_GAIN,FROST_GATE,FROST_EQ,FROST_IR,FROST_TRIM,FROST_EMPTY,
FROST_HIGHPASS,FROST_LOWPASS,FROST_ACOUSTIC,
```

Category boundaries are cumulative offsets. With the current picker, adding
one Utility entry changes `{0,7,11,15,18,29,35,43,51,53}` to:

```c
static const unsigned char frost_picker_start[FROST_CATEGORIES+1] =
    {0,7,11,15,18,29,35,44,52,54};
```

Recalculate if the table has changed since this tutorial. Keep every effect
exactly once and retain Empty. The final offset equals the picker array length.
The control-catalog effect entry itself already comes from the library macro.

No new PARA or footswitch handler is needed. Preserve selected-only bypass.
An enabled TRIM at LEVEL 0 is silent; bypassed TRIM returns dry audio.

## 9. Update the website and inventory

Regenerate the browser catalog from firmware metadata:

```sh
PYTHONDONTWRITEBYTECODE=1 python tools/export_effects.py
```

`web/effects.json` is intentional generated source data. Verify TRIM's ID,
category, LEVEL range and default. The live editor creates controls from this
data; do not add a separate browser-only effect definition. If website cache
versions change, keep entry points and shared imports aligned.

Add the effect's ID/folder/status to `effects/catalog.json`, and update the
effect reference in [User Guide](User%20Guide.md). For optional offline
`tools/chain_wav.py --effects trim` support, add `trim` with its actual ID to
that tool's `LIBRARY_TYPES`; this CLI mapping is separate from registration.

## 10. Verify the integrated effect

```sh
CC=clang PYTHONDONTWRITEBYTECODE=1 python -m unittest discover -s tests -v
cmake -S . -B "$FROST_WORK/host-trim" -DCMAKE_C_COMPILER=clang
cmake --build "$FROST_WORK/host-trim" --parallel 2
```

Catalog tests deliberately pin the released count and invalid-ID boundary.
For this revision update 52-effect expectations to 53, and tests that treated
ID 52 as invalid to the new boundary. Review category/picker expectations.
Keep checks that old IDs and presets still work; do not simply remove them.

Add integration tests for:

- One parameter and no workspace in `frost_effect_find(FROST_TRIM)`.
- Correct LEVEL 0/50/100 values through shared UI conversion and DSP output.
- Settled silence/half/unity output after parameter/enable smoothing.
- Dry bypass and independent settings in two instances.
- Preset capture/recall of TRIM and unchanged loading of older presets.
- Exactly one picker entry and the correct exported browser controls.

`tests/library_host.c`, `tests/test_effect_library.py` and
`tests/test_metadata.py` show the existing registry, state and UI contracts.
The standalone function test is useful but is not complete integration coverage.

## 11. Build and use the effect

Use a fresh external stage and the full playable profile in
[Build Firmware](Build%20Firmware.md). Check package sections, DSP/controller
memory use and warnings. Building does not install anything.

When installation is explicitly intended, follow the
[Installation Guide](Installation%20Guide.MD). On the pedal, select a block,
open TYPE → Utility/Cab → TRIM, enable it and try LEVEL 100/50/0. Save the
preset to retain the changes. Check the same control in the website.

Allow the engine's parameter and bypass smoothing to settle before comparing
levels. Check switching for clicks, silence, faults and overloads. Give a
distributed effect change a new version in `firmware/version.h` and qualify
it separately. Host checks and successful builds are not physical CPU/audio
measurements.

## 12. Extend to stateful/heavy effects

- Define a state struct in the effect header and reset every field. For small
  state, add a bounded member to `FrostEffectState` in `engine/effect.h`.
  Increasing that union increases storage for every slot.
- Keep histories, envelopes and LFOs independent between instances. Immutable
  coefficient tables can be shared.
- Large delays/convolution/reverbs need a workspace class, pool capacity,
  route admission, reset and preset-validation design. Setting
  `workspace_bytes` does not allocate a buffer for you.
- State the sample rate, latency and tail policy, including what preset recall
  and cut/suspend bypass do to histories.
- Use bounded work. Never allocate, parse files, wait on locks, access USB,
  log or write flash in audio processing.
- Test silence, impulses, extremes, feedback stability, long runs, block
  boundaries, instance isolation and numerical fault containment.
- Measure the complete chain on hardware; adding standalone cost estimates
  does not prove it meets the real-time deadline.

Finish with a clean source tree:

```sh
python tools/clean_build.py --archive "$FROST_WORK/trim-test-output"
python tools/check_repository.py --require-empty-build
```

TRIM is a tutorial example, not a new effect added to the released firmware
by the presence of this guide.
