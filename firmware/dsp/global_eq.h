#ifndef FROST_GLOBAL_EQ_H
#define FROST_GLOBAL_EQ_H
#include "effects/eq/tone.h"
#include "firmware/protocol/global_wire.h"
/* This fixed-address XIP section is packed in DSP-data. LARGE and VARS have
 * only bytes to spare; no extra resident audio code or effect slot is used. */
#if defined(FROST_OVERLAYS) && defined(__arm__) && defined(GP5_FIRMWARE)
#define FROST_GLOBAL_EQ_CODE __attribute__((section(".frost_global_eq"),noinline,optimize("Os","fp-contract=off")))
#else
#define FROST_GLOBAL_EQ_CODE
#endif
typedef struct {float delta[3],z[2];int8_t db[3];uint8_t active;} FrostGlobalEq;
static FrostGlobalEq frost_global_eq;
FROST_GLOBAL_EQ_CODE void frost_global_eq_init(void)
{unsigned j;for(j=0;j<3;++j){frost_global_eq.delta[j]=0.0f;frost_global_eq.db[j]=0;}frost_global_eq.z[0]=frost_global_eq.z[1]=0.0f;frost_global_eq.active=0;}
/* active: 0 = flat (the RAM caller skips this flash function entirely),
 * 1 = settled gain, 2 = gliding. It occupies existing struct padding.
 * Only a changed SET starts a glide; steady audio never recomputes exp2. */
FROST_GLOBAL_EQ_CODE int frost_global_eq_block(float *x,unsigned n)
{
    FrostGlobalEq *s=&frost_global_eq;float step[3],gain[3],z[2];unsigned i,j,settled=1,flat=1;
    if(!n || !s->active)return 1;
    z[0]=s->z[0];z[1]=s->z[1];
    if(s->active==2u){
        for(j=0;j<3;++j){float target=frost_db_to_gain((float)s->db[j])-1.0f,d=target-s->delta[j];
            if(d<0.00001f && d>-0.00001f)s->delta[j]=target;
            else settled=0;
            step[j]=0.25f*(target-s->delta[j])/(float)n;
            if(s->db[j])flat=0;
        }
        if(settled){s->active=flat?0u:1u;if(flat){s->z[0]=s->z[1]=0.0f;return 1;}}
    }
    if(s->active==1u){
        gain[0]=1.0f+s->delta[0];gain[1]=1.0f+s->delta[1];gain[2]=1.0f+s->delta[2];
        for(i=0;i<n;++i){
            x[i]=frost_tone_sample(x[i],gain,z);
            if(!(x[i]>=-16.0f && x[i]<=16.0f)){frost_global_eq_init();return 0;}
        }
    }else{
        for(i=0;i<n;++i){
            for(j=0;j<3;++j)gain[j]=1.0f+s->delta[j]+step[j]*(float)(i+1u);
            x[i]=frost_tone_sample(x[i],gain,z);
            if(!(x[i]>=-16.0f && x[i]<=16.0f)){frost_global_eq_init();return 0;}
        }
        for(j=0;j<3;++j)s->delta[j]+=step[j]*(float)n;
    }
    s->z[0]=z[0];s->z[1]=z[1];return 1;
}
/* beta.14 stereo: the side signal through the same three bands (its own
 * filter history), at the gains the mid block just used. Linear, so mid and
 * side filtered separately give the same left and right as filtering each
 * output. Only while a stereo effect writes a side signal. */
static float frost_global_eq_side_z[2];
FROST_GLOBAL_EQ_CODE void frost_global_eq_side(float *x,unsigned n)
{
    FrostGlobalEq *s=&frost_global_eq;float gain[3];unsigned i;
    if(!s->active){frost_global_eq_side_z[0]=frost_global_eq_side_z[1]=0.0f;return;}
    gain[0]=1.0f+s->delta[0];gain[1]=1.0f+s->delta[1];gain[2]=1.0f+s->delta[2];
    for(i=0;i<n;++i){x[i]=frost_tone_sample(x[i],gain,frost_global_eq_side_z);if(!(x[i]>=-16.0f && x[i]<=16.0f))x[i]=0.0f;}
}
FROST_GLOBAL_EQ_CODE void frost_global_eq_command(const uint8_t *p,unsigned n,uint8_t *reply)
{
    uint32_t v[6]={0},status=FROST_SLOT_BAD_FRAME;unsigned i;
    if(p && n>=12)v[1]=frost_wire_get32(p+8);
    if(p && n==FROST_GLOBAL_REQUEST_BYTES && p[0]==0x50 && p[1]==0x7f && p[2]==n && !p[3]){
        for(i=0;i<6;++i)v[i]=frost_wire_get32(p+4+4*i);
        if(v[0]!=1u)status=FROST_SLOT_BAD_VERSION;
        else if(!v[1])status=FROST_SLOT_BAD_REQUEST_ID;
        else if(v[2]>FROST_GLOBAL_SET)status=FROST_SLOT_BAD_OPERATION;
        else {
            status=FROST_SLOT_APPLIED;
            if(v[2]==FROST_GLOBAL_SET){
                for(i=3;i<6;++i)if((int32_t)v[i]<FROST_GLOBAL_EQ_MIN || (int32_t)v[i]>FROST_GLOBAL_EQ_MAX)status=FROST_SLOT_BAD_VALUE;
                if(status==FROST_SLOT_APPLIED){
                    for(i=0;i<3;++i)if(frost_global_eq.db[i]!=(int8_t)(int32_t)v[3+i])frost_global_eq.active=2u;
                    for(i=0;i<3;++i)frost_global_eq.db[i]=(int8_t)(int32_t)v[3+i];
                }
            }
        }
    }
    for(i=0;i<FROST_GLOBAL_REPLY_BYTES;++i)reply[i]=0;
    reply[0]=0x50;reply[1]=0x7f;reply[2]=FROST_GLOBAL_REPLY_BYTES;
    frost_wire_put32(reply+4,1);frost_wire_put32(reply+8,v[1]);frost_wire_put32(reply+12,status);
    for(i=0;i<3;++i)frost_wire_put32(reply+16+4*i,(uint32_t)(int32_t)frost_global_eq.db[i]);
}
#endif
