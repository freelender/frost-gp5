/* Offline pitch-effect harness (host only; never talks to the pedal).
 *
 * Built once per source tree by tools/pitch_harness.py: the working tree and
 * a Git export (e.g. HEAD) of the previous algorithms. It runs the REAL
 * firmware kernels (DROP, OCTAVE, SPACE/SHIMMER) exactly as the engine calls
 * them (per sample for the overlay kernels, 64-sample blocks for RV-6) and
 * provides deterministic test signals with a known ideal pitch-shifted
 * reference plus the quality metrics, so old and new algorithms are compared
 * on identical input. Host arithmetic only: ARM cycle counts come from the
 * emulator bench (tools/pitch_arm_bench.c), pedal costs from the pedal. */
#define FROST_REVERB_INT16 1
#include "engine/bundle.c"
#include <math.h>
#include <string.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API
#endif
#define PH_FS 44100.0

/* ---------------- kernels ------------------------------------------------ */
enum { PH_DROP=0, PH_OCTAVE=1, PH_SHIMMER=2, PH_SHIFTER=3, PH_KERNELS };
static FrostModulation ph_ring;
static FrostDropTune ph_drop;
static FrostOctave ph_octave;
static FrostReverb16 ph_rv_ws;
static FrostReverbModel16 ph_rv;
API unsigned ph_kernels(void){return PH_KERNELS;}
API unsigned ph_state_bytes(unsigned k)
{return k==PH_DROP?(unsigned)sizeof(FrostDropTune):k==PH_OCTAVE?(unsigned)sizeof(FrostOctave):(unsigned)sizeof(FrostReverbModel16);}
API unsigned ph_workspace_bytes(unsigned k)
{return k==PH_SHIMMER?(unsigned)sizeof(FrostReverb16):(unsigned)sizeof(FrostModulation);}
API unsigned ph_slot_union_bytes(void){return (unsigned)sizeof(FrostEffectState);}
/* Reset the kernel and process n samples with constant parameters p[0..3]. */
API int ph_process(unsigned k,const float *x,float *y,unsigned n,const float *p)
{
    unsigned i,b;
    memset(&ph_ring,0x5a,sizeof ph_ring);   /* garbage: the reset must define every byte it reads */
    if(k==PH_DROP){ph_drop.ring=&ph_ring;frost_drop_reset(&ph_drop);for(i=0;i<n;++i)y[i]=frost_drop_sample(&ph_drop,x[i],p);return 1;}
    if(k==PH_OCTAVE){memset(&ph_ring,0,sizeof ph_ring);ph_octave.ring=&ph_ring;frost_octave_reset(&ph_octave);for(i=0;i<n;++i)y[i]=frost_octave_sample(&ph_octave,x[i],p);return 1;}
    if(k==PH_SHIMMER){
        memset(&ph_rv_ws,0,sizeof ph_rv_ws);ph_rv.ws=&ph_rv_ws;frost_16_rv6_reset(&ph_rv);
        for(i=0;i<n;i+=b){b=n-i<64u?n-i:64u;frost_16_rv6_block(&ph_rv,x+i,y+i,b,p);}
        return 1;
    }
    if(k==PH_SHIFTER){
        /* SHIMMER's octave shifter alone (open loop): the RV-6 pitch line
         * (1414 samples, same storage) and the same grain functions as the
         * reverb uses, without the tank. */
        static int16_t line[1414];static float err;unsigned st[3]={0,0,0},v[3]={0,0,0},pos=0,L=1414u;
        memset(line,0,sizeof line);err=0.0f;
        for(i=0;i<n;++i){
            y[i]=r16_shim_out(line,st);
            line[pos]=frost_reverb16_quantize_fast(frost_rail(x[i],8.0f),4096.0f,&err);
            if(++pos==L)pos=0;
            r16_shim_advance(line,L,pos,st,v);
        }
        return 1;
    }
    return 0;
}

