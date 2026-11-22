/* Compact alternate backend. Original legacy helpers/callers remain intact. */
#include "reverbs16.h"
#include <stdint.h>
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../reverb/voicing.h"
/* ---------------- RV-6-style family --------------------------------------
 * Four-line feedback delay network with a Householder (orthogonal) mixing
 * matrix, per-line damping and per-line gains set from a target RT60
 * (g = 10^(-3 L / (fs RT))), preceded by Schroeder allpass diffusers.
 * ROOM: short lines, 0.3..2 s. HALL: longest lines, 1..8 s. PLATE: four
 * dense diffusers, brighter damping, 0.7..5 s. MOD: hall lines with two
 * slowly modulated read taps (chorused tail). SHIMMER: an octave-up
 * dual-grain pitch shifter on the reverb output is fed back into the network
 * and mixed into the wet path, so each regeneration climbs an octave.
 * E.LEVEL is the effect level (direct sound stays at unity); TONE damping
 * 1.5..12 kHz, higher = brighter; TIME sets RT60 within the mode range. */
typedef struct { unsigned short fdn[4],ap[4],pitch; float rt_min,rt_log2,ap_gain,tone,mod; } Rv6Mode16;
const Rv6Mode16 rv6_modes16[FROST_RV6_MODES]={
    {{293,401,499,587},{113,157,0,0},0,0.3f,2.737f,0.6f,0.8f,0.0f},
    {{1009,1151,1291,1373},{149,211,0,0},0,1.0f,3.0f,0.6f,1.0f,0.0f},
    {{659,773,887,1013},{89,127,163,211},0,0.7f,2.837f,0.7f,1.5f,0.0f},
    {{1009,1151,1291,1373},{149,211,0,0},0,1.0f,3.0f,0.6f,1.0f,6.0f},
    {{787,911,1031,1153},{149,211,0,0},976,2.0f,2.322f,0.6f,1.2f,0.0f}};
#define SHIMMER_WINDOW 900.0f
unsigned frost_16_rv6_mode_samples(unsigned mode)
{
    unsigned i,n=0;if(mode>=FROST_RV6_MODES)return 0;
    for(i=0;i<4;++i)n+=rv6_modes16[mode].fdn[i]+rv6_modes16[mode].ap[i];
    return n+rv6_modes16[mode].pitch;
}
void r16_model_clear(FrostReverbModelState *m)
{/* Reverse traversal also prevents GCC from merging this helper with the
   * frozen boot-region model_clear (its original callers must stay intact). */
 unsigned i;for(i=FROST_REVERB_MODEL_LINES;i;){--i;m->position[i]=m->valid[i]=0;}
 for(i=32;i;)m->z[--i]=0.0f;m->index=0;}
void frost_16_rv6_reset(FrostReverbModel16 *s){if(s->ws){r16_model_clear(&s->ws->model);s->ws->model.mode=255u;for(unsigned i=0;i<12;++i)s->ws->error[i]=0.0f;}}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA FROST_NOINLINE float r16_line_read(const FrostReverb16 *w,unsigned line,unsigned offset,unsigned length,unsigned delay)
{
    const FrostReverbModelState *m=&w->model;unsigned pos=m->position[line];
    if(delay>m->valid[line] || !delay)return 0.0f;
    return frost_reverb16_load(w->samples[offset+(pos>=delay?pos-delay:pos+length-delay)],1.0f/4096.0f);
}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA FROST_NOINLINE void r16_line_write(FrostReverb16 *w,unsigned line,unsigned offset,unsigned length,float v)
{
    FrostReverbModelState *m=&w->model;
    w->samples[offset+m->position[line]]=frost_reverb16_quantize_fast(frost_rail(v,8.0f),4096.0f,&w->error[line]);
    if(++m->position[line]==length)m->position[line]=0;
    if(m->valid[line]<length)++m->valid[line];
}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA float r16_line_read_frac(const FrostReverb16 *w,unsigned line,unsigned offset,unsigned length,float delay)
{
    unsigned d=(unsigned)delay;float f=delay-(float)d,a=r16_line_read(w,line,offset,length,d);
    return a+f*(r16_line_read(w,line,offset,length,d+1u)-a);
}
#if !(defined(__arm__) && defined(GP5_FIRMWARE))
/* Host-only: the independent per-sample reference with the current wet voicing, as the
 * bitwise reference for the unified 27a body. */
