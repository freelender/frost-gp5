#include "reverb.h"
#include "../../engine/hot_code.h"
#include "voicing.h"
FROST_HOT_TABLE const unsigned frost_reverb_lengths[FROST_REVERB_LINES]={1009u,1153u,1289u,1423u,149u,211u};
FROST_HOT static float reverb_limit(float x)
{return x>16.0f?16.0f:x< -16.0f?-16.0f:x;}
void frost_reverb_reset(FrostReverb *s)
{
    unsigned i;
    for(i=0;i<FROST_REVERB_LINES;++i)s->position[i]=s->valid[i]=0;
    for(i=0;i<FROST_REVERB_COMBS;++i)s->damped[i]=0.0f;
    if(FROST_REVERB_VOICING)for(i=0;i<6;++i)s->model.z[i]=0.0f;
}
FROST_HOT static float reverb_read(const FrostReverb *s,unsigned line,unsigned offset)
{return s->valid[line]<frost_reverb_lengths[line]?0.0f:s->samples[offset+s->position[line]];}
FROST_HOT static void reverb_write(FrostReverb *s,unsigned line,unsigned offset,float value)
{
    unsigned length=frost_reverb_lengths[line];
    s->samples[offset+s->position[line]]=reverb_limit(value);
    if(++s->position[line]==length)s->position[line]=0;
    if(s->valid[line]<length)++s->valid[line];
}
#if defined(__arm__) && defined(GP5_FIRMWARE)
/* 27b target: the block form is bitwise equal to this function for every
 * chunk size, including 1 (host test rv_block_matches_sample against the
 * original below); one copy of the network code bounds DSP flash. */
FROST_HOT float frost_reverb_sample(FrostReverb *s,float input,const float *p)
{float y;frost_reverb_block(s,&input,&y,1u,(const float (*)[3])p);return y;}
#else
FROST_HOT float frost_reverb_sample(FrostReverb *s,float input,const float *p)
{
    unsigned i,offset=0;float dry=input,wet=0.0f,a=1.0f-0.95f*p[1];
    if(FROST_REVERB_VOICING)input=frost_reverb_send(s->model.z,input);
    for(i=0;i<FROST_REVERB_COMBS;++i){
        float delayed=reverb_read(s,i,offset);
        s->damped[i]+=a*(delayed-s->damped[i]);
        float feedback=s->damped[i];
        if(FROST_REVERB_VOICING)feedback=frost_reverb_bass(&s->model.z[2+i],feedback,1.0f-p[0]);
        reverb_write(s,i,offset,0.25f*input+p[0]*feedback);
        wet+=delayed;offset+=frost_reverb_lengths[i];
    }
    wet*=0.25f;
    for(;i<FROST_REVERB_LINES;++i){
        float delayed=reverb_read(s,i,offset),output=delayed-0.5f*wet;
        reverb_write(s,i,offset,wet+0.5f*output);
        wet=output;offset+=frost_reverb_lengths[i];
    }
    wet=reverb_limit(wet);
    return dry+p[2]*(wet-dry);
}
#endif
/* 26a steady state: once every line has filled (valid==length, which then
 * never changes), the per-sample warm-up branches are dead and the ring wrap
 * happens at most once per chunk per line. Same per-sample arithmetic in the
 * same order as below, so outputs and state are bit-identical. */
FROST_HOT static int reverb_steady(const FrostReverb *s)
{
    unsigned i;
    for(i=0;i<FROST_REVERB_LINES;++i)if(s->valid[i]<frost_reverb_lengths[i])return 0;
    return 1;
}
/* 30b: executes from VARS RAM (FROST_DSP_RAM). Its delay lines are in LARGE, like
 * RV-6's, whose loop measured ~30% faster from RAM on the pedal (30a). */
