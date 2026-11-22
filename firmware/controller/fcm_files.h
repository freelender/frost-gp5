#ifndef FROST_FCM_FILES_H
#define FROST_FCM_FILES_H
/* Files in the controller's flash, OpenGP5 0.5 format (docs/Build%20Firmware.md,
 * upstream docs/platform/storage.md), restricted to run A 0x13F000..0x15FFFF
 * (33 sectors after every app, unused by Valeton's firmware). Frost reads them
 * memory-mapped; the computer writes them over USB with 7E 05/07/08/0D/0E
 * (same wire as OpenGP5 0.5, read/erase/program limited to run A).
 * Main-loop code only: an erase stops this processor for up to a few hundred
 * ms (the DSP keeps playing). Never called from the DSP or audio paths. */
#include <stdint.h>
#include "firmware/protocol/fcm_wire.h"
#define FROST_FILES_BASE 0x13f000u
#define FROST_FILES_SECTOR 0x1000u
#define FROST_FILES_SECTORS 33u
#define FROST_FILES_END (FROST_FILES_BASE+FROST_FILES_SECTORS*FROST_FILES_SECTOR)
#define FROST_FILE_MAGIC 0x46355047u            /* "GP5F" */
#define FROST_FILE_HEADER 64u
/* 1 = single amp-NN files (31s); 2 = packed group files + presets (stage 2,
 * docs/Build%20Firmware.md). Reported after the name in the 7E 0D reply. */
#define FROST_STORAGE_VERSION 2u
#ifndef FROST_FILE_AT                           /* memory-mapped flash; a host test maps it elsewhere */
#define FROST_FILE_AT(a) ((const volatile uint8_t *)(uintptr_t)(a))
#endif
int gp5_flash_erase(uint32_t addr);
int gp5_flash_program(uint32_t addr,const uint32_t *src,unsigned len);
void gp5_uart0_rx_err_clear(void);
void gp5_uart0_rx_int_clear(void);
/* 31y15: every controller flash erase/program goes through these. A write
 * stops this processor with interrupts off; with the IR link running, the
 * SPI slave then kept receiving and (pedal, 31y14) its receive path stayed
 * stuck afterwards (TX done, RX never). So the link is stopped before and
 * re-armed from scratch after; the DSP's resync pause realigns the frames. */
