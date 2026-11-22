#include "ir.h"
#include "../../engine/hot_code.h"
#ifndef FROST_IR_SCALAR
/* Four taps per branch, in the same accumulation order as the reference.
 * No coefficient reduction or additional buffering/algorithmic delay. */
FROST_HOT FROST_CORE static float frost_ir_dot(float sum,const float *c,const float *h,unsigned count)
{
    while(count>=4){
        sum+=c[0]*h[0];sum+=c[1]*h[-1];
        sum+=c[2]*h[-2];sum+=c[3]*h[-3];
        count-=4;c+=4;
        /* Keep pointer arithmetic within the history array, including when
         * this is the final span ending at history[0]. */
        if(count)h-=4;
    }
    while(count){sum+=*c++ * *h;--count;if(count)--h;}
    return sum;
}
#endif
/* 31a: external linkage keeps the new code out of the compiler's early static
 * function group (30b's boot-adjacent layout stays byte-identical). */
void frost_ir_fft_prepare(FrostIR *s,FrostIRFFT *f);
void frost_ir_fft_push(FrostIRFFT *f,float input);
/* 31a: one loader for float and q15 sources (value i = c[i] or q[i]*scale).
 * Float validation, sums and copies are unchanged from the float-only form. */
static inline __attribute__((always_inline)) float frost_ir_source(const float *c,const int16_t *q,float scale,unsigned i)
{ return c?c[i]:(float)q[i]*scale; }
int frost_ir_load_any(FrostIR *s,const float *c,const int16_t *q,float scale,unsigned taps);
FROST_COLD int frost_ir_load_any(FrostIR *s,const float *c,const int16_t *q,float scale,unsigned taps)
{
    unsigned i;float sum=0.0f;
    if((!c && !q) || !taps || taps>FROST_IR_MAX)return 0;
    if(!c && !(scale>0.0f && scale<=8.0f/32768.0f))return 0;
    for(i=0;i<taps;++i){float v=frost_ir_source(c,q,scale,i);
        if(!(v>=-8.0f && v<=8.0f))return 0;
        sum+=v<0?-v:v;
    }
    if(sum>8.0f)return 0;
    for(i=0;i<FROST_IR_MAX;++i){s->coefficients[i]=i<taps?frost_ir_source(c,q,scale,i):0.0f;s->history[i]=0.0f;}
    s->taps=taps;s->position=0;
    {FrostIRFFT *f=frost_ir_fft_of(s);if(f)frost_ir_fft_prepare(s,f);}
    ++frost_ir_generation;
    return 1;
}
int frost_ir_load(FrostIR *s,const float *coefficients,unsigned taps)
{ return coefficients?frost_ir_load_any(s,coefficients,0,0.0f,taps):0; }
int frost_ir_load_q15(FrostIR *s,const int16_t *q,unsigned taps,float scale)
{ return q?frost_ir_load_any(s,0,q,scale,taps):0; }
#ifdef FROST_IR_RAM
__attribute__((section(".frost_ir_hot"),noinline))
#endif
const float *frost_ir_fft_replay;
/* Stage 4: bumped by every coefficient change (loads, IR file transfers);
 * the IR link re-fingerprints the coefficients it mirrors to the controller. */
