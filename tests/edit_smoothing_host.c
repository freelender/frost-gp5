/* Compare the edit-block path with the original interleaved smoothing
 * sequence on independent chains, including nonzero audio and wet changes. */
#define FROST_EDIT_BLOCKS 1
#include "library_host.c"
static void original_edit(FrostInstance *s,const FrostEffect *d,float *x,unsigned n)
{
    float p0[FROST_PARAMETERS+1],wet[64],y[64];unsigned i,j;
    for(j=0;j<=FROST_PARAMETERS;++j)p0[j]=s->current[j];
    for(i=0;i<n;++i){
        s->wet=effect_smooth(s->wet,s->wet_target);wet[i]=s->wet;
        for(j=0;j<d->parameter_count;++j)
            s->current[j]=d->parameters[j].flags&FROST_PARAM_STEPPED?s->target[j]:effect_smooth(s->current[j],s->target[j]);
    }
    s->current[FROST_PARAMETERS]=s->wet_target;s->suspended=0;
    if(s->type==FROST_FCM)frost_fcm_block_kernel(&s->state.fcm,x,y,n,s->current,p0);
    else if(s->type==FROST_RV6)frost_active_rv6_block(&s->state.reverb_model,x,y,n,s->current);
    else for(i=0;i<n;++i)y[i]=d->sample(&s->state,x[i],s->current);
    for(i=0;i<n;++i){float in=x[i];x[i]=in+wet[i]*(y[i]-in);}
}
API int edit_smoothing_matches(unsigned type,unsigned seed)
{
    unsigned b,i,j,n,types[1]={type};float a[64],z[64];uint32_t rng=seed;
    const FrostEffect *d=frost_effect_find(type);FrostInstance *s,*ref;
    lib_init(0);lib_init(1);if(!d || !lib_configure(0,types,1) || !lib_configure(1,types,1))return -2;
    s=&fx[0].chain.slots[0];ref=&fx[1].chain.slots[0];lib_enable(0,0,1);lib_enable(1,0,1);
    for(b=0;b<120;++b){
        rng=rng*1664525u+1013904223u;n=1u+(rng%64u);
        if(b%5u==0){
            j=(rng>>8)%d->parameter_count;
            float v=d->parameters[j].minimum+(d->parameters[j].maximum-d->parameters[j].minimum)*.37f;
            if(d->parameters[j].flags&FROST_PARAM_STEPPED)v=(float)(unsigned)v;
            lib_set(0,0,j,v);lib_set(1,0,j,v);
        }
        if(b%11u==0){lib_enable(0,0,(b/11u)&1u);lib_enable(1,0,(b/11u)&1u);}
        for(i=0;i<n;++i){rng=rng*1664525u+1013904223u;a[i]=z[i]=.15f*(float)((int)(rng>>16)-32768)/32768.0f;}
        if(type==FROST_FCM)frost_fcm_ramped(s,d,a,n);else frost_instance_edit(s,d,a,n);
        original_edit(ref,d,z,n);
        if(memcmp(a,z,n*sizeof(float)) || memcmp(s->current,ref->current,sizeof s->current) || memcmp(&s->wet,&ref->wet,sizeof(float)))return (int)b;
    }
    return -1;
}