#ifdef FROST_IR_LINK
int frost_irl_flash_begin(void);void frost_irl_flash_end(void);
#else
static inline int frost_irl_flash_begin(void){return 1;}
static inline void frost_irl_flash_end(void){}
#endif
static int frost_flash_erase(uint32_t a){int r;if(!frost_irl_flash_begin())return -1;r=gp5_flash_erase(a);frost_irl_flash_end();return r;}
static int frost_flash_program(uint32_t a,const uint32_t *w,unsigned n){int r;if(!frost_irl_flash_begin())return -1;r=gp5_flash_program(a,w,n);frost_irl_flash_end();return r;}
static uint32_t files_word(uint32_t a)
{const volatile uint8_t *p=FROST_FILE_AT(a);return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t files_crc(uint32_t a,uint32_t n)
{
    uint32_t c=0xffffffffu;unsigned k;const volatile uint8_t *p=FROST_FILE_AT(a);
    while(n--){c^=*p++;for(k=0;k<8u;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
    return ~c;
}
/* A valid header at sector s: its address, else 0. */
static uint32_t files_header(unsigned s)
{
    uint32_t a=FROST_FILES_BASE+s*FROST_FILES_SECTOR,len,n;
    if(files_word(a)!=FROST_FILE_MAGIC || files_crc(a,56)!=files_word(a+56))return 0;
    len=files_word(a+12);n=files_word(a+20)&0xffffu;
    if(!n || s+n>FROST_FILES_SECTORS || len>n*FROST_FILES_SECTOR-FROST_FILE_HEADER)return 0;
    return a;
}
static int files_name_is(uint32_t a,const char *name)
{
    unsigned i;const volatile uint8_t *p=FROST_FILE_AT(a+24);
    for(i=0;i<32;++i){if(p[i]!=(uint8_t)name[i])return 0;if(!name[i])return 1;}
    return 0;
}
/* Newest copy (highest sequence) of firm/name, or 0. */
static uint32_t files_find(uint32_t firm,const char *name)
{
    uint32_t best=0,seq=0,a;unsigned s;
    for(s=0;s<FROST_FILES_SECTORS;++s)
        if((a=files_header(s)) && files_word(a+8)==firm && files_name_is(a,name) && (!best || files_word(a+4)>seq)){best=a;seq=files_word(a+4);}
    return best;
}
static void fcm_file_name(unsigned index,char name[8])
{name[0]='a';name[1]='m';name[2]='p';name[3]='-';name[4]=(char)('0'+index/10u%10u);name[5]=(char)('0'+index%10u);name[6]=name[7]=0;}
/* The intact FCM record for MODEL index (file data CRC, record size and the
 * record's own CRC all checked), as a flash address; 0 if none. *crc = the
 * record's crc32 word. */
static uint32_t fcm_file_record(unsigned index,uint32_t *crc)
{
    char name[8];uint32_t a,d;
    if(index>=FROST_FCM_MODELS)return 0;
    fcm_file_name(index,name);a=files_find(FROST_FCM_FIRM,name);
    if(!a || files_word(a+12)!=FROST_FCM_RECORD_BYTES)return 0;
    d=a+FROST_FILE_HEADER;
    if(files_crc(d,FROST_FCM_RECORD_BYTES)!=files_word(a+16))return 0;
    if(files_crc(d,FROST_FCM_RECORD_BYTES-4u)!=files_word(d+FROST_FCM_RECORD_BYTES-4u))return 0;
    *crc=files_word(d+FROST_FCM_RECORD_BYTES-4u);return d;
}
/* Display name of MODEL index into out (uppercased later by the font); 0 if no file. */
static uint32_t store_amp_record(unsigned index,uint32_t *crc);
static int fcm_file_label(unsigned index,char out[17])
{
    uint32_t crc,d=store_amp_record(index,&crc);unsigned i;const volatile uint8_t *p;
    if(!d)return 0;p=FROST_FILE_AT(d);
    for(i=0;i<16u && p[i];++i)out[i]=(char)p[i];
    out[i]=0;return 1;
}
static unsigned files_epoch;   /* bumped by every USB write/erase/notify: clients re-check */
/* Last USB erase/program: the pedal's own writes wait a moment after it so they
 * never pick the sector a computer-side copy-on-write is filling (preset_store.h). */
uint32_t gp5_ms(void);
static uint32_t files_host_write_ms;static unsigned files_host_written;
/* USB host commands 7E 05/07/08/0D/0E (OpenGP5 0.5 wire). pl = the platform's
 * reply buffer (pl[0..1] = 7E xx already). Returns reply length, 0 if the
 * request is not one of these (the platform then handles it as before). */
static unsigned frost_files_host(const uint8_t *q,unsigned len,uint8_t *pl,unsigned max)
{
    static uint32_t w[16];
    uint32_t a=len>=6?(uint32_t)q[2]|((uint32_t)q[3]<<8)|((uint32_t)q[4]<<16)|((uint32_t)q[5]<<24):0;
    unsigned i,cnt;
    if(len<2 || q[0]!=0x7e)return 0;
    if(q[1]==0x05 && len==7){          /* read <addr u32> <count <= 64> -> 7E 05 <ok> <bytes> */
        cnt=q[6];pl[2]=0;
        if(cnt && cnt<=64u && 3u+cnt<=max && a>=FROST_FILES_BASE && a<FROST_FILES_END && cnt<=FROST_FILES_END-a){
            const volatile uint8_t *p=FROST_FILE_AT(a);for(i=0;i<cnt;++i)pl[3+i]=p[i];pl[2]=1;return 3+cnt;
        }
        return 3;
    }
    if(q[1]==0x07 && len==6){          /* erase the 4 KB sector at <addr> */
        pl[2]=0;
        if(!(a&(FROST_FILES_SECTOR-1u)) && a>=FROST_FILES_BASE && a<FROST_FILES_END){
            pl[2]=frost_flash_erase(a)==0;gp5_uart0_rx_err_clear();gp5_uart0_rx_int_clear();++files_epoch;files_host_write_ms=gp5_ms();files_host_written=1;
        }
        return 3;
    }
    if(q[1]==0x08 && len>=10){         /* program <addr> <bytes, multiple of 4, <= 56> */
        cnt=len-6u;pl[2]=0;
        if(!(cnt&3u) && cnt<=56u && !(a&3u) && a>=FROST_FILES_BASE && a<FROST_FILES_END && cnt<=FROST_FILES_END-a){
            for(i=0;i<cnt;++i)((uint8_t *)w)[i]=q[6+i];
            pl[2]=frost_flash_program(a,w,cnt)==0;gp5_uart0_rx_err_clear();gp5_uart0_rx_int_clear();++files_epoch;files_host_write_ms=gp5_ms();files_host_written=1;
        }
        return 3;
    }
    if(q[1]==0x0d && len==2){          /* who: 7E 0D 1 <platform 0 2 1> "frost" 0 <Frost storage version> */
        static const char me[]="frost";
        pl[2]=1;pl[3]=0;pl[4]=2;pl[5]=1;for(i=0;me[i];++i)pl[6+i]=(uint8_t)me[i];pl[6+i]=0;
        pl[7+i]=FROST_STORAGE_VERSION;return 8+i;
    }
    if(q[1]==0x0e && len==6){          /* the computer changed files of firmware <id> */
        ++files_epoch;files_host_write_ms=gp5_ms();files_host_written=1;
        pl[2]=a==FROST_FCM_FIRM || a==0x42524946u || a==0x54535046u || a==0x424c4746u;return 3;
    }
    return 0;
}
#endif
