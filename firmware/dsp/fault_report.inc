/* 26b fault reporter (opt-in control-scheduler line-test profile only).
 * Replaces the pinned spin-forever default_handler. On any fault/unexpected
 * exception it records the stacked PC/LR and the fault status registers, then
 * keeps transmitting them every ~50 ms in the existing 0x4001 unsolicited
 * report envelope (signature FLT1, 28 bytes). The retained 23s app stores the
 * last unsolicited payload in its local 0x7e01 counters, so the crash site is
 * readable while the DSP answers nothing else. Audio stays stopped, exactly as
 * before (the old handler also never returned); ST[3] keeps its old value.
 * Polled UART only, no FPU, no stack beyond this frame. */
__attribute__((noreturn,used,noinline)) void frost_fault_report(const uint32_t *frame,uint32_t exc_return)
{
    uint32_t w[5];uint8_t r[28];unsigned i,b;uint8_t crc;
    ST[3]=0xDEAD0000u|(REG(0xE000ED04u)&0x1FFu);
    /* Only dereference a plausible, aligned exception frame inside SRAM. */
    if(((uint32_t)frame&3u) || (uint32_t)frame<0x20000000u || (uint32_t)frame>0x20007FE0u)frame=0;
    w[0]=frame?frame[6]:0xFFFFFFFFu;               /* faulting / interrupted PC */
    w[1]=frame?frame[5]:exc_return;                /* LR at the fault */
    w[2]=REG(0xE000ED28u);                         /* CFSR */
    w[3]=REG(0xE000ED2Cu)|((REG(0xE000ED04u)&0x1FFu)<<16); /* HFSR (bits 1,30,31) | IPSR in bits 16..24 */
    w[4]=REG(0xE000ED38u);                         /* BFAR (valid if CFSR.BFARVALID) */
    r[0]=0x01;r[1]=0x40;r[2]=28;r[3]=0;r[4]='F';r[5]='L';r[6]='T';r[7]='1';
    for(i=0;i<5;++i){r[8+4*i]=(uint8_t)w[i];r[9+4*i]=(uint8_t)(w[i]>>8);r[10+4*i]=(uint8_t)(w[i]>>16);r[11+4*i]=(uint8_t)(w[i]>>24);}
    crc=0;for(i=0;i<28;++i){crc^=r[i];for(b=0;b<8;++b)crc=(uint8_t)((crc&128u)?((uint32_t)crc<<1)^7u:(uint32_t)crc<<1);}
    for(;;){
        uint8_t h[4]={crc,32,0,1};uint32_t t0;
        for(i=0;i<32;++i){
            while(((REG(UART0+0x0Cu)>>11)&31u)>=16u){}
            REG(UART0)=i<4?h[i]:r[i-4];
        }
        t0=REG(0xE0001004u);while((uint32_t)(REG(0xE0001004u)-t0)<9600000u){}
    }
}
__attribute__((naked,noreturn)) void default_handler(void)
{
    __asm volatile("tst lr, #4\n\t"
                   "ite eq\n\t"
                   "mrseq r0, msp\n\t"
                   "mrsne r0, psp\n\t"
                   "mov r1, lr\n\t"
                   "b frost_fault_report\n");
}