float frost_16_rv6_sample_reference(FrostReverbModel16 *s,float x,const float *p)
{
    FrostReverb16 *w=s->ws;FrostReverbModelState *m=&w->model;
    unsigned mode=(unsigned)(p[3]+0.5f),i,off=0,offs[4];const Rv6Mode16 *c;
    float in,r[4],sum,wet,a,tone;
    if(mode>=FROST_RV6_MODES)mode=FROST_RV6_HALL;
    if(mode!=m->mode){r16_model_clear(m);m->mode=mode;m->z[8]=-1.0f;}
    c=&rv6_modes16[mode];
    if(p[2]!=m->z[8]){float rt=c->rt_min*frost_exp2(c->rt_log2*p[2]*0.01f);m->z[8]=p[2];
        for(i=0;i<4;++i)m->z[4+i]=frost_exp2(-9.9657843f*(float)c->fdn[i]/(44100.0f*rt));}
    tone=p[1]*0.01f;a=frost_onepole(frost_clampf(1500.0f*frost_exp2(3.0f*tone)*c->tone,500.0f,15000.0f));
    in=0.35f*x+m->z[12];
    in=frost_reverb_send(&m->z[16],in);
    for(i=0;i<4;++i){offs[i]=off;off+=c->fdn[i];}
    for(i=0;i<4 && c->ap[i];++i){
        unsigned L=c->ap[i],line=4u+i;float d=r16_line_read(w,line,off,L,L),y=d-c->ap_gain*in;
        r16_line_write(w,line,off,L,in+c->ap_gain*y);in=y;off+=L;
    }
    for(;i<4;++i)off+=c->ap[i];
    for(i=0;i<4;++i){
        float rd=c->mod!=0.0f && !(i&1u)?
            r16_line_read_frac(w,i,offs[i],c->fdn[i],(float)c->fdn[i]-1.0f-c->mod*(1.0f+frost_sin01(m->z[10+(i>>1)]))):
            r16_line_read(w,i,offs[i],c->fdn[i],c->fdn[i]);
        r[i]=frost_lp(&m->z[i],rd,a)*m->z[4+i];
        r[i]=frost_reverb_bass(&m->z[18+i],r[i],1.0f-m->z[4+i]);
    }
    if(c->mod!=0.0f){m->z[10]=frost_phase_advance(m->z[10],0.7f);m->z[11]=frost_phase_advance(m->z[11],0.93f);}
    sum=0.5f*(r[0]+r[1]+r[2]+r[3]);
    for(i=0;i<4;++i)r16_line_write(w,i,offs[i],c->fdn[i],r[i]-sum+in);
    wet=0.6f*(r[0]-r[1]+r[2]-r[3]);
    m->z[12]=0.0f;
    if(c->pitch){
        unsigned L=c->pitch,line=8u;float ph=m->z[13],q=ph+0.5f,g=frost_sin01(0.5f*ph),shift;
        if(q>=1.0f)q-=1.0f;
        g*=g;
        shift=g*r16_line_read_frac(w,line,off,L,2.0f+SHIMMER_WINDOW*(1.0f-ph))+(1.0f-g)*r16_line_read_frac(w,line,off,L,2.0f+SHIMMER_WINDOW*(1.0f-q));
        r16_line_write(w,line,off,L,wet);
        m->z[13]=ph+1.0f/SHIMMER_WINDOW;if(m->z[13]>=1.0f)m->z[13]-=1.0f;
        shift=frost_hp(&m->z[14],shift,frost_onepole(250.0f));
        m->z[12]=(0.25f+0.2f*p[2]*0.01f)*shift;wet+=0.7f*shift;
    }
    wet=frost_hp(&m->z[15],wet,frost_onepole(60.0f));
    return frost_rail(x+1.4f*p[0]*0.01f*wet,8.0f);
}
#endif
/* 27b: no fused multiply-add contraction in the target RV-6 code, so the
 * target evaluates exactly the IEEE single-precision operations written here
 * (as the host does). The host test proves the block/steady paths bitwise
 * equal to the per-sample reference under these rules. Versus 26b (whose
 * compiler fused some products) RV-6 output differs only by rounding. */
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
/* 27b: one RV-6 body for both forms. frost_16_rv6_block(s,x,out,n,p) produces,
 * bit for bit, what n successive per-sample calls with the same p produce (the
 * original per-sample implementation is kept as a host-only reference and
 * compared bitwise by the tests). Mode/TIME caches can only change on a block's
 * first sample because p is constant, so they are handled once; tone and HP
 * coefficients are pure functions of constants and are hoisted.
 * Validity counters are replaced by zero-filling the mode's memory whenever the
 * model is cleared (reset or mode change): a read of a never-written slot then
 * returns the same +0.0f the counter check returned (no RV-6 read uses delay 0
 * or a delay beyond its line), so outputs are identical. */
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA float r16_rv6_get(const int16_t *b,unsigned pos,unsigned length,unsigned delay)
{if(!delay)return 0.0f;return frost_reverb16_load(b[pos>=delay?pos-delay:pos+length-delay],1.0f/4096.0f);}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA FROST_NOINLINE FROST_COLD float r16_rv6_get_frac(const int16_t *b,unsigned pos,unsigned length,float delay)
{
    unsigned d=(unsigned)delay;float f=delay-(float)d,a=r16_rv6_get(b,pos,length,d);
    return a+f*(r16_rv6_get(b,pos,length,d+1u)-a);
}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA void r16_rv6_put(int16_t *b,unsigned *pos,unsigned length,float v,float *error)
{b[*pos]=frost_reverb16_quantize_fast(frost_rail(v,8.0f),4096.0f,error);if(++*pos==length)*pos=0;}
/* SHIMMER octave feedback (out of line: bounds DSP flash; same arithmetic). */
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA FROST_NOINLINE FROST_COLD float r16_rv6_shimmer(int16_t *b,unsigned *pos,unsigned L,float *z,float wet,float hp250,float time,float *error)
{
    float ph=z[13],q=ph+0.5f,g=frost_sin01(0.5f*ph),shift;
    if(q>=1.0f)q-=1.0f;
    g*=g;
    shift=g*r16_rv6_get_frac(b,*pos,L,2.0f+SHIMMER_WINDOW*(1.0f-ph))+(1.0f-g)*r16_rv6_get_frac(b,*pos,L,2.0f+SHIMMER_WINDOW*(1.0f-q));
    r16_rv6_put(b,pos,L,wet,error);
    z[13]=ph+1.0f/SHIMMER_WINDOW;if(z[13]>=1.0f)z[13]-=1.0f;
    shift=frost_hp(&z[14],shift,hp250);
    z[12]=(0.25f+0.2f*time*0.01f)*shift;return wet+0.7f*shift;
}
/* MOD chorused read tap (out of line). */
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA FROST_NOINLINE FROST_COLD float r16_rv6_mod_read(const int16_t *b,unsigned pos,unsigned length,float mod,float phase)
{return r16_rv6_get_frac(b,pos,length,(float)length-1.0f-mod*(1.0f+frost_sin01(phase)));}
/* ROOM/HALL/PLATE (no modulated or pitch taps): every line is read exactly at
 * its length, i.e. at the slot about to be written, so each sample reads and
 * then writes the same slot of each line, as the per-sample reference does.
 * 28a: the block is cut into stretches in which no line wraps; inside a
 * stretch every line is a plain array indexed by k (no wrap tests, no copies).
 * Same arithmetic, same order, same railed stores: bit-identical. */
