# Contributing to Frost

Start with README.md and [Build Firmware](docs/Build%20Firmware.md), then
follow the rules below.

For a new effect, follow [Build an effect](docs/Build%20effect.MD).

## Repository layout

| Path | Purpose |
|---|---|
| engine/ | Chain execution, registry, parameter metadata and presets |
| effects/ | Effect implementations and module-specific source notes |
| firmware/controller/ | Display, controls, storage and DSP clients |
| firmware/dsp/ | Audio adapter, scheduling, transport and guards |
| firmware/protocol/ | Shared wire formats and commands |
| firmware/version.h | Semantic firmware version and display spelling |
| web/ | Static browser editor, converter and firmware installer |
| tools/ | Build, conversion, validation and maintenance commands |
| tests/ | Native fixtures and browser/protocol regression tests |
| presets/ | Factory presets and compatibility identities |
| assets/branding/ | Approved logo and embedded startup image |
| docs/ | Seven maintained user/developer guides, including the effects list and MIDI commands |
| build/ | Transient output; empty in a prepared source checkout |

## Development workflow

1. Make a focused change on a Git branch. Keep hardware details outside DSP
   kernels and keep browser/firmware protocol changes aligned.
2. Run relevant host tests. Add a regression test for a real behavior change.
3. Build isolated firmware stages outside the repository. Check memory limits
   and image sections; preserve accepted artifacts separately.
4. Describe behavior changes, checks run and any remaining hardware checks.
   Host timing and emulator instruction counts are not pedal measurements.
5. Update an existing guide when behavior or build steps change. Do not add
   development logs to docs/.
6. Archive transient test output with tools/clean_build.py and run the source
   hygiene check before handoff.

Never commit captures, user IR WAVs, firmware binaries, compiler downloads,
generated model headers, backups or secrets. Fixtures must be synthetic or
redistributable. Retain attribution when adapting third-party source.

## Code rules

- Never allocate memory, block or write flash inside the audio callback.
- Keep protocol IDs, wire formats and preset storage compatible; add, never
  reassign. Transitions stay bounded and faults are handled, not hidden.
- Keep the controls: PARA turn selects, PARA press edits; on the Chain and
  Stomp screens the footswitch bypasses only the selected block.
- Keep the approved Frost logo and blue colours.
- Converted capture models are approximations; Frost does not run native NAM.
  AMP plus five effects is a target, not a guarantee for every chain.
- docs/ holds only the seven maintained guides; build/ stays empty. A new or
  changed effect also updates the [Effects list](docs/Effects%20list.md).

## Reporting problems

Include firmware version, audio setup, complete effect chain, OS/browser when
relevant, exact steps and expected/actual behavior. Note whether restarting
changes the result. Share personal backups privately.

## Releases and deployment

Use firmware/version.h for release identity. Source preparation does not
publish a release. Device writes and website deployment require separate
explicit authorization. The Pages workflow is manual; source pushes do not
automatically deploy the site.