unsigned frost_ir_generation;
FROST_HOT FROST_CORE float frost_ir_sample(FrostIR *s,float input)
{
    if(frost_ir_fft_replay)return *frost_ir_fft_replay++;
#ifdef FROST_IR_SCALAR
    unsigned k=0,j=s->position;float sum=0.0f;
#if !(defined(__arm__) && defined(GP5_FIRMWARE))
    /* Host: keep the FFT window in step with per-sample calls. On the target
     * an FFT-bound IR never runs per-sample with sample calls (64-sample
     * blocks; the only per-sample entry is a CUT slot resting dry, which makes
     * none), so this feed is not linked there (DSP flash). */
    {FrostIRFFT *f=frost_ir_fft_of(s);if(f && f->parts)frost_ir_fft_push(f,input);}
#endif
    s->history[j]=input;
    /* Two spans avoid division/modulo inside the MAC loop. Coefficients and
     * history are both caller-owned RAM, not serial flash reads. */
    do {sum+=s->coefficients[k++]*s->history[j];}while(j-- && k<s->taps);
    j=s->taps;
    while(k<s->taps)sum+=s->coefficients[k++]*s->history[--j];
    if(++s->position==s->taps)s->position=0;
    return sum;
#else
    unsigned first=s->position+1;float sum;
#if !(defined(__arm__) && defined(GP5_FIRMWARE))
    /* Host: keep the FFT window in step with per-sample calls. On the target
     * an FFT-bound IR never runs per-sample with sample calls (64-sample
     * blocks; the only per-sample entry is a CUT slot resting dry, which makes
     * none), so this feed is not linked there (DSP flash). */
    {FrostIRFFT *f=frost_ir_fft_of(s);if(f && f->parts)frost_ir_fft_push(f,input);}
#endif
    s->history[s->position]=input;
#ifdef FROST_IR_LINK
    /* Stage 4: the controller runs the full IR; this local fallback reads the
     * first FROST_IR_LOCAL_TAPS taps only (bounded cost without an FFT). */
    {unsigned len=s->taps<FROST_IR_LOCAL_TAPS?s->taps:FROST_IR_LOCAL_TAPS,n1=first<len?first:len;
     sum=frost_ir_dot(0.0f,s->coefficients,s->history+s->position,n1);
     if(n1<len)sum=frost_ir_dot(sum,s->coefficients+first,s->history+s->taps-1,len-n1);}
#else
    sum=frost_ir_dot(0.0f,s->coefficients,s->history+s->position,first);
    sum=frost_ir_dot(sum,s->coefficients+first,s->history+s->taps-1,s->taps-first);
#endif
    if(++s->position==s->taps)s->position=0;
    return sum;
#endif
}
/* 31a: the direct block FIR below is host/reference only; on the target the
 * FFT path serves the bound IR and an unbound IR runs the scalar path (frees
 * ~1.3 KB of DSP flash for the FFT code). */
#if !(defined(__arm__) && defined(GP5_FIRMWARE))
/* ---- Block FIR (revision 25a) ----
 * Register-blocked direct form: eight outputs share each coefficient load and
 * each history load (rotating window), so the inner loop is one coefficient
 * load, one history load and eight multiply-accumulates. Every output keeps
 * the reference accumulation: sum starts at 0.0f and adds c[k]*x[n-k] for
 * k=0..taps-1 in order, written as the same `acc+=c*h` statement, so results
 * are bit-identical to frost_ir_sample/frost_ir_pair under the same compiler
 * contraction. The unused tail of history[] (taps+count<=FROST_IR_MAX) holds
 * the block linearly; no extra RAM, no algorithmic delay. */
static void frost_ir_reverse(float *a,unsigned begin,unsigned end)
{
    while(begin+1<end){float t=a[begin];a[begin]=a[--end];a[end]=t;++begin;}
}
/* h points at the newest input of output 0; outputs 0..7 use h[0..7].
 * Reads h[-(taps-1)]..h[7] only (no load after the final tap). */
/* 30b: back in flash. From VARS RAM it measured slower on the pedal (94.8k vs
 * 80.7k): its coefficients/history are in VARS too (inferred bus contention). */
