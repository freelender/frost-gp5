#!/usr/bin/env bash
# Extract previously downloaded/hash-verified archives. Linux x86_64 / WSL only.
set -euo pipefail
FROST_ROOT=$(cd "$(dirname "$0")/.." && pwd)
FROST_ARCHIVES="${1:-$FROST_ROOT/build/pinned-toolchains}"
FROST_TOOLS="${2:-$FROST_ROOT/build/toolchains}"
cd "$FROST_ARCHIVES"
printf '%s  %s\n' \
  ff10eb1fa9dce66f8aeca5267df01ce0353748f2f2364a550eceb10c7953abc3 andes-v3s.txz \
  62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823 arm-14.2.rel1-linux-x86_64.tar.xz | sha256sum -c -
mkdir -p "$FROST_TOOLS/andes/v3s" "$FROST_TOOLS/arm"
test -x "$FROST_TOOLS/andes/v3s/nds32le-elf-mculib-v3s/bin/nds32le-elf-gcc" || tar -xJf andes-v3s.txz -C "$FROST_TOOLS/andes/v3s"
test -x "$FROST_TOOLS/arm/bin/arm-none-eabi-gcc" || tar -xJf arm-14.2.rel1-linux-x86_64.tar.xz -C "$FROST_TOOLS/arm" --strip-components=1
echo "$FROST_TOOLS"
