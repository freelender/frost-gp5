#include "fcm.h"
#if FCM_EXP_EXACT
#include <math.h>
static inline float fcm_exp(float z){return expf(z);}
#else
/* exp(z) for z <= 0 as (1+z/1024)^1024: ten squarings (stock uses the same
 * family). Relative error grows with |z| but the result is then tiny. */
static inline float fcm_exp(float z)
{
    float t;if(z<-1023.0f)return 0.0f;t=1.0f+z*(1.0f/1024.0f);
    t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;return t;
}
#endif
static inline float sat(const FcmModel *m,unsigned i,float u)
{return u>0.0f?m->Ap[i]*(1.0f-fcm_exp(-m->Kp[i]*u)):m->An[i]*(fcm_exp(m->Kn[i]*u)-1.0f);}
/* y[f] = sum_k h[k]*x[f-k] over a linear history buffer whose newest n
 * samples start at hist+len-1 (the previous len-1 samples precede them). */
static void fir_block(float *hist,unsigned len,const float *h,const float *x,float *y,unsigned n)
{
    unsigned f,k;float *cur=hist+len-1u;
    for(f=0;f<n;++f)cur[f]=x[f];
    for(f=0;f<n;++f){float acc=0.0f;const float *p=cur+f;for(k=0;k<len;++k)acc+=h[k]*p[-(int)k];y[f]=acc;}
    for(k=0;k+1u<len;++k)hist[k]=hist[n+k];   /* keep the last len-1 samples */
}
void fcm_reset(FcmState *s,const FcmModel *m)
{
    unsigned i,j;s->m=m;s->env=0.0f;
    for(j=0;j<FCM_MAX_PRE+FCM_BLOCK;++j)s->xh[j]=0.0f;
    for(j=0;j<FCM_MAX_POST+FCM_BLOCK;++j)s->ph[j]=0.0f;
    for(i=0;i<FCM_MAX_STAGES;++i){
        for(j=0;j<FCM_MAX_AA/2u+FCM_BLOCK;++j)s->uh[i][j]=0.0f;
        for(j=0;j<FCM_MAX_AA+4u*FCM_BLOCK;++j)s->vh[i][j]=0.0f;
        for(j=0;j<FCM_MAX_MID+FCM_BLOCK;++j)s->mh[i][j]=0.0f;
    }
}
/* One oversampled saturation stage over n input-rate samples (in place). */
static void stage(FcmState *s,unsigned i,float *u,unsigned n)
{
    const FcmModel *m=s->m;unsigned OS=m->os,L=m->n_aa,T=(L+OS-1u)/OS,f,p,k;
    float *uh=s->uh[i],*ucur=uh+T-1u,*vh=s->vh[i],*vcur=vh+L-1u;const float *h=m->aa;
    for(f=0;f<n;++f)ucur[f]=u[f]*(float)OS;
    /* upsample (polyphase), saturate */
    for(f=0;f<n;++f)for(p=0;p<OS;++p){
        float acc=0.0f;
        for(k=p;k<L;k+=OS)acc+=h[k]*ucur[(int)f-(int)((k-p)/OS)];
        vcur[f*OS+p]=sat(m,i,acc);
    }
    /* anti-alias and keep every OS-th sample */
    for(f=0;f<n;++f){float acc=0.0f;const float *q=vcur+f*OS;for(k=0;k<L;++k)acc+=h[k]*q[-(int)k];u[f]=acc;}
    for(k=0;k+1u<T;++k)uh[k]=uh[n+k];
    for(k=0;k+1u<L;++k)vh[k]=vh[n*OS+k];
}
void fcm_process(FcmState *s,const float *x,float *y,unsigned n)
{
    const FcmModel *m=s->m;float u[FCM_BLOCK],e[FCM_BLOCK];unsigned i,f;
    fir_block(s->xh,m->n_pre,m->pre,x,u,n);
    for(f=0;f<n;++f)u[f]*=m->gain;
    if(m->env_a!=0.0f){float v=s->env;for(f=0;f<n;++f){v+=m->env_a*((u[f]<0.0f?-u[f]:u[f])-v);e[f]=v;}s->env=v;}
    for(i=0;i<m->stages;++i){
        if(m->env_a!=0.0f)for(f=0;f<n;++f)u[f]-=m->env_k[i]*e[f];
        stage(s,i,u,n);
        if(i+1u<m->stages)fir_block(s->mh[i],m->n_mid,m->mid[i],u,u,n);
    }
    fir_block(s->ph,m->n_post,m->post,u,y,n);
}
