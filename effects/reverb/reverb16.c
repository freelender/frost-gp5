/* Compact alternate backend derived from the float network; the original
 * implementation stays intact for GP-5 boot layout and reference checks. */
#include "reverb16.h"
#include "../../engine/hot_code.h"
#include "voicing.h"
/* Saturate before the C float-to-int conversion; NaN is never converted.
 * The truncating quantizer prevents rounded fixed-point DC limit cycles. */
FROST_LARGE_ONLY float frost_reverb16_load(int16_t v,float inverse_scale)
{return (float)v*inverse_scale;}
FROST_LARGE_ONLY int16_t frost_reverb16_store(float v,float scale)
{float q=v*scale;if(q!=q)return 0;if(q>32767.0f)q=32767.0f;if(q< -32768.0f)q= -32768.0f;return (int16_t)q;}
FROST_LARGE_ONLY int16_t frost_reverb16_quantize(float v,float scale,float *error)
{
    float q=v*scale;int16_t n;
    /* Below one storage LSB, discard residual rather than circulating a
     * quantized tail forever. This is an explicit low-level tail cutoff. */
    if(q> -1.0f && q<1.0f){*error=0.0f;return 0;}
    q+=*error;
    if(q!=q || q>32767.0f || q< -32768.0f){*error=0.0f;return frost_reverb16_store(q,1.0f);}
    n=frost_reverb16_store(q+(q>=0.0f?0.5f:-0.5f),1.0f);
    *error=q-(float)n;return n;
}


FROST_HOT FROST_LARGE_ONLY FROST_NOFMA float reverb16_limit(float x)
{
    union {float f;uint32_t u;} v;uint32_t magnitude;v.f=x;
    magnitude=v.u&0x7fffffffu;
    if(magnitude>0x41800000u && magnitude<=0x7f800000u)
        v.u=(v.u&0x80000000u)|0x41800000u;
    return v.f;
}
void frost_reverb16_reset(FrostReverb16 *s)
{
    unsigned i;
    for(i=0;i<FROST_REVERB_LINES;++i)s->position[i]=s->valid[i]=0;
    for(i=0;i<FROST_REVERB_COMBS;++i)s->damped[i]=0.0f;
    for(i=0;i<12;++i)s->error[i]=0.0f;
}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA float reverb16_read(const FrostReverb16 *s,unsigned line,unsigned offset)
{return s->valid[line]<frost_reverb_lengths[line]?0.0f:frost_rv16f_get(s->samples[offset+s->position[line]]);}
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA void reverb16_write(FrostReverb16 *s,unsigned line,unsigned offset,float value)
{
    unsigned length=frost_reverb_lengths[line];
    s->samples[offset+s->position[line]]=frost_rv16f_put(reverb16_limit(value),&s->error[line]);
    if(++s->position[line]==length)s->position[line]=0;
    if(s->valid[line]<length)++s->valid[line];
}
#if defined(__arm__) && defined(GP5_FIRMWARE)
/* 27b target: the block form is bitwise equal to this function for every
 * chunk size, including 1 (host test rv_block_matches_sample against the
 * original below); one copy of the network code bounds DSP flash. */
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA float frost_reverb16_sample(FrostReverb16 *s,float input,const float *p)
{float y;frost_reverb16_block(s,&input,&y,1u,(const float (*)[3])p);return y;}
#else
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA float frost_reverb16_sample(FrostReverb16 *s,float input,const float *p)
{
    unsigned i,offset=0;float dry=input,wet=0.0f,a=1.0f-0.95f*p[1];
    /* Reset spare voicing state on first use; keep boot reset code frozen. */
    if(!s->valid[0])for(i=0;i<6;++i)s->model.z[i]=0.0f;
    input=frost_reverb_send(s->model.z,input);
    for(i=0;i<FROST_REVERB_COMBS;++i){
        float delayed=reverb16_read(s,i,offset);
        s->damped[i]+=a*(delayed-s->damped[i]);
        float feedback=s->damped[i];
        feedback=frost_reverb_bass(&s->model.z[2+i],feedback,1.0f-p[0]);
        reverb16_write(s,i,offset,0.25f*input+p[0]*feedback);
        wet+=delayed;offset+=frost_reverb_lengths[i];
    }
    wet*=0.25f;
    for(;i<FROST_REVERB_LINES;++i){
        float delayed=reverb16_read(s,i,offset),output=delayed-0.5f*wet;
        reverb16_write(s,i,offset,wet+0.5f*output);
        wet=output;offset+=frost_reverb_lengths[i];
    }
    wet=reverb16_limit(wet);
    return dry+p[2]*(wet-dry);
}
#endif
/* 26a steady state: once every line has filled (valid==length, which then
 * never changes), the per-sample warm-up branches are dead and the ring wrap
 * happens at most once per chunk per line. Same per-sample arithmetic in the
 * same order as below, so outputs and state are bit-identical. */
FROST_HOT FROST_LARGE_ONLY FROST_NOFMA int reverb16_steady(const FrostReverb16 *s)
{
    unsigned i;
    for(i=0;i<FROST_REVERB_LINES;++i)if(s->valid[i]<frost_reverb_lengths[i])return 0;
    return 1;
}
/* Size-optimized loops bound LARGE code after adding the wet filters.
 * Arithmetic remains strict and block/sample equivalence is tested. */
