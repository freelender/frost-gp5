#include "preset_read_host.c"
API unsigned bank_admit_words(const uint32_t *words)
{
    unsigned i;
    for(i=0;i<FROST_PRESET_WORDS;++i)preset_set_word(&frost_chain.pending_preset,i,words[i]);
    return frost_slots_request_preset(&slot_protocol,&frost_chain,&frost_chain.pending_preset,slot_protocol.revision);
}