FROST_HOT FROST_CORE void frost_ir_dot8(const float *c,const float *h,unsigned taps,float *out)
{
    float a0=0.0f,a1=0.0f,a2=0.0f,a3=0.0f,a4=0.0f,a5=0.0f,a6=0.0f,a7=0.0f;
    float r0=h[0],r1=h[1],r2=h[2],r3=h[3],r4=h[4],r5=h[5],r6=h[6],r7=h[7];
    unsigned k=0;
    /* Step: window v_j (input of output j at tap k) lives in a named register;
     * after the tap, the register holding v_7 receives the next older sample. */
#define FROST_IR_STEP(V0,V1,V2,V3,V4,V5,V6,V7) do { float cc=c[k];         a0+=cc*V0;a1+=cc*V1;a2+=cc*V2;a3+=cc*V3;a4+=cc*V4;a5+=cc*V5;a6+=cc*V6;a7+=cc*V7;         ++k;V7=h[-(int)k]; } while(0)
    /* Eight taps per pass; the pass's last load is for tap k+8 < taps. */
    while(k+9<=taps){
        FROST_IR_STEP(r0,r1,r2,r3,r4,r5,r6,r7);
        FROST_IR_STEP(r7,r0,r1,r2,r3,r4,r5,r6);
        FROST_IR_STEP(r6,r7,r0,r1,r2,r3,r4,r5);
        FROST_IR_STEP(r5,r6,r7,r0,r1,r2,r3,r4);
        FROST_IR_STEP(r4,r5,r6,r7,r0,r1,r2,r3);
        FROST_IR_STEP(r3,r4,r5,r6,r7,r0,r1,r2);
        FROST_IR_STEP(r2,r3,r4,r5,r6,r7,r0,r1);
        FROST_IR_STEP(r1,r2,r3,r4,r5,r6,r7,r0);
    }
#undef FROST_IR_STEP
    /* Remaining 1..8 taps: rotate by explicit moves, no load after the last. */
    for(;;){
        float cc=c[k];
        a0+=cc*r0;a1+=cc*r1;a2+=cc*r2;a3+=cc*r3;a4+=cc*r4;a5+=cc*r5;a6+=cc*r6;a7+=cc*r7;
        if(++k>=taps)break;
        r7=r6;r6=r5;r5=r4;r4=r3;r3=r2;r2=r1;r1=r0;r0=h[-(int)k];
    }
    out[0]=a0;out[1]=a1;out[2]=a2;out[3]=a3;out[4]=a4;out[5]=a5;out[6]=a6;out[7]=a7;
}
FROST_HOT FROST_CORE float frost_ir_dot1(const float *c,const float *h,unsigned taps)
{
    float sum=0.0f;unsigned k;
    for(k=0;k<taps;++k)sum+=c[k]*h[-(int)k];
    return sum;
}
int frost_ir_block_fits(const FrostIR *s,unsigned count)
{ return s->taps && count && s->taps+count<=FROST_IR_MAX; }
FROST_HOT FROST_CORE void frost_ir_block(FrostIR *s,const float *input,float *output,unsigned count)
{
    unsigned taps=s->taps,p=s->position,i;float *h=s->history;
    /* Rotate the ring so history[0..taps) is chronological, oldest first.
     * position is the next write slot, i.e. the oldest retained input. The
     * engine's steady state is p==1 (one scalar sample after a block). */
    if(p==1){float t=h[0];for(i=1;i<taps;++i)h[i-1]=h[i];h[taps-1]=t;}
    else if(p){frost_ir_reverse(h,0,p);frost_ir_reverse(h,p,taps);frost_ir_reverse(h,0,taps);}
    for(i=0;i<count;++i)h[taps+i]=input[i];
    i=0;
    /* count%8==7 (63 after the engine's scalar sample): one group starting a
     * sample early recomputes the previous output (discarded) instead of seven
     * single-output dots. Its oldest read is history[0]. */
    if(count%8u==7u){float t[8];unsigned j;
        frost_ir_dot8(s->coefficients,h+taps-1,taps,t);
        for(j=0;j<7;++j)output[j]=t[j+1];
        i=7;}
    for(;i+8<=count;i+=8)frost_ir_dot8(s->coefficients,h+taps+i,taps,output+i);
    for(;i<count;++i)output[i]=frost_ir_dot1(s->coefficients,h+taps+i,taps);
    /* Keep the newest taps inputs, oldest first; next write overwrites [0]. */
    for(i=0;i<taps;++i)h[i]=h[count+i];
    s->position=0;
}
/* Host/reference only since 26a; the target uses frost_ir_block or the scalar path. */
FROST_HOT void frost_ir_pair(FrostIR *s,float input0,float input1,float *output0,float *output1)
{
    unsigned position=s->position,taps=s->taps,next,span,count;
    const float *c=s->coefficients+1,*h;
    float a=0.0f,b=0.0f,previous=input0;
    s->history[position]=input0;
    a+=s->coefficients[0]*input0;b+=s->coefficients[0]*input1;
    /* Output1's next delayed value is output0's previous delayed value.
     * One coefficient + one history load feeds two independent accumulators.
     * Split at the ring seam, with four coefficients per loop branch. */
#define FROST_IR_PAIR_TAP(C,H) do {float coefficient=(C),value=(H);a+=coefficient*value;b+=coefficient*previous;previous=value;} while(0)
    for(span=0;span<2;++span){
        count=span?taps-position-1:position;
        if(!count)continue;
        h=s->history+(span?taps-1:position-1);
        while(count>=4){
            FROST_IR_PAIR_TAP(c[0],h[0]);FROST_IR_PAIR_TAP(c[1],h[-1]);
            FROST_IR_PAIR_TAP(c[2],h[-2]);FROST_IR_PAIR_TAP(c[3],h[-3]);
            count-=4;c+=4;if(count)h-=4;
        }
        while(count){FROST_IR_PAIR_TAP(*c++,*h);--count;if(count)--h;}
    }
#undef FROST_IR_PAIR_TAP
    next=s->position+1;if(next==taps)next=0;
    s->history[next]=input1;
    if(++next==taps)next=0;
    s->position=next;
    *output0=a;*output1=b;
}
#endif
/* ---- Partitioned FFT convolution (revision 31a) ----
 * Uniform-partitioned overlap-save: each 64-sample block forms the window
 * [previous 64, current 64], whose 128-point real spectrum enters a
 * frequency-domain delay line (FDL). Y = sum_p X_{b-p} * H_p, where H_p is the
 * spectrum of taps 64p..64p+63 zero-padded to 128. The last 64 samples of the
 * inverse transform are exactly the linear convolution of the current block:
 * no added latency. A 128-point real FFT runs as a 64-point complex FFT of
 * even/odd sample pairs plus a split step. One decimation-in-frequency FFT
 * (natural in, bit-reversed out) serves both directions: the inverse runs it
 * on the conjugated spectrum and conjugates the gathered output. No
 * permutation pass is needed; the forward split and the output gather address
 * bit-reversed positions through a 6-bit reversal. Spectra are packed: bin 0
 * holds (DC, Nyquist), both real. All scale factors (1/2 per split, 1/64 for
 * the inverse) are folded into H as one exact 1/128. Float32 throughout;
 * results differ from the direct form only by rounding. */
