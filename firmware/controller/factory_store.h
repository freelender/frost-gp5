#ifndef FROST_FACTORY_STORE_H
#define FROST_FACTORY_STORE_H
/* First-use copy into ordinary editable preset storage; main loop only.
 * The persistent marker prevents deleted presets reappearing on reboot.
 * Existing preset groups are never replaced during an upgrade. */
#include "factory_bank.h"
static unsigned factory_error,factory_checked;
static int factory_store_tick(uint32_t now)
{
    unsigned group,k,i,changed;uint32_t old;char name[8];
    if(globals.factory_state==1u || factory_error)return 0;
    if(files_host_written && (uint32_t)(now-files_host_write_ms)<FROST_STORE_GUARD_MS)return 1;
    if(!globals.factory_state){
        for(group=0;group<FROST_PST_FILES;++group){
            store_group_name(name,'p','s','t',group);
            if(files_find(FROST_PST_FIRM,name)){
                globals.factory_state=1;
                if(!globals_save(now))globals.factory_state=0;
                return 0; /* Existing user storage: always continue normal boot. */
            }
        }
        globals.factory_state=2;
        if(!globals_save(now)){globals.factory_state=0;factory_error=1;return 0;}
        return 1;
    }
    for(group=0;group<2u;++group){
        store_group_name(name,'p','s','t',group);
        old=store_group(FROST_PST_FIRM,name,FROST_PST_SLOTS*FROST_PST_BYTES);
        /* Preserve any unreadable existing group rather than overwrite it. */
        if(!old && files_find(FROST_PST_FIRM,name)){factory_error=1;return 0;}
        for(k=0;k<FROST_PST_SLOTS*FROST_PST_BYTES;++k)store_buf[k]=old?FROST_FILE_AT(old)[k]:0;
        changed=0;
        for(i=group*FROST_PST_SLOTS;i<FROST_FACTORY_COUNT && i<(group+1u)*FROST_PST_SLOTS;++i){
            unsigned at=(i%FROST_PST_SLOTS)*FROST_PST_BYTES,zero=1,erased=1;
            for(k=0;k<FROST_PST_BYTES;++k){if(store_buf[at+k])zero=0;if(store_buf[at+k]!=255u)erased=0;}
            if(!zero && !erased)continue; /* Includes edits made after an interrupted setup. */
            for(k=0;k<FROST_PST_BYTES;++k)store_buf[at+k]=frost_factory_records[i][k];
            changed=1;
        }
        if(changed){if(!store_write_group(FROST_PST_FIRM,name,FROST_PST_SLOTS*FROST_PST_BYTES)){factory_error=1;return 0;}return 1;}
    }
    old=globals.last_preset;
    if(globals.last_preset==255u && store_preset(0))globals.last_preset=0;
    globals.factory_state=1;
    if(!globals_save(now)){globals.factory_state=2;globals.last_preset=old;factory_error=1;}
    return 0;
}
#endif
