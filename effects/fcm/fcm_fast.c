#include "fcm_fast.h"
/* FCM_FN: section/placement attribute for every function (an overlay module
 * in the bench build: overlay code may not call code outside itself, so all
 * helpers live in the same section and no libgcc/libc calls are used). */
#ifndef FCM_FN
#define FCM_FN
#endif
#if FCM_EXP_EXACT
#include <math.h>
static inline float fast_exp(float z){return expf(z);}
#else
static inline float fast_exp(float z)
{float t;if(z<-1023.0f)return 0.0f;t=1.0f+z*(1.0f/1024.0f);t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;t*=t;return t;}
#endif
FCM_FN static unsigned rev6(unsigned k){unsigned r=0,i;for(i=0;i<6u;++i){r=(r<<1)|(k&1u);k>>=1;}return r;}
/* 64-point complex DIF FFT in place (bit-reversed output); w = W128^k (cos,-sin). */
FCM_FN static void fft64(float *z,const float *w)
{
    unsigned half,j,n,step;
    for(half=32u,step=4u;half;half>>=1,step<<=1)
        for(j=0;j<half;++j){
            const float wr=w[j*step],wi=w[j*step+1u];
            for(n=j;n<64u;n+=2u*half){
                float *a=z+2u*n,*b=a+2u*half;const float ar=a[0],ai=a[1],br=b[0],bi=b[1],dr=ar-br,di=ai-bi;
                a[0]=ar+br;a[1]=ai+bi;b[0]=dr*wr-di*wi;b[1]=dr*wi+di*wr;
            }
        }
}
/* 128 real samples -> packed half spectrum (bin 0 = [DC, Nyquist]). */
FCM_FN static void rfft(const float *w,const float *frame,float *sc,float *out)
{
    unsigned k;for(k=0;k<FCM_FFT;++k)sc[k]=frame[k];
    fft64(sc,w);
    out[0]=sc[0]+sc[1];out[1]=sc[0]-sc[1];
    for(k=1;k<=32u;++k){
        const float *a=sc+2u*rev6(k),*b=sc+2u*rev6(64u-k);
        const float er=0.5f*(a[0]+b[0]),ei=0.5f*(a[1]-b[1]),orr=0.5f*(a[1]+b[1]),oi=-0.5f*(a[0]-b[0]);
        const float wr=w[2u*k],wi=w[2u*k+1u],tr=wr*orr-wi*oi,ti=wr*oi+wi*orr;
        out[2u*k]=er+tr;out[2u*k+1u]=ei+ti;out[2u*(64u-k)]=er-tr;out[2u*(64u-k)+1u]=ti-ei;
    }
}
FCM_FN static void conv_prepare(FcmFast *s,FcmConv *c,const float *h,unsigned taps)
{
    unsigned p,k,j;c->parts=(taps+FCM_BLOCK-1u)/FCM_BLOCK;c->head=0;
    for(p=0;p<c->parts;++p){
        float fr[FCM_FFT];
        for(k=0;k<FCM_FFT;++k){j=p*FCM_BLOCK+k;fr[k]=k<FCM_BLOCK&&j<taps?h[j]:0.0f;}
        rfft(s->twiddle,fr,s->scratch,c->spec[p]);
        for(k=0;k<FCM_FFT;++k){c->spec[p][k]*=0x1p-7f;c->fdl[p][k]=0.0f;}
    }
    for(k=0;k<FCM_FFT;++k)c->frame[k]=0.0f;
}
/* y[0..64) = (h * x) for the current 64 inputs (overlap-save, uniform partitions). */
FCM_FN static void conv_block(FcmFast *s,FcmConv *c,const float *x,float *y)
{
    const float *w=s->twiddle;float *z=s->scratch;unsigned i,k,p,slot;
    for(i=0;i<FCM_BLOCK;++i)c->frame[FCM_BLOCK+i]=x[i];
    c->head=c->head?c->head-1u:c->parts-1u;
    rfft(w,c->frame,z,c->fdl[c->head]);
    for(k=0;k<FCM_BLOCK;++k)c->frame[k]=c->frame[FCM_BLOCK+k];
    for(k=0;k<FCM_FFT;++k)z[k]=0.0f;
    for(p=0,slot=c->head;p<c->parts;++p){
        const float *a=c->fdl[slot],*h=c->spec[p];
        z[0]+=a[0]*h[0];z[1]+=a[1]*h[1];
        for(k=2;k<FCM_FFT;k+=2){z[k]+=a[k]*h[k]-a[k+1]*h[k+1];z[k+1]+=a[k]*h[k+1]+a[k+1]*h[k];}
        if(++slot==c->parts)slot=0;
    }
    {const float dc=z[0],ny=z[1];z[0]=dc+ny;z[1]=ny-dc;}
    for(k=1;k<=32u;++k){
        float *a=z+2u*k,*b=z+2u*(64u-k);const float yr=a[0],yi=a[1],mr=b[0],mi=b[1];
        const float er=yr+mr,ei=yi-mi,dr=yr-mr,di=yi+mi,wr=w[2u*k],wi=-w[2u*k+1u],orr=dr*wr-di*wi,oi=dr*wi+di*wr;
        a[0]=er-oi;a[1]=-(ei+orr);b[0]=er+oi;b[1]=ei-orr;
    }
    fft64(z,w);
    for(i=0;i<FCM_BLOCK;i+=2){const float *r=z+2u*rev6(32u+i/2u);y[i]=r[0];y[i+1u]=-r[1];}
}
FCM_FN void fcm_fast_reset(FcmFast *s,const FcmModel *m)
{
    unsigned k,i,j,p,T;
    s->m=m;s->env=0.0f;
#ifdef FCM_TWIDDLE_TABLE
    for(k=0;k<FCM_FFT;++k)s->twiddle[k]=FCM_TWIDDLE_TABLE[k];   /* no double math in overlay code */
#else
    {double c=1.0,sn=0.0;const double cs=0.99879545620517241,ss=0.049067674327418015; /* cos/sin(2*pi/128) */
     for(k=0;k<64u;++k){s->twiddle[2u*k]=(float)c;s->twiddle[2u*k+1u]=(float)-sn;{double t=c*cs-sn*ss;sn=sn*cs+c*ss;c=t;}}}
#endif
    for(k=0;k<m->n_aa;++k)s->aa[k]=m->aa[k];
    conv_prepare(s,&s->pre,m->pre,m->n_pre);conv_prepare(s,&s->post,m->post,m->n_post);
    for(i=0;i+1u<m->stages;++i)conv_prepare(s,&s->mid[i],m->mid[i],m->n_mid);
    T=(m->n_aa+m->os-1u)/m->os;
    for(p=0;p<m->os;++p)for(j=0;j<T;++j){unsigned q=p+j*m->os;s->poly[p*T+j]=q<m->n_aa?m->aa[q]:0.0f;}
    for(i=0;i<FCM_MAX_STAGES;++i){for(j=0;j<FCM_MAX_AA/2u+FCM_BLOCK;++j)s->uh[i][j]=0.0f;for(j=0;j<FCM_MAX_AA+4u*FCM_BLOCK;++j)s->vh[i][j]=0.0f;}
}
FCM_FN static void fast_stage(FcmFast *s,unsigned i,float *u)
{
    const FcmModel *m=s->m;const unsigned OS=m->os,L=m->n_aa,T=(L+OS-1u)/OS;
    const float Ap=m->Ap[i],An=m->An[i],Kp=m->Kp[i],Kn=m->Kn[i],*h=m->aa;
    float *uh=s->uh[i],*uc=uh+T-1u,*vh=s->vh[i],*vc=vh+L-1u;unsigned f,p,j,k;
    for(f=0;f<FCM_BLOCK;++f)uc[f]=u[f]*(float)OS;
    for(f=0;f<FCM_BLOCK;++f){
        const float *q=uc+f;
        for(p=0;p<OS;++p){
            const float *hp=s->poly+p*T;float a=0.0f;
            for(j=0;j<T;++j)a+=hp[j]*q[-(int)j];
            vc[f*OS+p]=a>0.0f?Ap*(1.0f-fast_exp(-Kp*a)):An*(fast_exp(Kn*a)-1.0f);
        }
    }
    for(f=0;f<FCM_BLOCK;++f){const float *q=vc+f*OS;float a=0.0f;for(k=0;k<L;++k)a+=h[k]*q[-(int)k];u[f]=a;}
    for(k=0;k+1u<T;++k)uh[k]=uh[FCM_BLOCK+k];
    for(k=0;k+1u<L;++k)vh[k]=vh[FCM_BLOCK*OS+k];
}
/* Specialized stage for os=2, n_aa=20 (the shipping configuration): fixed
 * trip counts and the anti-alias filter's symmetry (firwin is linear-phase,
 * h[k]=h[19-k]) folded in the decimator. Same operations up to float
 * reassociation in the folded sum. */
