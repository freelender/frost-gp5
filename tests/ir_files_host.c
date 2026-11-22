#include "slot_editor_host.c"
API void irf_cab(unsigned slot,unsigned cab){ir_cabs[slot]=(uint16_t)cab;ir_file_client.next_check=0;}
API unsigned irf_cab_get(unsigned slot){return ir_cabs[slot];}
API unsigned irf_busy(void){return ir_file_client.state;}
API unsigned irf_ref(unsigned preset,unsigned bank){uint32_t r=store_preset(preset);return r?FROST_FILE_AT(r)[248+2*bank]|FROST_FILE_AT(r)[249+2*bank]<<8:0xffff;}
API unsigned irf_record(unsigned index){uint32_t crc;return store_ir_record(index,&crc)!=0;}
API unsigned irf_command(const uint8_t *p,unsigned n,uint8_t *r){frost_ir_file_command(p,n,r);return 32;}
API void irf_two(void){uint8_t types[6]={0,1,2,3,3,255},order[6]={0,1,2,3,4,5};editor_queue_route(&editor,types,order,6);}
API void irf_value_view(void){selected=3;view=PARAMETERS;parameter=0;}  /* CAB: first row since 2026-11-19 */

API unsigned irf_store_write(unsigned index,const uint8_t *r){
    char name[8]={'i','r','-',0,0,0,0,0};unsigned k,g=index/3;uint32_t d;
    name[3]=(char)('0'+g/10);name[4]=(char)('0'+g%10);
    d=store_group(FROST_IR_FIRM,name,3144);
    for(k=0;k<3144;++k)store_buf[k]=d?FROST_FILE_AT(d)[k]:0;
    for(k=0;k<1048;++k)store_buf[(index%3)*1048+k]=r[k];
    return store_write_group(FROST_IR_FIRM,name,3144);
}
API void irf_lost_reply(unsigned count){drop_count=count;drop_mode=1;}
API void irf_client_restart(void){ir_file_client_init(&editor);}
