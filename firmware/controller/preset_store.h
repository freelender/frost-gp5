#ifndef FROST_PRESET_STORE_H
#define FROST_PRESET_STORE_H
/* Stage 2 storage (docs/Build%20Firmware.md): one-sector GROUP files in controller
 * flash area A, each an array of fixed-size slots, rewritten copy-on-write
 * (new copy into a free sector, header last, read back, then the old sector
 * erased: a power cut leaves the old or the new file).
 *   presets: firmware FPST, files pst-0..pst-3, 15 x 264-byte records (60)
 *   AMP models: firmware FFCM, files amp-0..amp-6, 10 x 400-byte records (64)
 * Main-loop code only (an erase holds this processor up to a few hundred ms;
 * the DSP keeps playing). Never called from audio paths. */
#include "fcm_files.h"
#include "engine/control_catalog.h"
#include "firmware/protocol/preset_wire.h"
#include "firmware/protocol/ir_wire.h"
#define FROST_PST_FIRM 0x54535046u            /* "FPST" */
#define FROST_PST_SLOTS 15u
#define FROST_PST_FILES 4u
#define FROST_PRESETS (FROST_PST_SLOTS*FROST_PST_FILES)   /* 60 */
#define FROST_PST_BYTES 264u
#define FROST_PST_NAME 16u
#define FROST_AMP_SLOTS 10u
#define FROST_AMP_FILES 7u
#define FROST_GROUP_DATA (FROST_FILES_SECTOR-FROST_FILE_HEADER)   /* 4032 */
#define FROST_STORE_GUARD_MS 2000u            /* no pedal write this soon after a USB flash write */
static uint8_t store_buf[FROST_GROUP_DATA] __attribute__((aligned(4)));

