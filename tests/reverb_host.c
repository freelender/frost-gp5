#include "framework_host.c"
#include "../engine/control_catalog.h"
static struct {unsigned before;FrostReverb reverb;unsigned after;} memory[2];
static FrostDelay delays[2];
static FrostModulation mods[2][FROST_MOD_WORKSPACES];
API int rv_bind(unsigned c){return frost_chain_bind_reverb(&chains[c],&memory[c].reverb);}
API int rv_unbind(unsigned c){return frost_chain_bind_reverb(&chains[c],0);}
API void rv_bind_others(unsigned c){(void)frost_chain_bind_delay(&chains[c],&delays[c]);(void)frost_chain_bind_modulation(&chains[c],mods[c]);}
API void rv_poison(unsigned c)
{unsigned i;memory[c].before=0x12345678u;memory[c].after=0x87654321u;
 for(i=0;i<FROST_REVERB_SAMPLES;++i){union {unsigned u;float f;} v;v.u=0x7fc00000u;memory[c].reverb.samples[i]=v.f;}}
API void rv_reset(unsigned c){frost_reverb_reset(&memory[c].reverb);}
API unsigned rv_valid(unsigned c,unsigned line){return memory[c].reverb.valid[line];}
API unsigned rv_word(unsigned c,unsigned i)
{union {float f;unsigned u;} v;v.f=memory[c].reverb.samples[i];return v.u;}
API float rv_damped(unsigned c,unsigned i){return memory[c].reverb.damped[i];}
API int rv_guard(unsigned c){return memory[c].before==0x12345678u && memory[c].after==0x87654321u;}
API unsigned rv_bytes(void){return sizeof(FrostReverb);}
API float rv_sample(unsigned c,float x,float decay,float damp,float mix)
{float p[3]={decay,damp,mix};return frost_reverb_sample(&memory[c].reverb,x,p);}
API int rv_policy(unsigned c,unsigned slot,unsigned p){return frost_chain_set_bypass_policy(&chains[c],slot,p);}
API int rv_meta(unsigned p,unsigned field)
{const FrostControlParameter *d=&frost_control_find(FROST_REVERB)->p[p];return field==0?d->minimum:field==1?d->maximum:field==2?d->initial:d->scale;}
API float rv_bound(unsigned p,unsigned field)
{const FrostParameter *d=&frost_effect_find(FROST_REVERB)->parameters[p];return field==0?d->minimum:field==1?d->maximum:d->initial;}

/* 26a: frost_reverb_block (warm-up and steady paths) must equal
 * frost_reverb_sample bit for bit. Returns -1, or the first mismatching index. */
static FrostReverb rv_a,rv_b;
API int rv_block_matches_sample(unsigned total,unsigned seed)
{
    unsigned i=0,k,n;uint32_t r=seed*2654435761u+1u;
    float in[FROST_REVERB_CHUNK],ya[FROST_REVERB_CHUNK],p[FROST_REVERB_CHUNK][3];
    frost_reverb_reset(&rv_a);frost_reverb_reset(&rv_b);
    while(i<total){
        r=r*1664525u+1013904223u;n=1u+(r>>28)%FROST_REVERB_CHUNK;if(n>total-i)n=total-i;
        for(k=0;k<n;++k){
            float t=(float)(i+k);
            in[k]=0.3f*(float)((int)((i+k)*7919u%2001u)-1000)/1000.0f;
            p[k][0]=0.5f+0.4f*(float)((i+k)/997u%3u)/2.0f+0.0001f*(float)(k&1);
            p[k][1]=0.25f+0.5f*(float)((i+k)/1511u%2u);
            p[k][2]=0.35f+0.00001f*t;
        }
        for(k=0;k<n;++k)ya[k]=frost_reverb_sample(&rv_a,in[k],p[k]);
        frost_reverb_block(&rv_b,in,in,n,(const float (*)[3])p);
        for(k=0;k<n;++k)if(memcmp(&ya[k],&in[k],sizeof(float)))return (int)(i+k);
        i+=n;
    }
    for(k=0;k<FROST_REVERB_SAMPLES;++k)if(memcmp(&rv_a.samples[k],&rv_b.samples[k],sizeof(float)))return 1000000+(int)k;
    return -1;
}