/* Splice log of the last DROP run (working-tree splicer only). */
#ifdef FROST_PS_N
static unsigned ph_log_n,ph_log_t[4096];static float ph_log_j[4096],ph_log_s[4096],ph_log_d[4096];
API unsigned ph_drop_log(unsigned i,float *j,float *score,float *d){if(i>=ph_log_n||i>=4096u)return 0xffffffffu;*j=ph_log_j[i];*score=ph_log_s[i];*d=ph_log_d[i];return ph_log_t[i];}
API unsigned ph_drop_logged(void){return ph_log_n;}
API int ph_drop_trace(const float *x,float *y,unsigned n,const float *p)
{
    unsigned i;memset(&ph_ring,0x5a,sizeof ph_ring);ph_drop.ring=&ph_ring;frost_drop_reset(&ph_drop);ph_log_n=0;
    for(i=0;i<n;++i){float d0=ph_drop.head.d;unsigned f0=ph_drop.head.fade;y[i]=frost_drop_sample(&ph_drop,x[i],p);
        if(ph_drop.head.fade && ph_drop.head.fade==ph_drop.head.fade_len && !f0){if(ph_log_n<4096u){ph_log_t[ph_log_n]=i;ph_log_j[ph_log_n]=d0-ph_drop.head.d;ph_log_s[ph_log_n]=ph_drop.head.score;ph_log_d[ph_log_n]=d0;}++ph_log_n;}}
    return 1;
}
#endif
/* ---------------- test signals ------------------------------------------- */
static uint32_t ph_seed=1u;
static double ph_noise(void){ph_seed=ph_seed*1664525u+1013904223u;return (double)(int32_t)ph_seed*(1.0/2147483648.0);}
/* One plucked-string-like note added into x (and its ideal shifted copy into
 * ref when ref != NULL): H harmonics, amplitude 1/h with a soft pickup comb,
 * stiff-string inharmonicity B, per-harmonic exponential decay (higher
 * harmonics faster; decay_s <= 0 means sustained), 1.5 ms attack ramp and a
 * short pick click (x only: noise has no defined pitch). */
static double ph_vib_cents=0.0,ph_vib_hz=5.5;
/* Vibrato for the following ph_add_note calls (0 = none). */
API void ph_set_vibrato(double cents,double hz){ph_vib_cents=cents;ph_vib_hz=hz;}
API void ph_add_note(float *x,float *ref,unsigned n,unsigned start,double f0,double amp,double decay_s,double ratio,unsigned seed,double click)
{
    unsigned i,h,H;double ph0[24],vp=0.0;
    ph_seed=seed*2654435761u+7u;
    H=(unsigned)(7000.0/f0);if(H>24u)H=24u;if(H<1u)H=1u;
    for(h=0;h<H;++h)ph0[h]=6.283185307*(0.5+0.5*ph_noise());
    for(i=start;i<n;++i){
        double t=(double)(i-start)/PH_FS,a=t<0.0015?t/0.0015:1.0,s=0.0,r=0.0,tt;
        /* vibrato: warped time whose rate is the instantaneous frequency factor */
        vp+=pow(2.0,ph_vib_cents/1200.0*sin(6.283185307*ph_vib_hz*t))/PH_FS;tt=ph_vib_cents!=0.0?vp:t;
        for(h=1;h<=H;++h){
            double fh=f0*(double)h*sqrt(1.0+0.00012*(double)(h*h)),g=amp/(double)h*(0.6+0.4*fabs(sin(0.9*(double)h)));
            double e=decay_s>0.0?exp(-t*(1.0+0.35*(double)(h-1))/decay_s):1.0;
            if(fh*ratio>20000.0)continue;
            s+=g*e*sin(6.283185307*fh*tt+ph0[h-1]);
            r+=g*e*sin(6.283185307*fh*ratio*tt+ph0[h-1]);
        }
        if(x)x[i]+=(float)(a*s);
        if(ref)ref[i]+=(float)(a*r);
    }
    if(x && click>0.0){double z=0.0;for(i=start;i<start+132u && i<n;++i){double v=ph_noise()*click*exp(-(double)(i-start)/30.0);z+=0.5*(v-z);x[i]+=(float)(v-z);}}
}
/* Karplus-Strong pluck (no ideal reference): realistic decay and brightness. */
API void ph_add_pluck(float *x,unsigned n,unsigned start,double f0,double amp,unsigned seed)
{
    static float line[4096];unsigned L=(unsigned)(PH_FS/f0+0.5),i,k=0;float prev=0.0f;
    if(L<2u)L=2u;if(L>4096u)L=4096u;
    ph_seed=seed*2246822519u+3u;
    for(i=0;i<L;++i)line[i]=(float)(amp*ph_noise());
    for(i=start;i<n;++i){
        float v=line[k],nv=0.996f*0.5f*(v+prev);prev=v;line[k]=nv;if(++k==L)k=0;x[i]+=v;
    }
}

