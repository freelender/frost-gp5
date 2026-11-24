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
 * Costs are the slot-load meter maxima measured on the pedal, each type alone
 * with default settings, rounded up to 100 cycles: the larger of the silent
 * sweep (2026-11-23, build 32h) and a sweep WHILE PLAYING (2026-11-24, build
 * 32v; reverbs and delays cost more with a signal). SPACE is costed per MODE
 * (frost_load_rv6_mode): a preset uses its stored mode, a route change the
 * slot's current mode, and a MODE edit is refused when the new mode would not
 * fit (frost_slots_apply). Checked against the pedal while playing:
 *   SCREAM UK-800 IR DIGI SPACE(mode 2)  272,400  measured 272-278k, late blocks (refused now)
 *   BOOST  UK-800 IR DIGI SPACE(mode 2)  247,900  (factory Singing Lead since beta.10)
 * Re-measure when an effect's cost changes
 * (each effect alone on the pedal with the slot-load meter, while playing). */
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
      3700,  8200,  6200, 23900, 10800, 23100,   /*  0.. 5 */
     22600, 22500, 70200, 33300, 40600, 37900,   /*  6..11 */
     27300, 56700, 40400, 43600, 41700, 32000,   /* 12..17 */
     55000, 63500, 65600, 18300, 24000, 28800,   /* 18..23 */
     29900, 41900, 33500, 12200,110700, 12200,   /* 24..29 */
     22200, 38600, 35800, 88700, 33500, 32400,   /* 30..35 */
     28300, 12800, 12100, 21700, 15200, 29600,   /* 36..41 */
     52700, 42200, 45800, 53800, 55900, 53100,   /* 42..47 */
     54500, 81600, 78900, 78900,   /* 48..51 */
     /* 52..58 ROTARY RING CRUSH H-TREM MUFF BOOST SWELL */
     32900, 13100,  9700, 18700, 36100,  8800, 16200
};
typedef char FrostLoadCostTable[(sizeof frost_load_cost/sizeof frost_load_cost[0]==FROST_EFFECTS)?1:-1];
/* SPACE (RV-6) by MODE 0..4, measured while playing (2026-11-24). */
static const uint32_t frost_load_rv6_mode[5] FROST_XDATA={61400,63500,81600,97000,106200};
#define FROST_LOAD_MODE_UNKNOWN 255u
/* modes[i]: SPACE MODE of slot i (FROST_LOAD_MODE_UNKNOWN = the default cost); may be NULL. */
uint32_t frost_load_estimate_modes(const unsigned *types,const unsigned *modes);
FROST_XCODE uint32_t frost_load_estimate_modes(const unsigned *types,const unsigned *modes)
{
    unsigned i;uint32_t sum=FROST_LOAD_BASE_CYCLES;
    for(i=0;i<FROST_SLOTS;++i)if(types[i]<FROST_EFFECTS)
        sum+=types[i]==FROST_RV6 && modes && modes[i]<5u?frost_load_rv6_mode[modes[i]]:frost_load_cost[types[i]];
    return sum;
}
static inline uint32_t frost_load_estimate(const unsigned *types){return frost_load_estimate_modes(types,0);}
static inline int frost_load_fits(const unsigned *types){return frost_load_estimate(types)<=FROST_LOAD_BUDGET_CYCLES;}
static inline int frost_load_fits_modes(const unsigned *types,const unsigned *modes)
{return frost_load_estimate_modes(types,modes)<=FROST_LOAD_BUDGET_CYCLES;}
#endif
