#include "framework_host.c"
#include "../engine/control_catalog.h"
static FrostOverdrive direct[2];
API void od_reset(unsigned c){frost_overdrive_reset(&direct[c]);}
API float od_sample(unsigned c,float x,float drive,float tone,float level)
{float p[3]={drive,tone,level};return frost_overdrive_sample(&direct[c],x,p);}
API int od_policy(unsigned c,unsigned slot,unsigned p){return frost_chain_set_bypass_policy(&chains[c],slot,p);}
API float od_history(unsigned c,unsigned slot,unsigned i)
{FrostOverdrive *s=&chains[c].slots[slot].state.overdrive;return i==0?s->previous:i==1?s->highpass:s->tone;}
API int od_meta(unsigned p,unsigned field)
{const FrostControlParameter *d=&frost_control_find(FROST_OVERDRIVE)->p[p];
 return field==0?d->minimum:field==1?d->maximum:field==2?d->initial:field==3?d->scale:d->step;}
API float od_bound(unsigned p,unsigned field)
{const FrostParameter *d=&frost_effect_find(FROST_OVERDRIVE)->parameters[p];return field==0?d->minimum:field==1?d->maximum:d->initial;}
