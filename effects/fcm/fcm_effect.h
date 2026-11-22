#ifndef FROST_FCM_EFFECT_H
#define FROST_FCM_EFFECT_H
/* EXPERIMENTAL "AMP" block: Frost Capture Model (FCM), a SnapTone-class amp
 * model converted per NAM file (docs/Build%20Firmware.md). A converted
 * model is an APPROXIMATION of its source NAM, not NAM.
 * Controls (library units): MODEL 0..63, GAIN -12..+12 dB input,
 * LEVEL 0..100 (50 = unity), BASS/MIDDLE/TREBLE -12..+12 dB
 * (post-model EQ, 0 = neutral; existing parameter indices are preserved).
 * Models: files in the controller's flash (docs/Build%20Firmware.md). The controller
 * sends the record of the selected MODEL to the DSP (FROST_FCM_COMMAND,
 * firmware/protocol/fcm_wire.h); the DSP holds one received record. A MODEL
 * without a received record plays the built-in default model (MIT-licensed
 * NAM Core example, converted). State lives in one modulation-class workspace
 * (line storage): the per-slot state only holds the workspace pointer. */
#include <stdint.h>
#include "../modulation/modulation.h"
#include "fcm_iir.h"
#include "../common/block.h"
#define FROST_FCM_NAME 16u
typedef struct { char name[FROST_FCM_NAME]; FcmiModel model; uint32_t crc32; } FrostFcmRecord; /* crc32 over name+model */
/* The one record received from the controller. Written only by the command
 * handler (main loop, outside audio); read by the AMP block at (re)load. */
typedef struct { uint32_t state,index,crc,next,epoch; FrostFcmRecord rec; } FrostFcmRx;
extern FrostFcmRx frost_fcm_rx;
typedef struct { FrostModulation *ring; } FrostFcm;
/* p: parameters at the END of the block. p_start (0 = none): parameters at its
 * start; GAIN, LEVEL and tone gains then ramp linearly across the block (knob moves stay
 * on the block path instead of per-sample calls). MODEL is always p[0]. */
typedef FrostBlockFn FrostFcmBlockFn;
void frost_fcm_reset(FrostFcm *s);
float frost_fcm_sample(FrostFcm *s,float x,const float *p);
void frost_fcm_block(void *state,const float *x,float *y,unsigned n,const float *p,const float *p_start);
/* Block entry usable by the engine (the loaded overlay copy on target). */
extern FrostFcmBlockFn frost_fcm_block_kernel;
/* effects/fcm/fcm_transfer.c (not overlay code). */
uint32_t frost_fcm_crc32(const void *data,unsigned bytes);
void frost_fcm_rx_init(void);
void frost_fcm_command(const uint8_t *request,unsigned bytes,uint8_t *reply);
#endif
