#ifndef FROST_REVERB_STORAGE_H
#define FROST_REVERB_STORAGE_H
#include "reverb.h"
#include "../reverbs/reverbs.h"
#ifdef FROST_REVERB_INT16
#include "../reverbs/reverbs16.h"
typedef FrostReverb16 FrostReverbWorkspace;
typedef FrostReverbModel16 FrostReverbActiveModel;
#define frost_active_reverb_reset frost_reverb16_reset
#define frost_active_reverb_sample frost_reverb16_sample
#define frost_active_reverb_block frost_reverb16_block
#define frost_active_rv6_reset frost_16_rv6_reset
#define frost_active_rv6_sample frost_16_rv6_sample
#define frost_active_rv6_block frost_16_rv6_block
#define frost_active_spring_reset frost_16_spring_reset
#define frost_active_spring_sample frost_16_spring_sample
#define frost_active_spring_block frost_16_spring_block   /* 32e */
#else
typedef FrostReverb FrostReverbWorkspace;
typedef FrostReverbModel FrostReverbActiveModel;
#define frost_active_reverb_reset frost_reverb_reset
#define frost_active_reverb_sample frost_reverb_sample
#define frost_active_reverb_block frost_reverb_block
#define frost_active_rv6_reset frost_rv6_reset
#define frost_active_rv6_sample frost_rv6_sample
#define frost_active_rv6_block frost_rv6_block
#define frost_active_spring_reset frost_spring_reset
#define frost_active_spring_sample frost_spring_sample
#endif
#endif
