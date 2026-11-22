# Contributing to Frost

Start with README.md and [Build Firmware](docs/Build%20Firmware.md). Follow
AGENTS.md whether working manually or with an AI assistant.

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
| docs/ | Six maintained user/developer guides, including MIDI commands |
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

## Reporting problems

Include firmware version, audio setup, complete effect chain, OS/browser when
relevant, exact steps and expected/actual behavior. Note whether restarting
changes the result. Share personal backups privately.

## Releases and deployment

Use firmware/version.h for release identity. Source preparation does not
publish a release. Device writes and website deployment require separate
explicit authorization. The Pages workflow is manual; source pushes do not
automatically deploy the site.
