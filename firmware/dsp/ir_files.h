/* Main-loop receiver. No audio-path changes or receive array. While the
 * existing cabinet override holds IR processing off, record bytes occupy
 * coefficients[251..511]. Q15 samples begin at byte 1024, so forward expansion
 * into coefficients[0..511] never overwrites an unread sample. Byte access
 * avoids strict-aliasing violations. Functions have external linkage to keep
 * the boot-adjacent static-function layout intact. */
#include "firmware/protocol/ir_wire.h"
static struct { uint32_t state,slot,crc,next,restore,running;float sum,scale; } frost_ir_rx;
void frost_ir_file_init(void)
{frost_ir_rx.state=FROST_IR_IDLE;frost_ir_rx.slot=6;frost_ir_rx.crc=frost_ir_rx.next=frost_ir_rx.restore=0;}
FROST_COLD void frost_ir_file_builtin(unsigned slot)
{
    unsigned i,taps=FROST_TEST_IR_STORED;FrostIR *ir=frost_chain.slots[slot].state.ir;
    const FrostIRFFT *f=frost_ir_fft_of(ir);
#ifndef FROST_IR_LINK
    if(!(f && f->parts) && taps>128u)taps=128u;
#else
    (void)f;
#endif
    for(i=0;i<512;++i){
#ifdef FROST_TEST_IR_Q15
        ir->coefficients[i]=i<taps?(float)frost_test_ir_q15[i]*frost_test_ir_q15_scale:0.0f;
#else
        ir->coefficients[i]=i<taps?frost_test_ir[i]:0.0f;
#endif
        ir->history[i]=0.0f;
    }
    ir->taps=taps;ir->position=0;++frost_ir_generation;frost_ir_rx.state=FROST_IR_PREPARING;frost_ir_rx.next=0;
}
FROST_COLD void frost_ir_file_command(const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t status=FROST_SLOT_BAD_FRAME,token=n>=12?frost_wire_get32(p+8):0;
    unsigned i,op=0,slot=6,count=0,off=0;uint32_t crc=0;
    if(n>=32 && (unsigned)(p[2]|p[3]<<8)==n && frost_wire_get32(p+4)==1 && token){
        op=frost_wire_get32(p+12);slot=frost_wire_get32(p+16);crc=frost_wire_get32(p+20);
        off=frost_wire_get32(p+24);count=frost_wire_get32(p+28);
        if(op>FROST_IR_STATUS || count>20 || n!=32+4*count || (op!=FROST_IR_WRITE && count))status=FROST_SLOT_BAD_FRAME;
        else if(op==FROST_IR_STATUS)status=FROST_SLOT_APPLIED;
        else if(slot>=6 || frost_chain.slots[slot].type!=FROST_IR)status=FROST_SLOT_BAD_SLOT;
        else if(frost_chain.transition)status=FROST_SLOT_ACCEPTED;
        /* The second workspace has no FFT storage. Never install 512 taps
         * there: the established line-test route guard admits only 128 direct
         * taps. Reject before touching any coefficients or override state. */
#ifndef FROST_IR_LINK
        else if(op==FROST_IR_BEGIN && (!frost_ir_fft_of(frost_chain.slots[slot].state.ir) || FROST_IR_FFT_PARTS<8u))status=FROST_SLOT_BAD_VALUE;
#endif
        else if(op==FROST_IR_BEGIN || op==FROST_IR_CLEAR){
            if(frost_ir_rx.state==FROST_IR_IDLE){
                frost_ir_rx.restore=frost_ir_bypass;frost_ir_rx.state=FROST_IR_FADING;
                frost_ir_rx.slot=slot;frost_ir_rx.crc=crc;frost_ir_rx.next=0;frost_ir_rx.running=0xffffffffu;
            }
            if(frost_ir_rx.slot!=slot || frost_ir_rx.crc!=crc)status=FROST_SLOT_BAD_SESSION;
            else if(!frost_ir_bypass){(void)frost_chain_request_ir_bypass(&frost_chain,1);status=FROST_SLOT_ACCEPTED;}
            else if(op==FROST_IR_CLEAR){
                frost_ir_file_builtin(slot);status=FROST_SLOT_APPLIED;
            }else {frost_ir_rx.state=FROST_IR_RECEIVING;status=FROST_SLOT_APPLIED;}
        }else if(slot!=frost_ir_rx.slot || crc!=frost_ir_rx.crc)status=FROST_SLOT_BAD_SESSION;
        else if(op==FROST_IR_COMMIT && frost_ir_rx.state==FROST_IR_IDLE)status=FROST_SLOT_REPLAYED;
        else if(op==FROST_IR_COMMIT && frost_ir_rx.state==FROST_IR_RESTORING){frost_ir_rx.state=FROST_IR_IDLE;status=FROST_SLOT_APPLIED;}
        else if(frost_ir_rx.state<FROST_IR_RECEIVING || !frost_ir_bypass)status=FROST_SLOT_BAD_SESSION;
        else {
            FrostIR *ir=frost_chain.slots[slot].state.ir;
            uint8_t *data=(uint8_t *)(void *)ir->coefficients+1004u;
            if(op==FROST_IR_WRITE && frost_ir_rx.state==FROST_IR_RECEIVING){
                if(!count || off>261 || count>261-off || off>frost_ir_rx.next)status=FROST_SLOT_BAD_OFFSET;
                else if(off<frost_ir_rx.next){
                    status=FROST_SLOT_REPLAYED;
                    if(off+count>frost_ir_rx.next)status=FROST_SLOT_BAD_OFFSET;
                    else for(i=0;i<4*count;++i)if(data[4*off+i]!=p[32+i])status=FROST_SLOT_BAD_CHECKSUM;
                }else {unsigned k;for(i=0;i<4*count;++i){
                    uint32_t v=p[32+i];data[4*off+i]=(uint8_t)v;frost_ir_rx.running^=v;
                    for(k=0;k<8;++k)frost_ir_rx.running=(frost_ir_rx.running>>1)^(0xedb88320u&(0u-(frost_ir_rx.running&1u)));
                }frost_ir_rx.next+=count;status=FROST_SLOT_APPLIED;}
            }else if(op!=FROST_IR_COMMIT)status=FROST_SLOT_BAD_OPERATION;
            else if(frost_ir_rx.state==FROST_IR_RECEIVING){
                union {uint32_t u;float f;} scale;scale.u=frost_wire_get32(data+16);
                if(frost_ir_rx.next!=261)status=FROST_SLOT_INCOMPLETE;
                else if((frost_ir_rx.running^0xffffffffu)!=crc || !(scale.f>0.0f && scale.f<=8.0f/32767.0f)){
                    frost_ir_file_builtin(slot);status=FROST_SLOT_BAD_CHECKSUM;
                }else {frost_ir_rx.scale=scale.f;frost_ir_rx.sum=0.0f;frost_ir_rx.next=0;frost_ir_rx.state=FROST_IR_VALIDATING;status=FROST_SLOT_APPLIED;}
            }else if(frost_ir_rx.state==FROST_IR_VALIDATING){
                unsigned end=frost_ir_rx.next+64;
                for(i=frost_ir_rx.next;i<end;++i){int v=(int)(int16_t)(data[20+2*i]|data[21+2*i]<<8);frost_ir_rx.sum+=(float)(v<0?-v:v)*frost_ir_rx.scale;}
                frost_ir_rx.next=end;status=FROST_SLOT_APPLIED;
                if(end==512){
                    if(!(frost_ir_rx.sum>0.0f && frost_ir_rx.sum<=8.0001f)){frost_ir_file_builtin(slot);status=FROST_SLOT_BAD_VALUE;}
                    else {frost_ir_rx.next=0;frost_ir_rx.state=FROST_IR_EXPANDING;}
                }
            }else if(frost_ir_rx.state==FROST_IR_EXPANDING){
                unsigned end=frost_ir_rx.next+64;
                for(i=frost_ir_rx.next;i<end;++i){int v=(int)(int16_t)(data[20+2*i]|data[21+2*i]<<8);ir->coefficients[i]=(float)v*frost_ir_rx.scale;ir->history[i]=0.0f;}
                frost_ir_rx.next=end;status=FROST_SLOT_APPLIED;
                if(end==512){ir->taps=512;ir->position=0;++frost_ir_generation;frost_ir_rx.next=0;frost_ir_rx.state=FROST_IR_PREPARING;}
            }else if(frost_ir_rx.state==FROST_IR_PREPARING){
                FrostIRFFT *f=frost_ir_fft_of(ir);unsigned parts=(ir->taps+63)/64;
                if(parts>FROST_IR_FFT_PARTS)parts=0;
                if(f && frost_ir_rx.next<parts){
                    unsigned part=frost_ir_rx.next++;
                    if(!part)frost_ir_fft_twiddles(f);
                    for(i=0;i<128;++i)f->frame[i]=i<64?ir->coefficients[part*64+i]:0.0f;
                    frost_ir_fft_forward(f->twiddle,f->frame,f->scratch,f->spectrum[part]);
                    for(i=0;i<128;++i)f->spectrum[part][i]*=0x1p-7f;
                }else {
                    if(f){f->parts=parts;frost_ir_fft_clear(f);}
                    frost_ir_rx.state=FROST_IR_RESTORING;
                    (void)frost_chain_request_ir_bypass(&frost_chain,frost_ir_rx.restore);
                }
                status=FROST_SLOT_APPLIED;
            }else status=FROST_SLOT_BAD_SESSION;
        }
    }
    for(i=0;i<32;++i)reply[i]=0;
    reply[0]=0x4c;reply[1]=0x7f;reply[2]=32;
    frost_wire_put32(reply+4,1);frost_wire_put32(reply+8,token);frost_wire_put32(reply+12,status);
    frost_wire_put32(reply+16,frost_ir_rx.state);frost_wire_put32(reply+20,frost_ir_rx.slot);
    frost_wire_put32(reply+24,frost_ir_rx.crc);frost_wire_put32(reply+28,frost_ir_rx.next);
    /* An idle STATUS for a valid IR slot also reports its current tap count
     * in next. Global recovery (slot 6) retains the original reply contract. */
    if(op==FROST_IR_STATUS && status==FROST_SLOT_APPLIED && frost_ir_rx.state==FROST_IR_IDLE && slot<6u && frost_chain.slots[slot].type==FROST_IR){
        frost_wire_put32(reply+20,slot);frost_wire_put32(reply+28,frost_chain.slots[slot].state.ir->taps);
    }
}
