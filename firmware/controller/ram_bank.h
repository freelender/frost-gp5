#ifndef FROST_RAM_BANK_H
#define FROST_RAM_BANK_H
#include "preset_bank.h"
#include "firmware/protocol/preset_wire.h"
/* Controller-owned volatile overlays; valid bit publishes only a complete body.
 * Init logically discards old storage. No copies/changes in DSP audio. */
/* Each entry holds a version-1 (1071) or version-2 (1101) body; word 0 selects
 * the length and unused tail words stay zero. */
typedef struct {uint32_t words[FROST_BANK_COUNT][FROST_PRESET_MAX_WORDS],valid,epoch;} FrostRamBank;
static FrostRamBank ram_bank;
static void ram_bank_init(void){ram_bank.valid=0;ram_bank.epoch=1;}
static unsigned ram_bank_valid(unsigned profile){return (ram_bank.valid>>profile)&1u;}
static uint32_t ram_bank_raw(unsigned profile,unsigned index)
{return ram_bank_valid(profile)?ram_bank.words[profile][index]:frost_bank_word(profile,index);}
static unsigned ram_bank_words(unsigned profile){return frost_preset_words(ram_bank_raw(profile,0));}
static uint32_t ram_bank_word(unsigned profile,unsigned index)
{return index<ram_bank_words(profile)?ram_bank_raw(profile,index):0;}
static int ram_bank_publish(unsigned profile,const uint32_t *words)
{
    unsigned i,n=frost_preset_words(words[0]);if(ram_bank.epoch==UINT32_MAX)return 0;
    for(i=0;i<FROST_PRESET_MAX_WORDS;++i)ram_bank.words[profile][i]=i<n?words[i]:0;
    ram_bank.valid|=1u<<profile;++ram_bank.epoch;return 1;
}
#endif
