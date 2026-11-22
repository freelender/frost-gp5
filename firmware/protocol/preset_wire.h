#ifndef FROST_PRESET_WIRE_H
#define FROST_PRESET_WIRE_H
#include "slot_wire.h"
#include "engine/metadata.h"
#define FROST_PRESET_COMMAND 0x7f43u
#define FROST_PRESET_REQUEST_BYTES 48u
#define FROST_PRESET_REPLY_BYTES 48u
#define FROST_PRESET_WORDS 1071u
/* Version-2 body: the 1071 version-1 words unchanged, then parameters 3..7
 * of slots 0..5 (slot-major, 5 words each). Word 0 (version) selects the
 * length; version-1 transfers/reads/CRCs are byte-identical to before. */
#define FROST_PRESET_EXTENSION_WORDS (FROST_SLOT_COUNT*(FROST_PARAMETERS-3u))
#define FROST_PRESET_MAX_WORDS (FROST_PRESET_WORDS+FROST_PRESET_EXTENSION_WORDS)
/* Version 3 (stage 2, docs/Build%20Firmware.md) is COMPACT: version-1 words 0..46
 * (taps words 45/46 zero) then the 30 extended parameter words, no IR samples:
 * 77 words. At COMMIT the DSP keeps the IR it holds (its IR workspaces) and
 * recalls the rest atomically, exactly like version 2. */
#define FROST_PRESET_VERSION_COMPACT 3u
#define FROST_PRESET_COMPACT_WORDS (47u+FROST_PRESET_EXTENSION_WORDS)
static inline unsigned frost_preset_words(uint32_t version)
{return version==3u?FROST_PRESET_COMPACT_WORDS:version==2u?FROST_PRESET_MAX_WORDS:FROST_PRESET_WORDS;}
#define FROST_PRESET_CHUNK_WORDS 5u
/* Explicit little-endian words, not native struct memory.
 * Request: version/token/op/session/base_revision/index/data[5].
 * Reply: version/token/status/revision/session/state/next/token_floor/phase/
 * committed_revision/prefix_crc32. */
enum { FROST_PRESET_STATUS, FROST_PRESET_BEGIN, FROST_PRESET_WRITE,
       FROST_PRESET_COMMIT, FROST_PRESET_ABORT };
enum { FROST_TRANSFER_EMPTY, FROST_TRANSFER_RECEIVING, FROST_TRANSFER_ACCEPTED,
       FROST_TRANSFER_COMPLETE, FROST_TRANSFER_INVALID, FROST_TRANSFER_ABORTED };
#endif
