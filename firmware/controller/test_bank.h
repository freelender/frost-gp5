#ifndef FROST_TEST_BANK_H
#define FROST_TEST_BANK_H
#include "editor_client.h"
/* Public synthetic qualification profiles, not cabinet captures or saved user
 * presets. Generate canonical words from compact ROM descriptors; never reserve
 * a second full preset in controller RAM. All profiles share this tiny FIR. */
#define FROST_TEST_PRESETS 3u
typedef struct {const char *name;uint8_t types[6],pre,cut;} FrostTestPreset;
static const FrostTestPreset frost_test_bank[FROST_TEST_PRESETS]={
    {"CLEAN TEST",{FROST_GAIN,FROST_GATE,FROST_EQ,FROST_IR,FROST_EMPTY,FROST_EMPTY},3,0},
    {"SPACE TEST",{FROST_GAIN,FROST_CHORUS,FROST_DELAY,FROST_REVERB,FROST_IR,FROST_EMPTY},1,14},
    {"DRIVE TEST",{FROST_GAIN,FROST_GATE,FROST_OVERDRIVE,FROST_FLANGER,FROST_IR,FROST_EMPTY},3,8}
};
static uint32_t test_bank_word(unsigned preset,unsigned index)
{
    const FrostTestPreset *b=&frost_test_bank[preset];
    if(index==0)return 1;
    if(index==1)return 44100;
    if(index==2)return b->pre;
    if(index<9)return b->types[index-3];
    if(index<15)return index-9;
    if(index<21)return b->types[index-15]!=FROST_EMPTY;
    if(index<27)return (b->cut>>(index-21))&1u;
    if(index<45){
        unsigned slot=(index-27)/3,p=(index-27)%3,type=b->types[slot];
        const FrostControlEffect *d=frost_control_find(type);
        if(p>=d->count)return 0;
        if(type==FROST_GAIN)return editor_bits(.75f);
        if(type==FROST_OVERDRIVE && p==2)return editor_bits(.25f);
        if(type==FROST_DELAY && p==0)return editor_bits(80.0f);
        return editor_bits(d->dsp[p].initial);
    }
    if(index==45)return 4;
    if(index>=47 && index<51){
        static const float fir[4]={.5f,.25f,.125f,.0625f};
        return editor_bits(fir[index-47]);
    }
    return 0;
}
#endif