/* Bit-exact frost_rail(v,8.0f) on the IEEE bits: |v|>8 (finite or infinite)
 * becomes +/-8 with v's sign; NaN, +/-0 and |v|<=8 pass unchanged. Integer
 * compares avoid the FPU compare/status transfers of the float form. */
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA uint32_t r16_rv6_rail_bits(uint32_t u)
{uint32_t a=u&0x7fffffffu;if(a>0x41000000u && a<=0x7f800000u)u=(u&0x80000000u)|0x41000000u;return u;}
typedef union { float f; uint32_t u; } Rv6Bits16;
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA float r16_rv6_rail(float v)
{Rv6Bits16 w;w.f=v;w.u=r16_rv6_rail_bits(w.u);return w.f;}
FROST_HOT FROST_CORE FROST_NOINLINE FROST_DSP_RAM_NOFMA FROST_COLD void r16_rv6_steady(int16_t *samples,const unsigned *offs,const unsigned *len,unsigned lines,
                                               unsigned *pos,float *z,float *error,float a,float ap_gain,float hp60,float lvl,
                                               const float *x,float *out,unsigned count)
{
    float l0=z[0],l1=z[1],l2=z[2],l3=z[3],g0=z[4],g1=z[5],g2=z[6],g3=z[7],h=z[15],fb=z[12];
    int16_t *ap[4];unsigned base,n,i,k,nap=lines-4u;
    for(base=0;base<count;base+=n){
        int16_t *f0,*f1,*f2,*f3;const float *xs=x+base;float *os=out+base;
        n=count-base;
        for(i=0;i<lines;++i)if(len[i]-pos[i]<n)n=len[i]-pos[i];   /* >=1: pos<len */
        f0=samples+offs[0]+pos[0];f1=samples+offs[1]+pos[1];
        f2=samples+offs[2]+pos[2];f3=samples+offs[3]+pos[3];
        for(i=0;i<nap;++i)ap[i]=samples+offs[4+i]+pos[4+i];
        for(k=0;k<n;++k){
            float in=0.35f*xs[k]+fb,r0,r1,r2,r3,sum,wet;
            in=frost_reverb_send(&z[16],in);
            for(i=0;i<nap;++i){float d=frost_reverb16_load(ap[i][k],1.0f/4096.0f),y=d-ap_gain*in;ap[i][k]=frost_reverb16_quantize_fast(r16_rv6_rail(in+ap_gain*y),4096.0f,&error[4+i]);in=y;}
            r0=frost_lp(&l0,frost_reverb16_load(f0[k],1.0f/4096.0f),a)*g0;
            r1=frost_lp(&l1,frost_reverb16_load(f1[k],1.0f/4096.0f),a)*g1;
            r2=frost_lp(&l2,frost_reverb16_load(f2[k],1.0f/4096.0f),a)*g2;
            r3=frost_lp(&l3,frost_reverb16_load(f3[k],1.0f/4096.0f),a)*g3;
            {
                r0=frost_reverb_bass(&z[18],r0,1.0f-g0);r1=frost_reverb_bass(&z[19],r1,1.0f-g1);
                r2=frost_reverb_bass(&z[20],r2,1.0f-g2);r3=frost_reverb_bass(&z[21],r3,1.0f-g3);
            }
            sum=0.5f*(r0+r1+r2+r3);
            f0[k]=frost_reverb16_quantize_fast(r16_rv6_rail(r0-sum+in),4096.0f,&error[0]);f1[k]=frost_reverb16_quantize_fast(r16_rv6_rail(r1-sum+in),4096.0f,&error[1]);
            f2[k]=frost_reverb16_quantize_fast(r16_rv6_rail(r2-sum+in),4096.0f,&error[2]);f3[k]=frost_reverb16_quantize_fast(r16_rv6_rail(r3-sum+in),4096.0f,&error[3]);
            wet=0.6f*(r0-r1+r2-r3);
            fb=0.0f;
            wet=frost_hp(&h,wet,hp60);
            os[k]=r16_rv6_rail(xs[k]+lvl*wet);   /* 31o: == frost_rail(v,8.0f) */
        }
        for(i=0;i<lines;++i){pos[i]+=n;if(pos[i]==len[i])pos[i]=0;}
    }
    z[0]=l0;z[1]=l1;z[2]=l2;z[3]=l3;z[15]=h;z[12]=fb;
}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA FROST_CORE FROST_NOINLINE FROST_COLD void frost_16_rv6_block(FrostReverbModel16 *s,const float *x,float *out,unsigned count,const float *p)
{
    FrostReverb16 *w=s->ws;FrostReverbModelState *m=&w->model;
    unsigned mode=(unsigned)(p[3]+0.5f),i,k,off=0,nap,fresh=0,offs[9],len[9],pos[9];const Rv6Mode16 *c;
    float a,z[22u],ap_gain,hp250,hp60,lvl,mod,tone;
    if(!count)return;
    if(mode>=FROST_RV6_MODES)mode=FROST_RV6_HALL;
    if(mode!=m->mode){r16_model_clear(m);m->mode=mode;m->z[8]=-1.0f;fresh=1;}
    c=&rv6_modes16[mode];
    if(p[2]!=m->z[8]){float rt=c->rt_min*frost_exp2(c->rt_log2*p[2]*0.01f);m->z[8]=p[2];
        for(i=0;i<4;++i)m->z[4+i]=frost_exp2(-9.9657843f*(float)c->fdn[i]/(44100.0f*rt));}
    tone=p[1]*0.01f;a=frost_onepole(frost_clampf(1500.0f*frost_exp2(3.0f*tone)*c->tone,500.0f,15000.0f));
    for(i=0;i<4;++i){offs[i]=off;len[i]=c->fdn[i];off+=c->fdn[i];}
    for(nap=0;nap<4 && c->ap[nap];++nap){offs[4+nap]=off;len[4+nap]=c->ap[nap];off+=c->ap[nap];}
    for(i=nap;i<4;++i){offs[4+i]=off;len[4+i]=1u;off+=c->ap[i];}
    offs[8]=off;len[8]=c->pitch;
    /* off+pitch == frost_16_rv6_mode_samples(mode): the whole mode partition. */
    /* 31y5: the fresh clear writes 2 samples per store (was one per store in
     * the first block of a new mode: a long block). Same zeros. */
    if(fresh){typedef uint32_t __attribute__((may_alias)) w32;w32 *q=(w32 *)(void *)w->samples;unsigned nw=(off+len[8])>>1;
        for(i=0;i<nw;++i)q[i]=0;if((off+len[8])&1u)w->samples[off+len[8]-1u]=0;for(i=0;i<12;++i)w->error[i]=0.0f;}
    for(i=0;i<9;++i)pos[i]=m->position[i];
    for(i=0;i<22u;++i)z[i]=m->z[i];
    ap_gain=c->ap_gain;hp250=frost_onepole(250.0f);hp60=frost_onepole(60.0f);lvl=1.4f*p[0]*0.01f;mod=c->mod;
    if(mod==0.0f && !c->pitch)r16_rv6_steady(w->samples,offs,len,4u+nap,pos,z,w->error,a,ap_gain,hp60,lvl,x,out,count);
    else for(k=0;k<count;++k){
        float in=0.35f*x[k]+z[12],r[4],sum,wet;
        in=frost_reverb_send(&z[16],in);
        for(i=0;i<nap;++i){
            int16_t *b=w->samples+offs[4+i];unsigned L=len[4+i];
            float d=r16_rv6_get(b,pos[4+i],L,L),y=d-ap_gain*in;
            r16_rv6_put(b,&pos[4+i],L,in+ap_gain*y,&w->error[4+i]);in=y;
        }
        for(i=0;i<4;++i){
            const int16_t *b=w->samples+offs[i];
            float rd=mod!=0.0f && !(i&1u)?
                r16_rv6_mod_read(b,pos[i],len[i],mod,z[10+(i>>1)]):
                r16_rv6_get(b,pos[i],len[i],len[i]);
            r[i]=frost_lp(&z[i],rd,a)*z[4+i];
            r[i]=frost_reverb_bass(&z[18+i],r[i],1.0f-z[4+i]);
        }
        if(mod!=0.0f){z[10]=frost_phase_advance(z[10],0.7f);z[11]=frost_phase_advance(z[11],0.93f);}
        sum=0.5f*(r[0]+r[1]+r[2]+r[3]);
        for(i=0;i<4;++i)r16_rv6_put(w->samples+offs[i],&pos[i],len[i],r[i]-sum+in,&w->error[i]);
        wet=0.6f*(r[0]-r[1]+r[2]-r[3]);
        z[12]=0.0f;
        if(c->pitch)wet=r16_rv6_shimmer(w->samples+offs[8],&pos[8],len[8],z,wet,hp250,p[2],&w->error[8]);
        wet=frost_hp(&z[15],wet,hp60);
        out[k]=frost_rail(x[k]+lvl*wet,8.0f);
    }
    for(i=0;i<9;++i)m->position[i]=pos[i];
    for(i=0;i<22u;++i)m->z[i]=z[i];
}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA FROST_CORE float frost_16_rv6_sample(FrostReverbModel16 *s,float x,const float *p)
{float y;frost_16_rv6_block(s,&x,&y,1u,p);return y;}
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(__GNUC__)
#pragma GCC pop_options
#endif
/* ---------------- 6G15-style spring ---------------------------------------
 * DWELL drives a saturating, bright (pre-emphasized) tube driver into a
 * two-spring tank. Each spring is a ~35/41 ms delay loop containing a cascade
 * of 12 stretched first-order allpass sections A(z^5), the parametric
 * dispersion approach of Valimaki, Parker and Abel (JAES 2010) used to make
 * the chirped echoes ("drip") of real springs, plus a ~4.5 kHz loss filter,
 * 100 Hz high-pass and a fixed ~1.8 s nominal tank decay (legacy boot copy: ~2.7 s). Recovery: 150 Hz HP; TONE
 * is the passive high cut after recovery (2..10 kHz, higher = brighter); MIX
 * (the 6G15 MIXER) adds the wet spring to the unity dry signal. */
