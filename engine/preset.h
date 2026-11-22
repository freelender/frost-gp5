#ifndef FROST_PRESET_H
#define FROST_PRESET_H
#include <stdint.h>
#include "metadata.h"
#include "../effects/cabinet_ir/ir.h"
/* Version 1: original nine types, three parameters per slot (1071 words).
 * Version 2: any registered type, eight parameters per slot (1101 words; the
 * version-1 words are an exact prefix and parameters 3..7 are appended).
 * Capture emits version 1 whenever every slot is an original type, so legacy
 * configurations keep byte-identical captures/CRCs. */
#define FROST_PRESET_VERSION 1u
#define FROST_PRESET_VERSION_EXTENDED 2u
#define FROST_PRESET_IRS 2u
/* Native caller-owned snapshot, NOT a wire/file struct. No pointers/histories.
 * IR banks correspond to IR slots in ascending stable slot-ID order. Unused
 * parameters/banks/coefficient tails must be zero. Never memcpy onto a wire. */
typedef struct {
    uint8_t types[FROST_SLOT_COUNT],order[FROST_SLOT_COUNT];
    uint8_t enabled[FROST_SLOT_COUNT],bypass[FROST_SLOT_COUNT];
    float parameters[FROST_SLOT_COUNT][FROST_PARAMETERS];
    unsigned ir_taps[FROST_PRESET_IRS];
    float ir_coefficients[FROST_PRESET_IRS][FROST_IR_MAX];
    unsigned pre_count,version,sample_rate;
} FrostPreset;
#endif