FCM_FN static void fast_stage_2x20(FcmFast *s,unsigned i,float *u)
{
    const FcmModel *m=s->m;const float Ap=m->Ap[i],An=m->An[i],Kp=m->Kp[i],Kn=m->Kn[i],*h=s->aa;
    float *uh=s->uh[i],*uc=uh+9,*vh=s->vh[i],*vc=vh+19;const float *p0=s->poly,*p1=s->poly+10;unsigned f,j;
    for(f=0;f<FCM_BLOCK;++f)uc[f]=u[f]*2.0f;
    for(f=0;f<FCM_BLOCK;++f){
        const float *q=uc+f;float a0=0.0f,a1=0.0f;
        for(j=0;j<10u;++j){const float v=q[-(int)j];a0+=p0[j]*v;a1+=p1[j]*v;}
        vc[2u*f]=a0>0.0f?Ap*(1.0f-fast_exp(-Kp*a0)):An*(fast_exp(Kn*a0)-1.0f);
        vc[2u*f+1u]=a1>0.0f?Ap*(1.0f-fast_exp(-Kp*a1)):An*(fast_exp(Kn*a1)-1.0f);
    }
    for(f=0;f<FCM_BLOCK;++f){const float *q=vc+2u*f;float a=0.0f;for(j=0;j<10u;++j)a+=h[j]*(q[-(int)j]+q[-(int)(19u-j)]);u[f]=a;}
    for(j=0;j<9u;++j)uh[j]=uh[FCM_BLOCK+j];
    for(j=0;j<19u;++j)vh[j]=vh[2u*FCM_BLOCK+j];
}
FCM_FN void fcm_fast_process(FcmFast *s,const float *x,float *y)
{
    const FcmModel *m=s->m;float u[FCM_BLOCK],e[FCM_BLOCK];unsigned i,f;
    conv_block(s,&s->pre,x,u);
    for(f=0;f<FCM_BLOCK;++f)u[f]*=m->gain;
    if(m->env_a!=0.0f){float v=s->env;for(f=0;f<FCM_BLOCK;++f){v+=m->env_a*((u[f]<0.0f?-u[f]:u[f])-v);e[f]=v;}s->env=v;}
    for(i=0;i<m->stages;++i){
        if(m->env_a!=0.0f)for(f=0;f<FCM_BLOCK;++f)u[f]-=m->env_k[i]*e[f];
        if(m->os==2u && m->n_aa==20u)fast_stage_2x20(s,i,u);else fast_stage(s,i,u);
        if(i+1u<m->stages)conv_block(s,&s->mid[i],u,u);
    }
    conv_block(s,&s->post,u,y);
}
