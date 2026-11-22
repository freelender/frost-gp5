#include "framework_host.c"
static FrostDelay delay_memory[2];
static FrostModulation mod_memory[2][FROST_MOD_WORKSPACES];
static FrostReverb reverb_memory[2];
static struct {unsigned frames,invalid,clips,hash;float peak;} result[2];
API int qa_setup(unsigned c,const unsigned *types,unsigned count,unsigned policy)
{
    unsigned i;float ir[512];fx_init(c);
    if(!frost_chain_bind_delay(&chains[c],&delay_memory[c]) ||
       !frost_chain_bind_modulation(&chains[c],mod_memory[c]) ||
       !frost_chain_bind_reverb(&chains[c],&reverb_memory[c]) ||
       !frost_chain_configure(&chains[c],types,count))return 0;
    /* Dense deterministic test FIR, absolute coefficient sum <1.21.
     * It is NOT a measured guitar cabinet. Exercise all 512 supported taps. */
    ir[0]=0.7f;for(i=1;i<512;++i)ir[i]=(i&1u)?0.001f:-0.001f;
    for(i=0;i<count;++i){
        if(types[i]==FROST_IR && !frost_chain_load_ir(&chains[c],i,ir,512))return 0;
        if(!frost_chain_set_bypass_policy(&chains[c],i,policy) || !frost_chain_enable(&chains[c],i,1))return 0;
    }
    return 1;
}
static void qa_control(unsigned c,unsigned frame)
{
    unsigned event=frame/1024u,slot=event%5u;
    const FrostEffect *d=frost_effect_find(chains[c].slots[slot].type);
    if(d && !chains[c].transition){
        unsigned p=(event/5u)%d->parameter_count;
        (void)frost_chain_enable(&chains[c],slot,!(event&1u));
        (void)frost_chain_set(&chains[c],slot,p,(event&2u)?d->parameters[p].maximum:d->parameters[p].minimum);
    }
    if(frame==16384u){
        unsigned i,types[6],order[6];
        for(i=0;i<6;++i){types[i]=chains[c].slots[i].type;order[i]=5u-i;}
        (void)frost_chain_request_route(&chains[c],types,order,2);
    }
}
API int qa_run(unsigned c,unsigned frames,unsigned chunk,unsigned controls,float *capture)
{
    unsigned position=0,i;float x[257],y[257];
    if(!chunk || chunk>257)return 0;
    result[c].frames=result[c].invalid=result[c].clips=0;result[c].hash=2166136261u;result[c].peak=0.0f;
    while(position<frames){
        unsigned n=frames-position;if(n>chunk)n=chunk;
        if(controls){unsigned until=1024u-position%1024u;if(!((position)%1024u))qa_control(c,position);if(n>until)n=until;}
        for(i=0;i<n;++i)x[i]=(float)((int)(((position+i)*137u)%2047u)-1023)*(1.0f/4096.0f);
        frost_chain_process(&chains[c],x,y,n);
        for(i=0;i<n;++i){
            union {float f;uint32_t u;} bits;float a=y[i]<0.0f?-y[i]:y[i];bits.f=y[i];
            if(!((bits.u&0x7f800000u)!=0x7f800000u && a<=FROST_SIGNAL_LIMIT))++result[c].invalid;
            if(a>=1.0f)++result[c].clips;if(a>result[c].peak)result[c].peak=a;
            result[c].hash=(result[c].hash^bits.u)*16777619u;
            if(capture)capture[position+i]=y[i];
        }
        result[c].frames+=n;position+=n;
    }
    return 1;
}
API unsigned qa_value(unsigned c,unsigned field)
{return field==0?result[c].frames:field==1?result[c].invalid:field==2?result[c].clips:field==3?result[c].hash:chains[c].faults;}
API float qa_peak(unsigned c){return result[c].peak;}
API void qa_poison_eq(unsigned c,unsigned slot)
{union {uint32_t u;float f;} v;v.u=0x7fc00000u;chains[c].slots[slot].state.eq.low=v.f;}
API void qa_boundary(unsigned c,unsigned pre){chains[c].pre_count=pre;}
API unsigned qa_delay_valid(unsigned c){return delay_memory[c].valid;}
API unsigned qa_reverb_valid(unsigned c){return reverb_memory[c].valid[0];}
API void qa_saturate_faults(unsigned c){chains[c].faults=UINT32_MAX;}
