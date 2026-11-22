#include "library_host.c"
static FrostInstance ne_instance;
static FrostDelay ne_delay;
API void ne_reset(unsigned type)
{frost_instance_init_workspace(&ne_instance,type,type==FROST_TAPE?&ne_delay:0);}
API int ne_set(unsigned p,float v)
{const FrostEffect *d=frost_effect_find(ne_instance.type);if(!d || p>=d->parameter_count || v<d->parameters[p].minimum || v>d->parameters[p].maximum)return 0;
 ne_instance.current[p]=v;return 1;}
API void ne_render(const float *x,float *y,unsigned n)
{const FrostEffect *d=frost_effect_find(ne_instance.type);unsigned i;for(i=0;i<n;++i)y[i]=d->sample(&ne_instance.state,x[i],ne_instance.current);}
