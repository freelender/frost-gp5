# Acoustic

Original AC-3-inspired body/attack simulator. See the
[revision, controls, validation and limits](../../docs/User%20Guide.md).

Four modes: Standard, Jumbo, Piezo (stored ID 2), Enhance (appended ID 3).
BODY sets damped air/wood resonance; TOP shapes pick attack and upper content.
MIX and LEVEL retain their stored positions and defaults. Reverb uses a
separate chain block. Clean input/full-range output is the starting setup.

64 bytes of local history, within the existing 120-byte slot union; no
external workspace. Host/emulator checks do not establish AC-3 sound matching
or physical sound/CPU qualification. No proprietary pedal DSP is included.