/* ---------------- metrics ------------------------------------------------ */
static void ph_fft(double *re,double *im,unsigned N)
{
    unsigned i,j=0,len,k;
    for(i=1;i<N;++i){unsigned bit=N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){double t=re[i];re[i]=re[j];re[j]=t;t=im[i];im[i]=im[j];im[j]=t;}}
    for(len=2;len<=N;len<<=1){
        double ang=-6.283185307179586/(double)len,wr=cos(ang),wi=sin(ang);
        for(i=0;i<N;i+=len){double cr=1.0,ci=0.0;
            for(k=0;k<len/2u;++k){
                double ur=re[i+k],ui=im[i+k],vr=re[i+k+len/2]*cr-im[i+k+len/2]*ci,vi=re[i+k+len/2]*ci+im[i+k+len/2]*cr,t;
                re[i+k]=ur+vr;im[i+k]=ui+vi;re[i+k+len/2]=ur-vr;im[i+k+len/2]=ui-vi;
                t=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=t;
            }
        }
    }
}
#define PH_N 4096u
static double ph_re[PH_N],ph_im[PH_N];
static void ph_spectrum(const float *x,unsigned n,unsigned at,double *mag)
{
    unsigned i;
    for(i=0;i<PH_N;++i){double w=0.5-0.5*cos(6.283185307*(double)i/(double)PH_N);unsigned j=at+i;ph_re[i]=j<n?w*(double)x[j]:0.0;ph_im[i]=0.0;}
    ph_fft(ph_re,ph_im,PH_N);
    for(i=0;i<PH_N/2u;++i)mag[i]=ph_re[i]*ph_re[i]+ph_im[i]*ph_im[i];
}
/* Log-spectral distance (dB) between y (delayed by lag) and the ideal ref,
 * 4096-point Hann frames, hop 1024, bins 50 Hz..9 kHz, over frames whose ref
 * energy is within 40 dB of the loudest; each bin floored 70 dB below the
 * frame's ref peak, so inaudible differences do not count. Magnitudes only:
 * phase and small timing differences are allowed. */
