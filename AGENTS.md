# Working on Frost

Read README.md, CONTRIBUTING.md and the relevant guide in docs/ before changes.
This folder is the product repository; the surrounding workspace is not a
synchronized source tree.

## Repository hygiene

- docs/ contains exactly Installation Guide.MD, Build Firmware.md,
  Build effect.MD, Website Guide.md, User Guide.md and Midi Command.md. Update these guides; do not add status,
  planning, handoff, diary, trial-report or validation-dump documents.
- Put task notes, logs, screenshots, recordings, generated firmware, compiler
  archives, model captures, backups and temporary scripts outside this folder.
  Use an explicit external work directory. Do not embed private model banks.
- build/ is transient output for tools/tests that require it. It must be empty
  at handoff. Use tools/clean_build.py to archive output outside the repo. Do
  not add .gitkeep or documentation inside build/.
- Do not leave __pycache__, .pyc, binary outputs or scratch files in source.
  Use Git branches instead of duplicate source snapshots or experiment folders.
- Run tools/check_repository.py --require-empty-build before handoff. Preserve
  earlier evidence outside the repo; never delete user data casually.

## Code organization and behavior

Effect DSP belongs in effects/<effect>/; chain logic in engine/; UI/storage
clients in firmware/controller/; audio adaptation in firmware/dsp/; shared
wire formats in firmware/protocol/; browser tools in web/. Keep reusable
tools in tools/ and meaningful regression tests in tests/.
firmware/version.h is the canonical release identity.

Preserve the confirmed Frost logo/blue colors and controls: PARA rotation
selects, PARA press edits; Chain/Stomp footswitch bypass affects only the
selected block. Converted capture models are approximations; Frost does not
run native NAM. AMP plus five effects is a target, not an all-chain guarantee.
Prioritize beta reliability before new effects. Do not restart NAM research
without a new user request.

Never allocate, block or write flash inside an audio callback. Preserve
protocol IDs, bounded transitions, fault handling and storage compatibility.
Distinguish host/emulator checks from physical measurements. Default builds
are muted; playable ADC/line-test builds are explicit.

Build/test requests do not authorize flashing, publication, deployment or user
storage writes. Obtain explicit authorization for each device update. Do not
flash merely to validate documentation or source-tree cleanup.
