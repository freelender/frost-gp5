#ifndef FROST_BANK_READ_H
#define FROST_BANK_READ_H
#include "ram_bank.h"
#include "firmware/protocol/bank_read_wire.h"
static uint32_t bank_crc_byte(uint32_t crc,unsigned byte)
{unsigned i;crc^=byte;for(i=0;i<8;++i)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);return crc;}
static uint32_t bank_crc_word(uint32_t crc,uint32_t word)
{unsigned i;for(i=0;i<4;++i)crc=bank_crc_byte(crc,(word>>(8*i))&255u);return crc;}
static unsigned bank_name_length(unsigned entry)
{unsigned n=0;const char *name=frost_bank_name(entry);while(n<64 && name[n])++n;return n;}
static uint32_t bank_body_crc(unsigned entry)
{unsigned i,n=ram_bank_words(entry);uint32_t crc=UINT32_MAX;for(i=0;i<n;++i)crc=bank_crc_word(crc,ram_bank_word(entry,i));return crc^UINT32_MAX;}
/* Largest entry length: 1071 for an all-version-1 bank (unchanged STATUS),
 * 1101 once any entry uses version 2. INFO reports each entry's length. */
static unsigned bank_max_words(void)
{unsigned p,n=FROST_PRESET_WORDS;for(p=0;p<FROST_BANK_COUNT;++p)if(ram_bank_words(p)>n)n=ram_bank_words(p);return n;}
static uint32_t bank_all_crc(void)
{
    unsigned p,i,n;uint32_t crc=UINT32_MAX;
    crc=bank_crc_word(crc,1);crc=bank_crc_word(crc,FROST_BANK_COUNT);crc=bank_crc_word(crc,ram_bank.valid);
    for(p=0;p<FROST_BANK_COUNT;++p){
        const unsigned char *name=(const unsigned char *)frost_bank_name(p);n=bank_name_length(p);
        crc=bank_crc_word(crc,n);for(i=0;i<n;++i)crc=bank_crc_byte(crc,name[i]);
        n=ram_bank_words(p);for(i=0;i<n;++i)crc=bank_crc_word(crc,ram_bank_word(p,i));
    }
    return crc^UINT32_MAX;
}
/* Called only by the serialized controller main loop. No DSP requests, timers,
 * retained cursor, allocation or mutation; caller owns60-byte reply storage.
 * Request/reply may share unaligned memory, neither aliases bank storage. */
static unsigned bank_read_command(const uint8_t *request,unsigned bytes,uint8_t *reply)
{
    uint32_t v[7]={0},data[5]={0};unsigned i,n=0,status=FROST_SLOT_BAD_FRAME,valid=0;
    if(request && bytes>=12)v[1]=frost_wire_get32(request+8);
    if(request && bytes==32 && request[0]==0x46 && request[1]==0x7f && request[2]==32 && !request[3]){
        for(i=0;i<7;++i)v[i]=frost_wire_get32(request+4+4*i);
        valid=v[0]==1 && v[1]!=0;
        if(v[0]!=1)status=FROST_SLOT_BAD_VERSION;
        else if(!v[1])status=FROST_SLOT_BAD_REQUEST_ID;
        else if(v[2]>FROST_BANK_NAME)status=FROST_SLOT_BAD_OPERATION;
        else if(v[6])status=FROST_SLOT_BAD_FRAME;
        else if(v[2]==FROST_BANK_STATUS && (v[4]!=UINT32_MAX || v[5]))status=FROST_SLOT_BAD_FRAME;
        else if((v[2]!=FROST_BANK_STATUS && !v[3]) || (v[3] && v[3]!=ram_bank.epoch))status=FROST_SLOT_STALE_REVISION;
        else if(v[2]!=FROST_BANK_STATUS && v[4]>=FROST_BANK_COUNT)status=FROST_SLOT_BAD_SLOT;
        else if((v[2]==FROST_BANK_INFO && v[5]) || (v[2]==FROST_BANK_WORDS && v[5]>=ram_bank_words(v[4])) ||
                (v[2]==FROST_BANK_NAME && v[5]>=bank_name_length(v[4])))status=FROST_SLOT_BAD_OFFSET;
        else {
            status=FROST_SLOT_APPLIED;
            if(v[2]==FROST_BANK_STATUS){data[0]=bank_all_crc();data[1]=bank_max_words();data[2]=64;}
            else if(v[2]==FROST_BANK_INFO){data[0]=bank_body_crc(v[4]);data[1]=bank_name_length(v[4]);data[2]=ram_bank_valid(v[4]);data[3]=ram_bank_words(v[4]);}
            else if(v[2]==FROST_BANK_WORDS){
                n=ram_bank_words(v[4])-v[5];if(n>5)n=5;
                for(i=0;i<n;++i)data[i]=ram_bank_word(v[4],v[5]+i);
            }else {
                const unsigned char *name=(const unsigned char *)frost_bank_name(v[4]);
                n=bank_name_length(v[4])-v[5];if(n>20)n=20;
                for(i=0;i<n;++i)data[i/4]|=(uint32_t)name[v[5]+i]<<(8*(i%4));
            }
        }
    }
    for(i=0;i<60;++i)reply[i]=0;
    reply[0]=0x46;reply[1]=0x7f;reply[2]=60;
    frost_wire_put32(reply+4,1);frost_wire_put32(reply+8,v[1]);frost_wire_put32(reply+12,status);
    if(valid){
        frost_wire_put32(reply+16,ram_bank.epoch);frost_wire_put32(reply+20,FROST_BANK_COUNT);frost_wire_put32(reply+24,ram_bank.valid);
        frost_wire_put32(reply+28,v[4]);frost_wire_put32(reply+32,v[5]);
    }
    frost_wire_put32(reply+36,n);for(i=0;i<5;++i)frost_wire_put32(reply+40+4*i,data[i]);return 60;
}
#endif
