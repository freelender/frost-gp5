#ifndef FROST_IR_NORMALIZE_H
#define FROST_IR_NORMALIZE_H
/* Main-loop-only cabinet calibration. 1024-point zero-padded FFT estimates
 * the 512-tap response peak. Separate scratch does not touch IRQ convolution
 * histories. No allocation, flash writes or audio-callback work. */
static float ir_norm_re[1024],ir_norm_im[1024];
static float ir_norm_sqrt(float x)
{
    union {float f;uint32_t u;} v;float y;unsigned i;
    if(!(x>0.0f))return 0.0f;
    v.f=x;v.u=0x5f3759dfu-(v.u>>1);y=v.f;
    for(i=0;i<3u;++i)y=y*(1.5f-.5f*x*y*y);
    return x*y;
}
static float ir_record_normal_scale(uint32_t address)
{
    static const float roots[10][2]={
        {-1.0f, -1.224646799e-16f},
        {6.123233996e-17f, -1.0f},
        {0.7071067812f, -0.7071067812f},
        {0.9238795325f, -0.3826834324f},
        {0.9807852804f, -0.195090322f},
        {0.9951847267f, -0.09801714033f},
        {0.9987954562f, -0.04906767433f},
        {0.9996988187f, -0.02454122852f},
        {0.9999247018f, -0.01227153829f},
        {0.9999811753f, -0.006135884649f}
    };
    float l1=0.0f,peak=0.0f,gain;unsigned i,j=0,n,stage;
    union {float f;uint32_t u;} scale;
    scale.u=files_word(address+16u);
    if(!(scale.f>0.0f && scale.f<=8.0f/32767.0f))return scale.f;
    for(i=0;i<1024u;++i){
        float h=0.0f;
        if(i<512u){uint32_t w=files_word(address+20u+(i&~1u)*2u);int q=(int)(int16_t)(i&1u?w>>16:w);h=(float)q*scale.f;}
        ir_norm_re[i]=h;ir_norm_im[i]=0.0f;l1+=h<0.0f?-h:h;
    }
    if(!(l1>0.0f && l1<=8.001f))return scale.f;
    for(i=1;i<1024u;++i){
        unsigned bit=512u;float t;
        while(j&bit){j^=bit;bit>>=1;}j^=bit;
        if(i<j){t=ir_norm_re[i];ir_norm_re[i]=ir_norm_re[j];ir_norm_re[j]=t;}
    }
    for(n=2u,stage=0;n<=1024u;n<<=1,++stage){
        for(i=0;i<1024u;i+=n){float wr=1.0f,wi=0.0f;
            for(j=0;j<n/2u;++j){
                unsigned a=i+j,b=a+n/2u;float br=ir_norm_re[b],bi=ir_norm_im[b];
                float tr=wr*br-wi*bi,ti=wr*bi+wi*br,ar=ir_norm_re[a],ai=ir_norm_im[a],next;
                ir_norm_re[a]=ar+tr;ir_norm_im[a]=ai+ti;ir_norm_re[b]=ar-tr;ir_norm_im[b]=ai-ti;
                next=wr*roots[stage][0]-wi*roots[stage][1];wi=wr*roots[stage][1]+wi*roots[stage][0];wr=next;
            }
        }
    }
    for(i=0;i<=512u;++i){float e=ir_norm_re[i]*ir_norm_re[i]+ir_norm_im[i]*ir_norm_im[i];if(e>peak)peak=e;}
    peak=ir_norm_sqrt(peak);if(!(peak>0.0f))return scale.f;
    gain=1.0f/peak;if(gain*l1>7.999f)gain=7.999f/l1;
    /* Already calibrated browser records stay byte-exact. Float FFT roundoff
     * must not create another gain change on every load/export round trip. */
    if(gain>.999f && gain<1.001f && l1<=7.9995f)return scale.f;
    return scale.f*gain;
}
#endif
