/* AMP/FCM record transfer, DSP side (firmware/protocol/fcm_wire.h).
 * Command handler only: runs in the DSP main loop between audio blocks, never
 * in fw_process. Not overlay code (the overlay may not be loaded). No
 * allocation; the one record buffer is frost_fcm_rx (fcm_effect.c). */
#include "fcm_effect.h"
#include "../../firmware/protocol/fcm_wire.h"
typedef char frost_fcm_record_bytes[sizeof(FrostFcmRecord)==FROST_FCM_RECORD_BYTES?1:-1];
uint32_t frost_fcm_crc32(const void *data,unsigned bytes)
{
    const uint8_t *b=(const uint8_t *)data;uint32_t c=0xffffffffu;unsigned i,k;
    for(i=0;i<bytes;++i){c^=b[i];for(k=0;k<8u;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
    return ~c;
}
void frost_fcm_rx_init(void)
{frost_fcm_rx.state=FROST_FCM_RX_EMPTY;frost_fcm_rx.index=frost_fcm_rx.crc=frost_fcm_rx.next=0;}
static int frost_fcm_record_valid(const FrostFcmRecord *r,uint32_t crc)
{
    return r->crc32==crc && frost_fcm_crc32(r,(unsigned)(sizeof(*r)-sizeof(r->crc32)))==crc &&
        r->model.n_pre<=FCMI_MAX_SOS && r->model.n_mid<=FCMI_MAX_SOS && r->model.n_post<=FCMI_MAX_SOS;
}
void frost_fcm_command(const uint8_t *p,unsigned n,uint8_t *reply)
{
    FrostFcmRx *r=&frost_fcm_rx;uint32_t token=0,status=FROST_SLOT_BAD_FRAME,v[7]={0};unsigned i;
    if(p && n>=12)token=frost_wire_get32(p+8);
    if(p && n>=FROST_FCM_REQUEST_BYTES && p[0]==(FROST_FCM_COMMAND&255u) && p[1]==(FROST_FCM_COMMAND>>8) &&
       (unsigned)(p[2]|(p[3]<<8))==n){
        for(i=0;i<7;++i)v[i]=frost_wire_get32(p+4+4*i);
        /* v: version, token, op, index, crc, offset, count */
        if(v[0]!=FROST_FCM_VERSION)status=FROST_SLOT_BAD_VERSION;
        else if(!v[1])status=FROST_SLOT_BAD_REQUEST_ID;
        else if(v[2]>FROST_FCM_CLEAR)status=FROST_SLOT_BAD_OPERATION;
        else if(v[6]>FROST_FCM_CHUNK_WORDS || n!=FROST_FCM_REQUEST_BYTES+4u*v[6] || (v[2]!=FROST_FCM_WRITE && v[6]))status=FROST_SLOT_BAD_FRAME;
        else if(v[2]==FROST_FCM_STATUS)status=FROST_SLOT_APPLIED;
        else if(v[2]==FROST_FCM_CLEAR){
            if(r->state!=FROST_FCM_RX_EMPTY){frost_fcm_rx_init();++r->epoch;}
            status=FROST_SLOT_APPLIED;
        }
        else if(v[3]>=FROST_FCM_MODELS || !v[4])status=FROST_SLOT_BAD_VALUE;
        else if(v[2]==FROST_FCM_BEGIN){
            /* Receiving overwrites the buffer: nothing is held until COMMIT
             * (the AMP block keeps playing its own copy meanwhile). */
            r->state=FROST_FCM_RX_RECEIVING;r->index=v[3];r->crc=v[4];r->next=0;status=FROST_SLOT_APPLIED;
        }
        else if(v[2]==FROST_FCM_WRITE){
            if(r->state!=FROST_FCM_RX_RECEIVING || r->index!=v[3] || r->crc!=v[4])status=FROST_SLOT_BAD_SESSION;
            else if(!v[6] || v[5]>FROST_FCM_RECORD_WORDS || v[6]>FROST_FCM_RECORD_WORDS-v[5])status=FROST_SLOT_BAD_OFFSET;
            else if(v[5]+v[6]<=r->next)status=FROST_SLOT_REPLAYED;   /* already have these words */
            else if(v[5]!=r->next)status=FROST_SLOT_BAD_OFFSET;
            else {
                uint32_t *w=(uint32_t *)(void *)&r->rec;
                for(i=0;i<v[6];++i)w[v[5]+i]=frost_wire_get32(p+FROST_FCM_REQUEST_BYTES+4u*i);
                r->next+=v[6];status=FROST_SLOT_APPLIED;
            }
        }
        else { /* COMMIT */
            if(r->state==FROST_FCM_RX_HELD && r->index==v[3] && r->crc==v[4])status=FROST_SLOT_REPLAYED;
            else if(r->state!=FROST_FCM_RX_RECEIVING || r->index!=v[3] || r->crc!=v[4])status=FROST_SLOT_BAD_SESSION;
            else if(r->next!=FROST_FCM_RECORD_WORDS)status=FROST_SLOT_INCOMPLETE;
            else if(!frost_fcm_record_valid(&r->rec,v[4])){frost_fcm_rx_init();status=FROST_SLOT_BAD_CHECKSUM;}
            else {r->state=FROST_FCM_RX_HELD;++r->epoch;status=FROST_SLOT_APPLIED;}
        }
    }
    for(i=0;i<FROST_FCM_REPLY_BYTES;++i)reply[i]=0;
    reply[0]=(uint8_t)FROST_FCM_COMMAND;reply[1]=(uint8_t)(FROST_FCM_COMMAND>>8);reply[2]=FROST_FCM_REPLY_BYTES;
    frost_wire_put32(reply+4,FROST_FCM_VERSION);frost_wire_put32(reply+8,token);frost_wire_put32(reply+12,status);
    frost_wire_put32(reply+16,r->state==FROST_FCM_RX_HELD?r->index:UINT32_MAX);
    frost_wire_put32(reply+20,r->state==FROST_FCM_RX_HELD?r->crc:0u);
    frost_wire_put32(reply+24,r->state);frost_wire_put32(reply+28,r->index);frost_wire_put32(reply+32,r->crc);
    frost_wire_put32(reply+36,r->next);frost_wire_put32(reply+40,r->epoch);
}
