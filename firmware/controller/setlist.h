#ifndef FROST_SETLIST_H
#define FROST_SETLIST_H
/* FSET v1 occupies bytes 64..4031 of glb-0, protected by its own CRC.
 * Header 32, order[64], variable songs: id,count,name[16], then
 * count x {name[16],preset}. Main-loop flash reads only, no audio work. */
#define FROST_SET_BYTES (FROST_GLOBAL_GROUP_BYTES-64u)
#define FROST_SET_MAX_SONGS 64u
#define FROST_SET_MAX_PARTS 16u
static uint32_t set_addr,set_epoch,set_crc;
static uint16_t set_songs[64];
static unsigned set_count,set_order,set_part,set_loaded,set_pending,set_prev_order,set_prev_part,set_error;
static int setlist_updating(void)
{return files_host_written && (uint32_t)(gp5_ms()-files_host_write_ms)<FROST_STORE_GUARD_MS;}
static int set_name_valid(uint32_t a)
{unsigned i;for(i=0;i<16u;++i){unsigned c=FROST_FILE_AT(a)[i];if(c && (c<32u || c>126u))return 0;}return 1;}
static void setlist_refresh(void)
{
    uint32_t a,d,crc=0;unsigned i,j,pos,count,orders;uint16_t offsets[64];
    if(set_epoch==files_epoch && set_epoch!=UINT32_MAX)return;
    /* USB copy-on-write may erase the old sector before the next paint.
     * Drop every flash pointer during the write burst; validate only when idle.
     * Leave the epoch invalid so the idle transition is retried without reboot. */
    if(setlist_updating()){
        set_epoch=UINT32_MAX;set_addr=set_crc=0;set_count=set_loaded=set_pending=0;return;
    }
    set_epoch=files_epoch;d=store_group(FROST_GLB_FIRM,"glb-0",FROST_GLOBAL_GROUP_BYTES);a=d?d+64u:0;
    for(i=0;i<64u;++i)offsets[i]=0;
    if(!a || files_word(a)!=0x54455346u || FROST_FILE_AT(a)[4]!=1u)goto invalid;
    count=FROST_FILE_AT(a)[5];orders=FROST_FILE_AT(a)[6];
    if(count>64u || orders>64u || !set_name_valid(a+8u))goto invalid;
    crc=files_word(a+FROST_SET_BYTES-4u);
    if(files_crc(a,FROST_SET_BYTES-4u)!=crc)goto invalid;
    pos=96u;
    for(i=0;i<count;++i){
        unsigned id,n;if(pos+18u>FROST_SET_BYTES-4u)goto invalid;
        id=FROST_FILE_AT(a)[pos];n=FROST_FILE_AT(a)[pos+1u];
        if(id>=64u || offsets[id] || !n || n>16u || pos+18u+17u*n>FROST_SET_BYTES-4u || !set_name_valid(a+pos+2u))goto invalid;
        offsets[id]=(uint16_t)pos;
        for(j=0;j<n;++j)if(!set_name_valid(a+pos+18u+17u*j) || FROST_FILE_AT(a)[pos+34u+17u*j]>=FROST_PRESETS)goto invalid;
        pos+=18u+17u*n;
    }
    for(i=0;i<orders;++i)if(FROST_FILE_AT(a)[32u+i]>=64u || !offsets[FROST_FILE_AT(a)[32u+i]])goto invalid;
    if(!set_addr || crc!=set_crc){set_order=set_part=set_loaded=set_pending=set_error=0;}
    set_addr=a;set_crc=crc;set_count=orders;for(i=0;i<64u;++i)set_songs[i]=offsets[i];return;
invalid:
    set_addr=set_crc=0;set_count=set_loaded=set_pending=0;
}
static uint32_t setlist_song(unsigned order)
{unsigned id,offset;if(!set_addr || order>=set_count)return 0;
 id=FROST_FILE_AT(set_addr)[32u+order];if(id>=64u)return 0;
 offset=set_songs[id];return offset>=96u && offset+18u<=FROST_SET_BYTES-4u?set_addr+offset:0;}
static uint32_t setlist_part(unsigned order,unsigned part)
{uint32_t s=setlist_song(order);unsigned n=s?FROST_FILE_AT(s)[1]:0;return n<=16u && part<n?s+18u+17u*part:0;}
static void setlist_name(uint32_t a,char out[17])
{unsigned i;for(i=0;i<16u && a && FROST_FILE_AT(a)[i];++i){char c=(char)FROST_FILE_AT(a)[i];out[i]=c>='a' && c<='z'?(char)(c-32):c;}out[i]=0;}
static void setlist_init(void)
{set_epoch=UINT32_MAX;set_addr=set_crc=0;set_count=set_order=set_part=set_loaded=set_pending=set_error=0;setlist_refresh();}
#endif
