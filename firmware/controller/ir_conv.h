#ifndef FROST_IR_CONV_H
#define FROST_IR_CONV_H
/* Stage 4: 512-tap fixed-point FIR for the control processor (docs/Build%20Firmware.md).
 * Input int32 (DSP samples x 2^23), coefficients int16 packed in pairs (tap 2i
 * in the low half), out[n] = sum_k round(x[n-k] * q[k] / 2^16), saturating.
 * The NDS32 build uses the DSP extension's rounding multiply-accumulate
 * (kmmawb/kmmawt); the host build has the same rounding, so they are
 * bit-identical (as OpenGP5 NAM Loader's kernel). Four outputs per pass reuse
 * each coefficient word and each input word. */
#include <stdint.h>
#include "firmware/protocol/ir_link_wire.h"
#if defined(__NDS32_EXT_DSP__)
#include <nds32_intrinsic.h>
#define IRC_KMB(acc,a,b) __nds32__kmmawb_u((acc),(a),(b))
#define IRC_KMT(acc,a,b) __nds32__kmmawt_u((acc),(a),(b))
#else
static inline int32_t irc_sat32(int64_t v){return v>2147483647LL?2147483647:v<-2147483648LL?(int32_t)0x80000000u:(int32_t)v;}
static inline int32_t IRC_KMB(int32_t acc,int32_t a,uint32_t b){return irc_sat32((int64_t)acc+(((int64_t)a*(int16_t)b+0x8000)>>16));}
static inline int32_t IRC_KMT(int32_t acc,int32_t a,uint32_t b){return irc_sat32((int64_t)acc+(((int64_t)a*(int16_t)(b>>16)+0x8000)>>16));}
#endif
#define IRC_HIST FROST_IRL_TAPS                         /* index of the first new input */
typedef struct {
    int32_t x[IRC_HIST+FROST_IRL_BLOCK];                /* [guard][511 older][64 new]; the guard is read, never used */
    uint32_t q[FROST_IRL_TAPS/2u];                      /* active coefficients, pairs */
} IrcStream;
static void irc_clear(IrcStream *s){unsigned i;for(i=0;i<sizeof s->x/sizeof s->x[0];++i)s->x[i]=0;}
/* in: 64 new inputs. out: 64 outputs. Advances the history. */
#if defined(__NDS32__)
__attribute__((optimize("O2")))
#endif
static void irc_block(IrcStream *s,const int32_t *in,int32_t *out)
{
    unsigned n,k;int32_t *x=s->x;
    for(n=0;n<FROST_IRL_BLOCK;++n)x[IRC_HIST+n]=in[n];
    for(n=0;n<FROST_IRL_BLOCK;n+=4u){
        /* p[j] = x for output n+j at lag 0; tap t reads p[j-t]. Four taps per
         * pass (two coefficient words), taps in ascending order per output. */
        const int32_t *p=x+IRC_HIST+n;const uint32_t *q=s->q;
        int32_t a0=0,a1=0,a2=0,a3=0,x3=p[3],x2=p[2],x1=p[1],x0=p[0];
        for(k=0;k<FROST_IRL_TAPS;k+=4u){
            uint32_t c0=q[0],c1=q[1];int32_t m1=p[-1],m2=p[-2],m3=p[-3];
            a3=IRC_KMB(a3,x3,c0);a3=IRC_KMT(a3,x2,c0);a3=IRC_KMB(a3,x1,c1);a3=IRC_KMT(a3,x0,c1);
            a2=IRC_KMB(a2,x2,c0);a2=IRC_KMT(a2,x1,c0);a2=IRC_KMB(a2,x0,c1);a2=IRC_KMT(a2,m1,c1);
            a1=IRC_KMB(a1,x1,c0);a1=IRC_KMT(a1,x0,c0);a1=IRC_KMB(a1,m1,c1);a1=IRC_KMT(a1,m2,c1);
            a0=IRC_KMB(a0,x0,c0);a0=IRC_KMT(a0,m1,c0);a0=IRC_KMB(a0,m2,c1);a0=IRC_KMT(a0,m3,c1);
            /* next four taps: lags +4 */
            x3=m1;x2=m2;x1=m3;x0=p[-4];p-=4;q+=2;
        }
        out[n]=a0;out[n+1]=a1;out[n+2]=a2;out[n+3]=a3;
    }
    for(n=1;n<IRC_HIST;++n)x[n]=x[n+FROST_IRL_BLOCK];
}
#endif