static const float frost_ir_cos128[33]={ /* cos(2*pi*k/128), k = 0..32 */
    0x1p+0f,0x1.ff621ep-1f,0x1.fd88dap-1f,0x1.fa7558p-1f,0x1.f6297cp-1f,0x1.f0a7fp-1f,0x1.e9f416p-1f,
    0x1.e2121p-1f,0x1.d906bcp-1f,0x1.ced7bp-1f,0x1.c38b3p-1f,0x1.b72834p-1f,0x1.a9b662p-1f,0x1.9b3e04p-1f,
    0x1.8bc806p-1f,0x1.7b5df2p-1f,0x1.6a09e6p-1f,0x1.57d694p-1f,0x1.44cf32p-1f,0x1.30ff8p-1f,0x1.1c73b4p-1f,
    0x1.07387ap-1f,0x1.e2b5d4p-2f,0x1.b5d1p-2f,0x1.87de2ap-2f,0x1.58f9a8p-2f,0x1.294062p-2f,0x1.f19f98p-3f,
    0x1.8f8b84p-3f,0x1.2c8106p-3f,0x1.917a6cp-4f,0x1.91f66p-5f,0.0f};
static inline __attribute__((always_inline)) unsigned frost_ir_rev6(unsigned k)
{
#if defined(__arm__) && defined(__GNUC__)
    unsigned r;__asm__("rbit %0, %1" : "=r"(r) : "r"(k));return r>>26;
#else
    unsigned r=0,i;for(i=0;i<6;++i){r=(r<<1)|(k&1u);k>>=1;}return r;
#endif
}
/* In place, 64 interleaved complex values; w = W128^k as (cos,-sin) pairs.
 * Out of line: one copy serves the forward and the inverse transform. */
