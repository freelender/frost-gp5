#define FROST_REVERB_INT16 1
#include "../engine/bundle.c"
#include <string.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API
#endif
static struct {unsigned before;FrostReverb w;unsigned after;} rv16_float;
static struct {unsigned before;FrostReverb16 w;unsigned after;} rv16_compact;
API unsigned rv16_bytes(unsigned compact){return compact?sizeof(FrostReverb16):sizeof(FrostReverb);}
API int rv16_store(float x,float scale){return frost_reverb16_store(x,scale);}
API float rv16_load(int value,float scale){return frost_reverb16_load((int16_t)value,scale);}
API float rv16_model_z(unsigned i){return rv16_compact.w.model.z[i];}
API unsigned rv16_half(float x){return frost_rv16f_float_to_half(x);}
API float rv16_half_value(unsigned h){return frost_rv16f_half_to_float(h&0xffffu);}
API int rv16_quantizer_case(float value,float scale,float residual)
{
    float a=residual,b=residual;
    int16_t x=frost_reverb16_quantize(value,scale,&a);
    int16_t y=frost_reverb16_quantize_fast(value,scale,&b);
    return x==y && memcmp(&a,&b,sizeof a)==0;
}
API int rv16_quantizer_sweep(void)
{
    int i;unsigned j,k;const float fractions[]={-.75f,-.5f,-.25f,0,.25f,.5f,.75f};
    const float residuals[]={-.5f,-.25f,-0.0f,0,.25f,.5f};
    for(i=-32769;i<=32769;++i)for(j=0;j<7;++j)for(k=0;k<6;++k){
        float scaled=(float)i+fractions[j];
        if(!rv16_quantizer_case(scaled*(1.0f/2048.0f),2048.0f,residuals[k]))return i+32770;
        if(!rv16_quantizer_case(scaled*(1.0f/4096.0f),4096.0f,residuals[k]))return i+32770;
    }
    return 0;
}
API int rv16_quantizer_random(unsigned count)
{
    uint32_t rng=0x31b00b5u;unsigned i;union {uint32_t u;float f;} value,error,scale;
    for(i=0;i<count;++i){
        rng=rng*1664525u+1013904223u;value.u=rng;
        rng=rng*1664525u+1013904223u;error.u=rng;
        rng=rng*1664525u+1013904223u;scale.u=rng;
        if(!rv16_quantizer_case(value.f,scale.f,error.f))return (int)(i+1);
    }
    return 0;
}
API int rv16_limit_random(unsigned count)
{
    uint32_t rng=0x31c0ffeeu;unsigned i;union {uint32_t u;float f;} value;
    for(i=0;i<count;++i){
        float a,b;rng=rng*1664525u+1013904223u;value.u=rng;
        a=value.f>16.0f?16.0f:value.f< -16.0f?-16.0f:value.f;
        b=reverb16_limit(value.f);
        if(memcmp(&a,&b,sizeof a))return (int)(i+1);
    }
    return 0;
}
API int rv16_render(unsigned compact,unsigned kind,unsigned mode,const float *x,float *out,unsigned n,float level,float tone,float time)
{
    unsigned k,base;float p[4]={level,tone,time,(float)mode};
    FrostReverbModel a={&rv16_float.w};FrostReverbModel16 b={&rv16_compact.w};
    memset(&rv16_float,0xa5,sizeof rv16_float);memset(&rv16_compact,0xa5,sizeof rv16_compact);
    rv16_float.before=rv16_float.after=rv16_compact.before=rv16_compact.after=0x12345678u;
    if(kind==0){
        frost_reverb_reset(a.ws);frost_reverb16_reset(b.ws);
        for(k=0;k<n;++k)out[k]=compact?frost_reverb16_sample(b.ws,x[k],p):frost_reverb_sample(a.ws,x[k],p);
    }else if(kind==1){
        frost_rv6_reset(&a);frost_16_rv6_reset(&b);
        for(base=0;base<n;base+=64){unsigned m=n-base;if(m>64)m=64;
            if(compact)frost_16_rv6_block(&b,x+base,out+base,m,p);else frost_rv6_block(&a,x+base,out+base,m,p);}
    }else{
        frost_spring_reset(&a);frost_16_spring_reset(&b);
        for(k=0;k<n;++k)out[k]=compact?frost_16_spring_sample(&b,x[k],p):frost_spring_sample(&a,x[k],p);
    }
    return rv16_float.before!=0x12345678u || rv16_float.after!=0x12345678u || rv16_compact.before!=0x12345678u || rv16_compact.after!=0x12345678u;
}
API int rv16_block_sample(unsigned kind,unsigned mode,unsigned count)
{
    static FrostReverb16 a,b;FrostReverbModel16 am={&a},bm={&b};
    unsigned k,base;uint32_t rng=1979;float p[4]={50,50,85,(float)mode};
    if(!kind){p[0]=.95f;p[1]=.7f;p[2]=.7f;frost_reverb16_reset(&a);frost_reverb16_reset(&b);}
    else{frost_16_rv6_reset(&am);frost_16_rv6_reset(&bm);}
    for(base=0;base<count;){
        float x[16],y[16],z[16],params[16][3];unsigned n=1+(base%16);if(n>count-base)n=count-base;
        for(k=0;k<n;++k){rng=rng*1664525u+1013904223u;x[k]=((int)(rng>>16)-32768)*(1.0f/32768.0f);
            params[k][0]=p[0];params[k][1]=p[1];params[k][2]=p[2];
            y[k]=kind?frost_16_rv6_sample_reference(&am,x[k],p):frost_reverb16_sample(&a,x[k],p);}
        if(kind)frost_16_rv6_block(&bm,x,z,n,p);else frost_reverb16_block(&b,x,z,n,(const float (*)[3])params);
        if(memcmp(y,z,n*sizeof(float)))return (int)base;
        base+=n;
    }
    return -1;
}
/* 32e: SPRING block body vs the per-sample reference, bitwise, from stale
 * workspaces, chunks 1..64, DWELL/MIX/TONE changing between chunks and the
 * output and every state word compared at the end. -1 = identical. */
