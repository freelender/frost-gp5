#include "fcm_effect.h"
#include "../eq/tone.h"
#include "../../firmware/protocol/fcm_wire.h"
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
/* Everything the kernels call lives in the same overlay module (.ovl.fcm):
 * overlay code may not call code outside itself (except the far helpers). */
#if defined(__GNUC__) && !defined(__clang__)
#define FROST_FCM_OPT __attribute__((optimize("no-tree-loop-distribute-patterns","fp-contract=off")))
#else
#define FROST_FCM_OPT
#endif
#define FCMI_FN FROST_HOT FROST_OVL(fcm) FROST_FCM_OPT
#include "fcm_iir.c"
#include "fcm_default_model.h"   /* frost_fcm_default_model (public, MIT-derived) */
#if defined(__arm__) && defined(GP5_FIRMWARE)
__attribute__((section(".large")))   /* zeroed at start-up = EMPTY */
#endif
FrostFcmRx frost_fcm_rx;
#ifdef FROST_OVERLAYS
FrostFcmBlockFn frost_fcm_block_kernel=0;  /* set by frost_ovl_load to the loaded copy */
#else
FrostFcmBlockFn frost_fcm_block_kernel=frost_fcm_block;
#endif
/* from_rx: the workspace model is the received record (index loaded-1). */
typedef struct { uint32_t loaded,epoch,from_rx; FcmiModel model; FcmiState st; float tone[2]; } FrostFcmWorkspace;
typedef char frost_fcm_ws_fits[sizeof(FrostFcmWorkspace)<=sizeof(((FrostModulation *)0)->line)?1:-1];
/* Cold: marks the workspace for (re)setup on the next audio call. */
void frost_fcm_reset(FrostFcm *s){if(s->ring)s->ring->valid=0;}
/* The received record, if it is HELD for this MODEL and structurally sane
 * (the handler checked its CRC at COMMIT). */
FCMI_FN static const FcmiModel *held_model(unsigned index)
{
    const FrostFcmRx *r=&frost_fcm_rx;
    if(r->state!=FROST_FCM_RX_HELD || r->index!=index)return 0;
    if(r->rec.model.n_pre>FCMI_MAX_SOS || r->rec.model.n_mid>FCMI_MAX_SOS || r->rec.model.n_post>FCMI_MAX_SOS)return 0;
    return &r->rec.model;
}
FCMI_FN static FrostFcmWorkspace *setup(FrostFcm *s,const float *p)
{
    FrostFcmWorkspace *w=(FrostFcmWorkspace *)(void *)&s->ring->line;unsigned idx=(unsigned)(p[0]+0.5f);
    const FcmiModel *held=0;int reload=!s->ring->valid || w->loaded!=idx+1u;
    /* A new epoch matters only if it changes what this MODEL plays. */
    if(!reload && w->epoch!=frost_fcm_rx.epoch){held=held_model(idx);reload=w->from_rx || held;if(!reload)w->epoch=frost_fcm_rx.epoch;}
    if(reload){
        const FcmiModel *src=held?held:held_model(idx);const uint32_t *a;uint32_t *d;unsigned k;
        w->from_rx=src!=0;if(!src)src=&frost_fcm_default_model;
        a=(const uint32_t *)(const void *)src;d=(uint32_t *)(void *)&w->model;
        for(k=0;k<sizeof(FcmiModel)/4u;++k)d[k]=a[k];
        fcmi_reset(&w->st,&w->model);w->tone[0]=w->tone[1]=0.0f;w->loaded=idx+1u;w->epoch=frost_fcm_rx.epoch;s->ring->valid=1u;
    }
    return w;
}
FCMI_FN void frost_fcm_block(void *state,const float *x,float *y,unsigned n,const float *p,const float *p_start)
{
    FrostFcm *s=(FrostFcm *)state;FrostFcmWorkspace *w=setup(s,p);const float g=frost_db_to_gain(p[1]),lv=frost_level(p[2]);unsigned f,j;float tone[3],tone0[3],dt[3];
    unsigned neutral=p[3]==0.0f && p[4]==0.0f && p[5]==0.0f;
    for(j=0;j<3;++j)tone[j]=neutral?1.0f:frost_db_to_gain(p[3+j]);
    if(p_start){
        /* Ramp from the block-start values (one block of <= FCMI_BLOCK samples). */
        const float g0=frost_db_to_gain(p_start[1]),l0=frost_level(p_start[2]);
        const float dg=(g-g0)/(float)n,dl=(lv-l0)/(float)n;float t[FCMI_BLOCK];
        neutral=neutral && p_start[3]==0.0f && p_start[4]==0.0f && p_start[5]==0.0f;
        for(j=0;j<3;++j){tone0[j]=neutral?1.0f:frost_db_to_gain(p_start[3+j]);dt[j]=(tone[j]-tone0[j])/(float)n;}
        if(n>FCMI_BLOCK)n=FCMI_BLOCK;
        if(neutral)w->tone[0]=w->tone[1]=0.0f;
        for(f=0;f<n;++f)t[f]=x[f]*(g0+dg*(float)(f+1u));
        fcmi_process(&w->st,t,t,n);
        for(f=0;f<n;++f){float v=t[f],tg[3];
            if(!neutral){for(j=0;j<3;++j)tg[j]=tone0[j]+dt[j]*(float)(f+1u);v=frost_tone_sample(v,tg,w->tone);}
            y[f]=frost_rail(v*(l0+dl*(float)(f+1u)),8.0f);}
        return;
    }
    if(neutral)w->tone[0]=w->tone[1]=0.0f;
    while(n){
        unsigned m=n<FCMI_BLOCK?n:FCMI_BLOCK;float t[FCMI_BLOCK];
        for(f=0;f<m;++f)t[f]=x[f]*g;
        fcmi_process(&w->st,t,t,m);
        for(f=0;f<m;++f){float v=neutral?t[f]:frost_tone_sample(t[f],tone,w->tone);y[f]=frost_rail(v*lv,8.0f);}
        x+=m;y+=m;n-=m;
    }
}
FROST_HOT FROST_NOINLINE FROST_OVL(fcm) FROST_FCM_OPT
float frost_fcm_sample(FrostFcm *s,float x,const float *p)
{float y;frost_fcm_block(s,&x,&y,1u,p,0);return y;}
#include "../../engine/overlay_far_off.h"