void frost_ir_fft64_dif(float *z,const float *w);
/* Stage 4 (FROST_IR_LINK target): the DSP-side IR is the fallback, so its FFT
 * kernels move from LARGE to flash (same optimize flags: same arithmetic) and
 * free LARGE for the IR link's per-block code. */
#if defined(FROST_IR_LINK) && defined(FROST_LARGE_CODE) && defined(__arm__) && defined(GP5_FIRMWARE)
#define FROST_IR_FFT_CODE __attribute__((noinline,optimize("O2","no-tree-loop-distribute-patterns","fp-contract=off")))
#else
#define FROST_IR_FFT_CODE FROST_AUDIO_RAM_NOFMA
#endif
FROST_IR_FFT_CODE FROST_CORE FROST_NOINLINE void frost_ir_fft64_dif(float *z,const float *w)
{
    unsigned half,j,n,step;
    for(half=32u,step=4u;half;half>>=1,step<<=1){
        for(j=0;j<half;++j){
            const float wr=w[j*step],wi=w[j*step+1u];
            for(n=j;n<64u;n+=2u*half){
                float *a=z+2u*n,*b=a+2u*half;
                const float ar=a[0],ai=a[1],br=b[0],bi=b[1],dr=ar-br,di=ai-bi;
                a[0]=ar+br;a[1]=ai+bi;
                b[0]=dr*wr-di*wi;b[1]=dr*wi+di*wr;
            }
        }
    }
}
/* frame (128 real samples) -> packed spectrum out; scratch receives the FFT. */
void frost_ir_fft_forward(const float *w,const float *frame,float *scratch,float *out);
FROST_IR_FFT_CODE FROST_CORE FROST_NOINLINE void frost_ir_fft_forward(const float *w,const float *frame,float *scratch,float *out)
{
    unsigned k;
    for(k=0;k<FROST_IR_FFT_SIZE;++k)scratch[k]=frame[k];
    frost_ir_fft64_dif(scratch,w);
    out[0]=scratch[0]+scratch[1];out[1]=scratch[0]-scratch[1];
    for(k=1;k<=32u;++k){
        const float *a=scratch+2u*frost_ir_rev6(k),*b=scratch+2u*frost_ir_rev6(64u-k);
        /* Ze = (A + conj B)/2, Zo = -i (A - conj B)/2 for A = Z[k], B = Z[64-k] */
        const float er=0.5f*(a[0]+b[0]),ei=0.5f*(a[1]-b[1]);
        const float orr=0.5f*(a[1]+b[1]),oi=-0.5f*(a[0]-b[0]);
        const float wr=w[2u*k],wi=w[2u*k+1u];
        const float tr=wr*orr-wi*oi,ti=wr*oi+wi*orr;
        out[2u*k]=er+tr;out[2u*k+1u]=ei+ti;
        out[2u*(64u-k)]=er-tr;out[2u*(64u-k)+1u]=ti-ei;
    }
}
void frost_ir_fft_twiddles(FrostIRFFT *f);
FROST_COLD FROST_NOINLINE void frost_ir_fft_twiddles(FrostIRFFT *f)
{
    unsigned k;
    for(k=0;k<64u;++k){
        const float c=k<=32u?frost_ir_cos128[k]:-frost_ir_cos128[64u-k];
        const float s=k<=32u?frost_ir_cos128[32u-k]:frost_ir_cos128[k-32u];
        f->twiddle[2u*k]=c;f->twiddle[2u*k+1u]=-s;
    }
}
void frost_ir_fft_clear(FrostIRFFT *f);
FROST_NOINLINE void frost_ir_fft_clear(FrostIRFFT *f)
{
    unsigned p,k;
    for(p=0;p<FROST_IR_FFT_PARTS;++p)for(k=0;k<FROST_IR_FFT_SIZE;++k)f->input[p][k]=0.0f;
    for(k=0;k<FROST_IR_FFT_SIZE;++k)f->frame[k]=0.0f;
    f->head=0;f->fill=0;f->dirty=0;
}
FROST_COLD FROST_NOINLINE void frost_ir_fft_prepare(FrostIR *s,FrostIRFFT *f)
{
    unsigned p,k,parts=(s->taps+FROST_IR_FFT_BLOCK-1u)/FROST_IR_FFT_BLOCK;
    frost_ir_fft_twiddles(f);
    if(parts>FROST_IR_FFT_PARTS)parts=0; /* too long: the direct form serves it */
    for(p=0;p<parts;++p){
        for(k=0;k<FROST_IR_FFT_SIZE;++k)f->frame[k]=k<FROST_IR_FFT_BLOCK?s->coefficients[p*FROST_IR_FFT_BLOCK+k]:0.0f;
        frost_ir_fft_forward(f->twiddle,f->frame,f->scratch,f->spectrum[p]);
        for(k=0;k<FROST_IR_FFT_SIZE;++k)f->spectrum[p][k]*=0x1p-7f;
    }
    f->parts=parts;
    frost_ir_fft_clear(f);
}
/* Registry: FrostIR address -> workspace. Bound/unbound cold only. */
struct FrostIRFFTBinding {const FrostIR *ir;FrostIRFFT *fft;};
struct FrostIRFFTBinding frost_ir_fft_bindings[FROST_IR_FFT_BINDINGS];
FrostIRFFT *frost_ir_fft_of(const FrostIR *s)
{
    unsigned i;
    for(i=0;i<FROST_IR_FFT_BINDINGS;++i)if(frost_ir_fft_bindings[i].ir==s)return frost_ir_fft_bindings[i].fft;
    return 0;
}
FROST_COLD int frost_ir_bind_fft(FrostIR *s,FrostIRFFT *workspace)
{
    unsigned i,slot=FROST_IR_FFT_BINDINGS;
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    if(workspace && !workspace->spectrum)return 0;
#endif
    for(i=0;i<FROST_IR_FFT_BINDINGS;++i){
        if(frost_ir_fft_bindings[i].ir==s){slot=i;break;}
        if(slot==FROST_IR_FFT_BINDINGS && !frost_ir_fft_bindings[i].ir)slot=i;
    }
    if(slot==FROST_IR_FFT_BINDINGS)return workspace?0:1;
    frost_ir_fft_bindings[slot].ir=workspace?s:0;frost_ir_fft_bindings[slot].fft=workspace;
    for(i=0;i<FROST_IR_MAX;++i)s->history[i]=0.0f;
    s->position=0;
    if(workspace)frost_ir_fft_prepare(s,workspace);
    return 1;
}
void frost_ir_fft_reset(const FrostIR *s)
{ FrostIRFFT *f=frost_ir_fft_of(s);if(f)frost_ir_fft_clear(f); }
void frost_ir_clear(FrostIR *s)
{
    unsigned i;for(i=0;i<s->taps;++i)s->history[i]=0.0f;
    frost_ir_fft_reset(s);
}
int frost_ir_fft_ready(const FrostIR *s)
{ const FrostIRFFT *f=frost_ir_fft_of(s);return f && f->parts && !f->fill; }
/* Window complete: transform it into the newest FDL slot and keep its second
 * half as the next window's first half. */