API int rv16_spring_block_matches(unsigned total,unsigned seed)
{
    static FrostReverb16 a,b;FrostReverbModel16 am={&a},bm={&b};
    unsigned i=0,k,n;uint32_t r=seed*2654435761u+1u;float x[64],y[64],z[64],p[3]={40,20,60};
    memset(&a,0x5a,sizeof a);memset(&b,0x5a,sizeof b);frost_16_spring_reset(&am);frost_16_spring_reset(&bm);
    while(i<total){
        r=r*1664525u+1013904223u;n=1u+(r>>26)%64u;if(n>total-i)n=total-i;
        if(((r>>8)&15u)==0)p[0]=(float)((r>>12)%101u);
        if(((r>>9)&15u)==0)p[1]=(float)((r>>14)%101u);
        if(((r>>10)&15u)==0)p[2]=(float)((r>>16)%101u)+0.5f;
        for(k=0;k<n;++k){r=r*1664525u+1013904223u;x[k]=(i+k)%6000u<300u?((int)(r>>16)-32768)*(1.2f/32768.0f):0.0f;}
        for(k=0;k<n;++k)y[k]=frost_16_spring_sample_reference(&am,x[k],p);
        if(n&1u)for(k=0;k<n;++k)z[k]=frost_16_spring_sample(&bm,x[k],p);else frost_16_spring_block(&bm,x,z,n,p);
        if(memcmp(y,z,n*sizeof(float)))return (int)i;
        i+=n;
    }
    if(memcmp(&a,&b,sizeof a))return 2000000000;
    return -1;
}
API int rv16_chain(unsigned type,const float *x,float *out,unsigned count)
{
    static FrostChain chain;static FrostReverbWorkspace w;unsigned types[]={type};
    frost_chain_init(&chain);if(!frost_chain_bind_reverb(&chain,&w) || !frost_chain_configure(&chain,types,1))return -1;
    frost_chain_enable(&chain,0,1);
    return (int)frost_chain_process_guarded(&chain,x,out,count,0);
}