#define SPRING_A 1543u
#define SPRING_B 1811u
#define SPRING_AP_BASE (SPRING_A+SPRING_B)
#define SPRING_AP_SAMPLES (2u*FROST_SPRING_STAGES*FROST_SPRING_STRETCH)
unsigned frost_16_spring_samples(void){return SPRING_AP_BASE+SPRING_AP_SAMPLES;}
void frost_16_spring_reset(FrostReverbModel16 *s){if(s->ws){r16_model_clear(&s->ws->model);s->ws->model.mode=255u;for(unsigned i=0;i<12;++i)s->ws->error[i]=0.0f;}}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA float r16_spring_chain(FrostReverb16 *w,unsigned spring,float x,float coef)
{
    /* Canonical stretched allpass: v[n]=x[n]-a v[n-K], y[n]=a v[n]+v[n-K]. */
    float *v=w->spring_allpass+spring*FROST_SPRING_STAGES*FROST_SPRING_STRETCH;
    unsigned k,i=w->model.index;
    FROST_UNROLL(2)
    for(k=0;k<FROST_SPRING_STAGES;++k,v+=FROST_SPRING_STRETCH){
        float old=v[i],nv=x-coef*old;v[i]=nv;x=coef*nv+old;
    }
    return x;
}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA FROST_NOINLINE float frost_16_spring_sample(FrostReverbModel16 *s,float x,const float *p)
{
    FrostReverb16 *w=s->ws;FrostReverbModelState *m=&w->model;
    float d=p[0]*0.01f,drive,in,ra,rb,ca,cb,wet,a=frost_onepole(4500.0f);
    if(m->mode!=254u){
        /* Allpass memory has no validity counters: clear it once (120 floats). */
        unsigned i;r16_model_clear(m);m->mode=254u;
        for(i=0;i<SPRING_AP_SAMPLES;++i)w->spring_allpass[i]=0.0f;for(i=0;i<12;++i)w->error[i]=0.0f;
        m->z[8]=-1.0f;   /* 31o: TONE cache key; TONE is never negative */
    }
    /* 31o: the TONE low-pass coefficient is recomputed only when TONE changes
     * (same expression, so the cached value is bit-identical). */
    if(p[2]!=m->z[8]){m->z[8]=p[2];m->z[9]=frost_onepole(2000.0f*frost_exp2(2.32f*p[2]*0.01f));}
    drive=0.4f+6.0f*d*d;
    in=x+0.8f*frost_hp(&m->z[0],x,frost_onepole(1000.0f));
    in=0.5f*frost_soft_clip(in*drive)/(0.4f+0.6f*d);
    {in=frost_hp(&m->z[7],in,frost_onepole(180.0f));in=frost_hp(&m->z[10],in,frost_onepole(180.0f));}
    ra=r16_line_read(w,0,0,SPRING_A,SPRING_A);rb=r16_line_read(w,1,SPRING_A,SPRING_B,SPRING_B);
    ca=frost_hp(&m->z[3],frost_lp(&m->z[1],r16_spring_chain(w,0,ra,0.62f),a),frost_onepole(100.0f));
    cb=frost_hp(&m->z[4],frost_lp(&m->z[2],r16_spring_chain(w,1,rb,0.66f),a),frost_onepole(100.0f));
    if(++m->index==FROST_SPRING_STRETCH)m->index=0;
    r16_line_write(w,0,0,SPRING_A,in+0.8905f*ca);
    r16_line_write(w,1,SPRING_A,SPRING_B,in+0.8725f*cb);
    wet=frost_hp(&m->z[5],ca+cb,frost_onepole(150.0f));
    wet=frost_lp(&m->z[6],wet,m->z[9]);
    return frost_rail(x+1.4f*p[1]*0.01f*wet,8.0f);
}
