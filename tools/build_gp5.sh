#!/usr/bin/env bash
# Build/check the muted diagnostic only. Never flashes.
set -euo pipefail
FROST_ROOT=$(cd "$(dirname "$0")/.." && pwd)
FROST_STAGE=$(cd "${1:-$FROST_ROOT/build/gp5-muted}" && pwd -P)
FROST_TOOLS=$(cd "${2:-$FROST_ROOT/build/toolchains}" && pwd -P)
test -f "$FROST_STAGE/frost-input.json"
test -x "$FROST_TOOLS/arm/bin/arm-none-eabi-gcc"
test -x "$FROST_TOOLS/andes/v3s/nds32le-elf-mculib-v3s/bin/nds32le-elf-gcc"
if test -e "$FROST_STAGE/fw-build" || test -L "$FROST_STAGE/fw-build"; then
  test "$(readlink -f "$FROST_STAGE/fw-build")" = "$FROST_TOOLS" || { echo 'Stage uses another toolchain folder'; exit 1; }
else
  ln -s "$FROST_TOOLS" "$FROST_STAGE/fw-build"
fi
cd "$FROST_STAGE"
FROST_OUTPUT=$(python3 -c 'import json; m=json.load(open("frost-input.json")); mode=m.get("audio_output","muted"); assert mode in ("muted","line-test"); print("GP5_Frost_line_test.bin" if mode=="line-test" else "GP5_Frost_muted.bin")')
bash platform/build_firmware.sh frost "build/$FROST_OUTPUT"
python3 tools/gp5tool.py check "build/$FROST_OUTPUT"
sha256sum "build/$FROST_OUTPUT"
if test "$FROST_OUTPUT" = GP5_Frost_line_test.bin; then
  echo 'AUDIO-CAPABLE unity-master line-test candidate built; NOT muted firmware. No device access or flash.'
else
  echo 'Muted diagnostic built and container checked. No device access; not hardware-ready.'
fi