API double ph_lsd(const float *y,const float *ref,unsigned n,unsigned lag,unsigned from,unsigned to)
{
    static double a[PH_N/2],b[PH_N/2];double total=0.0,emax=0.0;unsigned at,frames=0,i,lo=(unsigned)(50.0*PH_N/PH_FS),hi=(unsigned)(9000.0*PH_N/PH_FS);
    if(to>n)to=n;
    for(at=from;at+PH_N<=to;at+=1024u){double e=0.0;for(i=0;i<PH_N;++i)e+=(double)ref[at+i]*ref[at+i];if(e>emax)emax=e;}
    for(at=from;at+PH_N<=to && at+lag+PH_N<=n;at+=1024u){
        double e=0.0,peak=0.0,d=0.0;
        for(i=0;i<PH_N;++i)e+=(double)ref[at+i]*ref[at+i];
        if(e<emax*1e-4)continue;
        ph_spectrum(ref,n,at,b);ph_spectrum(y,n,at+lag,a);
        for(i=lo;i<hi;++i)if(b[i]>peak)peak=b[i];
        for(i=lo;i<hi;++i){double fa=a[i]>peak*1e-7?a[i]:peak*1e-7,fb=b[i]>peak*1e-7?b[i]:peak*1e-7,v=10.0*log10(fa/fb);d+=v*v;}
        total+=sqrt(d/(double)(hi-lo));++frames;
    }
    return frames?total/(double)frames:-1.0;
}
/* Fraction of y's spectral energy (50 Hz..9 kHz) that lies within +/-cents of
 * the given partial frequencies, as "harmonic to other" dB; frames of 4096,
 * hop 2048, over [from,to). Grain/splice modulation, aliasing and wrong-pitch
 * energy all land between the partials. */
API double ph_partial_ratio(const float *y,unsigned n,unsigned from,unsigned to,const double *freqs,unsigned nf,double cents)
{
    static double a[PH_N/2];double in=0.0,out=0.0;unsigned at,i,j,lo=(unsigned)(50.0*PH_N/PH_FS),hi=(unsigned)(9000.0*PH_N/PH_FS);
    double tol=pow(2.0,cents/1200.0);
    if(to>n)to=n;
    for(at=from;at+PH_N<=to;at+=2048u){
        ph_spectrum(y,n,at,a);
        for(i=lo;i<hi;++i){
            double f=(double)i*PH_FS/PH_N;int hit=0;
            for(j=0;j<nf && !hit;++j){double w=freqs[j]*(tol-1.0)+2.5*PH_FS/PH_N;hit=fabs(f-freqs[j])<=w;}
            if(hit)in+=a[i];else out+=a[i];
        }
    }
    return 10.0*log10((in+1e-30)/(out+1e-30));
}
/* Envelope modulation ("warble") of y relative to the ideal reference over
 * [from,to): RMS in 5 ms frames, y's envelope divided by ref's (lag applied),
 * returned as the standard deviation of that ratio in dB. A steady input that
 * comes out steady scores ~0. */
API double ph_envelope_mod(const float *y,const float *ref,unsigned n,unsigned lag,unsigned from,unsigned to)
{
    unsigned f=220u,at,i,c=0;double s=0.0,s2=0.0;
    if(to>n)to=n;
    for(at=from;at+f<=to && at+lag+f<=n;at+=f){
        double ey=0.0,er=0.0,v;
        for(i=0;i<f;++i){ey+=(double)y[at+lag+i]*y[at+lag+i];er+=(double)ref[at+i]*ref[at+i];}
        if(er<1e-9)continue;
        v=10.0*log10((ey+1e-12)/er);s+=v;s2+=v*v;++c;
    }
    if(c<2u)return -1.0;
    s/=(double)c;return sqrt(fmax(s2/(double)c-s*s,0.0));
}
/* Amplitude modulation of one partial: y heterodyned at f, low-passed
 * (two one-poles, 25 Hz), magnitude sampled every 2 ms over [from,to);
 * returns the standard deviation (dB) around the best straight line in dB
 * (a natural exponential decay scores 0). Splice dips and grain-rate
 * modulation of that note show up here even inside a chord. */
