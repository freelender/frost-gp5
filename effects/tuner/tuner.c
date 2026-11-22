#include "tuner.h"
/* Cold/flash code: runs only while the tuner is active (the chain is skipped
 * and the output muted then). Bounded work per block, see tuner.h. */
enum { TUNER_IDLE, TUNER_COARSE, TUNER_FINE };
int frost_tuner_start(FrostTuner *t,void *mem,unsigned bytes)
{
    float *f=(float *)mem;
    if(!mem || bytes<FROST_TUNER_BYTES)return 0;
    t->buf[0]=f;t->buf[1]=f+FROST_TUNER_N;t->c=f+2u*FROST_TUNER_N;t->d=t->c+FROST_TUNER_CN;t->fd=t->d+FROST_TUNER_CTAU_MAX+2u;
    t->fill=t->cap=t->ana=t->step=t->odd=t->fine0=0;t->phase=TUNER_IDLE;
    t->lp=t->pair=t->level=t->coarse=0.0f;t->freq=0.0f;t->clarity=0.0f;t->seq=0;t->active=1;return 1;
}
void frost_tuner_stop(FrostTuner *t){t->active=0;t->phase=TUNER_IDLE;}
/* External linkage: no static function may land in the DSP's boot-adjacent region. */
void frost_tuner_none(FrostTuner *t);
void frost_tuner_none(FrostTuner *t){t->freq=0.0f;t->clarity=0.0f;t->phase=TUNER_IDLE;++t->seq;}
/* Coarse pass done: YIN cumulative mean normalized difference, threshold, parabola. */
void frost_tuner_coarse_pick(FrostTuner *t);
void frost_tuner_coarse_pick(FrostTuner *t)
{
    float *n=t->d,sum=0.0f,best=1e30f,den,off;unsigned tau,pick=0;int f0;
    for(tau=1;tau<=FROST_TUNER_CTAU_MAX+1u;++tau){sum+=n[tau];n[tau]=sum>0.0f?n[tau]*(float)tau/sum:1.0f;}
    for(tau=FROST_TUNER_CTAU_MIN;tau<=FROST_TUNER_CTAU_MAX;++tau){
        if(n[tau]<0.2f){while(tau<FROST_TUNER_CTAU_MAX && n[tau+1]<n[tau])++tau;pick=tau;break;}
        if(n[tau]<best){best=n[tau];pick=tau;}
    }
    if(!pick || n[pick]>0.5f){frost_tuner_none(t);return;}
    den=n[pick-1]-2.0f*n[pick]+n[pick+1];off=den>1e-12f?0.5f*(n[pick-1]-n[pick+1])/den:0.0f;
    if(off>0.5f)off=0.5f;if(off<-0.5f)off=-0.5f;
    t->coarse=4.0f*((float)pick+off);t->clarity=1.0f-n[pick];
    f0=(int)(t->coarse+0.5f)-FROST_TUNER_FINE;if(f0<1)f0=1;
    if(f0+2*FROST_TUNER_FINE+(int)FROST_TUNER_W>=(int)FROST_TUNER_N)f0=(int)FROST_TUNER_N-(int)FROST_TUNER_W-2*FROST_TUNER_FINE-1;
    t->fine0=(unsigned)f0;t->step=0;t->phase=TUNER_FINE;
}
/* Fine pass done: minimum of the raw difference, parabola. */
void frost_tuner_fine_pick(FrostTuner *t);
void frost_tuner_fine_pick(FrostTuner *t)
{
    const float *d=t->fd;unsigned k,m=1;float den,off;
    for(k=2;k<2u*FROST_TUNER_FINE;++k)if(d[k]<d[m])m=k;
    den=d[m-1]-2.0f*d[m]+d[m+1];off=den>1e-20f?0.5f*(d[m-1]-d[m+1])/den:0.0f;
    if(off>0.5f)off=0.5f;if(off<-0.5f)off=-0.5f;
    t->freq=FROST_TUNER_RATE/((float)(t->fine0+m)+off);t->phase=TUNER_IDLE;++t->seq;
}
void frost_tuner_block(FrostTuner *t,const float *x,unsigned n)
{
    unsigned i,k;
    if(!t->active)return;
    /* capture: one-pole low-pass (~3 kHz), average pairs -> 22.05 kHz */
    for(i=0;i<n;++i){
        t->lp+=0.348f*(x[i]-t->lp);
        if(!t->odd){t->pair=t->lp;t->odd=1;continue;}
        t->odd=0;
        if(t->fill<FROST_TUNER_N)t->buf[t->cap][t->fill++]=0.5f*(t->pair+t->lp);
    }
    if(t->fill==FROST_TUNER_N && t->phase==TUNER_IDLE){
        const float *b=t->buf[t->cap];float e=0.0f;
        for(k=0;k<FROST_TUNER_W;++k)e+=b[k]*b[k];
        t->level=e/(float)FROST_TUNER_W;
        if(t->level>1e-6f){
            t->ana=t->cap;t->cap^=1u;
            for(k=0;k<FROST_TUNER_CN;++k)t->c[k]=0.25f*(b[4*k]+b[4*k+1]+b[4*k+2]+b[4*k+3]);
            t->step=1;t->phase=TUNER_COARSE;
        }else frost_tuner_none(t);
        t->fill=0;
        return;                                       /* analysis starts next block */
    }
    if(t->phase==TUNER_COARSE){
        unsigned stop=t->step+FROST_TUNER_COARSE_PER_BLOCK;const float *c=t->c;
        if(stop>FROST_TUNER_CTAU_MAX+2u)stop=FROST_TUNER_CTAU_MAX+2u;
        for(;t->step<stop;++t->step){
            float s=0.0f;const float *a=c+t->step;
            for(k=0;k<FROST_TUNER_CW;++k){float df=c[k]-a[k];s+=df*df;}
            t->d[t->step]=s;
        }
        if(t->step>=FROST_TUNER_CTAU_MAX+2u)frost_tuner_coarse_pick(t);
    }else if(t->phase==TUNER_FINE){
        const float *b=t->buf[t->ana];unsigned stop=t->step+FROST_TUNER_FINE_PER_BLOCK;
        if(stop>2u*FROST_TUNER_FINE+1u)stop=2u*FROST_TUNER_FINE+1u;
        for(;t->step<stop;++t->step){
            float s=0.0f;const float *a=b+t->fine0+t->step;
            for(k=0;k<FROST_TUNER_W;++k){float df=b[k]-a[k];s+=df*df;}
            t->fd[t->step]=s;
        }
        if(t->step>=2u*FROST_TUNER_FINE+1u)frost_tuner_fine_pick(t);
    }
}
