#ifndef FROST_CONTROLLER_PRESET_BANK_H
#define FROST_CONTROLLER_PRESET_BANK_H
#include <stdint.h>
#ifdef FROST_USER_BANK
/* Generated only from a validated .fbank by the staging tool; never include
 * arbitrary caller-supplied C. Immutable controller ROM, no DSP snapshot copy. */
#include "user_bank.h"
#if FROST_USER_PRESET_COUNT < 1 || FROST_USER_PRESET_COUNT > 16
#error Frost user bank must contain1..16 presets
#endif
#define FROST_BANK_COUNT FROST_USER_PRESET_COUNT
#define FROST_BANK_TITLE "MUTED USER BANK"
#define FROST_BANK_MENU "USER PRESETS"
#define FROST_BANK_NOTE "IR DATA / NO SAVE"
static const char *frost_bank_name(unsigned i){return frost_user_presets[i].name;}
static uint32_t frost_bank_word(unsigned i,unsigned word)
{const uint32_t *w=frost_user_presets[i].words;return word<(w[0]==2u?1101u:1071u)?w[word]:0;}
#else
#include "test_bank.h"
#define FROST_BANK_COUNT FROST_TEST_PRESETS
#define FROST_BANK_TITLE "MUTED TEST BANK"
#define FROST_BANK_MENU "TEST PRESETS"
#define FROST_BANK_NOTE "SYNTHETIC IR / NO SAVE"
static const char *frost_bank_name(unsigned i){return frost_test_bank[i].name;}
static uint32_t frost_bank_word(unsigned i,unsigned word){return test_bank_word(i,word);}
#endif
/* Existing font has uppercase and a small punctuation set. Preserve full UTF-8
 * names in ROM; display uppercase, one * per unsupported codepoint, and > for
 * truncation. Use existing visible glyphs without changing legacy references. */
static void __attribute__((unused)) frost_bank_label(unsigned i,char out[24])
{
    const unsigned char *name=(const unsigned char *)frost_bank_name(i);unsigned n=0;
    while(*name && n<23){
        unsigned ch=*name++;
        if(ch>=128){while((*name&0xc0u)==0x80u)++name;ch='*';}
        if(ch>='a' && ch<='z')ch-='a'-'A';
        if(!((ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch==' ' || ch=='%' ||
             ch=='*' || ch=='+' || ch=='-' || ch=='.' || ch=='/' || ch==':' || ch=='>'))ch='*';
        out[n++]=(char)ch;
    }
    if(*name && n)out[n-1]='>';out[n]=0;
}
#endif