FROST_HOT FROST_REVERB_FLOAT_DSP static void reverb_block_steady(FrostReverb *s,const float *in,float *out,unsigned n,const float (*p)[3])
{
    float wet[FROST_REVERB_CHUNK],send[FROST_REVERB_CHUNK];unsigned i,k,offset=0;
    for(k=0;k<n;++k){wet[k]=0.0f;send[k]=FROST_REVERB_VOICING?frost_reverb_send(s->model.z,in[k]):in[k];}
    for(i=0;i<FROST_REVERB_COMBS;++i){
        const unsigned length=frost_reverb_lengths[i];
        float *line=s->samples+offset,damped=s->damped[i];
        unsigned position=s->position[i];
        for(k=0;k<n;){
            unsigned end=k+(length-position);float *q=line+position;
            if(end>n)end=n;
            position+=end-k;
            for(;k<end;++k,++q){
                float a=1.0f-0.95f*p[k][1],delayed=*q;
                damped+=a*(delayed-damped);
                float feedback=damped;
                if(FROST_REVERB_VOICING)feedback=frost_reverb_bass(&s->model.z[2+i],feedback,1.0f-p[k][0]);
                *q=reverb_limit(0.25f*send[k]+p[k][0]*feedback);
                wet[k]+=delayed;
            }
            if(position==length)position=0;
        }
        s->damped[i]=damped;s->position[i]=position;offset+=length;
    }
    for(k=0;k<n;++k)wet[k]*=0.25f;
    for(;i<FROST_REVERB_LINES;++i){
        const unsigned length=frost_reverb_lengths[i];
        float *line=s->samples+offset;unsigned position=s->position[i];
        for(k=0;k<n;){
            unsigned end=k+(length-position);float *q=line+position;
            if(end>n)end=n;
            position+=end-k;
            for(;k<end;++k,++q){
                float delayed=*q,output=delayed-0.5f*wet[k];
                *q=reverb_limit(wet[k]+0.5f*output);
                wet[k]=output;
            }
            if(position==length)position=0;
        }
        s->position[i]=position;offset+=length;
    }
    for(k=0;k<n;++k){float w=reverb_limit(wet[k]);out[k]=in[k]+p[k][2]*(w-in[k]);}
}
FROST_HOT FROST_REVERB_FLOAT_AUDIO void frost_reverb_block(FrostReverb *s,const float *in,float *out,unsigned n,const float (*p)[3])
{
    float wet[FROST_REVERB_CHUNK],send[FROST_REVERB_CHUNK];unsigned i,k,offset=0;
    if(reverb_steady(s)){reverb_block_steady(s,in,out,n,p);return;}
    for(k=0;k<n;++k){wet[k]=0.0f;send[k]=FROST_REVERB_VOICING?frost_reverb_send(s->model.z,in[k]):in[k];}
    for(i=0;i<FROST_REVERB_COMBS;++i){
        const unsigned length=frost_reverb_lengths[i];
        float *line=s->samples+offset,damped=s->damped[i];
        unsigned position=s->position[i],valid=s->valid[i];
        for(k=0;k<n;++k){
            float a=1.0f-0.95f*p[k][1],delayed=valid<length?0.0f:line[position];
            damped+=a*(delayed-damped);
            float feedback=damped;
            if(FROST_REVERB_VOICING)feedback=frost_reverb_bass(&s->model.z[2+i],feedback,1.0f-p[k][0]);
            line[position]=reverb_limit(0.25f*send[k]+p[k][0]*feedback);
            if(++position==length)position=0;
            if(valid<length)++valid;
            wet[k]+=delayed;
        }
        s->damped[i]=damped;s->position[i]=position;s->valid[i]=valid;offset+=length;
    }
    for(k=0;k<n;++k)wet[k]*=0.25f;
    for(;i<FROST_REVERB_LINES;++i){
        const unsigned length=frost_reverb_lengths[i];
        float *line=s->samples+offset;unsigned position=s->position[i],valid=s->valid[i];
        for(k=0;k<n;++k){
            float delayed=valid<length?0.0f:line[position],output=delayed-0.5f*wet[k];
            line[position]=reverb_limit(wet[k]+0.5f*output);
            if(++position==length)position=0;
            if(valid<length)++valid;
            wet[k]=output;
        }
        s->position[i]=position;s->valid[i]=valid;offset+=length;
    }
    for(k=0;k<n;++k){float w=reverb_limit(wet[k]);out[k]=in[k]+p[k][2]*(w-in[k]);}
}
