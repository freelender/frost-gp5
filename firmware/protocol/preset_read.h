#ifndef FROST_PRESET_READ_H
#define FROST_PRESET_READ_H
#include "slots.h"
#include "preset_read_wire.h"
/* Read-only revision-fenced view of live targets/assets, not histories or upload
 * staging. Serialized with audio/setters; no retained state, allocation or IO.
 * Request: version/token/op/expected_revision/index. DESCRIBE index=0;
 * expected revision may be0 for discovery. READ needs matching nonzero revision.
 * Reply: version/token/status/revision/index/count/data[5]. DESCRIBE count=0,
 * data={body_crc,body_words,0,0,0}. READ count<=5 with zero final padding.
 * Errors zero index/count/data. Request may alias reply, neither aliases chain. */
void frost_preset_read_command(const FrostSlotProtocol *s,const FrostChain *chain,
    const uint8_t *request,unsigned bytes,uint8_t *reply);
#endif
