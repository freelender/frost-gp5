#include "slot_editor_host.c"
static uint8_t proto_req[64],bank_host_reply[100];
static unsigned proto_req_len,bank_host_reply_len,bank_host_class,bank_host_index;
static void proto_reply(uint8_t cls,uint8_t index,const uint8_t *p,unsigned n)
{unsigned i;bank_host_class=cls;bank_host_index=index;bank_host_reply_len=n;for(i=0;i<n && i<100;++i)bank_host_reply[i]=p[i];}
static void host_bank_command(void)
{
    static uint8_t pl[100];
#include "../firmware/controller/bank_host.inc"
}
API unsigned be_read(const uint8_t *p,unsigned n,uint8_t *reply){return fw_bank_read(p,n,reply);}
API unsigned be_bridge(const uint8_t *p,unsigned n)
{
    unsigned i;bank_host_reply_len=0;if(n>64)return 0;
    proto_req_len=n;for(i=0;i<n;++i)proto_req[i]=p[i];host_bank_command();return bank_host_reply_len;
}
API unsigned be_byte(unsigned i){return i<bank_host_reply_len?bank_host_reply[i]:0;}
API unsigned be_class(void){return bank_host_class;}
API unsigned be_index(void){return bank_host_index;}
API unsigned be_epoch(void){return ram_bank.epoch;}
API void be_set_epoch(unsigned epoch){ram_bank.epoch=epoch;}
