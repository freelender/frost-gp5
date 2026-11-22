#include "fcm_iir.h"
#if defined(__GNUC__) && !defined(__clang__)
#define FCMI_UNROLL _Pragma("GCC unroll 10")
#else
#define FCMI_UNROLL
#endif
#ifndef FCMI_FN
#define FCMI_FN
#endif
#ifndef FCMI_EXP_EXACT
#define FCMI_EXP_EXACT 0
#endif
#if FCMI_EXP_EXACT
#include <math.h>
static inline float fcmi_exp(float z){return expf(z);}
#else
/* exp(z), z <= 0, as (1+z/1024)^1024 (ten squarings, stock family). */
static inline float fcmi_exp(float z)
{float t;if(z<-1023.0f)return 0.0f;t=1.0f+z*(1.0f/1024.0f);t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;return t;}
#endif
/* In-place biquad cascade over n samples; per section the coefficients and
 * the two states stay in registers for the whole block. */
FCMI_FN static void fcmi_cascade(const FcmiBiquad *q,float (*z)[2],unsigned sections,float *x,unsigned n)
{
    unsigned k,f;
    for(k=0;k<sections;++k){
        const float b0=q[k].b0,b1=q[k].b1,b2=q[k].b2,a1=q[k].a1,a2=q[k].a2;float z1=z[k][0],z2=z[k][1];
        for(f=0;f<n;++f){const float v=x[f],y=b0*v+z1;z1=b1*v-a1*y+z2;z2=b2*v-a2*y;x[f]=y;}
        z[k][0]=z1;z[k][1]=z2;
    }
}
FCMI_FN void fcmi_reset(FcmiState *s,const FcmiModel *m)
{
    unsigned i,j;s->m=m;s->env=0.0f;
    for(i=0;i<FCMI_MAX_SOS;++i){s->zpre[i][0]=s->zpre[i][1]=s->zmid[i][0]=s->zmid[i][1]=s->zpost[i][0]=s->zpost[i][1]=0.0f;}
    for(i=0;i<FCMI_STAGES;++i){
        for(j=0;j<FCMI_AA/2u-1u+FCMI_BLOCK;++j)s->uh[i][j]=0.0f;
        for(j=0;j<FCMI_AA-1u+2u*FCMI_BLOCK;++j)s->vh[i][j]=0.0f;
    }
}
/* 2x oversampled asymmetric exponential saturation, polyphase 20-tap
 * anti-alias filter (phase p uses taps p, p+2, ...), symmetric decimator. */
FCMI_FN static void fcmi_stage(FcmiState *s,unsigned i,float *u,unsigned n)
{
    const FcmiModel *m=s->m;const float Ap=m->Ap[i],An=m->An[i],Kp=m->Kp[i],Kn=m->Kn[i],*h=m->aa;
    float *uh=s->uh[i],*uc=uh+9,*vh=s->vh[i],*vc=vh+19;unsigned f,j;
    for(f=0;f<n;++f)uc[f]=2.0f*u[f];
    for(f=0;f<n;++f){
        const float *q=uc+f;float a0=0.0f,a1=0.0f;
        FCMI_UNROLL
        for(j=0;j<10u;++j){const float v=q[-(int)j];a0+=h[2u*j]*v;a1+=h[2u*j+1u]*v;}
        vc[2u*f]=a0>0.0f?Ap*(1.0f-fcmi_exp(-Kp*a0)):An*(fcmi_exp(Kn*a0)-1.0f);
        vc[2u*f+1u]=a1>0.0f?Ap*(1.0f-fcmi_exp(-Kp*a1)):An*(fcmi_exp(Kn*a1)-1.0f);
    }
    for(f=0;f<n;++f){const float *q=vc+2u*f;float a=0.0f;FCMI_UNROLL for(j=0;j<10u;++j)a+=h[j]*(q[-(int)j]+q[-(int)(19u-j)]);u[f]=a;}
    for(j=0;j<9u;++j)uh[j]=uh[n+j];
    for(j=0;j<19u;++j)vh[j]=vh[2u*n+j];
}
FCMI_FN void fcmi_process(FcmiState *s,const float *x,float *y,unsigned n)
{
    const FcmiModel *m=s->m;float e[FCMI_BLOCK];unsigned i,f;
    for(f=0;f<n;++f)y[f]=x[f];
    fcmi_cascade(m->pre,s->zpre,m->n_pre,y,n);
    for(f=0;f<n;++f)y[f]*=m->gain;
    if(m->env_a!=0.0f){float v=s->env;for(f=0;f<n;++f){v+=m->env_a*((y[f]<0.0f?-y[f]:y[f])-v);e[f]=v;}s->env=v;}
    for(i=0;i<FCMI_STAGES;++i){
        if(m->env_a!=0.0f)for(f=0;f<n;++f)y[f]-=m->env_k[i]*e[f];
        fcmi_stage(s,i,y,n);
        if(i==0)fcmi_cascade(m->mid,s->zmid,m->n_mid,y,n);
    }
    fcmi_cascade(m->post,s->zpost,m->n_post,y,n);
}
