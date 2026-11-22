#ifndef FROST_CONTROLLER_DSP_USAGE_H
#define FROST_CONTROLLER_DSP_USAGE_H
#include <stdint.h>
#include "firmware/protocol/dsp_usage_wire.h"
typedef struct {
    uint32_t peak,last_report,mark;
    unsigned pending,available,valid,percent;
} FrostDSPUsage;
void frost_dsp_usage_init(FrostDSPUsage *s,uint32_t now);
void frost_dsp_usage_report(FrostDSPUsage *s,const uint8_t *p,unsigned n,uint32_t now);
/* Returns true only when the displayed value changes, at most every 2.5 s. */
unsigned frost_dsp_usage_tick(FrostDSPUsage *s,uint32_t now);
void frost_dsp_usage_text(const FrostDSPUsage *s,char *text);
#endif
