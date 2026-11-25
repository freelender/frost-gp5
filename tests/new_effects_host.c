#include "library_host.c"
static FrostInstance ne_instance;
static FrostDelay ne_delay;
API void ne_reset(unsigned type)
{frost_instance_init_workspace(&ne_instance,type,frost_type_is_delay(type)?&ne_delay:0);}
/* beta.14 stereo: output mode / fold rule, and a render that returns left and
 * right (mid +/- side) in 64-sample blocks like the pedal. */
/* beta.14: SL-100 settled block kernel in 64-sample blocks (compare with ne_render). */
API void ne_render_sl100_block(const float *x,float *y,unsigned n)
{unsigned i;for(i=0;i<n;i+=64u){unsigned m=n-i<64u?n-i:64u;frost_sl100_block(&ne_instance.state,x+i,y+i,m,ne_instance.current,0);}}
API void ne_stereo(unsigned output,unsigned route_ok){frost_stereo_output=output;frost_side_route_ok=route_ok;}
API void ne_render_stereo(const float *x,float *l,float *r,unsigned n)
{
    const FrostEffect *d=frost_effect_find(ne_instance.type);unsigned i,k;
    for(i=0;i<n;i+=FROST_SIDE_BLOCK){
        unsigned m=n-i<FROST_SIDE_BLOCK?n-i:FROST_SIDE_BLOCK;
        frost_side_begin();
        for(k=0;k<m;++k){float y=d->sample(&ne_instance.state,x[i+k],ne_instance.current);l[i+k]=r[i+k]=y;}
        if(frost_side_used)for(k=0;k<m;++k){l[i+k]+=frost_side[k];r[i+k]-=frost_side[k];}
    }
}
API int ne_set(unsigned p,float v)
{const FrostEffect *d=frost_effect_find(ne_instance.type);if(!d || p>=d->parameter_count || v<d->parameters[p].minimum || v>d->parameters[p].maximum)return 0;
 ne_instance.current[p]=v;return 1;}
API void ne_render(const float *x,float *y,unsigned n)
{const FrostEffect *d=frost_effect_find(ne_instance.type);unsigned i;for(i=0;i<n;++i)y[i]=d->sample(&ne_instance.state,x[i],ne_instance.current);}
