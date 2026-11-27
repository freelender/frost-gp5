#ifndef FROST_QUICK_PRESET_WIRE_H
#define FROST_QUICK_PRESET_WIRE_H
#include "preset_wire.h"
#define FROST_QUICK_COMMAND 0x7f4du
#define FROST_QUICK_HEADER 34u
#define FROST_QUICK_MAX_BYTES (FROST_QUICK_HEADER+FROST_SLOT_COUNT*FROST_PARAMETERS*5u)
#define FROST_QUICK_REPLY_BYTES 32u
#define FROST_QUICK_CAP_RETAIN_IR 1u
#define FROST_QUICK_CAP_QUIET 2u
#define FROST_QUICK_CAPS (FROST_QUICK_CAP_RETAIN_IR|FROST_QUICK_CAP_QUIET)
/* Lossless recall, version1. u32 version/token/base_revision at 4/8/12;
 * pre_count at16, packed 3-bit slot order at17..19; byte20 = retained-IR stable-slot mask (bits0..5), byte21 reserved0;
 * six {type,enabled|policy<<1} at22. Parameters follow in slot order:
 * byte0..249 = exact integer -100..149, byte250 + raw float32 otherwise.
 * Built-in IR is resolved before the same atomic route commit. The internal
 * resolver must initialize both IR entries (unused taps/coefficients zero);
 * mask0 never inherits assets. A negotiated retained slot copies its current
 * coefficients into the pending snapshot before the atomic commit. Status is a
 * 12-byte frame with the original mutation token, never a second mutation.
 * Negotiated QUIET: a 16-byte request with the mutation token and base
 * revision fades to silence before decoding the full request. Repeat until
 * APPLIED/phase4. The full request commits at silence and warms two blocks
 * (phase5) before fading in. A 1-second quiet timeout restores the old sound.
 * Reply words: version,token,status,revision,phase,token_floor,capabilities (FROST_QUICK_CAP_*). */
#endif