/* 30b: executes from VARS RAM (FROST_DSP_RAM). Its delay lines are in LARGE, like
 * RV-6's, whose loop measured ~30% faster from RAM on the pedal (30a). */
FROST_HOT FROST_DSP_RAM_NOFMA FROST_COLD void reverb16_block_steady(FrostReverb16 *s,const float *in,float *out,unsigned n,const float (*p)[3])
{
    float wet[FROST_REVERB_CHUNK],send[FROST_REVERB_CHUNK];unsigned i,k,offset=0;
    for(k=0;k<n;++k){wet[k]=0.0f;send[k]=frost_reverb_send(s->model.z,in[k]);}
    for(i=0;i<FROST_REVERB_COMBS;++i){
        const unsigned length=frost_reverb_lengths[i];
        int16_t *line=s->samples+offset;float damped=s->damped[i];
        unsigned position=s->position[i];
        for(k=0;k<n;){
            unsigned end=k+(length-position);int16_t *q=line+position;
            if(end>n)end=n;
            position+=end-k;
            for(;k<end;++k,++q){
                float a=1.0f-0.95f*p[k][1],delayed=frost_rv16f_get(*q);
                damped+=a*(delayed-damped);
                float feedback=damped;
                feedback=frost_reverb_bass(&s->model.z[2+i],feedback,1.0f-p[k][0]);
                *q=frost_rv16f_put(reverb16_limit(0.25f*send[k]+p[k][0]*feedback),&s->error[i]);
                wet[k]+=delayed;
            }
            if(position==length)position=0;
        }
        s->damped[i]=damped;s->position[i]=position;offset+=length;
    }
    for(k=0;k<n;++k)wet[k]*=0.25f;
    for(;i<FROST_REVERB_LINES;++i){
        const unsigned length=frost_reverb_lengths[i];
        int16_t *line=s->samples+offset;unsigned position=s->position[i];
        for(k=0;k<n;){
            unsigned end=k+(length-position);int16_t *q=line+position;
            if(end>n)end=n;
            position+=end-k;
            for(;k<end;++k,++q){
                float delayed=frost_rv16f_get(*q),output=delayed-0.5f*wet[k];
                *q=frost_rv16f_put(reverb16_limit(wet[k]+0.5f*output),&s->error[i]);
                wet[k]=output;
            }
            if(position==length)position=0;
        }
        s->position[i]=position;offset+=length;
    }
    for(k=0;k<n;++k){float w=reverb16_limit(wet[k]);out[k]=in[k]+p[k][2]*(w-in[k]);}
}
FROST_HOT FROST_AUDIO_RAM_NOFMA FROST_COLD void frost_reverb16_block(FrostReverb16 *s,const float *in,float *out,unsigned n,const float (*p)[3])
{
    float wet[FROST_REVERB_CHUNK],send[FROST_REVERB_CHUNK];unsigned i,k,offset=0;
    /* Reset spare voicing state on first use; keep boot reset code frozen. */
    if(!s->valid[0])for(i=0;i<6;++i)s->model.z[i]=0.0f;
    if(reverb16_steady(s)){reverb16_block_steady(s,in,out,n,p);return;}
    for(k=0;k<n;++k){wet[k]=0.0f;send[k]=frost_reverb_send(s->model.z,in[k]);}
    for(i=0;i<FROST_REVERB_COMBS;++i){
        const unsigned length=frost_reverb_lengths[i];
        int16_t *line=s->samples+offset;float damped=s->damped[i];
        unsigned position=s->position[i],valid=s->valid[i];
        for(k=0;k<n;++k){
            float a=1.0f-0.95f*p[k][1],delayed=valid<length?0.0f:frost_rv16f_get(line[position]);
            damped+=a*(delayed-damped);
            float feedback=damped;
                feedback=frost_reverb_bass(&s->model.z[2+i],feedback,1.0f-p[k][0]);
            line[position]=frost_rv16f_put(reverb16_limit(0.25f*send[k]+p[k][0]*feedback),&s->error[i]);
            if(++position==length)position=0;
            if(valid<length)++valid;
            wet[k]+=delayed;
        }
        s->damped[i]=damped;s->position[i]=position;s->valid[i]=valid;offset+=length;
    }
    for(k=0;k<n;++k)wet[k]*=0.25f;
    for(;i<FROST_REVERB_LINES;++i){
        const unsigned length=frost_reverb_lengths[i];
        int16_t *line=s->samples+offset;unsigned position=s->position[i],valid=s->valid[i];
        for(k=0;k<n;++k){
            float delayed=valid<length?0.0f:frost_rv16f_get(line[position]),output=delayed-0.5f*wet[k];
            line[position]=frost_rv16f_put(reverb16_limit(wet[k]+0.5f*output),&s->error[i]);
            if(++position==length)position=0;
            if(valid<length)++valid;
            wet[k]=output;
        }
        s->position[i]=position;s->valid[i]=valid;offset+=length;
    }
    for(k=0;k<n;++k){float w=reverb16_limit(wet[k]);out[k]=in[k]+p[k][2]*(w-in[k]);}
}
