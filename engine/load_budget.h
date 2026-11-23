#ifndef FROST_LOAD_BUDGET_H
#define FROST_LOAD_BUDGET_H
/* 32i DSP load admission. A route or preset is refused (FROST_SLOT_DSP_LIMIT,
 * "DSP LIMIT" on the pedal) when its estimated DSP cycles per 64-sample block
 * exceed FROST_LOAD_BUDGET_CYCLES. The block deadline is 278,494 cycles (100%
 * on the DSP meter); the budget keeps ~10% for knob-edit paths, commands and
 * route fades.
 *
 * Estimate = FROST_LOAD_BASE_CYCLES + sum of the per-type costs of every
 * non-empty slot (bypassed slots included: they can be switched on later).
 * Costs are the slot-load meter maxima measured on the pedal (2026-11-23,
 * build 32h, each type alone with default settings, IR over the controller
 * link), rounded up to 100 cycles. Settings that cost more than the defaults
 * (e.g. RV-6 SHIMMER mode) are not part of a route change; the overload
 * protection still covers them. Check against the pedal:
 *   COMP+ DELUXE IR SPRING   estimate 175,100  measured peak ~165,000  (fits)
 *   NAM POG OCD DELUXE       estimate 326,400  meter ~120%, overload  (refused)
 * Re-measure when an effect's cost changes (frost-shimmer-32a/measure/sweep_costs.py). */
#include "engine/metadata.h"
#include "engine/hot_code.h"
/* Table and estimate live in dsp-data flash (FROST_XDATA/FROST_XCODE): DSP
 * code flash has no room, and they run only on a route/preset request.
 * Include from exactly one translation unit (firmware/protocol/slots.c). */
#define FROST_LOAD_BASE_CYCLES 20000u
#define FROST_LOAD_BUDGET_DEFAULT 250000u   /* ~90% of the block deadline */
#ifndef FROST_LOAD_BUDGET_CYCLES
#define FROST_LOAD_BUDGET_CYCLES FROST_LOAD_BUDGET_DEFAULT
#endif
static const uint32_t frost_load_cost[FROST_EFFECTS] FROST_XDATA={
      3100,  7900,  5500, 23900, 10800, 23100,   /*  0.. 5 */
     22600, 22500, 59600, 32400, 40600, 36800,   /*  6..11 */
     26500, 56700, 40400, 43600, 40200, 29200,   /* 12..17 */
     52400, 49500, 55300, 17600, 24000, 28800,   /* 18..23 */
     27300, 37300, 32800, 10900,110600, 11500,   /* 24..29 */
     22100, 38600, 35800, 85300, 33500, 31800,   /* 30..35 */
     27500, 12800, 12100, 21700, 15200, 29600,   /* 36..41 */
     49900, 39600, 45800, 53800, 55900, 53100,   /* 42..47 */
     54500, 79500, 78900, 78900,   /* 48..51 */
     /* 52..58 ROTARY RING CRUSH H-TREM MUFF BOOST SWELL: measured on the
      * pedal 2026-11-23 (build 32l), same method */
     32900, 13100,  9700, 18700, 34900,  8800, 13200
};
typedef char FrostLoadCostTable[(sizeof frost_load_cost/sizeof frost_load_cost[0]==FROST_EFFECTS)?1:-1];
uint32_t frost_load_estimate(const unsigned *types);
FROST_XCODE uint32_t frost_load_estimate(const unsigned *types)
{
    unsigned i;uint32_t sum=FROST_LOAD_BASE_CYCLES;
    for(i=0;i<FROST_SLOTS;++i)if(types[i]<FROST_EFFECTS)sum+=frost_load_cost[types[i]];
    return sum;
}
static inline int frost_load_fits(const unsigned *types){return frost_load_estimate(types)<=FROST_LOAD_BUDGET_CYCLES;}
#endif