static void store_group_name(char out[8],char a,char b,char c,unsigned g)
{out[0]=a;out[1]=b;out[2]=c;out[3]='-';out[4]=(char)('0'+g);out[5]=0;out[6]=out[7]=0;}
/* Data address of the newest intact group file, 0 if none. */
static uint32_t store_group(uint32_t firm,const char *name,unsigned bytes)
{
    uint32_t a=files_find(firm,name);
    if(!a || files_word(a+12)!=bytes || files_crc(a+FROST_FILE_HEADER,bytes)!=files_word(a+16))return 0;
    return a+FROST_FILE_HEADER;
}
static uint32_t store_crc_ram(const uint8_t *b,unsigned n)
{uint32_t c=0xffffffffu;unsigned k;while(n--){c^=*b++;for(k=0;k<8u;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static int store_sector_blank(unsigned s)
{uint32_t a=FROST_FILES_BASE+s*FROST_FILES_SECTOR,i;for(i=0;i<FROST_FILES_SECTOR;i+=4)if(files_word(a+i)!=0xffffffffu)return 0;return 1;}
/* Write store_buf[0..bytes) as the new copy of firm/name. 1 = done. */
static int store_write_group(uint32_t firm,const char *name,unsigned bytes)
{
    static uint32_t page[16];
    uint32_t old=files_find(firm,name),seq=0,a,h;unsigned s,t,i,free_s=FROST_FILES_SECTORS;uint8_t covered[FROST_FILES_SECTORS];
    for(s=0;s<FROST_FILES_SECTORS;++s)covered[s]=0;
    for(s=0;s<FROST_FILES_SECTORS;++s)if((h=files_header(s))){
        unsigned n=files_word(h+20)&0xffffu;if(files_word(h+4)>seq)seq=files_word(h+4);
        for(t=s;t<s+n && t<FROST_FILES_SECTORS;++t)covered[t]=1;
    }
    for(s=0;s<FROST_FILES_SECTORS && free_s==FROST_FILES_SECTORS;++s)if(!covered[s])free_s=s;
    if(free_s==FROST_FILES_SECTORS)return 0;
    a=FROST_FILES_BASE+free_s*FROST_FILES_SECTOR;
    if(!store_sector_blank(free_s) && frost_flash_erase(a))return 0;
    if(frost_flash_program(a+FROST_FILE_HEADER,(const uint32_t *)(const void *)store_buf,(bytes+3u)&~3u))return 0;
    for(i=0;i<bytes;++i)if(FROST_FILE_AT(a+FROST_FILE_HEADER)[i]!=store_buf[i])return 0;
    {   /* header last (OpenGP5 0.5 GP5F) */
        uint8_t *hb=(uint8_t *)(void *)page;uint32_t crc;
        for(i=0;i<64u;++i)hb[i]=i<60u?0:0xff;
        frost_wire_put32(hb,FROST_FILE_MAGIC);frost_wire_put32(hb+4,seq+1u);frost_wire_put32(hb+8,firm);frost_wire_put32(hb+12,bytes);
        frost_wire_put32(hb+16,store_crc_ram(store_buf,bytes));hb[20]=1;hb[21]=0;
        for(i=0;i<31u && name[i];++i)hb[24+i]=(uint8_t)name[i];
        crc=store_crc_ram(hb,56);frost_wire_put32(hb+56,crc);
        if(frost_flash_program(a,page,60))return 0;
    }
    if(files_header(free_s)!=a)return 0;
    /* then forget EVERY older copy (a power cut after a header but before an
     * erase can leave two; erasing only the newest would leak a sector) */
    (void)old;
    for(s=0;s<FROST_FILES_SECTORS;++s)if(s!=free_s && (h=files_header(s)) && files_word(h+8)==firm && files_name_is(h,name))(void)frost_flash_erase(h);
    gp5_uart0_rx_err_clear();gp5_uart0_rx_int_clear();++files_epoch;
    return 1;
}
/* ---- presets ---- */
/* Flash address of preset i's record (own magic/version/CRC checked), 0 if empty. */
static uint32_t store_preset(unsigned i)
{
    char name[8];uint32_t d,r;
    if(i>=FROST_PRESETS)return 0;
    store_group_name(name,'p','s','t',i/FROST_PST_SLOTS);
    if(!(d=store_group(FROST_PST_FIRM,name,FROST_PST_SLOTS*FROST_PST_BYTES)))return 0;
    r=d+(i%FROST_PST_SLOTS)*FROST_PST_BYTES;
    if((files_word(r)&0xffffu)!=0x5046u || FROST_FILE_AT(r)[2]!=1u)return 0;
    if(files_crc(r,FROST_PST_BYTES-4u)!=files_word(r+FROST_PST_BYTES-4u))return 0;
    return r;
}
static void store_preset_name(unsigned i,char out[17])
{
    uint32_t r=store_preset(i);unsigned k;
    if(!r){out[0]=0;return;}
    for(k=0;k<16u && FROST_FILE_AT(r+4)[k];++k)out[k]=(char)FROST_FILE_AT(r+4)[k];
    out[k]=0;
}
/* Record (264 bytes) -> compact body (77 words, version 3). Unknown effect
 * types become EMPTY; missing parameters take the effect's defaults; extra
 * stored parameters are ignored. Returns 1 if the record is usable. */
static int store_record_to_words(const uint8_t *rec,uint32_t *w)
{
    unsigned i,j;union {float f;uint32_t u;} v;
    for(i=0;i<FROST_PRESET_COMPACT_WORDS;++i)w[i]=0;
    w[0]=FROST_PRESET_VERSION_COMPACT;w[1]=44100;w[2]=rec[20]>FROST_SLOT_COUNT?FROST_SLOT_COUNT:rec[20];
    for(i=0;i<FROST_SLOT_COUNT;++i){
        const uint8_t *s=rec+32+36*i;unsigned type=s[0];const FrostControlEffect *d=type==FROST_EMPTY?0:frost_control_find(type);
        if(type!=FROST_EMPTY && !d)type=FROST_EMPTY;
        w[3+i]=type;w[9+i]=rec[21+i]<FROST_SLOT_COUNT?rec[21+i]:i;
        w[15+i]=type==FROST_EMPTY?0:s[1]&1u;w[21+i]=s[2]>1u?0:s[2];
        for(j=0;d && j<d->count && j<FROST_PARAMETERS;++j){
            v.u=j<s[3]?frost_wire_get32(s+4+4*j):0;
            if(j>=s[3] || !(v.f>=d->dsp[j].minimum && v.f<=d->dsp[j].maximum))v.f=d->dsp[j].initial;
            if(j<3)w[27+3*i+j]=v.u;else w[47+5*i+(j-3)]=v.u;
        }
    }
    {   /* the order must be a permutation; otherwise fall back to slot order */
        unsigned m=0;for(i=0;i<FROST_SLOT_COUNT;++i)m|=1u<<w[9+i];
        if(m!=(1u<<FROST_SLOT_COUNT)-1u)for(i=0;i<FROST_SLOT_COUNT;++i)w[9+i]=i;
    }
    return 1;
}
/* Compact body -> record in rec (name and IR references given). */
static void store_words_to_record(const uint32_t *w,const char *name,const uint16_t *ir_refs,uint8_t *rec)
{
    unsigned i,j;
    for(i=0;i<FROST_PST_BYTES;++i)rec[i]=0;
    rec[0]=0x46;rec[1]=0x50;rec[2]=1;rec[3]=0;
    for(i=0;i<16u && name[i];++i)rec[4+i]=(uint8_t)name[i];
    rec[20]=(uint8_t)w[2];for(i=0;i<FROST_SLOT_COUNT;++i)rec[21+i]=(uint8_t)w[9+i];
    for(i=0;i<FROST_SLOT_COUNT;++i){
        uint8_t *s=rec+32+36*i;unsigned type=w[3+i];const FrostControlEffect *d=type==FROST_EMPTY?0:frost_control_find(type);
        s[0]=(uint8_t)type;s[1]=(uint8_t)w[15+i];s[2]=(uint8_t)w[21+i];s[3]=(uint8_t)(d?d->count:0);
        for(j=0;d && j<d->count && j<FROST_PARAMETERS;++j)frost_wire_put32(s+4+4*j,j<3?w[27+3*i+j]:w[47+5*i+(j-3)]);
    }
    for(i=0;i<2u;++i){rec[248+2*i]=(uint8_t)ir_refs[i];rec[249+2*i]=(uint8_t)(ir_refs[i]>>8);}
    frost_wire_put32(rec+FROST_PST_BYTES-4u,store_crc_ram(rec,FROST_PST_BYTES-4u));
}
/* Save compact words as preset i. Keeps the stored name (or "PRESET NN") and
 * IR references. 1 = written; 0 = refused/failed (old copy intact). */
static uint16_t store_ir_refs[2]={0xffffu,0xffffu};
/* 32p: a name for the next save (website 7E 20); empty keeps the slot's name. */
static char store_save_label[17];
static int store_save_preset(unsigned i,const uint32_t *w,uint32_t now)
{
    char name[8],label[17];uint16_t refs[2]={0xffffu,0xffffu};uint32_t d;unsigned k;
    if(i>=FROST_PRESETS || w[0]!=FROST_PRESET_VERSION_COMPACT)return 0;
    if(files_host_written && (uint32_t)(now-files_host_write_ms)<FROST_STORE_GUARD_MS)return 0;
    if(store_save_label[0]){for(k=0;k<17u;++k)label[k]=store_save_label[k];store_save_label[0]=0;}
    else store_preset_name(i,label);
    if(!label[0]){const char *p="PRESET ";for(k=0;p[k];++k)label[k]=p[k];label[k++]=(char)('0'+(i+1)/10);label[k++]=(char)('0'+(i+1)%10);label[k]=0;}
    refs[0]=store_ir_refs[0];refs[1]=store_ir_refs[1];
    store_group_name(name,'p','s','t',i/FROST_PST_SLOTS);
    d=store_group(FROST_PST_FIRM,name,FROST_PST_SLOTS*FROST_PST_BYTES);
    for(k=0;k<FROST_PST_SLOTS*FROST_PST_BYTES;++k)store_buf[k]=d?FROST_FILE_AT(d)[k]:0;
    store_words_to_record(w,label,refs,store_buf+(i%FROST_PST_SLOTS)*FROST_PST_BYTES);
    return store_write_group(FROST_PST_FIRM,name,FROST_PST_SLOTS*FROST_PST_BYTES);
}
/* ---- AMP models in group files (legacy single files still read) ---- */
static uint32_t store_amp_record(unsigned index,uint32_t *crc)
{
    char name[8];uint32_t d,r;
    if(index>=FROST_FCM_MODELS)return 0;
    store_group_name(name,'a','m','p',index/FROST_AMP_SLOTS);
    if((d=store_group(FROST_FCM_FIRM,name,FROST_AMP_SLOTS*FROST_FCM_RECORD_BYTES))){
        r=d+(index%FROST_AMP_SLOTS)*FROST_FCM_RECORD_BYTES;
        if(files_crc(r,FROST_FCM_RECORD_BYTES-4u)==files_word(r+FROST_FCM_RECORD_BYTES-4u)){*crc=files_word(r+FROST_FCM_RECORD_BYTES-4u);return r;}
    }
    return fcm_file_record(index,crc);              /* before migration: amp-NN single files */
}
/* IR records in ir-00..ir-20; same group/record CRC fences as AMP. */
static uint32_t store_ir_record(unsigned index,uint32_t *crc)
{
    char name[8]={'i','r','-',0,0,0,0,0};uint32_t d,a;unsigned g=index/3u;
    if(index>=63)return 0;
    name[3]=(char)('0'+g/10);name[4]=(char)('0'+g%10);
    if(!(d=store_group(FROST_IR_FIRM,name,3u*FROST_IR_RECORD_BYTES)))return 0;
    a=d+(index%3u)*FROST_IR_RECORD_BYTES;
    if(files_crc(a,1044)!=files_word(a+1044))return 0;
    *crc=files_word(a+1044);return a;
}
#endif