void frost_ir_fft_advance(FrostIRFFT *f);
FROST_IR_FFT_CODE FROST_CORE FROST_NOINLINE void frost_ir_fft_advance(FrostIRFFT *f)
{
    unsigned k;
    f->head=f->head?f->head-1u:f->parts-1u;f->dirty=1;
    frost_ir_fft_forward(f->twiddle,f->frame,f->scratch,f->input[f->head]);
    for(k=0;k<FROST_IR_FFT_BLOCK;++k)f->frame[k]=f->frame[FROST_IR_FFT_BLOCK+k];
}
/* Per-sample path keeps the FDL in step with the direct-form history. */
void frost_ir_fft_push(FrostIRFFT *f,float input)
{
    f->frame[FROST_IR_FFT_BLOCK+f->fill]=input;f->dirty=1;
    if(++f->fill==FROST_IR_FFT_BLOCK){f->fill=0;frost_ir_fft_advance(f);}
}
FROST_IR_FFT_CODE FROST_CORE void frost_ir_fft_block(FrostIR *s,const float *input,float *output)
{
    FrostIRFFT *f=frost_ir_fft_of(s);const float *w=f->twiddle;float *z=f->scratch;
    unsigned i,k,p,slot,parts=f->parts,taps=s->taps,position=s->position;
    for(i=0;i<FROST_IR_FFT_BLOCK;++i){
        const float x=input[i];
        f->frame[FROST_IR_FFT_BLOCK+i]=x;
        s->history[position]=x;if(++position==taps)position=0;
    }
    s->position=position;
    frost_ir_fft_advance(f);
    /* Y = sum_p X_{b-p} H_p into scratch (natural order, packed bin 0). */
    for(k=0;k<FROST_IR_FFT_SIZE;++k)z[k]=0.0f;
    for(p=0,slot=f->head;p<parts;++p){
        const float *x=f->input[slot],*h=f->spectrum[p];
        z[0]+=x[0]*h[0];z[1]+=x[1]*h[1];
        for(k=2;k<FROST_IR_FFT_SIZE;k+=2){z[k]+=x[k]*h[k]-x[k+1]*h[k+1];z[k+1]+=x[k]*h[k+1]+x[k+1]*h[k];}
        if(++slot==parts)slot=0;
    }
    /* Inverse split in place (pairs k, 64-k), stored conjugated for the DIF:
     * Ze = Y[k] + conj(Y[m]); Zo = conj(W^k) (Y[k] - conj(Y[m]));
     * Z[k] = Ze + i Zo; Z[m] = conj(Ze - i Zo). Both x2, folded into H. */
    {const float dc=z[0],ny=z[1];z[0]=dc+ny;z[1]=ny-dc;}
    for(k=1;k<=32u;++k){
        float *a=z+2u*k,*b=z+2u*(64u-k);
        const float yr=a[0],yi=a[1],mr=b[0],mi=b[1];
        const float er=yr+mr,ei=yi-mi,dr=yr-mr,di=yi+mi;
        const float wr=w[2u*k],wi=-w[2u*k+1u];
        const float orr=dr*wr-di*wi,oi=dr*wi+di*wr;
        a[0]=er-oi;a[1]=-(ei+orr);
        b[0]=er+oi;b[1]=ei-orr;
    }
    frost_ir_fft64_dif(z,w);
    /* Samples 64..127 = complex points 32..63 (bit-reversed, conjugated). */
    for(i=0;i<FROST_IR_FFT_BLOCK;i+=2){
        const float *r=z+2u*frost_ir_rev6(32u+i/2u);
        output[i]=r[0];output[i+1u]=-r[1];
    }
}
