#ifndef FROST_RECALL_GATE_H
#define FROST_RECALL_GATE_H
/* Final envelope after global bypass/EQ: commit overruns may repeat DMA
 * buffers, so both stereo channels must already contain complete silence.
 * Multiplication preserves non-finite samples for the existing output guard. */
FROST_XCODE FROST_NOINLINE static void frost_recall_gate(float *wet,float *side,unsigned count,
    unsigned phase,unsigned position,unsigned stereo)
{
    unsigned i;
    for(i=0;i<count;++i){
        float gain=0.0f;
        if(phase==FROST_ROUTE_OUT && position+i<FROST_ROUTE_FADE)
            gain=(float)(FROST_ROUTE_FADE-1u-position-i)/(float)FROST_ROUTE_FADE;
        else if(phase==FROST_ROUTE_IN)
            gain=position+i<FROST_ROUTE_FADE?(float)(position+i+1u)/(float)FROST_ROUTE_FADE:1.0f;
        wet[i]*=gain;if(stereo)side[i]*=gain;
    }
}
#endif
