/* ARM Cortex-M4 instruction-count bench for the DROP and OCTAVE kernels
 * (tools/pitch_arm_bench.py runs it in the Unicorn emulator). Emulator
 * instruction counts, NOT pedal cycles: the pedal's slot-load meter decides.
 * Compiled with the firmware's flags and overlay placement, so the .ovl.*
 * section sizes are the overlay module sizes. */
#define FROST_CONTROL_SCHEDULER 1
#define FROST_LARGE_CODE 1
#define FROST_OVERLAYS 1
#include "effects/common/frost_dsp.c"
#include "effects/drop_tune/drop_tune.c"
#include "effects/octave/octave.c"
static FrostModulation bench_ring;
static FrostDropTune bench_drop;
static FrostOctave bench_oct;
float bench_x[64],bench_y[64],bench_p[4];
uint32_t bench_rng=1u;unsigned bench_t;
void *memset(void *d,int c,unsigned n){unsigned char *p=d;while(n--)*p++=(unsigned char)c;return d;}
void *memcpy(void *d,const void *s,unsigned n){unsigned char *p=d;const unsigned char *q=s;while(n--)*p++=*q++;return d;}
void bench_reset(unsigned k)
{
    unsigned i;for(i=0;i<sizeof bench_ring;++i)((unsigned char *)&bench_ring)[i]=0;bench_t=0;bench_rng=1u;
    if(k==0){bench_drop.ring=&bench_ring;frost_drop_reset(&bench_drop);}else{bench_oct.ring=&bench_ring;frost_octave_reset(&bench_oct);}
}
/* kind 0: silence, 1: low chord (E2 B2 E3), 2: the chord plus a pick-like noise burst every 0.4 s.
 * bench_fill makes the next input block (not counted), bench_block runs the kernel on it. */
void bench_fill(unsigned kind)
{
    unsigned i;
    for(i=0;i<64u;++i,++bench_t){
        float v=0.0f;
        if(kind){
            float t=(float)bench_t*(1.0f/44100.0f),a=82.41f*t,b=123.47f*t,c=164.81f*t;
            v=0.12f*(frost_sin01(a-(float)(int)a)+0.8f*frost_sin01(b-(float)(int)b)+0.6f*frost_sin01(c-(float)(int)c));
            if(kind==2 && bench_t%17640u<200u){bench_rng=bench_rng*1664525u+1013904223u;v+=0.2f*((float)(int32_t)bench_rng*(1.0f/2147483648.0f));}
        }
        bench_x[i]=v;
    }
}
void bench_block(unsigned k)
{
    unsigned i;
    if(k==0)for(i=0;i<64u;++i)bench_y[i]=frost_drop_sample(&bench_drop,bench_x[i],bench_p);
    else for(i=0;i<64u;++i)bench_y[i]=frost_octave_sample(&bench_oct,bench_x[i],bench_p);
}
void _start(void){for(;;);}