API double ph_partial_am(const float *y,unsigned n,unsigned from,unsigned to,double f)
{
    double w=6.283185307*f/PH_FS,a=1.0-exp(-6.283185307*25.0/PH_FS),i1=0,q1=0,i2=0,q2=0,sx=0,sy=0,sxx=0,sxy=0,syy=0;unsigned k,c=0;
    if(to>n)to=n;
    for(k=from;k<to;++k){
        double v=(double)y[k];i1+=a*(v*cos(w*k)-i1);q1+=a*(-v*sin(w*k)-q1);i2+=a*(i1-i2);q2+=a*(q1-q2);
        if(k>from+2000u && !(k%88u)){double m=10.0*log10(i2*i2+q2*q2+1e-20),t=(double)k/PH_FS;sx+=t;sy+=m;sxx+=t*t;sxy+=t*m;syy+=m*m;++c;}
    }
    if(c<3u)return -1.0;
    {double mx=sx/c,my=sy/c,vx=sxx/c-mx*mx,cxy=sxy/c-mx*my,vy=syy/c-my*my,r=vy-(vx>0?cxy*cxy/vx:0.0);return sqrt(fmax(r,0.0));}
}
/* Lag (samples, 0..max_lag) maximising the correlation of 2 ms RMS envelopes. */
API unsigned ph_envelope_lag(const float *y,const float *ref,unsigned n,unsigned max_lag)
{
    static float ey[8192],er[8192];unsigned f=88u,m=n/f,i,k,best=0;double bs=-1.0;
    if(m>8192u)m=8192u;
    for(k=0;k<m;++k){double a=0.0,b=0.0;for(i=0;i<f;++i){a+=(double)y[k*f+i]*y[k*f+i];b+=(double)ref[k*f+i]*ref[k*f+i];}ey[k]=(float)sqrt(a);er[k]=(float)sqrt(b);}
    for(i=0;i*f<=max_lag;++i){double s=0.0;for(k=0;k+i<m;++k)s+=(double)ey[k+i]*er[k];if(s>bs){bs=s;best=i;}}
    {/* refine to a sample with a full-rate envelope around the coarse lag */
        unsigned lo=best*f>f?best*f-f:0u,hi=best*f+f,l;double bs2=-1.0;unsigned b2=best*f;
        for(l=lo;l<=hi && l<=max_lag;l+=4u){double s=0.0;for(k=0;k+l<n && k<n;k+=8u)s+=fabs((double)y[k+l])*fabs((double)ref[k]);if(s>bs2){bs2=s;b2=l;}}
        return b2;
    }
}
/* First sample at or after from where the 1 ms RMS envelope reaches frac of
 * the maximum envelope within [from, from+window). */
API unsigned ph_onset(const float *y,unsigned n,unsigned from,unsigned window,double frac)
{
    unsigned i,k,f=44u;double mx=0.0;
    for(i=from;i+f<=n && i<from+window;i+=4u){double e=0.0;for(k=0;k<f;++k)e+=(double)y[i+k]*y[i+k];if(e>mx)mx=e;}
    for(i=from;i+f<=n && i<from+window;i+=4u){double e=0.0;for(k=0;k<f;++k)e+=(double)y[i+k]*y[i+k];if(e>=mx*frac*frac)return i;}
    return n;
}
/* Pitch of the partial nearest to expect over [from,to): Hann-windowed DFT
 * magnitude scanned from -150 to +150 cents in 0.25-cent steps (the strongest
 * peak wins). Returns the error in cents. */
API double ph_pitch_cents(const float *y,unsigned from,unsigned to,double expect)
{
    double best=-1.0,bc=0.0;int c;unsigned i,n=to-from;
    for(c=-600;c<=600;++c){
        double f=expect*pow(2.0,(double)c/4800.0),w=6.283185307*f/PH_FS,cr=cos(w),ci=sin(w),re=0.0,im=0.0,pr=1.0,pi=0.0,t;
        for(i=0;i<n;++i){double h=(0.5-0.5*cos(6.283185307*(double)i/(double)n))*(double)y[from+i];re+=h*pr;im-=h*pi;t=pr*cr-pi*ci;pi=pr*ci+pi*cr;pr=t;}
        t=re*re+im*im;if(t>best){best=t;bc=(double)c/4.0;}
    }
    return bc;
}
/* Peak of |y| (for headroom checks). */
API double ph_peak(const float *y,unsigned n){unsigned i;double m=0.0;for(i=0;i<n;++i)if(fabs((double)y[i])>m)m=fabs((double)y[i]);return m;}
