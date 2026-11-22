#include "drop_tune.h"
#include "../common/frost_dsp.h"
#include "../../engine/hot_code.h"
#include "../../engine/overlay_far_on.h"
/* Overlay module (FROST_OVL(drop)): everything below is self-contained
 * arithmetic, no calls out of the module. See drop_tune.h. */
#define DROP_N FROST_MOD_PCM
#define DROP_D_MAX (DROP_N-16u*40u-4u)   /* head distance limit: the score window reads 640 older samples */
enum { DROP_IDLE, DROP_COARSE, DROP_FINE };
/* 2^(-n/12), n = 1..6 */
static const float drop_ratio[6]={0.94387431f,0.89089872f,0.84089642f,0.79370053f,0.74915354f,0.70710678f};
void frost_drop_reset(FrostDropTune *s)
{
    unsigned i;
    s->d=FROST_DROP_MIN_DISTANCE;s->d_old=s->last=s->left=s->right=0.0f;s->right_pending=0;
    s->fade=s->cap=s->lo=s->hi=0;s->state=DROP_COARSE;s->cand=s->best=FROST_DROP_JUMP_MIN;s->best_score=-2.0f;
    if(s->ring){for(i=0;i<DROP_N;++i)s->ring->line.pcm[i]=0;s->ring->position=0;s->ring->valid=DROP_N;}
}
static inline __attribute__((always_inline)) int drop_raw(const FrostModulation *m,unsigned k)
{unsigned p=m->position;return m->line.pcm[p>=k?p-k:p+DROP_N-k];}
/* Cubic Hermite read at fractional distance d (>= 2) behind the input. */
FROST_HOT FROST_OVL(drop) static float drop_tap(const FrostModulation *m,float d)
{
    unsigned n=(unsigned)d;float u=1.0f-(d-(float)n);
    float a0=(float)drop_raw(m,n+2u),a=(float)drop_raw(m,n+1u),b=(float)drop_raw(m,n),b1=(float)drop_raw(m,n-1u);
    float c1=0.5f*(b-a0),c2=a0-2.5f*a+2.0f*b-0.5f*b1,c3=0.5f*(b1-a0)+1.5f*(a-b);
    return (((c3*u+c2)*u+c1)*u+a)*(1.0f/32767.0f);
}
/* Signed squared normalized correlation between the 640 samples just older
 * than the head (distance n0, 16 points stride 40: more than one period of
 * a low E) and the same window J samples newer. Only audio already heard. */
FROST_HOT FROST_OVL(drop) static float drop_score(const FrostModulation *m,unsigned n0,unsigned j)
{
    float num=0.0f,ea=1.0f,eb=1.0f;unsigned k;
    for(k=0;k<16u;++k){
        unsigned r=n0+40u*k;
        float a=(float)drop_raw(m,r),b=(float)drop_raw(m,r-j);
        num+=a*b;ea+=a*a;eb+=b*b;
    }
    return num*(num<0.0f?-num:num)/(ea*eb);
}
FROST_HOT FROST_NOINLINE FROST_OVL(drop) float frost_drop_sample(FrostDropTune *s,float x,const float *p)
{
    FrostModulation *m=s->ring;unsigned k=(unsigned)(p[0]+0.5f);float r,inc,y,mix;
    float v=x*32767.0f;
    m->line.pcm[m->position]=(int16_t)(v>32767.0f?32767:v<-32768.0f?-32768:(int)v);
    if(++m->position==DROP_N)m->position=0;
    if(k>5u)k=5u;
    r=drop_ratio[k];inc=1.0f-r;
    s->d+=inc;
    if(s->d>(float)DROP_D_MAX)s->d=(float)DROP_D_MAX;     /* never read past the ring */
    y=drop_tap(m,s->d);
    if(s->fade){
        float g=(float)s->fade*(1.0f/(float)FROST_DROP_FADE);
        y+=g*(drop_tap(m,s->d_old)-y);
        s->d_old+=inc;if(s->d_old>(float)DROP_D_MAX)s->d_old=(float)DROP_D_MAX;
        --s->fade;
    }else if(s->state==DROP_COARSE){
        /* Scan jumps J = 48, 52, ... as the head drifts (J <= d - MIN keeps the
         * new head behind the input), one candidate every 4 samples. The first
         * good match (normalized correlation >= 0.9), taken at its peak, wins:
         * lowest latency with a whole-period jump. */
        unsigned cap=(unsigned)(s->d-FROST_DROP_MIN_DISTANCE);
        if(cap>FROST_DROP_JUMP_MAX)cap=FROST_DROP_JUMP_MAX;
        if(!(++s->hi&3u) && s->cand<=cap){
            float sc=drop_score(m,(unsigned)s->d,s->cand);
            unsigned past_peak=s->best_score>=0.81f && sc<s->best_score-0.04f;   /* a good match, clearly past its peak */
            if(sc>s->best_score){s->best_score=sc;s->best=s->cand;}
            s->cand+=4u;
            if(past_peak || s->cand>FROST_DROP_JUMP_MAX){
                s->lo=s->best>FROST_DROP_JUMP_MIN+3u?s->best-3u:FROST_DROP_JUMP_MIN;
                s->cap=s->best+3u<=cap?s->best+3u:cap;s->cand=s->lo;s->best_score=-2.0f;s->state=DROP_FINE;
            }
        }
    }else{
        /* fine search, one candidate per sample, then the splice */
        float sc=drop_score(m,(unsigned)s->d,s->cand),off=0.0f;
        if(sc>s->best_score){s->left=s->cand>s->lo?s->last:sc;s->best_score=sc;s->best=s->cand;s->right_pending=1;}
        else if(s->right_pending){s->right=sc;s->right_pending=0;}
        s->last=sc;
        if(++s->cand>s->cap){
            /* sub-sample jump: parabola through the scores around the peak */
            if(!s->right_pending && s->best>s->lo){
                float den=s->left-2.0f*s->best_score+s->right;
                if(den<-1e-6f){off=0.5f*(s->left-s->right)/den;if(off>0.5f)off=0.5f;if(off<-0.5f)off=-0.5f;}
            }
            s->right_pending=0;
            s->d_old=s->d;s->d-=(float)s->best+off;s->fade=FROST_DROP_FADE;
            s->cand=FROST_DROP_JUMP_MIN;s->best=FROST_DROP_JUMP_MIN;s->best_score=-2.0f;s->state=DROP_COARSE;
        }
    }
    mix=p[1]*0.01f;
    return x+mix*(y-x);
}
#include "../../engine/overlay_far_off.h"
